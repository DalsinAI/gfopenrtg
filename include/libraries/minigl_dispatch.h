/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MADE BY library/minigl/gen_api.py FROM docs/design/MiniGL-Compatible-Spec.md
 * (section 8). Do not edit: change the generator or the spec.
 *
 * The table minigl.library hands out (its function at -30): five longwords,
 * then one function pointer for each entry, in the order of the spec's
 * section 8. All fields are 32 bits, big-endian on the Amiga, no padding.
 * Entries are only ever appended; the ABI version stays 5. */
#ifndef LIBRARIES_MINIGL_DISPATCH_H
#define LIBRARIES_MINIGL_DISPATCH_H

#include <mgl/mgltypes.h>

struct Window;
struct BitMap;

#define MGL_DISPATCH_ABI        5       /* equal, or a program refuses the table */
#define MGL_DISPATCH_ENTRIES    205
#define MGL_DISPATCH_SIZE       840     /* header and entries, in bytes */
#define MGL_DISPATCH_SIZE_290   836     /* what a program built with 29.0 headers asks for at least */
#define MGL_BACKEND_AMICRADLE   (1UL << 16)   /* our flag; the published bits 0-2 are left clear */

typedef struct MGLDispatchTable {
    unsigned long abi_version;          /* 0: MGL_DISPATCH_ABI */
    unsigned long table_size;           /* 4: bytes, at least what the program knows */
    unsigned long backend_flags;        /* 8 */
    unsigned long reserved;             /* 12: 0 */
    GLcontext *current_context;         /* 16: read by every call, written by the client */
    void (*glActiveTextureARB)(GLcontext, GLenum); /* 1: 20 */
    void (*glAlphaFunc)(GLcontext, GLenum, GLclampf); /* 2: 24 */
    void (*glArrayElement)(GLcontext, GLint); /* 3: 28 */
    void (*glBegin)(GLcontext, GLenum); /* 4: 32 */
    void (*glBindTexture)(GLcontext, GLenum, GLuint); /* 5: 36 */
    void (*glBlendFunc)(GLcontext, GLenum, GLenum); /* 6: 40 */
    void (*glClear)(GLcontext, GLbitfield); /* 7: 44 */
    void (*glClearColor)(GLcontext, GLclampf, GLclampf, GLclampf, GLclampf); /* 8: 48 */
    void (*glClearDepth)(GLcontext, GLclampd); /* 9: 52 */
    void (*glColor3fv)(GLcontext, GLfloat*); /* 10: 56 */
    void (*glColor3ubv)(GLcontext, GLubyte*); /* 11: 60 */
    void (*glColor4f)(GLcontext, GLfloat, GLfloat, GLfloat, GLfloat); /* 12: 64 */
    void (*glColor4fv)(GLcontext, GLfloat*); /* 13: 68 */
    void (*glColor4ub)(GLcontext, GLubyte, GLubyte, GLubyte, GLubyte); /* 14: 72 */
    void (*glColor4ubv)(GLcontext, GLubyte*); /* 15: 76 */
    void (*glColorMask)(GLcontext, GLboolean, GLboolean, GLboolean, GLboolean); /* 16: 80 */
    void (*glColorPointer)(GLcontext, GLint, GLenum, GLsizei, const GLvoid*); /* 17: 84 */
    void (*glColorTable)(GLcontext, GLenum, GLenum, GLint, GLenum, GLenum, GLvoid*); /* 18: 88 */
    void (*glCullFace)(GLcontext, GLenum); /* 19: 92 */
    void (*glDeleteTextures)(GLcontext, GLsizei, const GLuint*); /* 20: 96 */
    void (*glDepthFunc)(GLcontext, GLenum); /* 21: 100 */
    void (*glDepthMask)(GLcontext, GLboolean); /* 22: 104 */
    void (*glDepthRange)(GLcontext, GLclampd, GLclampd); /* 23: 108 */
    void (*glDisableClientState)(GLcontext, GLenum); /* 24: 112 */
    void (*glDrawArrays)(GLcontext, GLenum, GLint, GLsizei); /* 25: 116 */
    void (*glDrawBuffer)(GLcontext, GLenum); /* 26: 120 */
    void (*glDrawElements)(GLcontext, GLenum, GLsizei, GLenum, const GLvoid*); /* 27: 124 */
    void (*glEnableClientState)(GLcontext, GLenum); /* 28: 128 */
    void (*glEnd)(GLcontext); /* 29: 132 */
    void (*glFinish)(GLcontext); /* 30: 136 */
    void (*glFlush)(GLcontext); /* 31: 140 */
    void (*glFogf)(GLcontext, GLenum, GLfloat); /* 32: 144 */
    void (*glFogfv)(GLcontext, GLenum, GLfloat*); /* 33: 148 */
    void (*glFrontFace)(GLcontext, GLenum); /* 34: 152 */
    void (*glFrustum)(GLcontext, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble); /* 35: 156 */
    void (*glGenTextures)(GLcontext, GLsizei, GLuint*); /* 36: 160 */
    void (*glGetBooleanv)(GLcontext, GLenum, GLboolean*); /* 37: 164 */
    GLenum (*glGetError)(GLcontext); /* 38: 168 */
    void (*glGetFloatv)(GLcontext, GLenum, GLfloat*); /* 39: 172 */
    void (*glGetIntegerv)(GLcontext, GLenum, GLint*); /* 40: 176 */
    const GLubyte* (*glGetString)(GLcontext, GLenum); /* 41: 180 */
    void (*glHint)(GLcontext, GLenum, GLenum); /* 42: 184 */
    GLboolean (*glIsEnabled)(GLcontext, GLenum); /* 43: 188 */
    void (*glLoadIdentity)(GLcontext); /* 44: 192 */
    void (*glLoadMatrixd)(GLcontext, const GLdouble*); /* 45: 196 */
    void (*glLoadMatrixf)(GLcontext, const GLfloat*); /* 46: 200 */
    void (*glLockArrays)(GLcontext, GLuint, GLsizei); /* 47: 204 */
    void (*glMatrixMode)(GLcontext, GLenum); /* 48: 208 */
    void (*glMultMatrixd)(GLcontext, const GLdouble*); /* 49: 212 */
    void (*glMultMatrixf)(GLcontext, const GLfloat*); /* 50: 216 */
    void (*glMultiTexCoord2fARB)(GLcontext, GLenum, GLfloat, GLfloat); /* 51: 220 */
    void (*glMultiTexCoord2fvARB)(GLcontext, GLenum, GLfloat*); /* 52: 224 */
    void (*glNormal3f)(GLcontext, GLfloat, GLfloat, GLfloat); /* 53: 228 */
    void (*glOrtho)(GLcontext, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble); /* 54: 232 */
    void (*glPixelStorei)(GLcontext, GLenum, GLint); /* 55: 236 */
    void (*glPointSize)(GLcontext, GLfloat); /* 56: 240 */
    void (*glPolygonMode)(GLcontext, GLenum, GLenum); /* 57: 244 */
    void (*glPopMatrix)(GLcontext); /* 58: 248 */
    void (*glPushMatrix)(GLcontext); /* 59: 252 */
    void (*glReadPixels)(GLcontext, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*); /* 60: 256 */
    void (*glRotated)(GLcontext, GLdouble, GLdouble, GLdouble, GLdouble); /* 61: 260 */
    void (*glRotatef)(GLcontext, GLfloat, GLfloat, GLfloat, GLfloat); /* 62: 264 */
    void (*glRotatefEXT)(GLcontext, GLfloat, const GLint); /* 63: 268 */
    void (*glRotatefEXTs)(GLcontext, GLfloat, GLfloat, const GLint); /* 64: 272 */
    void (*glScaled)(GLcontext, GLdouble, GLdouble, GLdouble); /* 65: 276 */
    void (*glScalef)(GLcontext, GLfloat, GLfloat, GLfloat); /* 66: 280 */
    void (*glScissor)(GLcontext, GLint, GLint, GLsizei, GLsizei); /* 67: 284 */
    void (*glShadeModel)(GLcontext, GLenum); /* 68: 288 */
    void (*glTexCoord2f)(GLcontext, GLfloat, GLfloat); /* 69: 292 */
    void (*glTexCoord2fv)(GLcontext, GLfloat*); /* 70: 296 */
    void (*glTexCoord4f)(GLcontext, GLfloat, GLfloat, GLfloat, GLfloat); /* 71: 300 */
    void (*glTexCoord4fv)(GLcontext, GLfloat*); /* 72: 304 */
    void (*glTexCoordPointer)(GLcontext, GLint, GLenum, GLsizei, const GLvoid*); /* 73: 308 */
    void (*glTexEnvi)(GLcontext, GLenum, GLenum, GLint); /* 74: 312 */
    void (*glTexGeni)(GLcontext, GLenum, GLenum, GLenum); /* 75: 316 */
    void (*glTexImage2D)(GLcontext, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*); /* 76: 320 */
    void (*glTexParameteri)(GLcontext, GLenum, GLenum, GLint); /* 77: 324 */
    void (*glTexSubImage2D)(GLcontext, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*); /* 78: 328 */
    void (*glTranslated)(GLcontext, GLdouble, GLdouble, GLdouble); /* 79: 332 */
    void (*glTranslatef)(GLcontext, GLfloat, GLfloat, GLfloat); /* 80: 336 */
    void (*gluLookAt)(GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat); /* 81: 340 */
    void (*gluPerspective)(GLfloat, GLfloat, GLfloat, GLfloat); /* 82: 344 */
    void (*glUnlockArrays)(GLcontext); /* 83: 348 */
    void (*glVertex2fv)(GLcontext, GLfloat*); /* 84: 352 */
    void (*glVertex3fv)(GLcontext, GLfloat*); /* 85: 356 */
    void (*glVertex4f)(GLcontext, GLfloat, GLfloat, GLfloat, GLfloat); /* 86: 360 */
    void (*glVertex4fv)(GLcontext, GLfloat*); /* 87: 364 */
    void (*glVertexPointer)(GLcontext, GLint, GLenum, GLsizei, const GLvoid*); /* 88: 368 */
    void (*glViewport)(GLcontext, GLint, GLint, GLsizei, GLsizei); /* 89: 372 */
    void (*mglClearPointer)(GLcontext); /* 90: 376 */
    void * (*mglCreateContext)(int, int, int, int); /* 91: 380 */
    void * (*mglCreateContextFromID)(GLint, GLint*, GLint*); /* 92: 384 */
    void (*mglDeleteContext)(GLcontext); /* 93: 388 */
    void (*mglDrawMultitexBuffer)(GLcontext, GLenum, GLenum, GLenum); /* 94: 392 */
    void (*mglEnableSync)(GLcontext, GLboolean); /* 95: 396 */
    void (*mglExit)(GLcontext); /* 96: 400 */
    void * (*mglGetInputWindowHandle)(GLcontext); /* 97: 404 */
    void * (*mglGetWindowHandle)(GLcontext); /* 98: 408 */
    void (*mglIdleFunc)(GLcontext, IdleFn); /* 99: 412 */
    void (*mglKeyFunc)(GLcontext, KeyHandlerFn); /* 100: 416 */
    GLboolean (*mglLockBack)(GLcontext, MGLLockInfo*); /* 101: 420 */
    GLboolean (*mglLockDisplay)(GLcontext); /* 102: 424 */
    void (*mglLockMode)(GLcontext, GLenum); /* 103: 428 */
    void (*mglMainLoop)(GLcontext); /* 104: 432 */
    void (*mglMinTriArea)(GLcontext, GLfloat); /* 105: 436 */
    void (*mglMouseFunc)(GLcontext, MouseHandlerFn); /* 106: 440 */
    void (*mglPrintMatrix)(GLcontext, int); /* 107: 444 */
    void (*mglPrintMatrixStack)(GLcontext, int); /* 108: 448 */
    GLboolean (*mglResizeContext)(GLcontext, GLsizei, GLsizei); /* 109: 452 */
    void (*mglSetPointer)(GLcontext); /* 110: 456 */
    void (*mglSetState)(GLcontext, GLenum, GLboolean); /* 111: 460 */
    void (*mglSetZOffset)(GLcontext, GLfloat); /* 112: 464 */
    void (*mglSpecialFunc)(GLcontext, SpecialHandlerFn); /* 113: 468 */
    void (*mglSwitchDisplay)(GLcontext); /* 114: 472 */
    void (*mglTexMemStat)(GLcontext, GLint*, GLint*); /* 115: 476 */
    void (*mglUnlockDisplay)(GLcontext); /* 116: 480 */
    void (*mglWriteShotPPM)(GLcontext, char*); /* 117: 484 */
    void (*mglChooseGuardBand)(GLboolean); /* 118: 488 */
    void (*mglChooseMtexBufferSize)(int); /* 119: 492 */
    void (*mglChooseNumberOfBuffers)(int); /* 120: 496 */
    void (*mglChoosePixelDepth)(int); /* 121: 500 */
    void (*mglChooseTextureBufferSize)(int); /* 122: 504 */
    void (*mglChooseVertexBufferSize)(int); /* 123: 508 */
    void (*mglChooseWindowMode)(GLboolean); /* 124: 512 */
    GLint (*mglGetSupportedScreenModes)(MGLScreenModeCallback); /* 125: 516 */
    void (*mglProhibitAlphaFallback)(GLboolean); /* 126: 520 */
    void (*mglProhibitMipMapping)(GLboolean); /* 127: 524 */
    void (*mglProposeCloseDesktop)(GLboolean); /* 128: 528 */
    void (*glPolygonOffset)(GLcontext, GLfloat, GLfloat); /* 129: 532 */
    void (*glClientActiveTextureARB)(GLcontext, GLenum); /* 130: 536 */
    void (*glInterleavedArrays)(GLcontext, GLenum, GLsizei, const GLvoid*); /* 131: 540 */
    void (*glMultiDrawArrays)(GLcontext, GLenum, const GLint*, const GLsizei*, GLsizei); /* 132: 544 */
    void (*glBlendEquation)(GLcontext, GLenum); /* 133: 548 */
    void (*glBlendFuncSeparate)(GLcontext, GLenum, GLenum, GLenum, GLenum); /* 134: 552 */
    GLboolean (*glIsTexture)(GLcontext, GLuint); /* 135: 556 */
    void (*mglChooseZBufferDepth)(int); /* 136: 560 */
    void (*glLineWidth)(GLcontext, GLfloat); /* 137: 564 */
    void (*glTexGenfv)(GLcontext, GLenum, GLenum, const GLfloat*); /* 138: 568 */
    void (*glCopyTexImage2D)(GLcontext, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint); /* 139: 572 */
    void (*glCopyTexSubImage2D)(GLcontext, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei); /* 140: 576 */
    GLboolean (*glAreTexturesResident)(GLcontext, GLsizei, const GLuint*, GLboolean*); /* 141: 580 */
    void (*glEdgeFlag)(GLcontext, GLboolean); /* 142: 584 */
    void (*glEdgeFlagPointer)(GLcontext, GLsizei, const GLvoid*); /* 143: 588 */
    void (*glEdgeFlagv)(GLcontext, const GLboolean*); /* 144: 592 */
    void (*glGetDoublev)(GLcontext, GLenum, GLdouble*); /* 145: 596 */
    void (*glGetPointerv)(GLcontext, GLenum, GLvoid**); /* 146: 600 */
    void (*glIndexi)(GLcontext, GLint); /* 147: 604 */
    void (*glIndexiv)(GLcontext, const GLint*); /* 148: 608 */
    void (*glIndexPointer)(GLcontext, GLenum, GLsizei, const GLvoid*); /* 149: 612 */
    void (*glPrioritizeTextures)(GLcontext, GLsizei, const GLuint*, const GLclampf*); /* 150: 616 */
    void (*glReadBuffer)(GLcontext, GLenum); /* 151: 620 */
    void * (*mglCreateContextFromWindow)(struct Window*); /* 152: 624 */
    void * (*mglCreateContextFromBitMap)(struct BitMap*); /* 153: 628 */
    void (*glLightf)(GLcontext, GLenum, GLenum, GLfloat); /* 154: 632 */
    void (*glLightfv)(GLcontext, GLenum, GLenum, const GLfloat*); /* 155: 636 */
    void (*glMaterialf)(GLcontext, GLenum, GLenum, GLfloat); /* 156: 640 */
    void (*glMaterialfv)(GLcontext, GLenum, GLenum, const GLfloat*); /* 157: 644 */
    void (*glLightModelf)(GLcontext, GLenum, GLfloat); /* 158: 648 */
    void (*glLightModelfv)(GLcontext, GLenum, const GLfloat*); /* 159: 652 */
    void (*glGetLightfv)(GLcontext, GLenum, GLenum, GLfloat*); /* 160: 656 */
    void (*glGetMaterialfv)(GLcontext, GLenum, GLenum, GLfloat*); /* 161: 660 */
    GLuint (*glGenLists)(GLcontext, GLsizei); /* 162: 664 */
    void (*glDeleteLists)(GLcontext, GLuint, GLsizei); /* 163: 668 */
    GLboolean (*glIsList)(GLcontext, GLuint); /* 164: 672 */
    void (*glNewList)(GLcontext, GLuint, GLenum); /* 165: 676 */
    void (*glEndList)(GLcontext); /* 166: 680 */
    void (*glCallList)(GLcontext, GLuint); /* 167: 684 */
    GLint (*gluBuild2DMipmaps)(GLcontext, GLenum, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*); /* 168: 688 */
    const GLubyte * (*gluErrorString)(GLenum); /* 169: 692 */
    GLUquadricObj * (*gluNewQuadric)(void); /* 170: 696 */
    void (*gluDeleteQuadric)(GLUquadricObj*); /* 171: 700 */
    void (*gluQuadricNormals)(GLUquadricObj*, GLenum); /* 172: 704 */
    void (*gluQuadricTexture)(GLUquadricObj*, GLboolean); /* 173: 708 */
    void (*gluQuadricDrawStyle)(GLUquadricObj*, GLenum); /* 174: 712 */
    void (*gluQuadricOrientation)(GLUquadricObj*, GLenum); /* 175: 716 */
    void (*gluCylinder)(GLUquadricObj*, GLdouble, GLdouble, GLdouble, GLint, GLint); /* 176: 720 */
    void (*gluSphere)(GLUquadricObj*, GLdouble, GLint, GLint); /* 177: 724 */
    void (*gluDisk)(GLUquadricObj*, GLdouble, GLdouble, GLint, GLint); /* 178: 728 */
    void (*glutInit)(int*, char**); /* 179: 732 */
    void (*glutInitDisplayMode)(unsigned int); /* 180: 736 */
    void (*glutInitWindowSize)(int, int); /* 181: 740 */
    void (*glutInitWindowPosition)(int, int); /* 182: 744 */
    int (*glutCreateWindow)(const char*); /* 183: 748 */
    void (*glutMainLoop)(void); /* 184: 752 */
    void (*glutDisplayFunc)(void (*)(void)); /* 185: 756 */
    void (*glutIdleFunc)(void (*)(void)); /* 186: 760 */
    void (*glutKeyboardFunc)(void (*)(unsigned char key, int x, int y)); /* 187: 764 */
    void (*glutReshapeFunc)(void (*)(int width, int height)); /* 188: 768 */
    void (*glutSwapBuffers)(void); /* 189: 772 */
    void (*glutPostRedisplay)(void); /* 190: 776 */
    int (*glutGet)(GLenum); /* 191: 780 */
    void (*glutGameModeString)(const char*); /* 192: 784 */
    int (*glutEnterGameMode)(void); /* 193: 788 */
    void (*glutLeaveGameMode)(void); /* 194: 792 */
    int (*glutGameModeGet)(GLenum); /* 195: 796 */
    void (*glutSolidCube)(GLdouble); /* 196: 800 */
    void (*glutSolidSphere)(GLdouble, GLint, GLint); /* 197: 804 */
    void (*glutSolidCone)(GLdouble, GLdouble, GLint, GLint); /* 198: 808 */
    void (*glutSolidTorus)(GLdouble, GLdouble, GLint, GLint); /* 199: 812 */
    void (*glutSolidDodecahedron)(void); /* 200: 816 */
    void (*glNormalPointer)(GLcontext, GLenum, GLsizei, const GLvoid*); /* 201: 820 */
    void (*gluQuadricCallback)(GLUquadricObj*, GLenum, MGLUfuncptr); /* 202: 824 */
    void (*glColorMaterial)(GLcontext, GLenum, GLenum); /* 203: 828 */
    void (*glTexEnvfv)(GLcontext, GLenum, GLenum, const GLfloat*); /* 204: 832 */
    void (*glNormal3fv)(GLcontext, GLfloat*); /* 205: 836 */
} MGLDispatchTable;

#endif
