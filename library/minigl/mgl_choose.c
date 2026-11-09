/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library: the mgl* calls that need no display. Clean-room, from
 * the spec's section 9.2 (see PROVENANCE.md).
 *
 * The requests a program makes before mglCreateContext (mglChoose*,
 * mglProhibit*) are library-wide, read when a display opens, and go back to
 * their defaults (mgl_prefs_defaults) when a context is deleted and when a
 * client opens the library. */
#include "mgl_internal.h"

/* Fullscreen unless on. */
void MGLChooseWindowMode(GLboolean on) { mgl_prefs.window = on ? 1 : 0; }

/* 2 or 3: a frame waits for the vertical blank. */
void MGLChooseNumberOfBuffers(int n) { mgl_prefs.buffers = n < 1 ? 1 : n > 3 ? 3 : n; }

/* Accepted; a context renders 32-bit colour whatever is asked. */
void MGLChoosePixelDepth(int bits) { mgl_prefs.depth = bits; }

/* 16, or anything else meaning 32. */
void MGLChooseZBufferDepth(int bits) { mgl_prefs.zbits = bits == 16 ? 16 : 32; }

/* Hints only: no buffer limits what is drawn. */
void MGLChooseVertexBufferSize(int n) { mgl_prefs.vbuf = n; }
void MGLChooseMtexBufferSize(int n) { mgl_prefs.mtexbuf = n; }

/* The number of texture names: names at or above it are refused. */
void MGLChooseTextureBufferSize(int n)
{
    if (n >= 1 && n <= 1048576) mgl_prefs.tex_names = (ULONG)n;
}

/* Stored, no effect. */
void MGLChooseGuardBand(GLboolean on) { mgl_prefs.guard = on ? 1 : 0; }
void MGLProhibitAlphaFallback(GLboolean on) { mgl_prefs.alpha_fallback = on ? 1 : 0; }
void MGLProhibitMipMapping(GLboolean on) { mgl_prefs.prohibit_mip = on ? 1 : 0; }

/* Fullscreen only: close the Workbench while the display is open. Consumed
 * when a display opens. */
void MGLProposeCloseDesktop(GLboolean on) { mgl_prefs.close_desktop = on ? 1 : 0; }

/* ---- calls on a context ---------------------------------------------------------------------- */

/* AmiCradle holds no display lock between frames, so no mode brackets
 * anything; the three modes are accepted. */
void MGLLockMode(GLcontext c, GLenum mode)
{
    if (c && (mode == MGL_LOCK_AUTOMATIC || mode == MGL_LOCK_MANUAL || mode == MGL_LOCK_SMART)) c->lock_mode = mode;
}

/* Off by default; on, a fullscreen frame waits for the vertical blank. */
void MGLEnableSync(GLcontext c, GLboolean on) { if (c) c->sync = on ? GL_TRUE : GL_FALSE; }

/* mglMainLoop's handlers; NULL removes one. */
void MGLIdleFunc(GLcontext c, IdleFn fn) { if (c) c->idle = fn; }
void MGLKeyFunc(GLcontext c, KeyHandlerFn fn) { if (c) c->key = fn; }
void MGLMouseFunc(GLcontext c, MouseHandlerFn fn) { if (c) c->mouse = fn; }
void MGLSpecialFunc(GLcontext c, SpecialHandlerFn fn) { if (c) c->special = fn; }

/* mglMainLoop returns after its current pass. */
void MGLExit(GLcontext c) { if (c) c->quit = 1; }

/* The blend function and unit 1's environment for the drawing that follows
 * (GL_MODULATE, GL_DECAL or GL_REPLACE); it draws nothing. */
void MGLDrawMultitexBuffer(GLcontext c, GLenum src, GLenum dst, GLenum env)
{
    int keep;
    if (!c) return;
    if (env != GL_MODULATE && env != GL_DECAL && env != GL_REPLACE) { mgl_error(c, GL_INVALID_ENUM); return; }
    GLBlendFunc(c, src, dst);
    keep = c->active;
    c->active = 1;
    GLTexEnvi(c, GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLint)env);
    c->active = keep;
}
