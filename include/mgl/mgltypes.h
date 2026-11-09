/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library-compatible interface: the types (clean-room, written from
 * docs/design/MiniGL-Compatible-Spec.md section 6; see
 * library/minigl/PROVENANCE.md). The GL types are OpenGL's, with one
 * difference a compiled program depends on: GLboolean is four bytes, so
 * every array of GLboolean has four-byte elements. */
#ifndef MGL_MGLTYPES_H
#define MGL_MGLTYPES_H

typedef unsigned int    GLenum;
typedef unsigned int    GLbitfield;
typedef unsigned int    GLboolean;      /* four bytes (spec 6) */
typedef signed char     GLbyte;
typedef unsigned char   GLubyte;
typedef short           GLshort;
typedef unsigned short  GLushort;
typedef int             GLint;
typedef unsigned int    GLuint;
typedef long            GLsizei;
typedef float           GLfloat;
typedef float           GLclampf;
typedef double          GLdouble;
typedef double          GLclampd;
typedef void            GLvoid;

/* A context: opaque to programs, which only pass it back. */
typedef struct GLcontext_t *GLcontext;

/* A GLU quadric: opaque, owned by the library. */
typedef struct GLUquadric GLUquadricObj;

/* mglLockBack's answer: six longwords. */
typedef struct MGLLockInfo {
    GLuint width;
    GLuint height;
    GLuint depth;                       /* bits per pixel: 32 */
    GLuint format;                      /* MGL_LOCK_FORMAT_RGBA8: bytes R, G, B, A */
    void *address;                      /* the first byte of the top row, or 0 */
    GLuint bytes_per_row;
} MGLLockInfo;

#define MGL_MODE_NAME_LEN 80

/* A screen mode, as mglGetSupportedScreenModes offers it. */
typedef struct MGLScreenMode {
    GLint mode_id;
    GLint width;
    GLint height;
    GLint bits;                         /* bits per pixel */
    char name[MGL_MODE_NAME_LEN];
} MGLScreenMode;

/* The keys a special-key function is given. */
typedef enum {
    MGL_KEY_F1 = 0, MGL_KEY_F2, MGL_KEY_F3, MGL_KEY_F4, MGL_KEY_F5,
    MGL_KEY_F6, MGL_KEY_F7, MGL_KEY_F8, MGL_KEY_F9, MGL_KEY_F10,
    MGL_KEY_UP = 10, MGL_KEY_DOWN = 11, MGL_KEY_LEFT = 12, MGL_KEY_RIGHT = 13
} MGLspecial;

typedef void (*IdleFn)(void);
typedef void (*KeyHandlerFn)(char key);
typedef void (*SpecialHandlerFn)(MGLspecial key);
typedef void (*MouseHandlerFn)(GLint x, GLint y, GLbitfield buttons);
typedef GLboolean (*MGLScreenModeCallback)(MGLScreenMode *mode);
typedef void (*MGLUfuncptr)();

/* The sizes a compiled program depends on (spec 6). */
typedef char mgl_boolean_is_4_bytes[sizeof(GLboolean) == 4 ? 1 : -1];
typedef char mgl_lockinfo_is_24_bytes[sizeof(MGLLockInfo) == 24 ? 1 : -1];
typedef char mgl_screenmode_is_96_bytes[sizeof(MGLScreenMode) == 96 ? 1 : -1];

#endif
