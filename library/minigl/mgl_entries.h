/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MADE BY library/minigl/gen_api.py FROM docs/design/MiniGL-Compatible-Spec.md
 * (section 8). Do not edit: change the generator or the spec.
 *
 * The library's functions behind the table's entries, with the spec's
 * types (a definition that differs does not compile). */
#ifndef MGL_ENTRIES_H
#define MGL_ENTRIES_H

struct Window;
struct BitMap;

void GLActiveTextureARB(GLcontext a0, GLenum a1); /* 1 */
void GLAlphaFunc(GLcontext a0, GLenum a1, GLclampf a2); /* 2 */
void GLArrayElement(GLcontext a0, GLint a1); /* 3 */
void GLBegin(GLcontext a0, GLenum a1); /* 4 */
void GLBindTexture(GLcontext a0, GLenum a1, GLuint a2); /* 5 */
void GLBlendFunc(GLcontext a0, GLenum a1, GLenum a2); /* 6 */
void GLClear(GLcontext a0, GLbitfield a1); /* 7 */
void GLClearColor(GLcontext a0, GLclampf a1, GLclampf a2, GLclampf a3, GLclampf a4); /* 8 */
void GLClearDepth(GLcontext a0, GLclampd a1); /* 9 */
void GLColor3fv(GLcontext a0, GLfloat*a1); /* 10 */
void GLColor3ubv(GLcontext a0, GLubyte*a1); /* 11 */
void GLColor4f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4); /* 12 */
void GLColor4fv(GLcontext a0, GLfloat*a1); /* 13 */
void GLColor4ub(GLcontext a0, GLubyte a1, GLubyte a2, GLubyte a3, GLubyte a4); /* 14 */
void GLColor4ubv(GLcontext a0, GLubyte*a1); /* 15 */
void GLColorMask(GLcontext a0, GLboolean a1, GLboolean a2, GLboolean a3, GLboolean a4); /* 16 */
void GLColorPointer(GLcontext a0, GLint a1, GLenum a2, GLsizei a3, const GLvoid*a4); /* 17 */
void GLColorTable(GLcontext a0, GLenum a1, GLenum a2, GLint a3, GLenum a4, GLenum a5, GLvoid*a6); /* 18 */
void GLCullFace(GLcontext a0, GLenum a1); /* 19 */
void GLDeleteTextures(GLcontext a0, GLsizei a1, const GLuint*a2); /* 20 */
void GLDepthFunc(GLcontext a0, GLenum a1); /* 21 */
void GLDepthMask(GLcontext a0, GLboolean a1); /* 22 */
void GLDepthRange(GLcontext a0, GLclampd a1, GLclampd a2); /* 23 */
void GLDisableClientState(GLcontext a0, GLenum a1); /* 24 */
void GLDrawArrays(GLcontext a0, GLenum a1, GLint a2, GLsizei a3); /* 25 */
void GLDrawBuffer(GLcontext a0, GLenum a1); /* 26 */
void GLDrawElements(GLcontext a0, GLenum a1, GLsizei a2, GLenum a3, const GLvoid*a4); /* 27 */
void GLEnableClientState(GLcontext a0, GLenum a1); /* 28 */
void GLEnd(GLcontext a0); /* 29 */
void GLFinish(GLcontext a0); /* 30 */
void GLFlush(GLcontext a0); /* 31 */
void GLFogf(GLcontext a0, GLenum a1, GLfloat a2); /* 32 */
void GLFogfv(GLcontext a0, GLenum a1, GLfloat*a2); /* 33 */
void GLFrontFace(GLcontext a0, GLenum a1); /* 34 */
void GLFrustum(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4, GLdouble a5, GLdouble a6); /* 35 */
void GLGenTextures(GLcontext a0, GLsizei a1, GLuint*a2); /* 36 */
void GLGetBooleanv(GLcontext a0, GLenum a1, GLboolean*a2); /* 37 */
GLenum GLGetError(GLcontext a0); /* 38 */
void GLGetFloatv(GLcontext a0, GLenum a1, GLfloat*a2); /* 39 */
void GLGetIntegerv(GLcontext a0, GLenum a1, GLint*a2); /* 40 */
const GLubyte* GLGetString(GLcontext a0, GLenum a1); /* 41 */
void GLHint(GLcontext a0, GLenum a1, GLenum a2); /* 42 */
GLboolean GLIsEnabled(GLcontext a0, GLenum a1); /* 43 */
void GLLoadIdentity(GLcontext a0); /* 44 */
void GLLoadMatrixd(GLcontext a0, const GLdouble*a1); /* 45 */
void GLLoadMatrixf(GLcontext a0, const GLfloat*a1); /* 46 */
void GLLockArrays(GLcontext a0, GLuint a1, GLsizei a2); /* 47 */
void GLMatrixMode(GLcontext a0, GLenum a1); /* 48 */
void GLMultMatrixd(GLcontext a0, const GLdouble*a1); /* 49 */
void GLMultMatrixf(GLcontext a0, const GLfloat*a1); /* 50 */
void GLMultiTexCoord2fARB(GLcontext a0, GLenum a1, GLfloat a2, GLfloat a3); /* 51 */
void GLMultiTexCoord2fvARB(GLcontext a0, GLenum a1, GLfloat*a2); /* 52 */
void GLNormal3f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3); /* 53 */
void GLOrtho(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4, GLdouble a5, GLdouble a6); /* 54 */
void GLPixelStorei(GLcontext a0, GLenum a1, GLint a2); /* 55 */
void GLPointSize(GLcontext a0, GLfloat a1); /* 56 */
void GLPolygonMode(GLcontext a0, GLenum a1, GLenum a2); /* 57 */
void GLPopMatrix(GLcontext a0); /* 58 */
void GLPushMatrix(GLcontext a0); /* 59 */
void GLReadPixels(GLcontext a0, GLint a1, GLint a2, GLsizei a3, GLsizei a4, GLenum a5, GLenum a6, GLvoid*a7); /* 60 */
void GLRotated(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3, GLdouble a4); /* 61 */
void GLRotatef(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4); /* 62 */
void GLRotatefEXT(GLcontext a0, GLfloat a1, const GLint a2); /* 63 */
void GLRotatefEXTs(GLcontext a0, GLfloat a1, GLfloat a2, const GLint a3); /* 64 */
void GLScaled(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3); /* 65 */
void GLScalef(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3); /* 66 */
void GLScissor(GLcontext a0, GLint a1, GLint a2, GLsizei a3, GLsizei a4); /* 67 */
void GLShadeModel(GLcontext a0, GLenum a1); /* 68 */
void GLTexCoord2f(GLcontext a0, GLfloat a1, GLfloat a2); /* 69 */
void GLTexCoord2fv(GLcontext a0, GLfloat*a1); /* 70 */
void GLTexCoord4f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4); /* 71 */
void GLTexCoord4fv(GLcontext a0, GLfloat*a1); /* 72 */
void GLTexCoordPointer(GLcontext a0, GLint a1, GLenum a2, GLsizei a3, const GLvoid*a4); /* 73 */
void GLTexEnvi(GLcontext a0, GLenum a1, GLenum a2, GLint a3); /* 74 */
void GLTexGeni(GLcontext a0, GLenum a1, GLenum a2, GLenum a3); /* 75 */
void GLTexImage2D(GLcontext a0, GLenum a1, GLint a2, GLint a3, GLsizei a4, GLsizei a5, GLint a6, GLenum a7, GLenum a8, const GLvoid*a9); /* 76 */
void GLTexParameteri(GLcontext a0, GLenum a1, GLenum a2, GLint a3); /* 77 */
void GLTexSubImage2D(GLcontext a0, GLenum a1, GLint a2, GLint a3, GLint a4, GLsizei a5, GLsizei a6, GLenum a7, GLenum a8, const GLvoid*a9); /* 78 */
void GLTranslated(GLcontext a0, GLdouble a1, GLdouble a2, GLdouble a3); /* 79 */
void GLTranslatef(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3); /* 80 */
void GLULookAt(GLfloat a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4, GLfloat a5, GLfloat a6, GLfloat a7, GLfloat a8); /* 81 */
void GLUPerspective(GLfloat a0, GLfloat a1, GLfloat a2, GLfloat a3); /* 82 */
void GLUnlockArrays(GLcontext a0); /* 83 */
void GLVertex2fv(GLcontext a0, GLfloat*a1); /* 84 */
void GLVertex3fv(GLcontext a0, GLfloat*a1); /* 85 */
void GLVertex4f(GLcontext a0, GLfloat a1, GLfloat a2, GLfloat a3, GLfloat a4); /* 86 */
void GLVertex4fv(GLcontext a0, GLfloat*a1); /* 87 */
void GLVertexPointer(GLcontext a0, GLint a1, GLenum a2, GLsizei a3, const GLvoid*a4); /* 88 */
void GLViewport(GLcontext a0, GLint a1, GLint a2, GLsizei a3, GLsizei a4); /* 89 */
void MGLClearPointer(GLcontext a0); /* 90 */
void * MGLCreateContext(int a0, int a1, int a2, int a3); /* 91 */
void * MGLCreateContextFromID(GLint a0, GLint*a1, GLint*a2); /* 92 */
void MGLDeleteContext(GLcontext a0); /* 93 */
void MGLDrawMultitexBuffer(GLcontext a0, GLenum a1, GLenum a2, GLenum a3); /* 94 */
void MGLEnableSync(GLcontext a0, GLboolean a1); /* 95 */
void MGLExit(GLcontext a0); /* 96 */
void * MGLGetInputWindowHandle(GLcontext a0); /* 97 */
void * MGLGetWindowHandle(GLcontext a0); /* 98 */
void MGLIdleFunc(GLcontext a0, IdleFn a1); /* 99 */
void MGLKeyFunc(GLcontext a0, KeyHandlerFn a1); /* 100 */
GLboolean MGLLockBack(GLcontext a0, MGLLockInfo*a1); /* 101 */
GLboolean MGLLockDisplay(GLcontext a0); /* 102 */
void MGLLockMode(GLcontext a0, GLenum a1); /* 103 */
void MGLMainLoop(GLcontext a0); /* 104 */
void MGLMinTriArea(GLcontext a0, GLfloat a1); /* 105 */
void MGLMouseFunc(GLcontext a0, MouseHandlerFn a1); /* 106 */
void MGLPrintMatrix(GLcontext a0, int a1); /* 107 */
void MGLPrintMatrixStack(GLcontext a0, int a1); /* 108 */
GLboolean MGLResizeContext(GLcontext a0, GLsizei a1, GLsizei a2); /* 109 */
void MGLSetPointer(GLcontext a0); /* 110 */
void MGLSetState(GLcontext a0, GLenum a1, GLboolean a2); /* 111 */
void MGLSetZOffset(GLcontext a0, GLfloat a1); /* 112 */
void MGLSpecialFunc(GLcontext a0, SpecialHandlerFn a1); /* 113 */
void MGLSwitchDisplay(GLcontext a0); /* 114 */
void MGLTexMemStat(GLcontext a0, GLint*a1, GLint*a2); /* 115 */
void MGLUnlockDisplay(GLcontext a0); /* 116 */
void MGLWriteShotPPM(GLcontext a0, char*a1); /* 117 */
void MGLChooseGuardBand(GLboolean a0); /* 118 */
void MGLChooseMtexBufferSize(int a0); /* 119 */
void MGLChooseNumberOfBuffers(int a0); /* 120 */
void MGLChoosePixelDepth(int a0); /* 121 */
void MGLChooseTextureBufferSize(int a0); /* 122 */
void MGLChooseVertexBufferSize(int a0); /* 123 */
void MGLChooseWindowMode(GLboolean a0); /* 124 */
GLint MGLGetSupportedScreenModes(MGLScreenModeCallback a0); /* 125 */
void MGLProhibitAlphaFallback(GLboolean a0); /* 126 */
void MGLProhibitMipMapping(GLboolean a0); /* 127 */
void MGLProposeCloseDesktop(GLboolean a0); /* 128 */
void GLPolygonOffset(GLcontext a0, GLfloat a1, GLfloat a2); /* 129 */
void GLClientActiveTextureARB(GLcontext a0, GLenum a1); /* 130 */
void GLInterleavedArrays(GLcontext a0, GLenum a1, GLsizei a2, const GLvoid*a3); /* 131 */
void GLMultiDrawArrays(GLcontext a0, GLenum a1, const GLint*a2, const GLsizei*a3, GLsizei a4); /* 132 */
void GLBlendEquation(GLcontext a0, GLenum a1); /* 133 */
void GLBlendFuncSeparate(GLcontext a0, GLenum a1, GLenum a2, GLenum a3, GLenum a4); /* 134 */
GLboolean GLIsTexture(GLcontext a0, GLuint a1); /* 135 */
void MGLChooseZBufferDepth(int a0); /* 136 */
void GLLineWidth(GLcontext a0, GLfloat a1); /* 137 */
void GLTexGenfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3); /* 138 */
void GLCopyTexImage2D(GLcontext a0, GLenum a1, GLint a2, GLenum a3, GLint a4, GLint a5, GLsizei a6, GLsizei a7, GLint a8); /* 139 */
void GLCopyTexSubImage2D(GLcontext a0, GLenum a1, GLint a2, GLint a3, GLint a4, GLint a5, GLint a6, GLsizei a7, GLsizei a8); /* 140 */
GLboolean GLAreTexturesResident(GLcontext a0, GLsizei a1, const GLuint*a2, GLboolean*a3); /* 141 */
void GLEdgeFlag(GLcontext a0, GLboolean a1); /* 142 */
void GLEdgeFlagPointer(GLcontext a0, GLsizei a1, const GLvoid*a2); /* 143 */
void GLEdgeFlagv(GLcontext a0, const GLboolean*a1); /* 144 */
void GLGetDoublev(GLcontext a0, GLenum a1, GLdouble*a2); /* 145 */
void GLGetPointerv(GLcontext a0, GLenum a1, GLvoid**a2); /* 146 */
void GLIndexi(GLcontext a0, GLint a1); /* 147 */
void GLIndexiv(GLcontext a0, const GLint*a1); /* 148 */
void GLIndexPointer(GLcontext a0, GLenum a1, GLsizei a2, const GLvoid*a3); /* 149 */
void GLPrioritizeTextures(GLcontext a0, GLsizei a1, const GLuint*a2, const GLclampf*a3); /* 150 */
void GLReadBuffer(GLcontext a0, GLenum a1); /* 151 */
void * MGLCreateContextFromWindow(struct Window*a0); /* 152 */
void * MGLCreateContextFromBitMap(struct BitMap*a0); /* 153 */
void GLLightf(GLcontext a0, GLenum a1, GLenum a2, GLfloat a3); /* 154 */
void GLLightfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3); /* 155 */
void GLMaterialf(GLcontext a0, GLenum a1, GLenum a2, GLfloat a3); /* 156 */
void GLMaterialfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3); /* 157 */
void GLLightModelf(GLcontext a0, GLenum a1, GLfloat a2); /* 158 */
void GLLightModelfv(GLcontext a0, GLenum a1, const GLfloat*a2); /* 159 */
void GLGetLightfv(GLcontext a0, GLenum a1, GLenum a2, GLfloat*a3); /* 160 */
void GLGetMaterialfv(GLcontext a0, GLenum a1, GLenum a2, GLfloat*a3); /* 161 */
GLuint GLGenLists(GLcontext a0, GLsizei a1); /* 162 */
void GLDeleteLists(GLcontext a0, GLuint a1, GLsizei a2); /* 163 */
GLboolean GLIsList(GLcontext a0, GLuint a1); /* 164 */
void GLNewList(GLcontext a0, GLuint a1, GLenum a2); /* 165 */
void GLEndList(GLcontext a0); /* 166 */
void GLCallList(GLcontext a0, GLuint a1); /* 167 */
GLint GLUBuild2DMipmaps(GLcontext a0, GLenum a1, GLint a2, GLsizei a3, GLsizei a4, GLenum a5, GLenum a6, const GLvoid*a7); /* 168 */
const GLubyte * GLUErrorString(GLenum a0); /* 169 */
GLUquadricObj * GLUNewQuadric(void); /* 170 */
void GLUDeleteQuadric(GLUquadricObj*a0); /* 171 */
void GLUQuadricNormals(GLUquadricObj*a0, GLenum a1); /* 172 */
void GLUQuadricTexture(GLUquadricObj*a0, GLboolean a1); /* 173 */
void GLUQuadricDrawStyle(GLUquadricObj*a0, GLenum a1); /* 174 */
void GLUQuadricOrientation(GLUquadricObj*a0, GLenum a1); /* 175 */
void GLUCylinder(GLUquadricObj*a0, GLdouble a1, GLdouble a2, GLdouble a3, GLint a4, GLint a5); /* 176 */
void GLUSphere(GLUquadricObj*a0, GLdouble a1, GLint a2, GLint a3); /* 177 */
void GLUDisk(GLUquadricObj*a0, GLdouble a1, GLdouble a2, GLint a3, GLint a4); /* 178 */
void GLUTInit(int*a0, char**a1); /* 179 */
void GLUTInitDisplayMode(unsigned int a0); /* 180 */
void GLUTInitWindowSize(int a0, int a1); /* 181 */
void GLUTInitWindowPosition(int a0, int a1); /* 182 */
int GLUTCreateWindow(const char*a0); /* 183 */
void GLUTMainLoop(void); /* 184 */
void GLUTDisplayFunc(void (*a0)(void)); /* 185 */
void GLUTIdleFunc(void (*a0)(void)); /* 186 */
void GLUTKeyboardFunc(void (*a0)(unsigned char key, int x, int y)); /* 187 */
void GLUTReshapeFunc(void (*a0)(int width, int height)); /* 188 */
void GLUTSwapBuffers(void); /* 189 */
void GLUTPostRedisplay(void); /* 190 */
int GLUTGet(GLenum a0); /* 191 */
void GLUTGameModeString(const char*a0); /* 192 */
int GLUTEnterGameMode(void); /* 193 */
void GLUTLeaveGameMode(void); /* 194 */
int GLUTGameModeGet(GLenum a0); /* 195 */
void GLUTSolidCube(GLdouble a0); /* 196 */
void GLUTSolidSphere(GLdouble a0, GLint a1, GLint a2); /* 197 */
void GLUTSolidCone(GLdouble a0, GLdouble a1, GLint a2, GLint a3); /* 198 */
void GLUTSolidTorus(GLdouble a0, GLdouble a1, GLint a2, GLint a3); /* 199 */
void GLUTSolidDodecahedron(void); /* 200 */
void GLNormalPointer(GLcontext a0, GLenum a1, GLsizei a2, const GLvoid*a3); /* 201 */
void GLUQuadricCallback(GLUquadricObj*a0, GLenum a1, MGLUfuncptr a2); /* 202 */
void GLColorMaterial(GLcontext a0, GLenum a1, GLenum a2); /* 203 */
void GLTexEnvfv(GLcontext a0, GLenum a1, GLenum a2, const GLfloat*a3); /* 204 */
void GLNormal3fv(GLcontext a0, GLfloat*a1); /* 205 */

#endif
