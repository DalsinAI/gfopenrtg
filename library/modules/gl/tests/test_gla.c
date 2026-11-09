/* Copyright (c) 2026 AmiCradle Team. Mesa for AmigaOS 3.2 (Open), MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GLA core draws three scenes with softpipe (GL 1.x immediate mode,
 * GLES 2 shaders, GL core with a vertex array), each presented through the
 * hook, and their pictures' hashes are checked against a stored list. Then,
 * built with GLA_TEST_VIRGL and with ACVIRGL_LIB naming virglrenderer, the
 * same scenes through virgl on this machine's graphics chip (ACVirgl's host
 * side called directly, with "Amiga memory" an arena), each compared with
 * softpipe's picture: a graphics chip rounds a little differently, so a
 * picture matches when 99% of its pixels are within 8 of softpipe's on
 * every channel.
 *   test_gla [golden.txt] [dir]    dir: also write each picture as a .ppm */
#include "../gla/gla_core.h"
#include "../gla/gla_virgl.h"           /* gla_virgl_batch_wanted, its counts */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_GLEXT_PROTOTYPES 0
#include <GL/gl.h>
#include <GL/glext.h>

#define W 160
#define H 120

static unsigned char shot[W * H * 3];   /* R, G, B */
static unsigned char soft[3][W * H * 3];   /* softpipe's pictures */
static unsigned char soft_draws[3][W * H * 3];   /* ... of the 300-draw frames */
static int presents;

static void present(void *user, const struct gla_frame *f)
{
    unsigned x, y;
    int r = f->format == GLA_ARGB ? 1 : 2, g = f->format == GLA_ARGB ? 2 : 1, b = f->format == GLA_ARGB ? 3 : 0;
    (void)user;
    for (y = 0; y < f->h && y < H; y++) {
        const unsigned char *row = (const unsigned char *)f->pixels + y * f->stride;
        for (x = 0; x < f->w && x < W; x++) {
            shot[(y * W + x) * 3 + 0] = row[x * 4 + r];
            shot[(y * W + x) * 3 + 1] = row[x * 4 + g];
            shot[(y * W + x) * 3 + 2] = row[x * 4 + b];
        }
    }
    presents++;
}

static unsigned long hash_shot(void)
{
    unsigned long h = 2166136261u;
    int i;
    for (i = 0; i < W * H * 3; i++)
        h = ((h ^ shot[i]) * 16777619u) & 0xffffffffu;
    return h;
}

static void save_ppm(const char *dir, const char *name)
{
    char path[512];
    FILE *f;
    if (!dir)
        return;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    fwrite(shot, 1, sizeof shot, f);
    fclose(f);
}

static PFNGLCREATESHADERPROC pCreateShader;
static PFNGLSHADERSOURCEPROC pShaderSource;
static PFNGLCOMPILESHADERPROC pCompileShader;
static PFNGLGETSHADERIVPROC pGetShaderiv;
static PFNGLGETSHADERINFOLOGPROC pGetShaderInfoLog;
static PFNGLCREATEPROGRAMPROC pCreateProgram;
static PFNGLATTACHSHADERPROC pAttachShader;
static PFNGLBINDATTRIBLOCATIONPROC pBindAttribLocation;
static PFNGLLINKPROGRAMPROC pLinkProgram;
static PFNGLGETPROGRAMIVPROC pGetProgramiv;
static PFNGLUSEPROGRAMPROC pUseProgram;
static PFNGLVERTEXATTRIBPOINTERPROC pVertexAttribPointer;
static PFNGLENABLEVERTEXATTRIBARRAYPROC pEnableVertexAttribArray;
static PFNGLGENBUFFERSPROC pGenBuffers;
static PFNGLBINDBUFFERPROC pBindBuffer;
static PFNGLBUFFERDATAPROC pBufferData;
static PFNGLGENVERTEXARRAYSPROC pGenVertexArrays;
static PFNGLBINDVERTEXARRAYPROC pBindVertexArray;
static PFNGLGETUNIFORMLOCATIONPROC pGetUniformLocation;
static PFNGLUNIFORM1FPROC pUniform1f;

#define GET(p, n) do { *(void **)&p = gla_get_proc_address(n); if (!p) { printf("no %s\n", n); return 1; } } while (0)

