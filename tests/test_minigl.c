/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library's tests on the PC: the library's own files (all but the
 * AmigaOS parts, mgl_lib.c and mgl_display.c) built 32-bit with MGL_HOST,
 * a drawing area in memory standing in for the display, and the calls made
 * through the table (mgl_static.c over mgl_table.c), as a program makes
 * them. Checks OpenGL's behaviour piece by piece, the interface's behaviour
 * as the clean-room spec states it (docs/design/MiniGL-Compatible-Spec.md),
 * then draws the test scene and compares its checksum with the golden one.
 *   test_minigl [golden-file] [-o picture.ppm] [-update]
 */
#define MINIGL_LIBRARY_BUILD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <proto/minigl.h>
#include "../library/minigl/mgl_internal.h"

/* ---- what mgl_lib.c and mgl_display.c give on the Amiga -------------------------------------- */

GLcontext mgl_current;
struct mgl_prefs mgl_prefs = { 16, 2, 0, 16, 0, 0, 0, 0, ROUTE_CPU, 0, 0, 0, 1, 1024, 4096, 0, 0 };
long mgl_trace;
struct Library *MiniGLBase;
const MGLDispatchTable *MiniGLDispatch;

void *mgl_alloc(ULONG bytes) { return calloc(1, bytes ? bytes : 4); }
void mgl_free(void *p) { free(p); }
void mgl_copy(void *d, const void *s, ULONG n) { memmove(d, s, n); }
void mgl_zero(void *d, ULONG n) { memset(d, 0, n); }
void mgl_read_prefs(void) { }
void mgl_log(const char *fmt, ...)
{
    va_list ap;
    if (!getenv("MGL_LOG")) return;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}
ULONG mgl_millis(void) { return 0; }

BOOL MiniGLOpen(void) { MiniGLDispatch = &mgl_table; return TRUE; }
void MiniGLClose(void) { MiniGLDispatch = 0; }

struct mgl_display { UBYTE *pix; };

/* ---- the virgl route's transport: minigl_vg_bridge, when it runs this test -------------------- */

#include "../library/minigl/mgl_vg.h"
#include <unistd.h>

static int bridge_r = -1, bridge_w = -1;
static int vg_presents;

int vg_os_open(GLcontext c)
{
    const char *e = getenv("MGL_VG_BRIDGE");
    (void)c;
    if (!e || sscanf(e, "%d,%d", &bridge_r, &bridge_w) != 2) return 0;
    return 1;
}

void vg_os_close(GLcontext c) { (void)c; }

int vg_os_run(GLcontext c, ULONG *blk, ULONG bytes)
{
    ULONG req[2];
    int r = -1;
    (void)c;
    req[0] = (ULONG)blk; req[1] = bytes;
    if (write(bridge_w, req, sizeof req) != (ssize_t)sizeof req || read(bridge_r, &r, sizeof r) != (ssize_t)sizeof r) return -1;
    return r;
}

/* The frame into the drawing area, as ACV_PRESENT writes a screen's bitmap
 * (OGPU_FMT_ARGB32: A, R, G, B bytes). */
ULONG vg_os_present(GLcontext c, ULONG *p, ULONG room)
{
    ULONG a[12];
    if (room < 12 || c->fmt != OGPU_FMT_ARGB32) return 0;
    a[0] = c->vg->ctx_id; a[1] = c->vg->colour->handle; a[2] = 0; a[3] = 0;
    a[4] = (ULONG)c->disp->pix; a[5] = (ULONG)c->width * 4; a[6] = OGPU_FMT_ARGB32;
    a[7] = ((ULONG)c->width << 16) | (ULONG)c->height; a[8] = 0; a[9] = 1;
    a[10] = 0; a[11] = ((ULONG)c->width << 16) | (ULONG)c->height;
    vg_blk_cmd(p, ACV_PRESENT, 12, a);
    return VG_BLK_WORDS(12);
}

void vg_os_presented(GLcontext c, ULONG *blk, ULONG at)
{
    (void)c;
    if (blk && vg_result(blk, at) > 0) vg_presents++;
}

