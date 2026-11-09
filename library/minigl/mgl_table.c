/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MADE BY library/minigl/gen_api.py FROM docs/design/MiniGL-Compatible-Spec.md
 * (section 8). Do not edit: change the generator or the spec.
 *
 * The table the library hands out. An entry that takes a context goes
 * through a small function that does nothing for a NULL context (spec
 * 4.3: a program may call before it has one); the rest go straight to
 * the library's function. build.sh compiles this file in both halves
 * (FPU and soft), as mgl_table_fpu and mgl_table_soft; mgl_lib.c picks
 * one. */
#include "mgl_internal.h"

static void T_glActiveTextureARB(GLcontext a0, GLenum a1) { if (a0) GLActiveTextureARB(a0, a1); }
static void T_glAlphaFunc(GLcontext a0, GLenum a1, GLclampf a2) { if (a0) GLAlphaFunc(a0, a1, a2); }
static void T_glArrayElement(GLcontext a0, GLint a1) { if (a0) GLArrayElement(a0, a1); }
static void T_glBegin(GLcontext a0, GLenum a1) { if (a0) GLBegin(a0, a1); }
static void T_glBindTexture(GLcontext a0, GLenum a1, GLuint a2) { if (a0) GLBindTexture(a0, a1, a2); }
static void T_glBlendFunc(GLcontext a0, GLenum a1, GLenum a2) { if (a0) GLBlendFunc(a0, a1, a2); }
static void T_glClear(GLcontext a0, GLbitfield a1) { if (a0) GLClear(a0, a1); }
static void T_glClearColor(GLcontext a0, GLclampf a1, GLclampf a2, GLclampf a3, GLclampf a4) { if (a0) GLClearColor(a0, a1, a2, a3, a4); }
static void T_glClearDepth(GLcontext a0, GLclampd a1) { if (a0) GLClearDepth(a0, a1); }
static void T_glColor3fv(GLcontext a0, GLfloat*a1) { if (a0) GLColor3fv(a0, a1); }
static void T_glColor3ubv(GLcontext a0, GLubyte*a1) { if (a0) GLColor3ubv(a0, a1); }
static void T_glColor4f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4) { if (a0) GLColor4f(a0, a1, a2, a3, a4); }
static void T_glColor4fv(GLcontext a0, GLfloat*a1) { if (a0) GLColor4fv(a0, a1); }
static void T_glColor4ub(GLcontext a0, GLubyte a1, GLubyte a2, GLubyte a3, GLubyte a4) { if (a0) GLColor4ub(a0, a1, a2, a3, a4); }
static void T_glColor4ubv(GLcontext a0, GLubyte*a1) { if (a0) GLColor4ubv(a0, a1); }
static void T_glColorMask(GLcontext a0, GLboolean a1, GLboolean a2, GLboolean a3, GLboolean a4) { if (a0) GLColorMask(a0, a1, a2, a3, a4); }
static void T_glColorPointer(GLcontext a0, GLint a1, GLenum a2, GLsizei a3, const GLvoid*a4) { if (a0) GLColorPointer(a0, a1, a2, a3, a4); }
static void T_glColorTable(GLcontext a0, GLenum a1, GLenum a2, GLint a3, GLenum a4, GLenum a5, GLvoid*a6) { if (a0) GLColorTable(a0, a1, a2, a3, a4, a5, a6); }
static void T_glCullFace(GLcontext a0, GLenum a1) { if (a0) GLCullFace(a0, a1); }
static void T_glDeleteTextures(GLcontext a0, GLsizei a1, const GLuint*a2) { if (a0) GLDeleteTextures(a0, a1, a2); }
static void T_glDepthFunc(GLcontext a0, GLenum a1) { if (a0) GLDepthFunc(a0, a1); }
static void T_glDepthMask(GLcontext a0, GLboolean a1) { if (a0) GLDepthMask(a0, a1); }
static void T_glDepthRange(GLcontext a0, GLclampd a1, GLclampd a2) { if (a0) GLDepthRange(a0, a1, a2); }
static void T_glDisableClientState(GLcontext a0, GLenum a1) { if (a0) GLDisableClientState(a0, a1); }
static void T_glDrawArrays(GLcontext a0, GLenum a1, GLint a2, GLsizei a3) { if (a0) GLDrawArrays(a0, a1, a2, a3); }
static void T_glDrawBuffer(GLcontext a0, GLenum a1) { if (a0) GLDrawBuffer(a0, a1); }
static void T_glDrawElements(GLcontext a0, GLenum a1, GLsizei a2, GLenum a3, const GLvoid*a4) { if (a0) GLDrawElements(a0, a1, a2, a3, a4); }
static void T_glEnableClientState(GLcontext a0, GLenum a1) { if (a0) GLEnableClientState(a0, a1); }
static void T_glEnd(GLcontext a0) { if (a0) GLEnd(a0); }
static void T_glFinish(GLcontext a0) { if (a0) GLFinish(a0); }
static void T_glFlush(GLcontext a0) { if (a0) GLFlush(a0); }
static void T_glFogf(GLcontext a0, GLenum a1, GLfloat a2) { if (a0) GLFogf(a0, a1, a2); }
static void T_glFogfv(GLcontext a0, GLenum a1, GLfloat*a2) { if (a0) GLFogfv(a0, a1, a2); }
static void T_glFrontFace(GLcontext a0, GLenum a1) { if (a0) GLFrontFace(a0, a1); }
static void T_glFrustum(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4, GLdouble a5, GLdouble a6) { if (a0) GLFrustum(a0, a1, a2, a3, a4, a5, a6); }
static void T_glGenTextures(GLcontext a0, GLsizei a1, GLuint*a2) { if (a0) GLGenTextures(a0, a1, a2); }
static void T_glGetBooleanv(GLcontext a0, GLenum a1, GLboolean*a2) { if (a0) GLGetBooleanv(a0, a1, a2); }
static GLenum T_glGetError(GLcontext a0) { return a0 ? GLGetError(a0) : 0; }
static void T_glGetFloatv(GLcontext a0, GLenum a1, GLfloat*a2) { if (a0) GLGetFloatv(a0, a1, a2); }
static void T_glGetIntegerv(GLcontext a0, GLenum a1, GLint*a2) { if (a0) GLGetIntegerv(a0, a1, a2); }
static const GLubyte* T_glGetString(GLcontext a0, GLenum a1) { return a0 ? GLGetString(a0, a1) : 0; }
static void T_glHint(GLcontext a0, GLenum a1, GLenum a2) { if (a0) GLHint(a0, a1, a2); }
static GLboolean T_glIsEnabled(GLcontext a0, GLenum a1) { return a0 ? GLIsEnabled(a0, a1) : 0; }
static void T_glLoadIdentity(GLcontext a0) { if (a0) GLLoadIdentity(a0); }
static void T_glLoadMatrixd(GLcontext a0, const GLdouble*a1) { if (a0) GLLoadMatrixd(a0, a1); }
static void T_glLoadMatrixf(GLcontext a0, const GLfloat*a1) { if (a0) GLLoadMatrixf(a0, a1); }
static void T_glLockArrays(GLcontext a0, GLuint a1, GLsizei a2) { if (a0) GLLockArrays(a0, a1, a2); }
static void T_glMatrixMode(GLcontext a0, GLenum a1) { if (a0) GLMatrixMode(a0, a1); }
static void T_glMultMatrixd(GLcontext a0, const GLdouble*a1) { if (a0) GLMultMatrixd(a0, a1); }
static void T_glMultMatrixf(GLcontext a0, const GLfloat*a1) { if (a0) GLMultMatrixf(a0, a1); }
static void T_glMultiTexCoord2fARB(GLcontext a0, GLenum a1, GLfloat a2, GLfloat a3) { if (a0) GLMultiTexCoord2fARB(a0, a1, a2, a3); }
static void T_glMultiTexCoord2fvARB(GLcontext a0, GLenum a1, GLfloat*a2) { if (a0) GLMultiTexCoord2fvARB(a0, a1, a2); }
static void T_glNormal3f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3) { if (a0) GLNormal3f(a0, a1, a2, a3); }
static void T_glOrtho(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4, GLdouble a5, GLdouble a6) { if (a0) GLOrtho(a0, a1, a2, a3, a4, a5, a6); }
static void T_glPixelStorei(GLcontext a0, GLenum a1, GLint a2) { if (a0) GLPixelStorei(a0, a1, a2); }
static void T_glPointSize(GLcontext a0, GLfloat a1) { if (a0) GLPointSize(a0, a1); }
static void T_glPolygonMode(GLcontext a0, GLenum a1, GLenum a2) { if (a0) GLPolygonMode(a0, a1, a2); }
static void T_glPopMatrix(GLcontext a0) { if (a0) GLPopMatrix(a0); }
static void T_glPushMatrix(GLcontext a0) { if (a0) GLPushMatrix(a0); }
static void T_glReadPixels(GLcontext a0, GLint a1, GLint a2, GLsizei a3, GLsizei a4, GLenum a5, GLenum a6, GLvoid*a7) { if (a0) GLReadPixels(a0, a1, a2, a3, a4, a5, a6, a7); }
static void T_glRotated(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4) { if (a0) GLRotated(a0, a1, a2, a3, a4); }
static void T_glRotatef(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4) { if (a0) GLRotatef(a0, a1, a2, a3, a4); }
static void T_glRotatefEXT(GLcontext a0, GLfloat a1, const GLint a2) { if (a0) GLRotatefEXT(a0, a1, a2); }
static void T_glRotatefEXTs(GLcontext a0, GLfloat a1, GLfloat a2, const GLint a3) { if (a0) GLRotatefEXTs(a0, a1, a2, a3); }
static void T_glScaled(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3) { if (a0) GLScaled(a0, a1, a2, a3); }
static void T_glScalef(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3) { if (a0) GLScalef(a0, a1, a2, a3); }
static void T_glScissor(GLcontext a0, GLint a1, GLint a2, GLsizei a3, GLsizei a4) { if (a0) GLScissor(a0, a1, a2, a3, a4); }
static void T_glShadeModel(GLcontext a0, GLenum a1) { if (a0) GLShadeModel(a0, a1); }
static void T_glTexCoord2f(GLcontext a0, GLfloat a1, GLfloat a2) { if (a0) GLTexCoord2f(a0, a1, a2); }
static void T_glTexCoord2fv(GLcontext a0, GLfloat*a1) { if (a0) GLTexCoord2fv(a0, a1); }
static void T_glTexCoord4f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4) { if (a0) GLTexCoord4f(a0, a1, a2, a3, a4); }
static void T_glTexCoord4fv(GLcontext a0, GLfloat*a1) { if (a0) GLTexCoord4fv(a0, a1); }
static void T_glTexCoordPointer(GLcontext a0, GLint a1, GLenum a2, GLsizei a3, const GLvoid*a4) { if (a0) GLTexCoordPointer(a0, a1, a2, a3, a4); }
static void T_glTexEnvi(GLcontext a0, GLenum a1, GLenum a2, GLint a3) { if (a0) GLTexEnvi(a0, a1, a2, a3); }
static void T_glTexGeni(GLcontext a0, GLenum a1, GLenum a2, GLenum a3) { if (a0) GLTexGeni(a0, a1, a2, a3); }
static void T_glTexImage2D(GLcontext a0, GLenum a1, GLint a2, GLint a3, GLsizei a4, GLsizei a5, GLint a6, GLenum a7, GLenum a8, const GLvoid*a9) { if (a0) GLTexImage2D(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9); }
static void T_glTexParameteri(GLcontext a0, GLenum a1, GLenum a2, GLint a3) { if (a0) GLTexParameteri(a0, a1, a2, a3); }
static void T_glTexSubImage2D(GLcontext a0, GLenum a1, GLint a2, GLint a3, GLint a4, GLsizei a5, GLsizei a6, GLenum a7, GLenum a8, const GLvoid*a9) { if (a0) GLTexSubImage2D(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9); }
static void T_glTranslated(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3) { if (a0) GLTranslated(a0, a1, a2, a3); }
static void T_glTranslatef(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3) { if (a0) GLTranslatef(a0, a1, a2, a3); }
static void T_glUnlockArrays(GLcontext a0) { if (a0) GLUnlockArrays(a0); }
static void T_glVertex2fv(GLcontext a0, GLfloat*a1) { if (a0) GLVertex2fv(a0, a1); }
static void T_glVertex3fv(GLcontext a0, GLfloat*a1) { if (a0) GLVertex3fv(a0, a1); }
static void T_glVertex4f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4) { if (a0) GLVertex4f(a0, a1, a2, a3, a4); }
static void T_glVertex4fv(GLcontext a0, GLfloat*a1) { if (a0) GLVertex4fv(a0, a1); }
static void T_glVertexPointer(GLcontext a0, GLint a1, GLenum a2, GLsizei a3, const GLvoid*a4) { if (a0) GLVertexPointer(a0, a1, a2, a3, a4); }
static void T_glViewport(GLcontext a0, GLint a1, GLint a2, GLsizei a3, GLsizei a4) { if (a0) GLViewport(a0, a1, a2, a3, a4); }
static void T_mglClearPointer(GLcontext a0) { if (a0) MGLClearPointer(a0); }
static void T_mglDeleteContext(GLcontext a0) { if (a0) MGLDeleteContext(a0); }
static void T_mglDrawMultitexBuffer(GLcontext a0, GLenum a1, GLenum a2, GLenum a3) { if (a0) MGLDrawMultitexBuffer(a0, a1, a2, a3); }
static void T_mglEnableSync(GLcontext a0, GLboolean a1) { if (a0) MGLEnableSync(a0, a1); }
static void T_mglExit(GLcontext a0) { if (a0) MGLExit(a0); }
static void * T_mglGetInputWindowHandle(GLcontext a0) { return a0 ? MGLGetInputWindowHandle(a0) : 0; }
static void * T_mglGetWindowHandle(GLcontext a0) { return a0 ? MGLGetWindowHandle(a0) : 0; }
static void T_mglIdleFunc(GLcontext a0, IdleFn a1) { if (a0) MGLIdleFunc(a0, a1); }
static void T_mglKeyFunc(GLcontext a0, KeyHandlerFn a1) { if (a0) MGLKeyFunc(a0, a1); }
static GLboolean T_mglLockBack(GLcontext a0, MGLLockInfo*a1) { return a0 ? MGLLockBack(a0, a1) : 0; }
static GLboolean T_mglLockDisplay(GLcontext a0) { return a0 ? MGLLockDisplay(a0) : 0; }
static void T_mglLockMode(GLcontext a0, GLenum a1) { if (a0) MGLLockMode(a0, a1); }
static void T_mglMainLoop(GLcontext a0) { if (a0) MGLMainLoop(a0); }
static void T_mglMinTriArea(GLcontext a0, GLfloat a1) { if (a0) MGLMinTriArea(a0, a1); }
static void T_mglMouseFunc(GLcontext a0, MouseHandlerFn a1) { if (a0) MGLMouseFunc(a0, a1); }
static void T_mglPrintMatrix(GLcontext a0, int a1) { if (a0) MGLPrintMatrix(a0, a1); }
static void T_mglPrintMatrixStack(GLcontext a0, int a1) { if (a0) MGLPrintMatrixStack(a0, a1); }
static GLboolean T_mglResizeContext(GLcontext a0, GLsizei a1, GLsizei a2) { return a0 ? MGLResizeContext(a0, a1, a2) : 0; }
static void T_mglSetPointer(GLcontext a0) { if (a0) MGLSetPointer(a0); }
static void T_mglSetState(GLcontext a0, GLenum a1, GLboolean a2) { if (a0) MGLSetState(a0, a1, a2); }
static void T_mglSetZOffset(GLcontext a0, GLfloat a1) { if (a0) MGLSetZOffset(a0, a1); }
static void T_mglSpecialFunc(GLcontext a0, SpecialHandlerFn a1) { if (a0) MGLSpecialFunc(a0, a1); }
static void T_mglSwitchDisplay(GLcontext a0) { if (a0) MGLSwitchDisplay(a0); }
static void T_mglTexMemStat(GLcontext a0, GLint*a1, GLint*a2) { if (a0) MGLTexMemStat(a0, a1, a2); }
static void T_mglUnlockDisplay(GLcontext a0) { if (a0) MGLUnlockDisplay(a0); }
static void T_mglWriteShotPPM(GLcontext a0, char*a1) { if (a0) MGLWriteShotPPM(a0, a1); }
static void T_glPolygonOffset(GLcontext a0, GLfloat a1, GLfloat a2) { if (a0) GLPolygonOffset(a0, a1, a2); }
static void T_glClientActiveTextureARB(GLcontext a0, GLenum a1) { if (a0) GLClientActiveTextureARB(a0, a1); }
static void T_glInterleavedArrays(GLcontext a0, GLenum a1, GLsizei a2, const GLvoid*a3) { if (a0) GLInterleavedArrays(a0, a1, a2, a3); }
static void T_glMultiDrawArrays(GLcontext a0, GLenum a1, const GLint*a2, const GLsizei*a3, GLsizei a4) { if (a0) GLMultiDrawArrays(a0, a1, a2, a3, a4); }
static void T_glBlendEquation(GLcontext a0, GLenum a1) { if (a0) GLBlendEquation(a0, a1); }
static void T_glBlendFuncSeparate(GLcontext a0, GLenum a1, GLenum a2, GLenum a3, GLenum a4) { if (a0) GLBlendFuncSeparate(a0, a1, a2, a3, a4); }
static GLboolean T_glIsTexture(GLcontext a0, GLuint a1) { return a0 ? GLIsTexture(a0, a1) : 0; }
static void T_glLineWidth(GLcontext a0, GLfloat a1) { if (a0) GLLineWidth(a0, a1); }
static void T_glTexGenfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3) { if (a0) GLTexGenfv(a0, a1, a2, a3); }
static void T_glCopyTexImage2D(GLcontext a0, GLenum a1, GLint a2, GLenum a3, GLint a4, GLint a5, GLsizei a6, GLsizei a7, GLint a8) { if (a0) GLCopyTexImage2D(a0, a1, a2, a3, a4, a5, a6, a7, a8); }
static void T_glCopyTexSubImage2D(GLcontext a0, GLenum a1, GLint a2, GLint a3, GLint a4, GLint a5, GLint a6, GLsizei a7, GLsizei a8) { if (a0) GLCopyTexSubImage2D(a0, a1, a2, a3, a4, a5, a6, a7, a8); }
static GLboolean T_glAreTexturesResident(GLcontext a0, GLsizei a1, const GLuint*a2, GLboolean*a3) { return a0 ? GLAreTexturesResident(a0, a1, a2, a3) : 0; }
static void T_glEdgeFlag(GLcontext a0, GLboolean a1) { if (a0) GLEdgeFlag(a0, a1); }
static void T_glEdgeFlagPointer(GLcontext a0, GLsizei a1, const GLvoid*a2) { if (a0) GLEdgeFlagPointer(a0, a1, a2); }
static void T_glEdgeFlagv(GLcontext a0, const GLboolean*a1) { if (a0) GLEdgeFlagv(a0, a1); }
static void T_glGetDoublev(GLcontext a0, GLenum a1, GLdouble*a2) { if (a0) GLGetDoublev(a0, a1, a2); }
static void T_glGetPointerv(GLcontext a0, GLenum a1, GLvoid**a2) { if (a0) GLGetPointerv(a0, a1, a2); }
static void T_glIndexi(GLcontext a0, GLint a1) { if (a0) GLIndexi(a0, a1); }
static void T_glIndexiv(GLcontext a0, const GLint*a1) { if (a0) GLIndexiv(a0, a1); }
static void T_glIndexPointer(GLcontext a0, GLenum a1, GLsizei a2, const GLvoid*a3) { if (a0) GLIndexPointer(a0, a1, a2, a3); }
static void T_glPrioritizeTextures(GLcontext a0, GLsizei a1, const GLuint*a2, const GLclampf*a3) { if (a0) GLPrioritizeTextures(a0, a1, a2, a3); }
static void T_glReadBuffer(GLcontext a0, GLenum a1) { if (a0) GLReadBuffer(a0, a1); }
static void T_glLightf(GLcontext a0, GLenum a1, GLenum a2, GLfloat a3) { if (a0) GLLightf(a0, a1, a2, a3); }
static void T_glLightfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3) { if (a0) GLLightfv(a0, a1, a2, a3); }
static void T_glMaterialf(GLcontext a0, GLenum a1, GLenum a2, GLfloat a3) { if (a0) GLMaterialf(a0, a1, a2, a3); }
static void T_glMaterialfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3) { if (a0) GLMaterialfv(a0, a1, a2, a3); }
static void T_glLightModelf(GLcontext a0, GLenum a1, GLfloat a2) { if (a0) GLLightModelf(a0, a1, a2); }
static void T_glLightModelfv(GLcontext a0, GLenum a1, const GLfloat*a2) { if (a0) GLLightModelfv(a0, a1, a2); }
static void T_glGetLightfv(GLcontext a0, GLenum a1, GLenum a2, GLfloat*a3) { if (a0) GLGetLightfv(a0, a1, a2, a3); }
static void T_glGetMaterialfv(GLcontext a0, GLenum a1, GLenum a2, GLfloat*a3) { if (a0) GLGetMaterialfv(a0, a1, a2, a3); }
static GLuint T_glGenLists(GLcontext a0, GLsizei a1) { return a0 ? GLGenLists(a0, a1) : 0; }
static void T_glDeleteLists(GLcontext a0, GLuint a1, GLsizei a2) { if (a0) GLDeleteLists(a0, a1, a2); }
static GLboolean T_glIsList(GLcontext a0, GLuint a1) { return a0 ? GLIsList(a0, a1) : 0; }
static void T_glNewList(GLcontext a0, GLuint a1, GLenum a2) { if (a0) GLNewList(a0, a1, a2); }
static void T_glEndList(GLcontext a0) { if (a0) GLEndList(a0); }
static void T_glCallList(GLcontext a0, GLuint a1) { if (a0) GLCallList(a0, a1); }
static GLint T_gluBuild2DMipmaps(GLcontext a0, GLenum a1, GLint a2, GLsizei a3, GLsizei a4, GLenum a5, GLenum a6, const GLvoid*a7) { return a0 ? GLUBuild2DMipmaps(a0, a1, a2, a3, a4, a5, a6, a7) : 0; }
static void T_glNormalPointer(GLcontext a0, GLenum a1, GLsizei a2, const GLvoid*a3) { if (a0) GLNormalPointer(a0, a1, a2, a3); }
static void T_glColorMaterial(GLcontext a0, GLenum a1, GLenum a2) { if (a0) GLColorMaterial(a0, a1, a2); }
static void T_glTexEnvfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3) { if (a0) GLTexEnvfv(a0, a1, a2, a3); }
static void T_glNormal3fv(GLcontext a0, GLfloat*a1) { if (a0) GLNormal3fv(a0, a1); }