static int get_procs(void)
{
    GET(pCreateShader, "glCreateShader");
    GET(pShaderSource, "glShaderSource");
    GET(pCompileShader, "glCompileShader");
    GET(pGetShaderiv, "glGetShaderiv");
    GET(pGetShaderInfoLog, "glGetShaderInfoLog");
    GET(pCreateProgram, "glCreateProgram");
    GET(pAttachShader, "glAttachShader");
    GET(pBindAttribLocation, "glBindAttribLocation");
    GET(pLinkProgram, "glLinkProgram");
    GET(pGetProgramiv, "glGetProgramiv");
    GET(pUseProgram, "glUseProgram");
    GET(pVertexAttribPointer, "glVertexAttribPointer");
    GET(pEnableVertexAttribArray, "glEnableVertexAttribArray");
    GET(pGenBuffers, "glGenBuffers");
    GET(pBindBuffer, "glBindBuffer");
    GET(pBufferData, "glBufferData");
    GET(pGenVertexArrays, "glGenVertexArrays");
    GET(pBindVertexArray, "glBindVertexArray");
    GET(pGetUniformLocation, "glGetUniformLocation");
    GET(pUniform1f, "glUniform1f");
    return 0;
}

static GLuint program(const char *vs, const char *fs)
{
    const char *src[2] = { vs, fs };
    GLenum kind[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
    GLuint p = pCreateProgram();
    GLint ok;
    int i;
    for (i = 0; i < 2; i++) {
        GLuint s = pCreateShader(kind[i]);
        pShaderSource(s, 1, &src[i], NULL);
        pCompileShader(s);
        pGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            pGetShaderInfoLog(s, sizeof log, NULL, log);
            printf("shader: %s\n", log);
            return 0;
        }
        pAttachShader(p, s);
    }
    pBindAttribLocation(p, 0, "pos");
    pBindAttribLocation(p, 1, "col");
    pLinkProgram(p);
    pGetProgramiv(p, GL_LINK_STATUS, &ok);
    return ok ? p : 0;
}

static const GLfloat tri[] = {
    -0.8f, -0.7f,  1, 0, 0,
     0.8f, -0.5f,  0, 1, 0,
     0.1f,  0.8f,  0, 0, 1,
};

/* 1: GL 1.x, depth test, a smooth triangle under a flat quad. */
static void scene_fixed(void)
{
    glViewport(0, 0, W, H);
    glClearColor(0.1f, 0.2f, 0.3f, 1);
    glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0); glVertex3f(-0.8f, -0.7f, 0.5f);
    glColor3f(0, 1, 0); glVertex3f(0.8f, -0.5f, 0.5f);
    glColor3f(0, 0, 1); glVertex3f(0.1f, 0.8f, 0.5f);
    glEnd();
    glColor3f(1, 1, 0);
    glBegin(GL_QUADS);
    glVertex3f(-0.3f, -0.3f, 0.2f); glVertex3f(0.3f, -0.3f, 0.2f);
    glVertex3f(0.3f, 0.3f, 0.8f); glVertex3f(-0.3f, 0.3f, 0.8f);
    glEnd();
}

/* 2: GLES 2 shaders from a client-side array. */
static int scene_es2(void)
{
    GLuint p = program(
        "attribute vec2 pos; attribute vec3 col; varying vec3 v;\n"
        "void main() { v = col; gl_Position = vec4(pos, 0.0, 1.0); }\n",
        "precision mediump float; varying vec3 v; uniform float k;\n"
        "void main() { gl_FragColor = vec4(v * k, 1.0); }\n");
    if (!p)
        return 1;
    glViewport(0, 0, W, H);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pUseProgram(p);
    pUniform1f(pGetUniformLocation(p, "k"), 0.75f);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri);
    pVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri + 2);
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return 0;
}