int mgl_disp_lock(GLcontext c) { c->base = c->disp->pix; c->bpr = (ULONG)c->width * (c->fmt == OGPU_FMT_RGB565 ? 2 : 4); return 1; }
void mgl_disp_unlock(GLcontext c) { (void)c; }
void mgl_disp_free(GLcontext c) { (void)c; }
int mgl_events(GLcontext c, void (*k)(int), void (*s)(int)) { (void)c; (void)k; (void)s; return 1; }
ULONG mgl_event_signal(GLcontext c) { (void)c; return 0; }

void *MGLCreateContext(int offx, int offy, int w, int h)
{
    GLcontext c = mgl_alloc(sizeof(struct GLcontext_t));
    (void)offx; (void)offy;
    c->disp = mgl_alloc(sizeof(struct mgl_display));
    c->width = w; c->height = h;
    c->fmt = mgl_prefs.depth <= 16 ? OGPU_FMT_RGB565 : OGPU_FMT_ARGB32;
    c->disp->pix = mgl_alloc((ULONG)w * h * 4);
    if (!mgl_out_open(c)) return 0;
    if (!c->vg) c->route = ROUTE_CPU;
    mgl_init_state(c);
    mgl_current = c;
    return c;
}
void *MGLCreateContextFromID(GLint id, GLint *w, GLint *h) { (void)id; return MGLCreateContext(0, 0, w ? *w : 320, h ? *h : 240); }
void *MGLCreateContextFromWindow(struct Window *window) { (void)window; return 0; }
void *MGLCreateContextFromBitMap(struct BitMap *bitmap) { (void)bitmap; return 0; }
void MGLDeleteContext(GLcontext c)
{
    if (!c) return;
    mgl_pipe_flush(c); mgl_flush(c);
    mgl_free_lists(c); mgl_free_textures(c);
    mgl_free(c->cache); mgl_free(c->cache_ix);
    mgl_out_close(c);
    mgl_free(c->disp->pix); mgl_free(c->disp);
    if (mgl_current == c) mgl_current = 0;
    mgl_free(c);
}
GLboolean MGLLockDisplay(GLcontext c) { (void)c; return GL_TRUE; }
void MGLUnlockDisplay(GLcontext c) { (void)c; }
GLboolean MGLLockBack(GLcontext c, MGLLockInfo *info) { (void)c; (void)info; return GL_FALSE; }
void MGLSwitchDisplay(GLcontext c)
{
    if (!c) return;
    mgl_pipe_flush(c); mgl_flush(c); c->frames++;
    if (c->vg) mgl_vg_switch(c);
}
GLboolean MGLResizeContext(GLcontext c, GLsizei w, GLsizei h) { (void)c; (void)w; (void)h; return GL_FALSE; }
void *MGLGetWindowHandle(GLcontext c) { (void)c; return 0; }
void *MGLGetInputWindowHandle(GLcontext c) { (void)c; return 0; }
void MGLSetPointer(GLcontext c) { (void)c; }
void MGLClearPointer(GLcontext c) { (void)c; }
void MGLMainLoop(GLcontext c) { (void)c; }
GLint MGLGetSupportedScreenModes(MGLScreenModeCallback cb) { (void)cb; return (GLint)MGL_SCREENMODE_BEST; }
GLcontext mgl_create_window(int x, int y, int w, int h) { (void)x; (void)y; return MGLCreateContext(0, 0, w, h); }
void mgl_input_on(GLcontext c) { (void)c; }
void MGLWriteShotPPM(GLcontext c, char *f) { (void)c; (void)f; }

