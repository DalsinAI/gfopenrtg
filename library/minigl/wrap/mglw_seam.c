/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library as a face of GL.module: where MiniGL's own files meet the module.
 *
 * The wrapper keeps the files that are MiniGL's and not OpenGL's: the displays, input and mgl* calls
 * (../mgl_display.c), the choices (../mgl_choose.c), GLU and GLUT (../mgl_glu.c, ../mgl_glut.c) and the library
 * (../mgl_lib.c), built with MGLW defined. What they asked of MiniGL's own renderer they ask here instead:
 *
 *   mgl_out_open, mgl_out_close   a context's drawing: a session, a GL context and a buffer in GL.module that
 *                                 presents into the display's target (mgl_disp_target)
 *   mgl_vg_switch                 a finished frame: gla_swap
 *   mgl_flush, mgl_pipe_flush     glFlush; nothing waits on this side
 *   mgl_readback, mgl_vg_writeback  mglLockBack's pixels: glReadPixels, and back as glDrawPixels
 *   GLBegin ... GLViewport        the few GL calls those files make, as calls of the module's
 *
 * c->vg is set (to the wrapper's own state) so that the display takes the paths where the frame is shown by the
 * renderer and not copied by the display.
 *
 * Arguments go to the module through mglw_invoke as longs: a float is passed as its 32 bits (FL), since a
 * variable argument list would widen it to a double. */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include "../../modules/gl/gla/gla_core.h"
#include "../../modules/gl/gla/os3/gla_present_os3.h"

#include "mglw_session.h"
#include "../mgl_vg.h"

struct mglw_gl {
    struct gla_context *c;
    struct gla_buffer *b;
    struct gla_config cfg;
    struct gla_present hook;            /* read by GL.module while the buffer lives */
    struct gla_os3_target target;
    struct RastPort bmrp;               /* a program's bitmap, as a RastPort */
};

static ULONG FL(float f)
{
    union { float f; ULONG u; } v;
    v.f = f;
    return v.u;
}

#define G1(c, w, a) mglw_gla((c)->s, (w), 1, (ULONG)(a), 0, 0, 0, 0)
#define G2(c, w, a, b) mglw_gla((c)->s, (w), 2, (ULONG)(a), (ULONG)(b), 0, 0, 0)
#define G3(c, w, a, b, d) mglw_gla((c)->s, (w), 3, (ULONG)(a), (ULONG)(b), (ULONG)(d), 0, 0)
#define GL(c, name) MGLW_F(c, name)

/* ---- the context in the module ------------------------------------------------------------- */

void mglw_switch(struct mglw_context *x)
{
    GLcontext c = (GLcontext)x;
    if (G2(c, MGLW_G_MAKE_CURRENT, c->wgl->c, c->wgl->b)) c->s->current = x;
}

static void current(GLcontext c)
{
    if (c->s->current != (struct mglw_context *)c) mglw_switch((struct mglw_context *)c);
}

/* Where the display shows a frame, as GLA's target. */
static void set_target(GLcontext c)
{
    struct mglw_gl *g = c->wgl;
    struct RastPort *rp;
    struct BitMap *bm;
    int x, y, kind = mgl_disp_target(c, &rp, &bm, &x, &y);
    if (kind == MGL_TARGET_BITMAP) {
        InitRastPort(&g->bmrp);
        g->bmrp.BitMap = bm;
        rp = &g->bmrp;
    }
    g->target.rp = rp;
    g->target.bitmap = kind == MGL_TARGET_SCREEN ? bm : NULL;  /* a screen the library owns: drawn into directly */
    g->target.left = x;
    g->target.top = y;
    g->target.box_w = g->target.box_h = 0;
}

int mgl_out_open(GLcontext c)
{
    struct mglw_gl *g = AllocVec(sizeof *g, MEMF_ANY | MEMF_CLEAR);
    if (!g) return 0;
    c->wgl = g;
    if (!(c->s = mglw_session_get())) return 0;
    c->route = c->s->virgl ? ROUTE_VIRGL : ROUTE_CPU;
    g->cfg.profile = GLA_COMPAT;        /* OpenGL with the fixed pipeline: MiniGL's calls are its calls */
    g->cfg.doublebuf = 1;
    g->cfg.depth = 24;
    g->cfg.stencil = 8;
    set_target(c);
    G2(c, MGLW_G_OS3_PRESENT_INIT, &g->hook, &g->target);
    g->c = (struct gla_context *)G3(c, MGLW_G_CONTEXT_CREATE, c->s->display, &g->cfg, 0);
    if (g->c)
        g->b = (struct gla_buffer *)mglw_gla(c->s, MGLW_G_BUFFER_CREATE, 5, (ULONG)c->s->display, (ULONG)&g->cfg,
                                             (ULONG)c->width, (ULONG)c->height, (ULONG)&g->hook);
    if (!g->c || !g->b) return 0;      /* mgl_out_close follows (mgl_display.c's finish) */
    c->vg = (struct mgl_vg *)g;
    current(c);
    return c->s->current == (struct mglw_context *)c;
}

void mgl_out_close(GLcontext c)
{
    struct mglw_gl *g = c->wgl;
    if (c->s) {
        if (c->s->current == (struct mglw_context *)c) {
            G2(c, MGLW_G_MAKE_CURRENT, 0, 0);
            c->s->current = NULL;
        }
        if (g && g->b) G1(c, MGLW_G_BUFFER_DESTROY, g->b);
        if (g && g->c) G1(c, MGLW_G_CONTEXT_DESTROY, g->c);
        mglw_session_release(c->s);
        c->s = NULL;
    }
    if (g) FreeVec(g);
    c->wgl = NULL;
    c->vg = NULL;
}

/* mglResizeContext, after the display has its new screen. */
int mglw_resize(GLcontext c, int width, int height)
{
    set_target(c);
    G3(c, MGLW_G_BUFFER_RESIZE, c->wgl->b, width, height);
    current(c);
    mglw_invoke(c->s, GL(c, glViewport), 4, 0, 0, (ULONG)width, (ULONG)height);
    return 1;
}

/* ---- frames -------------------------------------------------------------------------------- */

void mgl_vg_switch(GLcontext c)
{
    current(c);
    G2(c, MGLW_G_SWAP, c->wgl->c, c->wgl->b);
}

void mgl_flush(GLcontext c)
{
    if (!c->s || !c->wgl || !c->wgl->c) return;
    current(c);
    mglw_invoke(c->s, GL(c, glFlush), 0);
}

void mgl_pipe_flush(GLcontext c) { (void)c; }

/* The frame's pixels, row 0 at the top, as ARGB longs. OpenGL's rows start at the bottom and its bytes come R, G,
 * B, A: read a row at a time, turned round here. */
void mgl_readback(GLcontext c, int x, int y, int w, int h, ULONG *argb, int stride)
{
    int row, i;
    current(c);
    mglw_invoke(c->s, GL(c, glPixelStorei), 2, GL_PACK_ALIGNMENT, 1);
    for (row = 0; row < h; row++) {
        ULONG *p = argb + (ULONG)row * (ULONG)stride;
        mglw_invoke(c->s, GL(c, glReadPixels), 7, (ULONG)x, (ULONG)(c->height - 1 - (y + row)), (ULONG)w, 1,
                    GL_RGBA, GL_UNSIGNED_BYTE, (ULONG)p);
        for (i = 0; i < w; i++) p[i] = (p[i] >> 8) | (p[i] << 24);     /* R,G,B,A bytes to A,R,G,B */
    }
}

/* mglLockBack's pixels back (R, G, B, A bytes, row 0 at the top): drawn over the frame a row at a time, with
 * nothing in the way (no test, no blend, no texture), and the state put back by the attribute stack. */
void mgl_vg_writeback(GLcontext c, const ULONG *rgba)
{
    int row;
    current(c);
    mglw_invoke(c->s, GL(c, glPushAttrib), 1, GL_ALL_ATTRIB_BITS);
    mglw_invoke(c->s, GL(c, glDisable), 1, GL_DEPTH_TEST);
    mglw_invoke(c->s, GL(c, glDisable), 1, GL_ALPHA_TEST);
    mglw_invoke(c->s, GL(c, glDisable), 1, GL_BLEND);
    mglw_invoke(c->s, GL(c, glDisable), 1, GL_SCISSOR_TEST);
    mglw_invoke(c->s, GL(c, glDisable), 1, GL_TEXTURE_2D);
    mglw_invoke(c->s, GL(c, glDisable), 1, GL_FOG);
    mglw_invoke(c->s, GL(c, glDisable), 1, GL_LIGHTING);
    mglw_invoke(c->s, GL(c, glPixelStorei), 2, GL_UNPACK_ALIGNMENT, 1);
    for (row = 0; row < c->height; row++) {
        mglw_invoke(c->s, GL(c, glWindowPos2i), 2, 0, (ULONG)(c->height - 1 - row));
        mglw_invoke(c->s, GL(c, glDrawPixels), 5, (ULONG)c->width, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                    (ULONG)(rgba + (ULONG)row * (ULONG)c->width));
    }
    mglw_invoke(c->s, GL(c, glPopAttrib), 0);
}

/* ---- what the kept files ask of the state and the renderer ----------------------------------- */

/* A new context: OpenGL's own defaults are the module's; MiniGL's clear colour is opaque white (spec 9.2). */
void mgl_init_state(GLcontext c)
{
    c->unpack_align = c->pack_align = 4;
    current(c);
    mglw_invoke(c->s, GL(c, glClearColor), 4, FL(1.0f), FL(1.0f), FL(1.0f), FL(1.0f));
    mglw_invoke(c->s, GL(c, glViewport), 4, 0, 0, (ULONG)c->width, (ULONG)c->height);
}

/* An error from the kept files (a bad argument to an mgl* call): the first is kept until glGetError reads it. */
void mgl_error(GLcontext c, GLenum e)
{
    if (c && !c->error) c->error = e;
}

void mgl_viewport_changed(GLcontext c) { (void)c; }
void mgl_free_lists(GLcontext c) { (void)c; }      /* the module's context takes its lists and textures with it */
void mgl_free_textures(GLcontext c) { (void)c; }

/* ---- the GL calls the kept files make ------------------------------------------------------- */

void GLBegin(GLcontext c, GLenum mode) { current(c); mglw_invoke(c->s, GL(c, glBegin), 1, (ULONG)mode); }
void GLEnd(GLcontext c) { current(c); mglw_invoke(c->s, GL(c, glEnd), 0); }

void GLVertex4f(GLcontext c, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    current(c);
    mglw_invoke(c->s, GL(c, glVertex4f), 4, FL(x), FL(y), FL(z), FL(w));
}

void GLNormal3f(GLcontext c, GLfloat x, GLfloat y, GLfloat z)
{
    current(c);
    mglw_invoke(c->s, GL(c, glNormal3f), 3, FL(x), FL(y), FL(z));
}

void GLTexCoord2f(GLcontext c, GLfloat s, GLfloat t)
{
    current(c);
    mglw_invoke(c->s, GL(c, glTexCoord2f), 2, FL(s), FL(t));
}

void GLViewport(GLcontext c, GLint x, GLint y, GLsizei w, GLsizei h)
{
    current(c);
    mglw_invoke(c->s, GL(c, glViewport), 4, (ULONG)x, (ULONG)y, (ULONG)w, (ULONG)h);
}

void GLTexEnvi(GLcontext c, GLenum target, GLenum pname, GLint param)
{
    current(c);
    mglw_invoke(c->s, GL(c, glTexEnvi), 3, (ULONG)target, (ULONG)pname, (ULONG)param);
}

void GLBlendFunc(GLcontext c, GLenum sf, GLenum df)
{
    current(c);
    mglw_invoke(c->s, GL(c, glBlendFunc), 2, (ULONG)sf, (ULONG)df);
}