/* 3: GL 3.3 core: a vertex array object and a buffer. */
static int scene_core(void)
{
    GLuint vao, vbo, p = program(
        "#version 330 core\nin vec2 pos; in vec3 col; out vec3 v;\n"
        "void main() { v = col; gl_Position = vec4(pos.y, -pos.x, 0.0, 1.0); }\n",
        "#version 330 core\nin vec3 v; out vec4 o;\n"
        "void main() { o = vec4(v.bgr, 1.0); }\n");
    if (!p)
        return 1;
    pGenVertexArrays(1, &vao);
    pBindVertexArray(vao);
    pGenBuffers(1, &vbo);
    pBindBuffer(GL_ARRAY_BUFFER, vbo);
    pBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
    pVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)0);
    pVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), (void *)(2 * sizeof(GLfloat)));
    pEnableVertexAttribArray(0);
    pEnableVertexAttribArray(1);
    glViewport(0, 0, W, H);
    glClearColor(0.5f, 0.5f, 0.5f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    pUseProgram(p);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    return 0;
}

struct scene {
    const char *name;
    struct gla_config cfg;
    int (*draw)(void);
};

static int draw_fixed(void) { scene_fixed(); return 0; }

static const struct scene scenes[] = {
    { "gl1-fixed", { GLA_COMPAT, 0, 0, 1, 24, 0, 0 }, draw_fixed },
    { "es2-shaders", { GLA_ES2, 2, 0, 1, 0, 0, 0 }, scene_es2 },
    { "gl33-core", { GLA_CORE, 3, 3, 0, 0, 0, 0 }, scene_core },
};

/* 300 draws a frame (9 Nov 2026, for GL.module's batched request blocks):
 * a 20 x 15 grid of quads, each its own draw, the three ways games feed them:
 *   0 immediate mode (Mesa's upload buffer, written unsynchronized);
 *   1 one buffer object rewritten with glBufferSubData before each draw (a
 *     buffer a queued draw still reads: Mesa flushes at every draw);
 *   2 one 4x4 texture rewritten with glTexSubImage2D before each draw (a
 *     lightmap's pattern), drawn GL_REPLACE. */
#define NDRAWS 300
#define DRAW_FRAMES 3
static PFNGLBUFFERSUBDATAPROC pBufferSubData;
static const char *const draw_modes[3] = { "immediate", "buffersubdata", "texsubimage" };

static void quad_of(int i, GLfloat *v)
{
    GLfloat x0 = -1.0f + (GLfloat)(i % 20) * 0.1f, y0 = -1.0f + (GLfloat)(i / 20) * (2.0f / 15.0f);
    GLfloat x1 = x0 + 0.1f, y1 = y0 + 2.0f / 15.0f;
    v[0] = x0; v[1] = y0; v[2] = x1; v[3] = y0; v[4] = x1; v[5] = y1; v[6] = x0; v[7] = y1;
}

static void colour_of(int i, int frame, GLubyte *c)
{
    c[0] = (GLubyte)(i * 37 + frame * 11); c[1] = (GLubyte)(i * 91 + 64); c[2] = (GLubyte)(255 - i * 13); c[3] = 255;
}