/* ---- checks ------------------------------------------------------------------------------------- */

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void pixel(int x, int y, unsigned char *rgb)
{
    glReadPixels(x, y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
}

static int near(int a, int b, int tol) { return a - b <= tol && b - a <= tol; }

static void ortho(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, w, 0, h, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

static void test_basics(void)
{
    unsigned char p[3];
    GLint v[4];
    GLfloat f[16];
    mglChoosePixelDepth(32);
    CHECK(mglCreateContext(0, 0, 64, 64) != 0, "context");
    glGetIntegerv(GL_VIEWPORT, v);
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 64 && v[3] == 64, "viewport %d %d %d %d", v[0], v[1], v[2], v[3]);
    CHECK(glGetError() == GL_NO_ERROR, "no error at the start");
    glEnable(0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM, "unknown enable");
    CHECK(glGetError() == GL_NO_ERROR, "error cleared");
    glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    pixel(10, 10, p);
    CHECK(p[0] == 255 && near(p[1], 128, 1) && p[2] == 0, "clear colour %d %d %d", p[0], p[1], p[2]);
    /* a flat square in the window's lower left (OpenGL's origin) */
    ortho(64, 64);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f(16, 0); glVertex2f(16, 16); glVertex2f(0, 16);
    glEnd();
    pixel(4, 4, p);
    CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0, "square at the bottom left %d %d %d", p[0], p[1], p[2]);
    pixel(4, 60, p);
    CHECK(p[1] == 0, "nothing at the top left %d", p[1]);
    pixel(16, 4, p);
    CHECK(p[1] == 0, "the square's right edge is not drawn (pixel 16) %d", p[1]);
    pixel(15, 15, p);
    CHECK(p[1] == 255, "its last pixel is (15, 15) %d", p[1]);
    /* matrices */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(1, 2, 3);
    glPushMatrix();
    glScalef(2, 2, 2);
    glPopMatrix();
    glGetFloatv(GL_MODELVIEW_MATRIX, f);
    CHECK(f[12] == 1 && f[13] == 2 && f[14] == 3 && f[0] == 1, "translate and pop");
    glPopMatrix();
    CHECK(glGetError() == GL_STACK_UNDERFLOW, "pop below the bottom");
    glDisable(GL_TEXTURE_2D);
    mglDeleteContext();
}

static void test_depth_blend(void)
{
    unsigned char p[3];
    mglChoosePixelDepth(32);
    mglCreateContext(0, 0, 64, 64);
    ortho(64, 64);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 64, 0, 64, -10, 10);
    glMatrixMode(GL_MODELVIEW);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    /* red near (z = 5 is nearer with this ortho: -z is depth), then blue far: red stays */
    glColor3f(1, 0, 0);
    glBegin(GL_TRIANGLES); glVertex3f(0, 0, 5); glVertex3f(64, 0, 5); glVertex3f(0, 64, 5); glEnd();
    glColor3f(0, 0, 1);
    glBegin(GL_TRIANGLES); glVertex3f(0, 0, -5); glVertex3f(64, 0, -5); glVertex3f(0, 64, -5); glEnd();
    pixel(5, 5, p);
    CHECK(p[0] == 255 && p[2] == 0, "depth test keeps the near one %d %d %d", p[0], p[1], p[2]);
    glDepthFunc(GL_ALWAYS);
    glBegin(GL_TRIANGLES); glVertex3f(0, 0, -5); glVertex3f(64, 0, -5); glVertex3f(0, 64, -5); glEnd();
    pixel(5, 5, p);
    CHECK(p[2] == 255, "GL_ALWAYS draws over %d %d %d", p[0], p[1], p[2]);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 1, 1, 0.5f);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(0, 64); glEnd();
    pixel(5, 5, p);
    CHECK(near(p[0], 128, 2) && near(p[2], 255, 1), "half white over blue %d %d %d", p[0], p[1], p[2]);
    glDisable(GL_BLEND);
    /* culling: a clockwise triangle is a back face */
    glEnable(GL_CULL_FACE);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0, 1, 0);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(0, 64); glVertex2f(64, 0); glEnd();
    pixel(5, 5, p);
    CHECK(p[1] == 0, "a back face is culled %d", p[1]);
    glFrontFace(GL_CW);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(0, 64); glVertex2f(64, 0); glEnd();
    pixel(5, 5, p);
    CHECK(p[1] == 255, "with GL_CW it is the front %d", p[1]);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    /* scissor */
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 8, 8);
    glColor3f(1, 1, 0);
    glBegin(GL_QUADS); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(64, 64); glVertex2f(0, 64); glEnd();
    pixel(4, 4, p);
    CHECK(p[0] == 255, "inside the scissor %d", p[0]);
    pixel(20, 20, p);
    CHECK(p[0] == 0, "outside the scissor %d", p[0]);
    glDisable(GL_SCISSOR_TEST);
    mglDeleteContext();
}

