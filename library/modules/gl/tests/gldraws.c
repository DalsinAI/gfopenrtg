/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLDraws (9 Nov 2026): GL's cost a draw, for the virgl transport's
 * batching (gla_virgl.c). Each frame is DRAWS quads, each its own draw, in
 * a grid over a WIDTH x HEIGHT buffer, fed one of three ways:
 *   immediate      glBegin/glEnd (Mesa's upload buffer)
 *   buffersubdata  one buffer object rewritten with glBufferSubData before
 *                  each draw (a buffer a queued draw still reads: Mesa
 *                  flushes at every draw, as AssaultCube's frames did)
 *   texsubimage    one 4x4 texture rewritten with glTexSubImage2D before each
 *                  draw (a lightmap's pattern)
 * Frames are presented through the GLA core's hook into memory (no screen),
 * so what is timed is GL, the transport and the host. For each mode it
 * prints the frames a second, the request blocks a frame and the 68k's wait
 * for them a frame (gla_get_stats: fences, fence_wait_us), and checks one
 * quad's colour.
 *   GLDraws [MODE immediate|buffersubdata|texsubimage|all] [FRAMES n]
 *           [DRAWS n] [WIDTH w] [HEIGHT h] [CPU]
 * Defaults: all, 100 frames, 300 draws, 320 x 240. Returns 0, or 5 when a
 * picture is wrong. Built by library/modules/gl/module/build.sh against
 * libGL.a, as GLArrays is. ENV:GFOpenGPU/GLBatch 0 turns batching off. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <gla/gla_core.h>
#ifdef __amigaos__
#include <gla/os3/gla_virgl_os3.h>
#include <proto/dos.h>
#else
#include <time.h>
#endif
#define GL_GLEXT_PROTOTYPES 0
#include <GL/gl.h>
#include <GL/glext.h>

#ifdef __amigaos__
unsigned long __stack = 256 * 1024;
#endif

static unsigned char *shot;             /* the last presented frame: R, G, B */
static unsigned sw, sh;

static void present(void *user, const struct gla_frame *f)
{
    int r = f->format == GLA_ARGB ? 1 : 2, g = f->format == GLA_ARGB ? 2 : 1, b = f->format == GLA_ARGB ? 3 : 0;
    unsigned x, y;
    (void)user;
    for (y = 0; y < f->h && y < sh; y++) {
        const unsigned char *row = (const unsigned char *)f->pixels + y * f->stride;
        for (x = 0; x < f->w && x < sw; x++) {
            shot[(y * sw + x) * 3 + 0] = row[x * 4 + r];
            shot[(y * sw + x) * 3 + 1] = row[x * 4 + g];
            shot[(y * sw + x) * 3 + 2] = row[x * 4 + b];
        }
    }
}

static double seconds(void)
{
#ifdef __amigaos__
    struct DateStamp d;
    DateStamp(&d);
    return d.ds_Days * 86400.0 + d.ds_Minute * 60.0 + d.ds_Tick / 50.0;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
#endif
}

static PFNGLGENBUFFERSPROC pGenBuffers;
static PFNGLBINDBUFFERPROC pBindBuffer;
static PFNGLBUFFERDATAPROC pBufferData;
static PFNGLBUFFERSUBDATAPROC pBufferSubData;
static PFNGLDELETEBUFFERSPROC pDeleteBuffers;

static int cols = 20, rows = 15;

static void quad_of(int i, GLfloat *v)
{
    GLfloat qw = 2.0f / (GLfloat)cols, qh = 2.0f / (GLfloat)rows;
    GLfloat x0 = -1.0f + (GLfloat)(i % cols) * qw, y0 = -1.0f + (GLfloat)((i / cols) % rows) * qh;
    v[0] = x0; v[1] = y0; v[2] = x0 + qw; v[3] = y0; v[4] = x0 + qw; v[5] = y0 + qh; v[6] = x0; v[7] = y0 + qh;
}

static void colour_of(int i, GLubyte *c)
{
    c[0] = (GLubyte)(i * 37); c[1] = (GLubyte)(i * 91 + 64); c[2] = (GLubyte)(255 - i * 13); c[3] = 255;
}

static void frame(int mode, int draws)
{
    GLfloat v[8];
    GLubyte c[4], texels[4 * 4 * 4];
    static const GLfloat st[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    int i, k;
    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < draws; i++) {
        quad_of(i, v);
        colour_of(i, c);
        if (mode == 0) {
            glColor4ubv(c);
            glBegin(GL_QUADS);
            for (k = 0; k < 4; k++) glVertex2f(v[k * 2], v[k * 2 + 1]);
            glEnd();
        } else if (mode == 1) {
            pBufferSubData(GL_ARRAY_BUFFER, 0, sizeof v, v);
            glColor4ubv(c);
            glDrawArrays(GL_QUADS, 0, 4);
        } else {
            for (k = 0; k < 16; k++) memcpy(texels + k * 4, c, 4);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, texels);
            glBegin(GL_QUADS);
            for (k = 0; k < 4; k++) { glTexCoord2f(st[k * 2], st[k * 2 + 1]); glVertex2f(v[k * 2], v[k * 2 + 1]); }
            glEnd();
        }
    }
}

static const char *const names[3] = { "immediate", "buffersubdata", "texsubimage" };

/* One mode: frames a second, blocks and waits a frame; 1 when its picture is right. */
static int run(struct gla_context *c, struct gla_buffer *b, int mode, int frames, int draws)
{
    GLuint vbo = 0, tex = 0;
    struct gla_stats st;
    double t0, t1;
    int f, ok, probe = draws - 1;
    unsigned px, py;
    GLubyte want[4];
    GLfloat v[8];
    if (mode == 1) {
        GLfloat zero[8] = { 0 };
        if (!pGenBuffers || !pBindBuffer || !pBufferData || !pBufferSubData) { printf("GLDraws %s: no buffer objects\n", names[mode]); return 1; }
        pGenBuffers(1, &vbo);
        pBindBuffer(GL_ARRAY_BUFFER, vbo);
        pBufferData(GL_ARRAY_BUFFER, sizeof zero, zero, GL_DYNAMIC_DRAW);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, (void *)0);
    } else if (mode == 2) {
        GLubyte none[4 * 4 * 4];
        memset(none, 0, sizeof none);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, none);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glEnable(GL_TEXTURE_2D);
    }
    frame(mode, draws);                 /* the first frame (shaders made) is not timed */
    gla_swap(c, b);
    gla_get_stats(NULL, 1);
    t0 = seconds();
    for (f = 0; f < frames; f++) {
        frame(mode, draws);
        gla_swap(c, b);
    }
    t1 = seconds();
    gla_get_stats(&st, 1);
    /* the last quad drawn: its middle must be its colour */
    if (probe >= cols * rows) probe = cols * rows - 1;
    quad_of(probe, v);
    colour_of(probe, want);
    px = (unsigned)(((v[0] + v[2]) / 2 + 1) / 2 * (GLfloat)sw);
    py = (unsigned)((1 - ((v[1] + v[5]) / 2 + 1) / 2) * (GLfloat)sh);
    if (px >= sw) px = sw - 1;
    if (py >= sh) py = sh - 1;
    {
        const unsigned char *p = shot + (py * sw + px) * 3;
        int k;
        ok = 1;
        for (k = 0; k < 3; k++) if (p[k] > want[k] + 8 || want[k] > p[k] + 8) ok = 0;
        printf("GLDraws %s: %d frames of %d draws in %.2f s: %.1f frames a second; %.1f request blocks a frame, "
               "the 68k waited %.2f ms a frame for them; picture %s (%u,%u,%u)\n", names[mode], frames, draws, t1 - t0,
               t1 > t0 ? frames / (t1 - t0) : 0.0, (double)st.fences / frames, st.fence_wait_us / 1000.0 / frames,
               ok ? "right" : "WRONG", p[0], p[1], p[2]);
    }
    if (mode == 1) { glDisableClientState(GL_VERTEX_ARRAY); pBindBuffer(GL_ARRAY_BUFFER, 0); if (pDeleteBuffers) pDeleteBuffers(1, &vbo); }
    if (mode == 2) { glDisable(GL_TEXTURE_2D); glDeleteTextures(1, &tex); }
    return ok;
}