static void frame_draws(int mode, int frame, GLuint vbo, GLuint tex)
{
    GLfloat v[8];
    GLubyte c[4], texels[4 * 4 * 4];
    static const GLfloat st[8] = { 0, 0, 1, 0, 1, 1, 0, 1 };
    int i, k;
    (void)vbo; (void)tex;               /* bound already */
    glViewport(0, 0, W, H);
    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < NDRAWS; i++) {
        quad_of(i, v);
        colour_of(i, frame, c);
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

#ifdef GLA_TEST_VIRGL
static unsigned long t_blocks;          /* request blocks the transport ran */
#endif

/* DRAW_FRAMES frames of a mode on display d, the last one's picture in keep;
 * the request blocks a frame (the transport's count) in *per_frame. */
static int run_draws(struct gla_display *d, int mode, unsigned char *keep, double *per_frame,
                     long (*direct)(void *, const struct gla_virgl_frame *))
{
    static const struct gla_config cfg = { GLA_COMPAT, 0, 0, 1, 0, 0, 0 };
    struct gla_present hook = { present, NULL };
    struct gla_context *c = gla_context_create(d, &cfg, NULL);
    struct gla_buffer *b = gla_buffer_create(d, &cfg, W, H, &hook);
    GLuint vbo = 0, tex = 0;
    unsigned long b0 = 0;
    int f, before = presents;
    gla_present_set_virgl(present, direct);     /* NULL: read back */
    if (!c || !b || !gla_make_current(c, b)) { printf("draws: no context\n"); return -1; }
    if (get_procs()) return -1;
    *(void **)&pBufferSubData = gla_get_proc_address("glBufferSubData");
    if (!pBufferSubData) { printf("no glBufferSubData\n"); return -1; }
    if (mode == 1) {
        GLfloat zero[8] = { 0 };
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
    glFinish();
#ifdef GLA_TEST_VIRGL
    b0 = t_blocks;
#endif
    for (f = 0; f < DRAW_FRAMES; f++) {
        frame_draws(mode, f, vbo, tex);
        gla_swap(c, b);
    }
#ifdef GLA_TEST_VIRGL
    *per_frame = (double)(t_blocks - b0) / DRAW_FRAMES;
#else
    (void)b0;
    *per_frame = 0;
#endif
    if (glGetError() != GL_NO_ERROR) { printf("draws %s: GL error\n", draw_modes[mode]); return 1; }
    if (presents - before != DRAW_FRAMES) { printf("draws %s: %d presents\n", draw_modes[mode], presents - before); return 1; }
    memcpy(keep, shot, sizeof shot);
    gla_make_current(NULL, NULL);
    gla_buffer_destroy(b);
    gla_context_destroy(c);
    return 0;
}

#if defined(__amigaos__)
/* libnix gives main a stack this big, whatever the Shell's Stack is. */
unsigned long __stack = 1024 * 1024;
#endif

/* How far the test got, flushed at once, so a crash still shows it. */
static void step(const char *what)
{
    printf("gla: %s\n", what);
    fflush(stdout);
}

#if defined(__amigaos__)
#include "../gla/os3/gla_virgl_os3.h"
#define GLA_TEST_VIRGL 1
#elif defined(GLA_TEST_VIRGL)
#include "host/virgl/acvirgl.h"
/* "Amiga memory" for the host side: an arena, addresses from 16 MiB. */
#define ARENA (64u << 20)
#define BASE 0x01000000u
static unsigned char *arena;
static unsigned long arena_used;
static struct acvirgl *host;
static void *t_alloc(void *u, unsigned long n)
{
    unsigned long at = (arena_used + 63) & ~63ul;
    (void)u;
    if (at + n > ARENA) return NULL;
    arena_used = at + n;
    memset(arena + at, 0, n);
    return arena + at;
}
static void t_free(void *u, void *p) { (void)u; (void)p; }
static unsigned long t_addr(void *u, const void *p) { (void)u; return BASE + (unsigned long)((const unsigned char *)p - arena); }
static uint8_t *t_map(void *u, uint32_t a, uint32_t len) { (void)u; return a >= BASE && a - BASE <= ARENA && len <= ARENA - (a - BASE) ? arena + (a - BASE) : NULL; }
static int t_run1(void *block, unsigned long bytes) { t_blocks++; return acvirgl_run(host, t_map, NULL, (uint32_t)t_addr(NULL, block), (uint32_t)bytes); }

/* Blocks sent without waiting (gla_virgl_send, 9 Nov 2026): run only when
 * waited for, or before the next block that is run, in the order sent, as
 * late as the ring's worker could: the 68k's side has gone on meanwhile, so
 * any memory a block reads that GL.module wrote again shows in the picture. */
#define TPEND 16
static struct { void *block; unsigned long bytes, ticket; } t_pend[TPEND];
static int t_npend;
static unsigned long t_tickets, t_deferred, t_late_err;
static void t_run_pending(unsigned long upto)
{
    while (t_npend && t_pend[0].ticket <= upto) {
        if (t_run1(t_pend[0].block, t_pend[0].bytes)) t_late_err++;
        memmove(t_pend, t_pend + 1, sizeof t_pend[0] * (size_t)--t_npend);
    }
}
static int t_send(void *u, void *block, unsigned long bytes, unsigned long *ticket)
{
    (void)u;
    if (t_npend == TPEND) t_run_pending(t_pend[0].ticket);
    t_pend[t_npend].block = block; t_pend[t_npend].bytes = bytes; t_pend[t_npend].ticket = *ticket = ++t_tickets;
    t_npend++;
    t_deferred++;
    return 0;
}
static int t_wait(void *u, unsigned long ticket) { (void)u; t_run_pending(ticket); return 0; }
static int t_run(void *u, void *block, unsigned long bytes) { (void)u; t_run_pending(~0ul); return t_run1(block, bytes); }

/* The direct present (ACV_PRESENT) as gfopenrtg.library sends it, into an
 * XRGB32 "screen" in the arena: one rectangle, the frame's changed part. */
#include "include/gfopengpu/virgl.h"
#include "include/gfopengpu/stream.h"
static unsigned char *screen;
static uint32_t be(uint32_t v)
{
    const unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    uint32_t r;
    memcpy(&r, b, 4);
    return r;
}
static long present_direct(void *user, const struct gla_virgl_frame *f)
{
    static uint32_t *w;
    unsigned x, y;
    int32_t r;
    (void)user;
    if (!w && !(w = t_alloc(NULL, 16 * 4))) return -1;
    w[0] = be(ACV_HDR(ACV_PRESENT, 14)); w[1] = 0;
    w[2] = be((uint32_t)f->context); w[3] = be((uint32_t)f->handle); w[4] = 0;
    w[5] = 0;                                       /* the frame at the screen's 0, 0 */
    w[6] = be((uint32_t)t_addr(NULL, screen)); w[7] = be(W * 4); w[8] = be(OGPU_FMT_XRGB32);
    w[9] = be((W << 16) | H); w[10] = 0; w[11] = be(1);
    w[12] = be(((uint32_t)f->x0 << 16) | (uint32_t)f->y0);
    w[13] = be(((uint32_t)(f->x1 - f->x0) << 16) | (uint32_t)(f->y1 - f->y0));
    if (t_run(NULL, w, 14 * 4)) return -1;
    r = (int32_t)be(w[1]);
    if (r < 0) return -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            const unsigned char *px = screen + (y * W + x) * 4;
            if (px[0]) return -1;                   /* X is written 0 */
            shot[(y * W + x) * 3 + 0] = px[1];
            shot[(y * W + x) * 3 + 1] = px[2];
            shot[(y * W + x) * 3 + 2] = px[3];
        }
    presents++;
    return r;
}
#endif
#if defined(GLA_TEST_VIRGL)
/* The transport: GFOpenGPU's ring on the Amiga, the host side called directly here. */
static int virgl_transport(struct gla_virgl_transport *t, char *why, int whylen)
{
#if defined(__amigaos__)
    if (gla_os3_virgl_transport(t)) return 1;
    snprintf(why, whylen, "gfopengpu.library has no OGPU_OP_VIRGL here");
    return 0;
#else
    if (!(host = acvirgl_create(NULL, why, whylen))) return 0;
    if (!(arena = calloc(1, ARENA))) { snprintf(why, whylen, "no memory"); return 0; }
    t->alloc = t_alloc; t->free = t_free; t->addr = t_addr; t->run = t_run; t->user = NULL;
    return 1;
#endif
}
#endif

/* The three scenes on a display; their hashes in got, pictures in keep if given. */
static int run_scenes(struct gla_display *d, unsigned long *got, unsigned char (*keep)[W * H * 3], const char *dir, const char *tag,
                      long (*direct)(void *, const struct gla_virgl_frame *))
{
    struct gla_present hook = { present, NULL };
    int i, fails = 0;
    gla_present_set_virgl(present, direct);     /* NULL: no direct present */
    printf("gla: %s gives GL %d (compat), %d (core), GLES %d\n", gla_display_driver(d),
           gla_display_version(d, GLA_COMPAT), gla_display_version(d, GLA_CORE),
           gla_display_version(d, GLA_ES2));
    for (i = 0; i < 3; i++) {
        const struct scene *s = &scenes[i];
        struct gla_context *c;
        struct gla_buffer *b;
        int before = presents;
        char nm[96];
        step(s->name);
        c = gla_context_create(d, &s->cfg, NULL);
        b = gla_buffer_create(d, &s->cfg, W, H, &hook);
        if (!c || !b || !gla_make_current(c, b)) {
            printf("%s: no context\n", s->name);
            return -1;
        }
        if (get_procs())
            return -1;
        if (s->draw())
            return -1;
        if (glGetError() != GL_NO_ERROR) {
            printf("%s: GL error\n", s->name);
            fails++;
        }
        if (s->cfg.doublebuf)
            gla_swap(c, b);
        else
            glFinish();
        if (presents == before) {
            printf("%s: nothing presented\n", s->name);
            fails++;
        }
        got[i] = hash_shot();
        if (keep) memcpy(keep[i], shot, sizeof shot);
        printf("%s (%s): %s, picture %08lx\n", s->name, tag, (const char *)glGetString(GL_VERSION), got[i]);
        snprintf(nm, sizeof nm, "%s-%s", s->name, tag);
        save_ppm(dir, nm);
        gla_make_current(NULL, NULL);
        gla_buffer_destroy(b);
        gla_context_destroy(c);
    }
    return fails;
}

/* How close a picture is to softpipe's: the share of pixels within 8 on
 * every channel, in tenths of a percent. */
static int closeness(const unsigned char *a, const unsigned char *b)
{
    int i, near = 0;
    for (i = 0; i < W * H; i++) {
        int k, ok = 1;
        for (k = 0; k < 3; k++) { int dd = a[i * 3 + k] - b[i * 3 + k]; if (dd < -8 || dd > 8) ok = 0; }
        near += ok;
    }
    return near * 1000 / (W * H);
}

int main(int argc, char **argv)
{
    const char *golden = argc > 1 ? argv[1] : NULL;
    const char *dir = argc > 2 ? argv[2] : NULL;
    struct gla_display *d;
    unsigned long got[3];
    int i, fails = 0, r;

    step("start");
    d = gla_display_create();
    step("display made");
    if (!d) {
        printf("gla: no display\n");
        return 1;
    }
    if ((r = run_scenes(d, got, soft, dir, "softpipe", NULL)) < 0) return 1;
    fails += r;
    {
        static unsigned char keep[W * H * 3];
        double pf;
        for (i = 0; i < 3; i++) {
            if ((r = run_draws(d, i, keep, &pf, NULL)) < 0) return 1;
            fails += r;
            memcpy(soft_draws[i], keep, sizeof keep);
        }
    }
    gla_display_destroy(d);

    if (golden) {
        FILE *f = fopen(golden, "r");
        char name[64];
        unsigned long want;
        int seen = 0;
        if (!f) {
            printf("no %s\n", golden);
            return 1;
        }
        while (fscanf(f, "%63s %lx", name, &want) == 2)
            for (i = 0; i < 3; i++)
                if (!strcmp(name, scenes[i].name)) {
                    seen++;
                    if (got[i] != want) {
                        printf("%s: picture %08lx, stored %08lx\n", name, got[i], want);
                        fails++;
                    }
                }
        fclose(f);
        if (seen != 3) {
            printf("%s has %d of the 3 scenes\n", golden, seen);
            fails++;
        }
    }

#ifdef GLA_TEST_VIRGL
    {
        char err[256];
        static unsigned char gpu[3][W * H * 3];
        struct gla_virgl_transport t;
        if (!virgl_transport(&t, err, sizeof err))
            printf("gla: virgl skipped (%s)\n", err);
        else if (!(d = gla_display_create_virgl(&t))) {
            printf("gla: virgl: no display\n");
            fails++;
        } else {
            struct gla_stats st;
            gla_get_stats(NULL, 1);
            if ((r = run_scenes(d, got, gpu, dir, "virgl", NULL)) < 0) return 1;
            fails += r;
            for (i = 0; i < 3; i++) {
                int c = closeness(gpu[i], soft[i]);
                printf("%s: virgl's picture is %d.%d%% softpipe's\n", scenes[i].name, c / 10, c % 10);
                if (c < 990) fails++;
            }
            gla_get_stats(&st, 1);
            if (!st.frames || st.direct || st.readback != st.frames || st.direct_state != GLA_DIRECT_NO_HOOK) {
                printf("gla: virgl read back %lu of %lu frames\n", st.readback, st.frames);
                fails++;
            }
#if !defined(__amigaos__)
            /* The same scenes again, the host writing each frame straight
             * into the screen (ACV_PRESENT): the pictures byte for byte the
             * read-back ones, and nothing read back. */
            {
                unsigned long direct[3];
                if (!(screen = t_alloc(NULL, W * H * 4))) return 1;
                if ((r = run_scenes(d, direct, NULL, dir, "virgl-direct", present_direct)) < 0) return 1;
                fails += r;
                for (i = 0; i < 3; i++)
                    if (direct[i] != got[i]) {
                        printf("%s: the direct present's picture %08lx, read back %08lx\n", scenes[i].name, direct[i], got[i]);
                        fails++;
                    }
                gla_get_stats(&st, 1);
                printf("gla: virgl direct: %lu frames, %lu straight in (%lu pixels), %lu read back\n",
                       st.frames, st.direct, st.direct_pixels, st.readback);
                if (!st.frames || st.direct != st.frames || st.readback || st.direct_state != GLA_DIRECT_ON) fails++;
            }
#endif
            gla_display_destroy(d);
            /* 300 draws a frame, each command its own request block and then
             * batched (gla_virgl.c, 9 Nov 2026): the same pictures, byte for
             * byte, both near softpipe's, and fewer blocks batched */
            {
                /* 0 a block a command; 1 batched; then batched and pipelined
                 * (9 Nov 2026), blocks sent without waiting run as late as
                 * they may (t_send), 2 read back, 3 presented straight in */
                static const char *const how[4] = { "a command apart", "batched", "pipelined", "pipelined, direct" };
                static unsigned char pic[4][3][W * H * 3];
                double per[4][3];
                int b, m, nb = 2;
#if !defined(__amigaos__)
                nb = screen ? 4 : 3;
#endif
                for (b = 0; b < nb; b++) {
                    gla_virgl_batch_wanted = b ? 1 : 0;
#if !defined(__amigaos__)
                    gla_virgl_send = b >= 2 ? t_send : NULL;
                    gla_virgl_wait = b >= 2 ? t_wait : NULL;
                    gla_virgl_pipeline_wanted = b >= 2 ? 1 : -1;
                    t_deferred = 0;
#endif
                    if (!(d = gla_display_create_virgl(&t))) { printf("gla: virgl: no display (%s)\n", how[b]); fails++; nb = b; break; }
                    for (m = 0; m < 3; m++) {
#if !defined(__amigaos__)
                        if ((r = run_draws(d, m, pic[b][m], &per[b][m], b == 3 ? present_direct : NULL)) < 0) return 1;
#else
                        if ((r = run_draws(d, m, pic[b][m], &per[b][m], NULL)) < 0) return 1;
#endif
                        fails += r;
                    }
                    gla_display_destroy(d);
#if !defined(__amigaos__)
                    if (b >= 2) {
                        printf("gla: %s: %lu blocks sent without waiting, run late; %lu of them failed\n", how[b], t_deferred, t_late_err);
                        if (!t_deferred || t_late_err) fails++;
                    }
#endif
                }
                gla_virgl_batch_wanted = -1;
                gla_virgl_pipeline_wanted = -1;
                gla_virgl_send = NULL;
                gla_virgl_wait = NULL;
                for (m = 0; m < 3; m++) {
                    char line[400];
                    int k = 0, c0 = closeness(pic[0][m], soft_draws[m]);
                    k += snprintf(line + k, sizeof line - (size_t)k, "draws %s: %d a frame, %d.%d%% softpipe's; request blocks a frame:",
                                  draw_modes[m], NDRAWS, c0 / 10, c0 % 10);
                    if (c0 < 990) fails++;
                    for (b = 0; b < nb; b++) {
                        int same = !memcmp(pic[0][m], pic[b][m], sizeof pic[0][m]);
                        k += snprintf(line + k, sizeof line - (size_t)k, " %.1f %s%s", per[b][m], how[b], same ? "" : " (PICTURE DIFFERS)");
                        if (!same) fails++;
                        if (b && per[b][m] > per[0][m]) fails++;
                    }
                    printf("%s\n", line);
                }
#if !defined(__amigaos__)
                /* where Mesa flushes at every draw, batching must take the blocks a frame far down */
                if (nb >= 2 && per[1][1] * 4 > per[0][1]) { printf("draws buffersubdata: batching kept %.1f of %.1f blocks a frame\n", per[1][1], per[0][1]); fails++; }
#endif
                printf("gla: blocks: %lu, %lu of them unwaited; queued commands refused: %lu\n", gla_virgl_blocks, gla_virgl_unwaited, gla_virgl_batch_errors);
                if (gla_virgl_batch_errors) fails++;
            }
        }
    }
#endif
    printf(fails ? "gla: %d failed\n" : "gla: all tests passed\n", fails);
    return fails != 0;
}