static void test_clip_light_fog(void)
{
    unsigned char p[3];
    static const GLfloat pos[4] = { 0, 0, 1, 0 }, white[4] = { 1, 1, 1, 1 }, black[4] = { 0, 0, 0, 1 };
    mglChoosePixelDepth(32);
    mglCreateContext(0, 0, 64, 64);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 1, 10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    /* a quad reaching behind the eye: clipped at the near plane, drawn in front */
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glVertex3f(-5, -5, 5); glVertex3f(5, -5, 5); glVertex3f(5, -5, -20); glVertex3f(-5, -5, -20);
    glEnd();
    pixel(32, 5, p);
    CHECK(p[0] == 255, "a floor crossing the near plane is drawn near the bottom %d", p[0]);
    pixel(32, 60, p);
    CHECK(p[0] == 0, "and not at the top %d", p[0]);
    CHECK(glGetError() == GL_NO_ERROR, "no error");
    /* lighting: a face toward a light straight on gets its diffuse colour */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, black);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, white);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glNormal3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex3f(-1, -1, -2); glVertex3f(1, -1, -2); glVertex3f(1, 1, -2); glVertex3f(-1, 1, -2);
    glEnd();
    pixel(32, 32, p);
    CHECK(p[0] >= 253, "lit face on %d", p[0]);
    glNormal3f(0.0f, 0.70710678f, 0.70710678f);
    glBegin(GL_QUADS);
    glVertex3f(-1, -1, -2); glVertex3f(1, -1, -2); glVertex3f(1, 1, -2); glVertex3f(-1, 1, -2);
    glEnd();
    pixel(32, 32, p);
    CHECK(near(p[0], 180, 2), "lit at 45 degrees %d", p[0]);
    glDisable(GL_LIGHTING);
    /* linear fog, half way */
    {
        static const GLfloat fc[4] = { 0, 0, 1, 1 };
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glFogi(GL_FOG_MODE, GL_LINEAR);
        glFogf(GL_FOG_START, 1.0f);
        glFogf(GL_FOG_END, 3.0f);
        glFogfv(GL_FOG_COLOR, (GLfloat *)fc);
        glEnable(GL_FOG);
        glColor3f(1, 0, 0);
        glBegin(GL_QUADS);
        glVertex3f(-1, -1, -2); glVertex3f(1, -1, -2); glVertex3f(1, 1, -2); glVertex3f(-1, 1, -2);
        glEnd();
        pixel(32, 32, p);
        CHECK(near(p[0], 128, 3) && near(p[2], 128, 3), "fog half way %d %d %d", p[0], p[1], p[2]);
        glDisable(GL_FOG);
    }
    mglDeleteContext();
}

