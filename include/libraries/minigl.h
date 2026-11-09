/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library-compatible interface: the library (clean-room, from
 * docs/design/MiniGL-Compatible-Spec.md sections 3 and 4; see
 * library/minigl/PROVENANCE.md). */
#ifndef LIBRARIES_MINIGL_H
#define LIBRARIES_MINIGL_H

#include <exec/types.h>
#include <libraries/minigl_dispatch.h>

#define MINIGL_NAME             "minigl.library"
#define MINIGL_VERSION          30      /* what this library reports */
#define MINIGL_OPEN_VERSION     29      /* what libminigl.a asks for: runs on 29.x libraries too */

struct Library;

extern struct Library *MiniGLBase;
extern const MGLDispatchTable *MiniGLDispatch;

#ifdef __cplusplus
extern "C" {
#endif

/* libminigl.a: opens the library, fetches its table (the function at -30)
 * and checks it (ABI version 5 exactly, at least MGL_DISPATCH_SIZE bytes).
 * FALSE, with the library closed again, when it can't. */
BOOL MiniGLOpen(void);
void MiniGLClose(void);

/* libmgl.a: the same, for programs that call functions. */
GLboolean MGLInit(void);
void MGLTerm(void);

#ifdef __cplusplus
}
#endif

#endif