const MGLDispatchTable mgl_table = {
    MGL_DISPATCH_ABI,
    MGL_DISPATCH_SIZE,
    MGL_BACKEND_AMICRADLE,
    0,
    &mgl_current,
    T_glActiveTextureARB, /* 1 glActiveTextureARB */
    T_glAlphaFunc, /* 2 glAlphaFunc */
    T_glArrayElement, /* 3 glArrayElement */
    T_glBegin, /* 4 glBegin */
    T_glBindTexture, /* 5 glBindTexture */
    T_glBlendFunc, /* 6 glBlendFunc */
    T_glClear, /* 7 glClear */
    T_glClearColor, /* 8 glClearColor */
    T_glClearDepth, /* 9 glClearDepth */
    T_glColor3fv, /* 10 glColor3fv */
    T_glColor3ubv, /* 11 glColor3ubv */
    T_glColor4f, /* 12 glColor4f */
    T_glColor4fv, /* 13 glColor4fv */
    T_glColor4ub, /* 14 glColor4ub */
    T_glColor4ubv, /* 15 glColor4ubv */
    T_glColorMask, /* 16 glColorMask */
    T_glColorPointer, /* 17 glColorPointer */
    T_glColorTable, /* 18 glColorTable */
    T_glCullFace, /* 19 glCullFace */
    T_glDeleteTextures, /* 20 glDeleteTextures */
    T_glDepthFunc, /* 21 glDepthFunc */
    T_glDepthMask, /* 22 glDepthMask */
    T_glDepthRange, /* 23 glDepthRange */
    T_glDisableClientState, /* 24 glDisableClientState */
    T_glDrawArrays, /* 25 glDrawArrays */
    T_glDrawBuffer, /* 26 glDrawBuffer */
    T_glDrawElements, /* 27 glDrawElements */
    T_glEnableClientState, /* 28 glEnableClientState */
    T_glEnd, /* 29 glEnd */
    T_glFinish, /* 30 glFinish */
    T_glFlush, /* 31 glFlush */
    T_glFogf, /* 32 glFogf */
    T_glFogfv, /* 33 glFogfv */
    T_glFrontFace, /* 34 glFrontFace */
    T_glFrustum, /* 35 glFrustum */
    T_glGenTextures, /* 36 glGenTextures */
    T_glGetBooleanv, /* 37 glGetBooleanv */
    T_glGetError, /* 38 glGetError */
    T_glGetFloatv, /* 39 glGetFloatv */
    T_glGetIntegerv, /* 40 glGetIntegerv */
    T_glGetString, /* 41 glGetString */
    T_glHint, /* 42 glHint */
    T_glIsEnabled, /* 43 glIsEnabled */
    T_glLoadIdentity, /* 44 glLoadIdentity */
    T_glLoadMatrixd, /* 45 glLoadMatrixd */
    T_glLoadMatrixf, /* 46 glLoadMatrixf */
    T_glLockArrays, /* 47 glLockArrays */
    T_glMatrixMode, /* 48 glMatrixMode */
    T_glMultMatrixd, /* 49 glMultMatrixd */
    T_glMultMatrixf, /* 50 glMultMatrixf */
    T_glMultiTexCoord2fARB, /* 51 glMultiTexCoord2fARB */
    T_glMultiTexCoord2fvARB, /* 52 glMultiTexCoord2fvARB */
    T_glNormal3f, /* 53 glNormal3f */
    T_glOrtho, /* 54 glOrtho */
    T_glPixelStorei, /* 55 glPixelStorei */
    T_glPointSize, /* 56 glPointSize */
    T_glPolygonMode, /* 57 glPolygonMode */
    T_glPopMatrix, /* 58 glPopMatrix */
    T_glPushMatrix, /* 59 glPushMatrix */
    T_glReadPixels, /* 60 glReadPixels */
    T_glRotated, /* 61 glRotated */
    T_glRotatef, /* 62 glRotatef */
    T_glRotatefEXT, /* 63 glRotatefEXT */
    T_glRotatefEXTs, /* 64 glRotatefEXTs */
    T_glScaled, /* 65 glScaled */
    T_glScalef, /* 66 glScalef */
    T_glScissor, /* 67 glScissor */
    T_glShadeModel, /* 68 glShadeModel */
    T_glTexCoord2f, /* 69 glTexCoord2f */
    T_glTexCoord2fv, /* 70 glTexCoord2fv */
    T_glTexCoord4f, /* 71 glTexCoord4f */
    T_glTexCoord4fv, /* 72 glTexCoord4fv */
    T_glTexCoordPointer, /* 73 glTexCoordPointer */
    T_glTexEnvi, /* 74 glTexEnvi */
    T_glTexGeni, /* 75 glTexGeni */
    T_glTexImage2D, /* 76 glTexImage2D */
    T_glTexParameteri, /* 77 glTexParameteri */
    T_glTexSubImage2D, /* 78 glTexSubImage2D */
    T_glTranslated, /* 79 glTranslated */
    T_glTranslatef, /* 80 glTranslatef */
    GLULookAt, /* 81 gluLookAt */
    GLUPerspective, /* 82 gluPerspective */
    T_glUnlockArrays, /* 83 glUnlockArrays */
    T_glVertex2fv, /* 84 glVertex2fv */
    T_glVertex3fv, /* 85 glVertex3fv */
    T_glVertex4f, /* 86 glVertex4f */
    T_glVertex4fv, /* 87 glVertex4fv */
    T_glVertexPointer, /* 88 glVertexPointer */
    T_glViewport, /* 89 glViewport */
    T_mglClearPointer, /* 90 mglClearPointer */
    MGLCreateContext, /* 91 mglCreateContext */
    MGLCreateContextFromID, /* 92 mglCreateContextFromID */
    T_mglDeleteContext, /* 93 mglDeleteContext */
    T_mglDrawMultitexBuffer, /* 94 mglDrawMultitexBuffer */
    T_mglEnableSync, /* 95 mglEnableSync */
    T_mglExit, /* 96 mglExit */
    T_mglGetInputWindowHandle, /* 97 mglGetInputWindowHandle */
    T_mglGetWindowHandle, /* 98 mglGetWindowHandle */
    T_mglIdleFunc, /* 99 mglIdleFunc */
    T_mglKeyFunc, /* 100 mglKeyFunc */
    T_mglLockBack, /* 101 mglLockBack */
    T_mglLockDisplay, /* 102 mglLockDisplay */
    T_mglLockMode, /* 103 mglLockMode */
    T_mglMainLoop, /* 104 mglMainLoop */
    T_mglMinTriArea, /* 105 mglMinTriArea */
    T_mglMouseFunc, /* 106 mglMouseFunc */
    T_mglPrintMatrix, /* 107 mglPrintMatrix */
    T_mglPrintMatrixStack, /* 108 mglPrintMatrixStack */
    T_mglResizeContext, /* 109 mglResizeContext */
    T_mglSetPointer, /* 110 mglSetPointer */
    T_mglSetState, /* 111 mglSetState */
    T_mglSetZOffset, /* 112 mglSetZOffset */
    T_mglSpecialFunc, /* 113 mglSpecialFunc */
    T_mglSwitchDisplay, /* 114 mglSwitchDisplay */
    T_mglTexMemStat, /* 115 mglTexMemStat */
    T_mglUnlockDisplay, /* 116 mglUnlockDisplay */
    T_mglWriteShotPPM, /* 117 mglWriteShotPPM */
    MGLChooseGuardBand, /* 118 mglChooseGuardBand */
    MGLChooseMtexBufferSize, /* 119 mglChooseMtexBufferSize */
    MGLChooseNumberOfBuffers, /* 120 mglChooseNumberOfBuffers */
    MGLChoosePixelDepth, /* 121 mglChoosePixelDepth */
    MGLChooseTextureBufferSize, /* 122 mglChooseTextureBufferSize */
    MGLChooseVertexBufferSize, /* 123 mglChooseVertexBufferSize */
    MGLChooseWindowMode, /* 124 mglChooseWindowMode */
    MGLGetSupportedScreenModes, /* 125 mglGetSupportedScreenModes */
    MGLProhibitAlphaFallback, /* 126 mglProhibitAlphaFallback */
    MGLProhibitMipMapping, /* 127 mglProhibitMipMapping */
    MGLProposeCloseDesktop, /* 128 mglProposeCloseDesktop */
    T_glPolygonOffset, /* 129 glPolygonOffset */
    T_glClientActiveTextureARB, /* 130 glClientActiveTextureARB */
    T_glInterleavedArrays, /* 131 glInterleavedArrays */
    T_glMultiDrawArrays, /* 132 glMultiDrawArrays */
    T_glBlendEquation, /* 133 glBlendEquation */
    T_glBlendFuncSeparate, /* 134 glBlendFuncSeparate */
    T_glIsTexture, /* 135 glIsTexture */
    MGLChooseZBufferDepth, /* 136 mglChooseZBufferDepth */
    T_glLineWidth, /* 137 glLineWidth */
    T_glTexGenfv, /* 138 glTexGenfv */
    T_glCopyTexImage2D, /* 139 glCopyTexImage2D */
    T_glCopyTexSubImage2D, /* 140 glCopyTexSubImage2D */
    T_glAreTexturesResident, /* 141 glAreTexturesResident */
    T_glEdgeFlag, /* 142 glEdgeFlag */
    T_glEdgeFlagPointer, /* 143 glEdgeFlagPointer */
    T_glEdgeFlagv, /* 144 glEdgeFlagv */
    T_glGetDoublev, /* 145 glGetDoublev */
    T_glGetPointerv, /* 146 glGetPointerv */
    T_glIndexi, /* 147 glIndexi */
    T_glIndexiv, /* 148 glIndexiv */
    T_glIndexPointer, /* 149 glIndexPointer */
    T_glPrioritizeTextures, /* 150 glPrioritizeTextures */
    T_glReadBuffer, /* 151 glReadBuffer */
    MGLCreateContextFromWindow, /* 152 mglCreateContextFromWindow */
    MGLCreateContextFromBitMap, /* 153 mglCreateContextFromBitMap */
    T_glLightf, /* 154 glLightf */
    T_glLightfv, /* 155 glLightfv */
    T_glMaterialf, /* 156 glMaterialf */
    T_glMaterialfv, /* 157 glMaterialfv */
    T_glLightModelf, /* 158 glLightModelf */
    T_glLightModelfv, /* 159 glLightModelfv */
    T_glGetLightfv, /* 160 glGetLightfv */
    T_glGetMaterialfv, /* 161 glGetMaterialfv */
    T_glGenLists, /* 162 glGenLists */
    T_glDeleteLists, /* 163 glDeleteLists */
    T_glIsList, /* 164 glIsList */
    T_glNewList, /* 165 glNewList */
    T_glEndList, /* 166 glEndList */
    T_glCallList, /* 167 glCallList */
    T_gluBuild2DMipmaps, /* 168 gluBuild2DMipmaps */
    GLUErrorString, /* 169 gluErrorString */
    GLUNewQuadric, /* 170 gluNewQuadric */
    GLUDeleteQuadric, /* 171 gluDeleteQuadric */
    GLUQuadricNormals, /* 172 gluQuadricNormals */
    GLUQuadricTexture, /* 173 gluQuadricTexture */
    GLUQuadricDrawStyle, /* 174 gluQuadricDrawStyle */
    GLUQuadricOrientation, /* 175 gluQuadricOrientation */
    GLUCylinder, /* 176 gluCylinder */
    GLUSphere, /* 177 gluSphere */
    GLUDisk, /* 178 gluDisk */
    GLUTInit, /* 179 glutInit */
    GLUTInitDisplayMode, /* 180 glutInitDisplayMode */
    GLUTInitWindowSize, /* 181 glutInitWindowSize */
    GLUTInitWindowPosition, /* 182 glutInitWindowPosition */
    GLUTCreateWindow, /* 183 glutCreateWindow */
    GLUTMainLoop, /* 184 glutMainLoop */
    GLUTDisplayFunc, /* 185 glutDisplayFunc */
    GLUTIdleFunc, /* 186 glutIdleFunc */
    GLUTKeyboardFunc, /* 187 glutKeyboardFunc */
    GLUTReshapeFunc, /* 188 glutReshapeFunc */
    GLUTSwapBuffers, /* 189 glutSwapBuffers */
    GLUTPostRedisplay, /* 190 glutPostRedisplay */
    GLUTGet, /* 191 glutGet */
    GLUTGameModeString, /* 192 glutGameModeString */
    GLUTEnterGameMode, /* 193 glutEnterGameMode */
    GLUTLeaveGameMode, /* 194 glutLeaveGameMode */
    GLUTGameModeGet, /* 195 glutGameModeGet */
    GLUTSolidCube, /* 196 glutSolidCube */
    GLUTSolidSphere, /* 197 glutSolidSphere */
    GLUTSolidCone, /* 198 glutSolidCone */
    GLUTSolidTorus, /* 199 glutSolidTorus */
    GLUTSolidDodecahedron, /* 200 glutSolidDodecahedron */
    T_glNormalPointer, /* 201 glNormalPointer */
    GLUQuadricCallback, /* 202 gluQuadricCallback */
    T_glColorMaterial, /* 203 glColorMaterial */
    T_glTexEnvfv, /* 204 glTexEnvfv */
    T_glNormal3fv, /* 205 glNormal3fv */
};

/* What the C compiler laid out is what the spec says. */
typedef char mgl_table_size_is_840[sizeof(MGLDispatchTable) == MGL_DISPATCH_SIZE ? 1 : -1];