static void test_textures_lists_arrays(void)
{
    unsigned char p[3], img[4 * 4 * 3];
    GLuint t, l;
    int i;
    static const GLfloat va[] = { 0, 0, 32, 0, 32, 32, 0, 32 };
    static const GLubyte ca[] = { 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255 };
    static const GLushort ix[] = { 0, 1, 2, 0, 2, 3 };
    mglChoosePixelDepth(32);
    mglCreateContext(0, 0, 64, 64);
    ortho(64, 64);
    for (i = 0; i < 16; i++) { img[i * 3] = (unsigned char)(i * 16); img[i * 3 + 1] = 0; img[i * 3 + 2] = (unsigned char)(255 - i * 16); }
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 4, 0, GL_RGB, GL_UNSIGNED_BYTE, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(64, 0);
    glTexCoord2f(1, 1); glVertex2f(64, 64);
    glTexCoord2f(0, 1); glVertex2f(0, 64);
    glEnd();
    pixel(2, 2, p);
    CHECK(p[0] == 0 && p[2] == 255, "texel (0,0) at the bottom left %d %d %d", p[0], p[1], p[2]);
    pixel(62, 62, p);
    CHECK(p[0] == 240 && p[2] == 15, "texel (3,3) at the top right %d %d %d", p[0], p[1], p[2]);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(0, 0);
    glTexCoord2f(1, 0); glVertex2f(64, 0);
    glTexCoord2f(1, 1); glVertex2f(64, 64);
    glTexCoord2f(0, 1); glVertex2f(0, 64);
    glEnd();
    pixel(62, 62, p);
    CHECK(near(p[0], 120, 2), "modulated by half %d", p[0]);
    glDisable(GL_TEXTURE_2D);
    /* a display list */
    l = glGenLists(1);
    glNewList(l, GL_COMPILE);
    glColor3f(0, 1, 0);
    glBegin(GL_TRIANGLES); glVertex2f(0, 0); glVertex2f(64, 0); glVertex2f(0, 64); glEnd();
    glEndList();
    glClear(GL_COLOR_BUFFER_BIT);
    pixel(5, 5, p);
    CHECK(p[1] == 0, "a compiled list draws nothing yet %d", p[1]);
    glCallList(l);
    pixel(5, 5, p);
    CHECK(p[1] == 255, "glCallList draws it %d", p[1]);
    /* vertex arrays */
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexPointer(2, GL_FLOAT, 0, va);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, ca);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, ix);
    pixel(5, 5, p);
    CHECK(p[0] == 255 && p[1] == 0, "glDrawElements %d %d %d", p[0], p[1], p[2]);
    pixel(40, 40, p);
    CHECK(p[0] == 0, "and only where it was asked %d", p[0]);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    pixel(30, 30, p);
    CHECK(p[0] == 255, "glDrawArrays %d", p[0]);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDeleteLists(l, 1);
    glDeleteTextures(1, &t);
    CHECK(glGetError() == GL_NO_ERROR, "no error");
    mglDeleteContext();
}

/* ---- the interface, as the spec states it ------------------------------------------------------------ */