int main(int argc, char **argv)
{
    struct gla_config cfg = { GLA_COMPAT, 0, 0, 1, 0, 0, 0 };
    struct gla_present hook = { present, NULL };
    struct gla_display *d = NULL;
    struct gla_context *c = NULL;
    struct gla_buffer *b = NULL;
    int i, cpu = 0, mode = -1, frames = 100, draws = 300, right = 0, ran = 0;
    sw = 320; sh = 240;
    for (i = 1; i < argc; i++) {
        if (!strcasecmp(argv[i], "CPU")) cpu = 1;
        else if (!strcasecmp(argv[i], "MODE") && i + 1 < argc) {
            const char *m = argv[++i];
            mode = !strcasecmp(m, "immediate") ? 0 : !strcasecmp(m, "buffersubdata") ? 1 : !strcasecmp(m, "texsubimage") ? 2 : -1;
        } else if (!strcasecmp(argv[i], "FRAMES") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcasecmp(argv[i], "DRAWS") && i + 1 < argc) draws = atoi(argv[++i]);
        else if (!strcasecmp(argv[i], "WIDTH") && i + 1 < argc) sw = (unsigned)atoi(argv[++i]);
        else if (!strcasecmp(argv[i], "HEIGHT") && i + 1 < argc) sh = (unsigned)atoi(argv[++i]);
    }
    if (frames < 1) frames = 1;
    if (draws < 1) draws = 1;
    if (sw < 16 || sw > 4096) sw = 320;
    if (sh < 16 || sh > 4096) sh = 240;
    if (!(shot = calloc(sw * sh, 3))) { printf("GLDraws: no memory\n"); return 20; }
#ifdef __amigaos__
    if (!cpu) {
        struct gla_virgl_transport t;
        if (gla_os3_virgl_transport(&t)) d = gla_display_create_virgl(&t);
    }
#endif
    (void)cpu;
    if (!d) d = gla_display_create();
    c = d ? gla_context_create(d, &cfg, NULL) : NULL;
    b = c ? gla_buffer_create(d, &cfg, sw, sh, &hook) : NULL;
    if (!b || !gla_make_current(c, b)) {
        printf("GLDraws: GL couldn't start\n");
        return 20;
    }
    pGenBuffers = (PFNGLGENBUFFERSPROC)gla_get_proc_address("glGenBuffers");
    pBindBuffer = (PFNGLBINDBUFFERPROC)gla_get_proc_address("glBindBuffer");
    pBufferData = (PFNGLBUFFERDATAPROC)gla_get_proc_address("glBufferData");
    pBufferSubData = (PFNGLBUFFERSUBDATAPROC)gla_get_proc_address("glBufferSubData");
    pDeleteBuffers = (PFNGLDELETEBUFFERSPROC)gla_get_proc_address("glDeleteBuffers");
    printf("GLDraws: %s (%s), %u x %u\n", (const char *)glGetString(GL_RENDERER), gla_display_driver(d), sw, sh);
    glViewport(0, 0, (GLsizei)sw, (GLsizei)sh);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    for (i = 0; i < 3; i++)
        if (mode < 0 || mode == i) { right += run(c, b, i, frames, draws); ran++; }
    gla_make_current(NULL, NULL);
    gla_buffer_destroy(b);
    gla_context_destroy(c);
    gla_display_destroy(d);
    free(shot);
    return right == ran ? 0 : 5;
}
