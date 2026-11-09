/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MADE BY library/minigl/gen_api.py FROM docs/design/MiniGL-Compatible-Spec.md
 * (section 8). Do not edit: change the generator or the spec.
 *
 * The calls a program makes, each through the table (MiniGLDispatch) with
 * the current context (spec 4.3, 8.1). Included by <mgl/gl.h> three ways:
 *   MGL_CALLS_INLINE   static inline functions (<proto/minigl.h>)
 *   MGL_CALLS_DEFINE   functions (library/minigl/mgl_static.c, libmgl.a)
 *   neither            prototypes of libmgl.a's functions */
#ifndef MGL_MINIGL_CALLS_H
#define MGL_MINIGL_CALLS_H

#include <libraries/minigl_dispatch.h>

extern const MGLDispatchTable *MiniGLDispatch;

#if defined(MGL_CALLS_INLINE)
#define MGL_CALL static __inline__
#define MGL_BODY(...) { __VA_ARGS__ }
#elif defined(MGL_CALLS_DEFINE)
#define MGL_CALL
#define MGL_BODY(...) { __VA_ARGS__ }
#else
#define MGL_CALL extern
#define MGL_BODY(...) ;
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* entry 1 */
MGL_CALL void glActiveTextureARB(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glActiveTextureARB(*MiniGLDispatch->current_context, a0);
)
/* entry 2 */
MGL_CALL void glAlphaFunc(GLenum a0, GLclampf a1)
MGL_BODY(
    MiniGLDispatch->glAlphaFunc(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 3 */
MGL_CALL void glArrayElement(GLint a0)
MGL_BODY(
    MiniGLDispatch->glArrayElement(*MiniGLDispatch->current_context, a0);
)
/* entry 4 */
MGL_CALL void glBegin(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glBegin(*MiniGLDispatch->current_context, a0);
)
/* entry 5 */
MGL_CALL void glBindTexture(GLenum a0, GLuint a1)
MGL_BODY(
    MiniGLDispatch->glBindTexture(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 6 */
MGL_CALL void glBlendFunc(GLenum a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->glBlendFunc(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 7 */
MGL_CALL void glClear(GLbitfield a0)
MGL_BODY(
    MiniGLDispatch->glClear(*MiniGLDispatch->current_context, a0);
)
/* entry 8 */
MGL_CALL void glClearColor(GLclampf a0, GLclampf a1, GLclampf a2, GLclampf a3)
MGL_BODY(
    MiniGLDispatch->glClearColor(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 9 */
MGL_CALL void glClearDepth(GLclampd a0)
MGL_BODY(
    MiniGLDispatch->glClearDepth(*MiniGLDispatch->current_context, a0);
)
/* entry 10 */
MGL_CALL void glColor3fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glColor3fv(*MiniGLDispatch->current_context, a0);
)
/* entry 11 */
MGL_CALL void glColor3ubv(GLubyte*a0)
MGL_BODY(
    MiniGLDispatch->glColor3ubv(*MiniGLDispatch->current_context, a0);
)
/* entry 12 */
MGL_CALL void glColor4f(GLfloat a0, GLfloat a1, GLfloat a2, GLfloat a3)
MGL_BODY(
    MiniGLDispatch->glColor4f(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 13 */
MGL_CALL void glColor4fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glColor4fv(*MiniGLDispatch->current_context, a0);
)
/* entry 14 */
MGL_CALL void glColor4ub(GLubyte a0, GLubyte a1, GLubyte a2, GLubyte a3)
MGL_BODY(
    MiniGLDispatch->glColor4ub(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 15 */
MGL_CALL void glColor4ubv(GLubyte*a0)
MGL_BODY(
    MiniGLDispatch->glColor4ubv(*MiniGLDispatch->current_context, a0);
)
/* entry 16 */
MGL_CALL void glColorMask(GLboolean a0, GLboolean a1, GLboolean a2, GLboolean a3)
MGL_BODY(
    MiniGLDispatch->glColorMask(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 17 */
MGL_CALL void glColorPointer(GLint a0, GLenum a1, GLsizei a2, const GLvoid*a3)
MGL_BODY(
    MiniGLDispatch->glColorPointer(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 18 */
MGL_CALL void glColorTable(GLenum a0, GLenum a1, GLint a2, GLenum a3, GLenum a4, GLvoid*a5)
MGL_BODY(
    MiniGLDispatch->glColorTable(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5);
)
/* entry 19 */
MGL_CALL void glCullFace(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glCullFace(*MiniGLDispatch->current_context, a0);
)
/* entry 20 */
MGL_CALL void glDeleteTextures(GLsizei a0, const GLuint*a1)
MGL_BODY(
    MiniGLDispatch->glDeleteTextures(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 21 */
MGL_CALL void glDepthFunc(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glDepthFunc(*MiniGLDispatch->current_context, a0);
)
/* entry 22 */
MGL_CALL void glDepthMask(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->glDepthMask(*MiniGLDispatch->current_context, a0);
)
/* entry 23 */
MGL_CALL void glDepthRange(GLclampd a0, GLclampd a1)
MGL_BODY(
    MiniGLDispatch->glDepthRange(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 24 */
MGL_CALL void glDisableClientState(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glDisableClientState(*MiniGLDispatch->current_context, a0);
)
/* entry 25 */
MGL_CALL void glDrawArrays(GLenum a0, GLint a1, GLsizei a2)
MGL_BODY(
    MiniGLDispatch->glDrawArrays(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 26 */
MGL_CALL void glDrawBuffer(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glDrawBuffer(*MiniGLDispatch->current_context, a0);
)
/* entry 27 */
MGL_CALL void glDrawElements(GLenum a0, GLsizei a1, GLenum a2, const GLvoid*a3)
MGL_BODY(
    MiniGLDispatch->glDrawElements(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 28 */
MGL_CALL void glEnableClientState(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glEnableClientState(*MiniGLDispatch->current_context, a0);
)
/* entry 29 */
MGL_CALL void glEnd(void)
MGL_BODY(
    MiniGLDispatch->glEnd(*MiniGLDispatch->current_context);
)
/* entry 30 */
MGL_CALL void glFinish(void)
MGL_BODY(
    MiniGLDispatch->glFinish(*MiniGLDispatch->current_context);
)
/* entry 31 */
MGL_CALL void glFlush(void)
MGL_BODY(
    MiniGLDispatch->glFlush(*MiniGLDispatch->current_context);
)
/* entry 32 */
MGL_CALL void glFogf(GLenum a0, GLfloat a1)
MGL_BODY(
    MiniGLDispatch->glFogf(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 33 */
MGL_CALL void glFogfv(GLenum a0, GLfloat*a1)
MGL_BODY(
    MiniGLDispatch->glFogfv(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 34 */
MGL_CALL void glFrontFace(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glFrontFace(*MiniGLDispatch->current_context, a0);
)
/* entry 35 */
MGL_CALL void glFrustum(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4, GLdouble a5)
MGL_BODY(
    MiniGLDispatch->glFrustum(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5);
)
/* entry 36 */
MGL_CALL void glGenTextures(GLsizei a0, GLuint*a1)
MGL_BODY(
    MiniGLDispatch->glGenTextures(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 37 */
MGL_CALL void glGetBooleanv(GLenum a0, GLboolean*a1)
MGL_BODY(
    MiniGLDispatch->glGetBooleanv(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 38 */
MGL_CALL GLenum glGetError(void)
MGL_BODY(
    return MiniGLDispatch->glGetError(*MiniGLDispatch->current_context);
)
/* entry 39 */
MGL_CALL void glGetFloatv(GLenum a0, GLfloat*a1)
MGL_BODY(
    MiniGLDispatch->glGetFloatv(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 40 */
MGL_CALL void glGetIntegerv(GLenum a0, GLint*a1)
MGL_BODY(
    MiniGLDispatch->glGetIntegerv(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 41 */
MGL_CALL const GLubyte* glGetString(GLenum a0)
MGL_BODY(
    return MiniGLDispatch->glGetString(*MiniGLDispatch->current_context, a0);
)
/* entry 42 */
MGL_CALL void glHint(GLenum a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->glHint(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 43 */
MGL_CALL GLboolean glIsEnabled(GLenum a0)
MGL_BODY(
    return MiniGLDispatch->glIsEnabled(*MiniGLDispatch->current_context, a0);
)
/* entry 44 */
MGL_CALL void glLoadIdentity(void)
MGL_BODY(
    MiniGLDispatch->glLoadIdentity(*MiniGLDispatch->current_context);
)
/* entry 45 */
MGL_CALL void glLoadMatrixd(const GLdouble*a0)
MGL_BODY(
    MiniGLDispatch->glLoadMatrixd(*MiniGLDispatch->current_context, a0);
)
/* entry 46 */
MGL_CALL void glLoadMatrixf(const GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glLoadMatrixf(*MiniGLDispatch->current_context, a0);
)
/* entry 47 */
MGL_CALL void glLockArrays(GLuint a0, GLsizei a1)
MGL_BODY(
    MiniGLDispatch->glLockArrays(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 48 */
MGL_CALL void glMatrixMode(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glMatrixMode(*MiniGLDispatch->current_context, a0);
)
/* entry 49 */
MGL_CALL void glMultMatrixd(const GLdouble*a0)
MGL_BODY(
    MiniGLDispatch->glMultMatrixd(*MiniGLDispatch->current_context, a0);
)
/* entry 50 */
MGL_CALL void glMultMatrixf(const GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glMultMatrixf(*MiniGLDispatch->current_context, a0);
)
/* entry 51 */
MGL_CALL void glMultiTexCoord2fARB(GLenum a0, GLfloat a1, GLfloat a2)
MGL_BODY(
    MiniGLDispatch->glMultiTexCoord2fARB(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 52 */
MGL_CALL void glMultiTexCoord2fvARB(GLenum a0, GLfloat*a1)
MGL_BODY(
    MiniGLDispatch->glMultiTexCoord2fvARB(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 53 */
MGL_CALL void glNormal3f(GLfloat a0, GLfloat a1, GLfloat a2)
MGL_BODY(
    MiniGLDispatch->glNormal3f(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 54 */
MGL_CALL void glOrtho(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4, GLdouble a5)
MGL_BODY(
    MiniGLDispatch->glOrtho(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5);
)
/* entry 55 */
MGL_CALL void glPixelStorei(GLenum a0, GLint a1)
MGL_BODY(
    MiniGLDispatch->glPixelStorei(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 56 */
MGL_CALL void glPointSize(GLfloat a0)
MGL_BODY(
    MiniGLDispatch->glPointSize(*MiniGLDispatch->current_context, a0);
)
/* entry 57 */
MGL_CALL void glPolygonMode(GLenum a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->glPolygonMode(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 58 */
MGL_CALL void glPopMatrix(void)
MGL_BODY(
    MiniGLDispatch->glPopMatrix(*MiniGLDispatch->current_context);
)
/* entry 59 */
MGL_CALL void glPushMatrix(void)
MGL_BODY(
    MiniGLDispatch->glPushMatrix(*MiniGLDispatch->current_context);
)
/* entry 60 */
MGL_CALL void glReadPixels(GLint a0, GLint a1, GLsizei a2, GLsizei a3, GLenum a4, GLenum a5, GLvoid*a6)
MGL_BODY(
    MiniGLDispatch->glReadPixels(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5, a6);
)
/* entry 61 */
MGL_CALL void glRotated(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3)
MGL_BODY(
    MiniGLDispatch->glRotated(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 62 */
MGL_CALL void glRotatef(GLfloat a0, GLfloat a1, GLfloat a2, GLfloat a3)
MGL_BODY(
    MiniGLDispatch->glRotatef(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 63 */
MGL_CALL void glRotatefEXT(GLfloat a0, const GLint a1)
MGL_BODY(
    MiniGLDispatch->glRotatefEXT(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 64 */
MGL_CALL void glRotatefEXTs(GLfloat a0, GLfloat a1, const GLint a2)
MGL_BODY(
    MiniGLDispatch->glRotatefEXTs(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 65 */
MGL_CALL void glScaled(GLdouble a0, GLdouble a1, GLdouble a2)
MGL_BODY(
    MiniGLDispatch->glScaled(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 66 */
MGL_CALL void glScalef(GLfloat a0, GLfloat a1, GLfloat a2)
MGL_BODY(
    MiniGLDispatch->glScalef(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 67 */
MGL_CALL void glScissor(GLint a0, GLint a1, GLsizei a2, GLsizei a3)
MGL_BODY(
    MiniGLDispatch->glScissor(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 68 */
MGL_CALL void glShadeModel(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glShadeModel(*MiniGLDispatch->current_context, a0);
)
/* entry 69 */
MGL_CALL void glTexCoord2f(GLfloat a0, GLfloat a1)
MGL_BODY(
    MiniGLDispatch->glTexCoord2f(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 70 */
MGL_CALL void glTexCoord2fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glTexCoord2fv(*MiniGLDispatch->current_context, a0);
)
/* entry 71 */
MGL_CALL void glTexCoord4f(GLfloat a0, GLfloat a1, GLfloat a2, GLfloat a3)
MGL_BODY(
    MiniGLDispatch->glTexCoord4f(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 72 */
MGL_CALL void glTexCoord4fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glTexCoord4fv(*MiniGLDispatch->current_context, a0);
)
/* entry 73 */
MGL_CALL void glTexCoordPointer(GLint a0, GLenum a1, GLsizei a2, const GLvoid*a3)
MGL_BODY(
    MiniGLDispatch->glTexCoordPointer(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 74 */
MGL_CALL void glTexEnvi(GLenum a0, GLenum a1, GLint a2)
MGL_BODY(
    MiniGLDispatch->glTexEnvi(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 75 */
MGL_CALL void glTexGeni(GLenum a0, GLenum a1, GLenum a2)
MGL_BODY(
    MiniGLDispatch->glTexGeni(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 76 */
MGL_CALL void glTexImage2D(GLenum a0, GLint a1, GLint a2, GLsizei a3, GLsizei a4, GLint a5, GLenum a6, GLenum a7, const GLvoid*a8)
MGL_BODY(
    MiniGLDispatch->glTexImage2D(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5, a6, a7, a8);
)
/* entry 77 */
MGL_CALL void glTexParameteri(GLenum a0, GLenum a1, GLint a2)
MGL_BODY(
    MiniGLDispatch->glTexParameteri(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 78 */
MGL_CALL void glTexSubImage2D(GLenum a0, GLint a1, GLint a2, GLint a3, GLsizei a4, GLsizei a5, GLenum a6, GLenum a7, const GLvoid*a8)
MGL_BODY(
    MiniGLDispatch->glTexSubImage2D(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5, a6, a7, a8);
)
/* entry 79 */
MGL_CALL void glTranslated(GLdouble a0, GLdouble a1, GLdouble a2)
MGL_BODY(
    MiniGLDispatch->glTranslated(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 80 */
MGL_CALL void glTranslatef(GLfloat a0, GLfloat a1, GLfloat a2)
MGL_BODY(
    MiniGLDispatch->glTranslatef(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 81 */
MGL_CALL void gluLookAt(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4, GLdouble a5, GLdouble a6, GLdouble a7, GLdouble a8)
MGL_BODY(
    if (*MiniGLDispatch->current_context) MiniGLDispatch->gluLookAt((GLfloat)a0, (GLfloat)a1, (GLfloat)a2, (GLfloat)a3, (GLfloat)a4, (GLfloat)a5, (GLfloat)a6, (GLfloat)a7, (GLfloat)a8);
)
/* entry 82 */
MGL_CALL void gluPerspective(GLdouble a0, GLdouble a1, GLdouble a2, GLdouble a3)
MGL_BODY(
    if (*MiniGLDispatch->current_context) MiniGLDispatch->gluPerspective((GLfloat)a0, (GLfloat)a1, (GLfloat)a2, (GLfloat)a3);
)
/* entry 83 */
MGL_CALL void glUnlockArrays(void)
MGL_BODY(
    MiniGLDispatch->glUnlockArrays(*MiniGLDispatch->current_context);
)
/* entry 84 */
MGL_CALL void glVertex2fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glVertex2fv(*MiniGLDispatch->current_context, a0);
)
/* entry 85 */
MGL_CALL void glVertex3fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glVertex3fv(*MiniGLDispatch->current_context, a0);
)
/* entry 86 */
MGL_CALL void glVertex4f(GLfloat a0, GLfloat a1, GLfloat a2, GLfloat a3)
MGL_BODY(
    MiniGLDispatch->glVertex4f(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 87 */
MGL_CALL void glVertex4fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glVertex4fv(*MiniGLDispatch->current_context, a0);
)
/* entry 88 */
MGL_CALL void glVertexPointer(GLint a0, GLenum a1, GLsizei a2, const GLvoid*a3)
MGL_BODY(
    MiniGLDispatch->glVertexPointer(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 89 */
MGL_CALL void glViewport(GLint a0, GLint a1, GLsizei a2, GLsizei a3)
MGL_BODY(
    MiniGLDispatch->glViewport(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 90 */
MGL_CALL void mglClearPointer(void)
MGL_BODY(
    MiniGLDispatch->mglClearPointer(*MiniGLDispatch->current_context);
)
/* entry 91 */
MGL_CALL void * mglCreateContext(int a0, int a1, int a2, int a3)
MGL_BODY(
    void * r = MiniGLDispatch->mglCreateContext(a0, a1, a2, a3);
    if (r) *MiniGLDispatch->current_context = (GLcontext)r;
    return r;
)
/* entry 92 */
MGL_CALL void * mglCreateContextFromID(GLint a0, GLint*a1, GLint*a2)
MGL_BODY(
    void * r = MiniGLDispatch->mglCreateContextFromID(a0, a1, a2);
    if (r) *MiniGLDispatch->current_context = (GLcontext)r;
    return r;
)
/* entry 93 */
MGL_CALL void mglDeleteContext(void)
MGL_BODY(
    GLcontext *p = MiniGLDispatch->current_context; GLcontext old = *p;
    if (old) { *p = 0; MiniGLDispatch->mglDeleteContext(old); }
)
/* entry 94 */
MGL_CALL void mglDrawMultitexBuffer(GLenum a0, GLenum a1, GLenum a2)
MGL_BODY(
    MiniGLDispatch->mglDrawMultitexBuffer(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 95 */
MGL_CALL void mglEnableSync(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->mglEnableSync(*MiniGLDispatch->current_context, a0);
)
/* entry 96 */
MGL_CALL void mglExit(void)
MGL_BODY(
    MiniGLDispatch->mglExit(*MiniGLDispatch->current_context);
)
/* entry 97 */
MGL_CALL void * mglGetInputWindowHandle(void)
MGL_BODY(
    return MiniGLDispatch->mglGetInputWindowHandle(*MiniGLDispatch->current_context);
)
/* entry 98 */
MGL_CALL void * mglGetWindowHandle(void)
MGL_BODY(
    return MiniGLDispatch->mglGetWindowHandle(*MiniGLDispatch->current_context);
)
/* entry 99 */
MGL_CALL void mglIdleFunc(IdleFn a0)
MGL_BODY(
    MiniGLDispatch->mglIdleFunc(*MiniGLDispatch->current_context, a0);
)
/* entry 100 */
MGL_CALL void mglKeyFunc(KeyHandlerFn a0)
MGL_BODY(
    MiniGLDispatch->mglKeyFunc(*MiniGLDispatch->current_context, a0);
)
/* entry 101 */
MGL_CALL GLboolean mglLockBack(MGLLockInfo*a0)
MGL_BODY(
    return MiniGLDispatch->mglLockBack(*MiniGLDispatch->current_context, a0);
)
/* entry 102 */
MGL_CALL GLboolean mglLockDisplay(void)
MGL_BODY(
    return MiniGLDispatch->mglLockDisplay(*MiniGLDispatch->current_context);
)
/* entry 103 */
MGL_CALL void mglLockMode(GLenum a0)
MGL_BODY(
    MiniGLDispatch->mglLockMode(*MiniGLDispatch->current_context, a0);
)
/* entry 104 */
MGL_CALL void mglMainLoop(void)
MGL_BODY(
    MiniGLDispatch->mglMainLoop(*MiniGLDispatch->current_context);
)
/* entry 105 */
MGL_CALL void mglMinTriArea(GLfloat a0)
MGL_BODY(
    MiniGLDispatch->mglMinTriArea(*MiniGLDispatch->current_context, a0);
)
/* entry 106 */
MGL_CALL void mglMouseFunc(MouseHandlerFn a0)
MGL_BODY(
    MiniGLDispatch->mglMouseFunc(*MiniGLDispatch->current_context, a0);
)
/* entry 107 */
MGL_CALL void mglPrintMatrix(int a0)
MGL_BODY(
    MiniGLDispatch->mglPrintMatrix(*MiniGLDispatch->current_context, a0);
)
/* entry 108 */
MGL_CALL void mglPrintMatrixStack(int a0)
MGL_BODY(
    MiniGLDispatch->mglPrintMatrixStack(*MiniGLDispatch->current_context, a0);
)
/* entry 109 */
MGL_CALL GLboolean mglResizeContext(GLsizei a0, GLsizei a1)
MGL_BODY(
    return MiniGLDispatch->mglResizeContext(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 110 */
MGL_CALL void mglSetPointer(void)
MGL_BODY(
    MiniGLDispatch->mglSetPointer(*MiniGLDispatch->current_context);
)
/* entry 111 */
MGL_CALL void mglSetState(GLenum a0, GLboolean a1)
MGL_BODY(
    MiniGLDispatch->mglSetState(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 112 */
MGL_CALL void mglSetZOffset(GLfloat a0)
MGL_BODY(
    MiniGLDispatch->mglSetZOffset(*MiniGLDispatch->current_context, a0);
)
/* entry 113 */
MGL_CALL void mglSpecialFunc(SpecialHandlerFn a0)
MGL_BODY(
    MiniGLDispatch->mglSpecialFunc(*MiniGLDispatch->current_context, a0);
)
/* entry 114 */
MGL_CALL void mglSwitchDisplay(void)
MGL_BODY(
    MiniGLDispatch->mglSwitchDisplay(*MiniGLDispatch->current_context);
)
/* entry 115 */
MGL_CALL void mglTexMemStat(GLint*a0, GLint*a1)
MGL_BODY(
    MiniGLDispatch->mglTexMemStat(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 116 */
MGL_CALL void mglUnlockDisplay(void)
MGL_BODY(
    MiniGLDispatch->mglUnlockDisplay(*MiniGLDispatch->current_context);
)
/* entry 117 */
MGL_CALL void mglWriteShotPPM(char*a0)
MGL_BODY(
    MiniGLDispatch->mglWriteShotPPM(*MiniGLDispatch->current_context, a0);
)
/* entry 118 */
MGL_CALL void mglChooseGuardBand(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->mglChooseGuardBand(a0);
)
/* entry 119 */
MGL_CALL void mglChooseMtexBufferSize(int a0)
MGL_BODY(
    MiniGLDispatch->mglChooseMtexBufferSize(a0);
)
/* entry 120 */
MGL_CALL void mglChooseNumberOfBuffers(int a0)
MGL_BODY(
    MiniGLDispatch->mglChooseNumberOfBuffers(a0);
)
/* entry 121 */
MGL_CALL void mglChoosePixelDepth(int a0)
MGL_BODY(
    MiniGLDispatch->mglChoosePixelDepth(a0);
)
/* entry 122 */
MGL_CALL void mglChooseTextureBufferSize(int a0)
MGL_BODY(
    MiniGLDispatch->mglChooseTextureBufferSize(a0);
)
/* entry 123 */
MGL_CALL void mglChooseVertexBufferSize(int a0)
MGL_BODY(
    MiniGLDispatch->mglChooseVertexBufferSize(a0);
)
/* entry 124 */
MGL_CALL void mglChooseWindowMode(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->mglChooseWindowMode(a0);
)
/* entry 125 */
MGL_CALL GLint mglGetSupportedScreenModes(MGLScreenModeCallback a0)
MGL_BODY(
    return MiniGLDispatch->mglGetSupportedScreenModes(a0);
)
/* entry 126 */
MGL_CALL void mglProhibitAlphaFallback(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->mglProhibitAlphaFallback(a0);
)
/* entry 127 */
MGL_CALL void mglProhibitMipMapping(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->mglProhibitMipMapping(a0);
)
/* entry 128 */
MGL_CALL void mglProposeCloseDesktop(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->mglProposeCloseDesktop(a0);
)
/* entry 129 */
MGL_CALL void glPolygonOffset(GLfloat a0, GLfloat a1)
MGL_BODY(
    MiniGLDispatch->glPolygonOffset(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 130 */
MGL_CALL void glClientActiveTextureARB(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glClientActiveTextureARB(*MiniGLDispatch->current_context, a0);
)
/* entry 131 */
MGL_CALL void glInterleavedArrays(GLenum a0, GLsizei a1, const GLvoid*a2)
MGL_BODY(
    MiniGLDispatch->glInterleavedArrays(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 132 */
MGL_CALL void glMultiDrawArrays(GLenum a0, const GLint*a1, const GLsizei*a2, GLsizei a3)
MGL_BODY(
    MiniGLDispatch->glMultiDrawArrays(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 133 */
MGL_CALL void glBlendEquation(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glBlendEquation(*MiniGLDispatch->current_context, a0);
)
/* entry 134 */
MGL_CALL void glBlendFuncSeparate(GLenum a0, GLenum a1, GLenum a2, GLenum a3)
MGL_BODY(
    MiniGLDispatch->glBlendFuncSeparate(*MiniGLDispatch->current_context, a0, a1, a2, a3);
)
/* entry 135 */
MGL_CALL GLboolean glIsTexture(GLuint a0)
MGL_BODY(
    return MiniGLDispatch->glIsTexture(*MiniGLDispatch->current_context, a0);
)
/* entry 136 */
MGL_CALL void mglChooseZBufferDepth(int a0)
MGL_BODY(
    MiniGLDispatch->mglChooseZBufferDepth(a0);
)
/* entry 137 */
MGL_CALL void glLineWidth(GLfloat a0)
MGL_BODY(
    MiniGLDispatch->glLineWidth(*MiniGLDispatch->current_context, a0);
)
/* entry 138 */
MGL_CALL void glTexGenfv(GLenum a0, GLenum a1, const GLfloat*a2)
MGL_BODY(
    MiniGLDispatch->glTexGenfv(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 139 */
MGL_CALL void glCopyTexImage2D(GLenum a0, GLint a1, GLenum a2, GLint a3, GLint a4, GLsizei a5, GLsizei a6, GLint a7)
MGL_BODY(
    MiniGLDispatch->glCopyTexImage2D(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5, a6, a7);
)
/* entry 140 */
MGL_CALL void glCopyTexSubImage2D(GLenum a0, GLint a1, GLint a2, GLint a3, GLint a4, GLint a5, GLsizei a6, GLsizei a7)
MGL_BODY(
    MiniGLDispatch->glCopyTexSubImage2D(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5, a6, a7);
)
/* entry 141 */
MGL_CALL GLboolean glAreTexturesResident(GLsizei a0, const GLuint*a1, GLboolean*a2)
MGL_BODY(
    return MiniGLDispatch->glAreTexturesResident(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 142 */
MGL_CALL void glEdgeFlag(GLboolean a0)
MGL_BODY(
    MiniGLDispatch->glEdgeFlag(*MiniGLDispatch->current_context, a0);
)
/* entry 143 */
MGL_CALL void glEdgeFlagPointer(GLsizei a0, const GLvoid*a1)
MGL_BODY(
    MiniGLDispatch->glEdgeFlagPointer(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 144 */
MGL_CALL void glEdgeFlagv(const GLboolean*a0)
MGL_BODY(
    MiniGLDispatch->glEdgeFlagv(*MiniGLDispatch->current_context, a0);
)
/* entry 145 */
MGL_CALL void glGetDoublev(GLenum a0, GLdouble*a1)
MGL_BODY(
    MiniGLDispatch->glGetDoublev(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 146 */
MGL_CALL void glGetPointerv(GLenum a0, GLvoid**a1)
MGL_BODY(
    MiniGLDispatch->glGetPointerv(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 147 */
MGL_CALL void glIndexi(GLint a0)
MGL_BODY(
    MiniGLDispatch->glIndexi(*MiniGLDispatch->current_context, a0);
)
/* entry 148 */
MGL_CALL void glIndexiv(const GLint*a0)
MGL_BODY(
    MiniGLDispatch->glIndexiv(*MiniGLDispatch->current_context, a0);
)
/* entry 149 */
MGL_CALL void glIndexPointer(GLenum a0, GLsizei a1, const GLvoid*a2)
MGL_BODY(
    MiniGLDispatch->glIndexPointer(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 150 */
MGL_CALL void glPrioritizeTextures(GLsizei a0, const GLuint*a1, const GLclampf*a2)
MGL_BODY(
    MiniGLDispatch->glPrioritizeTextures(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 151 */
MGL_CALL void glReadBuffer(GLenum a0)
MGL_BODY(
    MiniGLDispatch->glReadBuffer(*MiniGLDispatch->current_context, a0);
)
/* entry 152 */
MGL_CALL void * mglCreateContextFromWindow(struct Window*a0)
MGL_BODY(
    void * r = MiniGLDispatch->mglCreateContextFromWindow(a0);
    if (r) *MiniGLDispatch->current_context = (GLcontext)r;
    return r;
)
/* entry 153 */
MGL_CALL void * mglCreateContextFromBitMap(struct BitMap*a0)
MGL_BODY(
    void * r = MiniGLDispatch->mglCreateContextFromBitMap(a0);
    if (r) *MiniGLDispatch->current_context = (GLcontext)r;
    return r;
)
/* entry 154 */
MGL_CALL void glLightf(GLenum a0, GLenum a1, GLfloat a2)
MGL_BODY(
    MiniGLDispatch->glLightf(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 155 */
MGL_CALL void glLightfv(GLenum a0, GLenum a1, const GLfloat*a2)
MGL_BODY(
    MiniGLDispatch->glLightfv(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 156 */
MGL_CALL void glMaterialf(GLenum a0, GLenum a1, GLfloat a2)
MGL_BODY(
    MiniGLDispatch->glMaterialf(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 157 */
MGL_CALL void glMaterialfv(GLenum a0, GLenum a1, const GLfloat*a2)
MGL_BODY(
    MiniGLDispatch->glMaterialfv(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 158 */
MGL_CALL void glLightModelf(GLenum a0, GLfloat a1)
MGL_BODY(
    MiniGLDispatch->glLightModelf(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 159 */
MGL_CALL void glLightModelfv(GLenum a0, const GLfloat*a1)
MGL_BODY(
    MiniGLDispatch->glLightModelfv(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 160 */
MGL_CALL void glGetLightfv(GLenum a0, GLenum a1, GLfloat*a2)
MGL_BODY(
    MiniGLDispatch->glGetLightfv(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 161 */
MGL_CALL void glGetMaterialfv(GLenum a0, GLenum a1, GLfloat*a2)
MGL_BODY(
    MiniGLDispatch->glGetMaterialfv(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 162 */
MGL_CALL GLuint glGenLists(GLsizei a0)
MGL_BODY(
    return MiniGLDispatch->glGenLists(*MiniGLDispatch->current_context, a0);
)
/* entry 163 */
MGL_CALL void glDeleteLists(GLuint a0, GLsizei a1)
MGL_BODY(
    MiniGLDispatch->glDeleteLists(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 164 */
MGL_CALL GLboolean glIsList(GLuint a0)
MGL_BODY(
    return MiniGLDispatch->glIsList(*MiniGLDispatch->current_context, a0);
)
/* entry 165 */
MGL_CALL void glNewList(GLuint a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->glNewList(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 166 */
MGL_CALL void glEndList(void)
MGL_BODY(
    MiniGLDispatch->glEndList(*MiniGLDispatch->current_context);
)
/* entry 167 */
MGL_CALL void glCallList(GLuint a0)
MGL_BODY(
    MiniGLDispatch->glCallList(*MiniGLDispatch->current_context, a0);
)
/* entry 168 */
MGL_CALL GLint gluBuild2DMipmaps(GLenum a0, GLint a1, GLsizei a2, GLsizei a3, GLenum a4, GLenum a5, const GLvoid*a6)
MGL_BODY(
    return MiniGLDispatch->gluBuild2DMipmaps(*MiniGLDispatch->current_context, a0, a1, a2, a3, a4, a5, a6);
)
/* entry 169 */
MGL_CALL const GLubyte * gluErrorString(GLenum a0)
MGL_BODY(
    return MiniGLDispatch->gluErrorString(a0);
)
/* entry 170 */
MGL_CALL GLUquadricObj * gluNewQuadric(void)
MGL_BODY(
    return MiniGLDispatch->gluNewQuadric();
)
/* entry 171 */
MGL_CALL void gluDeleteQuadric(GLUquadricObj*a0)
MGL_BODY(
    MiniGLDispatch->gluDeleteQuadric(a0);
)
/* entry 172 */
MGL_CALL void gluQuadricNormals(GLUquadricObj*a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->gluQuadricNormals(a0, a1);
)
/* entry 173 */
MGL_CALL void gluQuadricTexture(GLUquadricObj*a0, GLboolean a1)
MGL_BODY(
    MiniGLDispatch->gluQuadricTexture(a0, a1);
)
/* entry 174 */
MGL_CALL void gluQuadricDrawStyle(GLUquadricObj*a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->gluQuadricDrawStyle(a0, a1);
)
/* entry 175 */
MGL_CALL void gluQuadricOrientation(GLUquadricObj*a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->gluQuadricOrientation(a0, a1);
)
/* entry 176 */
MGL_CALL void gluCylinder(GLUquadricObj*a0, GLdouble a1, GLdouble a2, GLdouble a3, GLint a4, GLint a5)
MGL_BODY(
    MiniGLDispatch->gluCylinder(a0, a1, a2, a3, a4, a5);
)
/* entry 177 */
MGL_CALL void gluSphere(GLUquadricObj*a0, GLdouble a1, GLint a2, GLint a3)
MGL_BODY(
    MiniGLDispatch->gluSphere(a0, a1, a2, a3);
)
/* entry 178 */
MGL_CALL void gluDisk(GLUquadricObj*a0, GLdouble a1, GLdouble a2, GLint a3, GLint a4)
MGL_BODY(
    MiniGLDispatch->gluDisk(a0, a1, a2, a3, a4);
)
/* entry 179 */
MGL_CALL void glutInit(int*a0, char**a1)
MGL_BODY(
    MiniGLDispatch->glutInit(a0, a1);
)
/* entry 180 */
MGL_CALL void glutInitDisplayMode(unsigned int a0)
MGL_BODY(
    MiniGLDispatch->glutInitDisplayMode(a0);
)
/* entry 181 */
MGL_CALL void glutInitWindowSize(int a0, int a1)
MGL_BODY(
    MiniGLDispatch->glutInitWindowSize(a0, a1);
)
/* entry 182 */
MGL_CALL void glutInitWindowPosition(int a0, int a1)
MGL_BODY(
    MiniGLDispatch->glutInitWindowPosition(a0, a1);
)
/* entry 183 */
MGL_CALL int glutCreateWindow(const char*a0)
MGL_BODY(
    return MiniGLDispatch->glutCreateWindow(a0);
)
/* entry 184 */
MGL_CALL void glutMainLoop(void)
MGL_BODY(
    MiniGLDispatch->glutMainLoop();
)
/* entry 185 */
MGL_CALL void glutDisplayFunc(void (*a0)(void))
MGL_BODY(
    MiniGLDispatch->glutDisplayFunc(a0);
)
/* entry 186 */
MGL_CALL void glutIdleFunc(void (*a0)(void))
MGL_BODY(
    MiniGLDispatch->glutIdleFunc(a0);
)
/* entry 187 */
MGL_CALL void glutKeyboardFunc(void (*a0)(unsigned char key, int x, int y))
MGL_BODY(
    MiniGLDispatch->glutKeyboardFunc(a0);
)
/* entry 188 */
MGL_CALL void glutReshapeFunc(void (*a0)(int width, int height))
MGL_BODY(
    MiniGLDispatch->glutReshapeFunc(a0);
)
/* entry 189 */
MGL_CALL void glutSwapBuffers(void)
MGL_BODY(
    MiniGLDispatch->glutSwapBuffers();
)
/* entry 190 */
MGL_CALL void glutPostRedisplay(void)
MGL_BODY(
    MiniGLDispatch->glutPostRedisplay();
)
/* entry 191 */
MGL_CALL int glutGet(GLenum a0)
MGL_BODY(
    return MiniGLDispatch->glutGet(a0);
)
/* entry 192 */
MGL_CALL void glutGameModeString(const char*a0)
MGL_BODY(
    MiniGLDispatch->glutGameModeString(a0);
)
/* entry 193 */
MGL_CALL int glutEnterGameMode(void)
MGL_BODY(
    return MiniGLDispatch->glutEnterGameMode();
)
/* entry 194 */
MGL_CALL void glutLeaveGameMode(void)
MGL_BODY(
    MiniGLDispatch->glutLeaveGameMode();
)
/* entry 195 */
MGL_CALL int glutGameModeGet(GLenum a0)
MGL_BODY(
    return MiniGLDispatch->glutGameModeGet(a0);
)
/* entry 196 */
MGL_CALL void glutSolidCube(GLdouble a0)
MGL_BODY(
    MiniGLDispatch->glutSolidCube(a0);
)
/* entry 197 */
MGL_CALL void glutSolidSphere(GLdouble a0, GLint a1, GLint a2)
MGL_BODY(
    MiniGLDispatch->glutSolidSphere(a0, a1, a2);
)
/* entry 198 */
MGL_CALL void glutSolidCone(GLdouble a0, GLdouble a1, GLint a2, GLint a3)
MGL_BODY(
    MiniGLDispatch->glutSolidCone(a0, a1, a2, a3);
)
/* entry 199 */
MGL_CALL void glutSolidTorus(GLdouble a0, GLdouble a1, GLint a2, GLint a3)
MGL_BODY(
    MiniGLDispatch->glutSolidTorus(a0, a1, a2, a3);
)
/* entry 200 */
MGL_CALL void glutSolidDodecahedron(void)
MGL_BODY(
    MiniGLDispatch->glutSolidDodecahedron();
)
/* entry 201 */
MGL_CALL void glNormalPointer(GLenum a0, GLsizei a1, const GLvoid*a2)
MGL_BODY(
    MiniGLDispatch->glNormalPointer(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 202 */
MGL_CALL void gluQuadricCallback(GLUquadricObj*a0, GLenum a1, MGLUfuncptr a2)
MGL_BODY(
    MiniGLDispatch->gluQuadricCallback(a0, a1, a2);
)
/* entry 203 */
MGL_CALL void glColorMaterial(GLenum a0, GLenum a1)
MGL_BODY(
    MiniGLDispatch->glColorMaterial(*MiniGLDispatch->current_context, a0, a1);
)
/* entry 204 */
MGL_CALL void glTexEnvfv(GLenum a0, GLenum a1, const GLfloat*a2)
MGL_BODY(
    MiniGLDispatch->glTexEnvfv(*MiniGLDispatch->current_context, a0, a1, a2);
)
/* entry 205 */
MGL_CALL void glNormal3fv(GLfloat*a0)
MGL_BODY(
    MiniGLDispatch->glNormal3fv(*MiniGLDispatch->current_context, a0);
)

/* ---- calls with no entry of their own (spec 8.1) ---- */

/* no entry */
MGL_CALL void glEnable(GLenum cap)
MGL_BODY(
    mglSetState(cap, GL_TRUE);
)
/* no entry */
MGL_CALL void glDisable(GLenum cap)
MGL_BODY(
    mglSetState(cap, GL_FALSE);
)
/* no entry */
MGL_CALL void glColor3f(GLfloat r, GLfloat g, GLfloat b)
MGL_BODY(
    glColor4f(r, g, b, 1.0f);
)
/* no entry */
MGL_CALL void glColor3ub(GLubyte r, GLubyte g, GLubyte b)
MGL_BODY(
    glColor4ub(r, g, b, 255);
)
/* no entry */
MGL_CALL void glVertex2f(GLfloat x, GLfloat y)
MGL_BODY(
    glVertex4f(x, y, 0.0f, 1.0f);
)
/* no entry */
MGL_CALL void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
MGL_BODY(
    glVertex4f(x, y, z, 1.0f);
)
/* no entry */
MGL_CALL void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
MGL_BODY(
    glTexEnvi(target, pname, (GLint)param);
)
/* no entry */
MGL_CALL void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
MGL_BODY(
    glTexEnvi(target, pname, params[0]);
)
/* no entry */
MGL_CALL void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
MGL_BODY(
    glTexParameteri(target, pname, (GLint)param);
)
/* no entry */
MGL_CALL void glFogi(GLenum pname, GLint param)
MGL_BODY(
    glFogf(pname, (GLfloat)param);
)
/* no entry */
MGL_CALL void glColorTableEXT(GLenum target, GLenum internalformat, GLint width, GLenum format, GLenum type, GLvoid *data)
MGL_BODY(
    glColorTable(target, internalformat, width, format, type, data);
)

#ifdef __cplusplus
}
#endif

#undef MGL_CALL
#undef MGL_BODY

#endif