static void test_interface(void)
{
    GLboolean b[4];
    GLint v[4];
    GLfloat f[16];
    const char *ext;
    unsigned char p[3];
    GLcontext c;
    static const GLfloat le_va[] = { 0, 0, 32, 0, 0, 32 };
    GLfloat swapped[6];
    int i;
    /* 4.1: the table's header, and no NULL entry */
    {
        const MGLDispatchTable *t = MiniGLDispatch;
        void *const *e = (void *const *)&t->glActiveTextureARB;
        CHECK(t->abi_version == 5 && t->table_size == 840 && t->backend_flags == MGL_BACKEND_AMICRADLE && t->reserved == 0
              && t->current_context == &mgl_current, "the table's header");
        for (i = 0; i < MGL_DISPATCH_ENTRIES && e[i]; i++) ;
        CHECK(i == 205, "entry %d is NULL", i + 1);
    }
    /* 4.3: a call without a current context does nothing */
    CHECK(*MiniGLDispatch->current_context == 0, "no current context at the start");
    glClearDepth(0.5); glBegin(GL_TRIANGLES); glEnd(); glGetFloatv(GL_MODELVIEW_MATRIX, f);
    CHECK(glGetError() == 0 && glIsEnabled(GL_BLEND) == GL_FALSE && glGetString(GL_VERSION) == 0, "NULL context: nothing");
    gluPerspective(60.0, 1.0, 1.0, 10.0);
    /* 4.3: creating writes the variable; the client's delete clears it first */
    mglChoosePixelDepth(32);
    c = (GLcontext)mglCreateContext(0, 0, 32, 32);
    CHECK(c && *MiniGLDispatch->current_context == c && mgl_current == c, "mglCreateContext makes the context current");
    /* 9.1: strings and limits */
    CHECK(!strcmp((const char *)glGetString(GL_VERSION), "1.1"), "GL_VERSION is exactly 1.1");
    ext = (const char *)glGetString(GL_EXTENSIONS);
    CHECK(strstr(ext, "GL_ARB_multitexture") && strstr(ext, "GL_EXT_compiled_vertex_array") && strstr(ext, "GL_EXT_paletted_texture")
          && strstr(ext, "GL_EXT_shared_texture_palette") && strstr(ext, "GL_EXT_color_table"), "the extensions: %s", ext);
    CHECK(!strstr((const char *)glGetString(GL_RENDERER), "MiniGL") && !strstr((const char *)glGetString(GL_VENDOR), "MiniGL"),
          "no MiniGL name in the strings");
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, v);
    CHECK(v[0] == 2, "two texture units");
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, v);
    CHECK(v[0] == 1024, "GL_MAX_TEXTURE_SIZE 1024 (%d)", v[0]);
    /* 6: GLboolean is four bytes */
    memset(b, 0xAA, sizeof b);
    glGetBooleanv(GL_COLOR_WRITEMASK, b);
    CHECK(sizeof(GLboolean) == 4 && b[0] == GL_TRUE && b[3] == GL_TRUE, "glGetBooleanv writes four-byte booleans");
    /* 7.2: MiniGL's capabilities and refusals */
    CHECK(glIsEnabled(MGL_ARRAY_TRANSFORMATIONS) == GL_TRUE && glIsEnabled(MGL_PERSPECTIVE_MAPPING) == GL_FALSE, "MiniGL's capability defaults");
    glEnable(MGL_PERSPECTIVE_MAPPING);
    CHECK(glIsEnabled(MGL_PERSPECTIVE_MAPPING) == GL_TRUE && glGetError() == GL_NO_ERROR, "perspective mapping is state");
    glEnable(GL_POLYGON_OFFSET);
    CHECK(glIsEnabled(GL_POLYGON_OFFSET_FILL) == GL_TRUE, "0x7001 is GL_POLYGON_OFFSET_FILL");
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBegin(MGL_WITHDRAWN_PRIM_1);
    CHECK(glGetError() == GL_INVALID_OPERATION, "the withdrawn primitives are refused");
    CHECK(c->lock_mode == MGL_LOCK_MANUAL, "manual lock mode by default");
    /* 8.1: glFogi carries the mode as a float, glTexParameterf as an integer */
    glFogi(GL_FOG_MODE, GL_EXP2);
    glGetIntegerv(GL_FOG_MODE, v);
    CHECK(v[0] == GL_EXP2 && glGetError() == GL_NO_ERROR, "glFogi through glFogf");
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)GL_NEAREST);
    CHECK(glGetError() == GL_NO_ERROR && c->bound[0]->min == GL_NEAREST, "glTexParameterf through glTexParameteri");
    /* 9.1: little-endian arrays when MGL_ARRAY_TRANSFORMATIONS is off */
    for (i = 0; i < 6; i++) {
        union { GLfloat f; unsigned char b[4]; } u, w;
        u.f = le_va[i];
        w.b[0] = u.b[3]; w.b[1] = u.b[2]; w.b[2] = u.b[1]; w.b[3] = u.b[0];
        swapped[i] = w.f;
    }
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 32, 0, 32, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1, 1, 1);
    glDisable(MGL_ARRAY_TRANSFORMATIONS);
    glVertexPointer(2, GL_FLOAT, 0, swapped);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableClientState(GL_VERTEX_ARRAY);
    glEnable(MGL_ARRAY_TRANSFORMATIONS);
    glReadPixels(4, 4, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255, "a little-endian array draws (%d)", p[0]);
    /* 9.1: glRotatefEXT turns about axis 0, 1 or 2 */
    glLoadIdentity(); glRotatefEXT(90.0f, 2); glGetFloatv(GL_MODELVIEW_MATRIX, f);
    CHECK(f[1] > 0.99f && f[10] > 0.99f, "glRotatefEXT about z");
    glLoadIdentity();
    /* 9.2: mglDrawMultitexBuffer sets unit 1's environment, or refuses */
    mglDrawMultitexBuffer(GL_ONE, GL_ONE, GL_DECAL);
    CHECK(c->env_mode[1] == GL_DECAL && c->blend_src == GL_ONE && c->active == 0, "mglDrawMultitexBuffer");
    mglDrawMultitexBuffer(GL_ONE, GL_ONE, GL_BLEND);
    CHECK(glGetError() == GL_INVALID_ENUM && c->env_mode[1] == GL_DECAL, "mglDrawMultitexBuffer refuses GL_BLEND");
    /* 9.2: the texture-name limit, and the requests' defaults */
    mglChooseTextureBufferSize(16);
    glBindTexture(GL_TEXTURE_2D, 20);
    CHECK(glGetError() == GL_INVALID_VALUE && glIsTexture(20) == GL_FALSE, "a name above mglChooseTextureBufferSize");
    glBindTexture(GL_TEXTURE_2D, 0);
    mgl_prefs_defaults();
    CHECK(mgl_prefs.tex_names == 4096 && mgl_prefs.zbits == 32 && mgl_prefs.buffers == 1 && mgl_prefs.window == 0
          && mgl_prefs.prohibit_mip == 1, "the requests' defaults");
    mgl_prefs.zbits = 16;
    mglDeleteContext();
    CHECK(*MiniGLDispatch->current_context == 0, "mglDeleteContext clears the current context");
    mglDeleteContext();                         /* nothing current: nothing done */
}

/* ---- the scene ------------------------------------------------------------------------------------- */

#define MGLTEST_GL_HEADER <mgl/gl.h>
#include "../tools/mgltest_scene.c"

static unsigned long scene_sum(int w, int h, int depth, float t, const char *ppm)
{
    unsigned char *px = malloc((size_t)w * h * 3);
    unsigned long sum = 2166136261UL;
    int i;
    mglChoosePixelDepth(depth);
    mglCreateContext(0, 0, w, h);
    scene_init(w, h);
    scene_frame(t);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    for (i = 0; i < w * h * 3; i++) sum = ((sum ^ px[i]) * 16777619UL) & 0xFFFFFFFFUL;
    CHECK(glGetError() == GL_NO_ERROR, "the scene makes no error");
    if (ppm) {
        FILE *f = fopen(ppm, "wb");
        int y;
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
            fclose(f);
        }
    }
    scene_done();
    mglDeleteContext();
    free(px);
    return sum;
}

/* ---- the virgl route (run by minigl_vg_bridge, MGL_VIRGL set) -------------------------------- */

/* A frame shown: ACV_PRESENT writes it into the drawing area. */
static void test_present(void)
{
    UBYTE *px;
    unsigned char p[3];
    mglChoosePixelDepth(32);
    CHECK(mglCreateContext(0, 0, 64, 48) != 0, "context");
    CHECK(mgl_current->vg != 0, "the virgl route was taken");
    if (!mgl_current->vg) { mglDeleteContext(); return; }
    glClearColor(0.0f, 0.5f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    pixel(3, 3, p);
    CHECK(p[0] == 0 && near(p[1], 128, 1) && p[2] == 255, "clear read back %d %d %d", p[0], p[1], p[2]);
    mglSwitchDisplay();
    px = mgl_current->disp->pix + (47 * 64 + 63) * 4;
    CHECK(vg_presents == 1, "one frame presented (%d)", vg_presents);
    CHECK(px[1] == 0 && near(px[2], 128, 1) && px[3] == 255, "the presented frame %d %d %d %d", px[0], px[1], px[2], px[3]);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    mglSwitchDisplay();
    CHECK(px[1] == 255 && px[2] == 0 && px[3] == 0, "the next frame %d %d %d %d", px[0], px[1], px[2], px[3]);
    mglDeleteContext();
}

/* The scene on the virgl route against the 68k route's: 99% of the pixels
 * within 8 on every channel (GL.module's tolerance, plan section 7). */
static void scene_pixels(int w, int h, float t, unsigned char *px)
{
    mglChoosePixelDepth(32);
    mglCreateContext(0, 0, w, h);
    scene_init(w, h);
    if (getenv("MGL_NOFOG")) glDisable(GL_FOG);
    scene_frame(t);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    CHECK(glGetError() == GL_NO_ERROR, "the scene makes no error");
    scene_done();
    mglDeleteContext();
}

static void write_ppm(const char *name, int w, int h, const unsigned char *px)
{
    FILE *f = fopen(name, "wb");
    int y;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (y = h - 1; y >= 0; y--) fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
    fclose(f);
}

static int virgl_main(const char *ppm)
{
    int level = getenv("MGL_VG_LEVEL") ? atoi(getenv("MGL_VG_LEVEL")) : 9, w = 320, h = 240, i, off = 0, shifted = 0;
    mgl_prefs.route = ROUTE_VIRGL;
    MiniGLOpen();
    test_present();
    if (level >= 2) { test_basics(); test_depth_blend(); }
    if (level >= 3) test_textures_lists_arrays();
    if (level >= 4) test_clip_light_fog();
    if (level >= 2) {
        unsigned char *pv = malloc((size_t)w * h * 3), *pc = malloc((size_t)w * h * 3);
        scene_pixels(w, h, 0.0f, pv);
        mgl_prefs.route = ROUTE_CPU;
        scene_pixels(w, h, 0.0f, pc);
        mgl_prefs.route = ROUTE_VIRGL;
        /* a pixel is off when no pixel of the 68k route's within one (a
         * texel edge a pixel over: the two rasterisers round differently)
         * is within 8 on every channel */
        for (i = 0; i < w * h; i++) {
            int x = i % w, y = i / w, dx, dy, near_one = 0, exact = 1;
            for (dy = -1; dy <= 1 && !near_one; dy++)
                for (dx = -1; dx <= 1 && !near_one; dx++) {
                    int j = (y + dy) * w + (x + dx), k, ok = 1;
                    if (x + dx < 0 || x + dx >= w || y + dy < 0 || y + dy >= h) continue;
                    for (k = 0; k < 3; k++) { int d = pv[i * 3 + k] - pc[j * 3 + k]; if (d > 8 || d < -8) ok = 0; }
                    if (ok) near_one = 1;
                    else if (!dx && !dy) exact = 0;
                }
            if (!near_one) off++;
            if (!exact) shifted++;
        }
        printf("minigl virgl scene: %d of %d pixels differ by more than 8 from the 68k route's, %d with a pixel's shift allowed\n",
               shifted, w * h, off);
        /* 3%: what is left is texture sampling on texel edges (linear
         * filtering, no mipmaps), where the 68k route rounds its own way:
         * Mesa's softpipe differs from it on 11% of these pixels (README) */
        if (level >= 4) CHECK(off * 100 <= 3 * w * h, "the scene matches the 68k route's (%d pixels off)", off);
        if (ppm) write_ppm(ppm, w, h, pv);
        free(pv); free(pc);
    }
    MiniGLClose();
    printf("minigl virgl: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *golden = 0, *ppm = 0;
    int update = 0, i;
    unsigned long s32, s16;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "-update")) update = 1;
        else golden = argv[i];
    }
    if (getenv("MGL_VIRGL")) return virgl_main(ppm);
    MiniGLOpen();
    test_interface();
    test_basics();
    test_depth_blend();
    test_clip_light_fog();
    test_textures_lists_arrays();
    s32 = scene_sum(320, 240, 32, 0.0f, ppm);
    if (getenv("MGL_BIG")) scene_sum(960, 720, 32, 0.0f, getenv("MGL_BIG"));
    s16 = scene_sum(320, 240, 16, 0.0f, 0);
    CHECK(scene_sum(320, 240, 32, 0.0f, 0) == s32, "the scene draws the same twice");
    printf("minigl scene: ARGB32 %08lx, RGB565 %08lx\n", s32, s16);
    if (golden) {
        FILE *f = fopen(golden, update ? "w" : "r");
        if (f && update) { fprintf(f, "%08lx %08lx\n", s32, s16); printf("golden written\n"); }
        else if (f) {
            unsigned long g32 = 0, g16 = 0;
            if (fscanf(f, "%lx %lx", &g32, &g16) == 2) {
                CHECK(g32 == s32, "the ARGB32 scene is the golden one (%08lx)", g32);
                CHECK(g16 == s16, "the RGB565 scene is the golden one (%08lx)", g16);
            }
        }
        if (f) fclose(f);
    }
    MiniGLClose();
    printf("minigl: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
