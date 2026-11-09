# minigl.library-compatible: interface specification (dispatch ABI 5)

> **Clean-room interface spec: written from published interface facts only; implementers must not read PiStorm3D/MiniGL sources.**

Team 5 helper, for Dale, 9 November 2026. IDEA 5 in `pistorm3d-perf-notes-20261109.md` (Team 5 project notes). The implementation plan is `MiniGL-Compatible-Plan.md`, next to this file.

This document says what a library must do so that programs built for the shared `minigl.library` (MiniGL 29.x and 30.0, "dispatch ABI 5") run on it unchanged. It is written in our own words. It holds no header text, no comments and no code from any MiniGL or PiStorm3D file. The C signatures below are ours, written with types only. Constant values are given where a compiled program depends on them, and most are OpenGL's own (Khronos) values.

**Licence position, short form (details in section 12):** re-implementing an interface is generally allowed (US: *Google v. Oracle*, 2021; EU: *SAS Institute v. World Programming*, 2012), but this is not legal advice. The file name `minigl.library` is needed because programs pass it to `OpenLibrary`. Dale should decide whether to get a legal opinion before we ship.

## 1. Sources, and what was not read

Interface facts were taken only from published material:

| Source | What was used |
|---|---|
| The minigl.library SDK headers as published for 29.1 and 30.0 (the `sdk/v29_1` and `sdk/v30` folders of github.com/arczi84/WarpMiniGL, `libraryroot/include` only), and the public `include/` folders of the branches of github.com/SteffenHaeuser/MiniGL_Library_68k | Library name and version number, the library vector offset, the dispatch table's order, its types and its size, the ABI and size checks, type sizes, constant values |
| `doc/mgl.doc` (the AutoDoc), `doc/mgl.changes`, `Readme.md`, `FAQ` of MiniGL_Library_68k (branch `PiStorm3D`) | What each `mgl*` call is documented to do, documented deviations from GL 1.1, the extension string |
| github.com/Sdursun/ReturnToCastleWolfenstein_Minigl68k: `MINIGL_NOTES.md`, `sp/amigaos/amiga_glimp.c`, `sp/amigaos/amiga_qgl.h` | Which calls a real game makes, in what order, and what it works around |
| The Khronos `GL/gl.h` and `GL/glext.h` (Mesa's copies on Dale's PC) | Checking that the SDK's `GL_*` values are the standard ones |
| The OpenGL 1.1 specification, GLU 1.3 and GLUT 3.7 | Behaviour of the `gl*`, `glu*` and `glut*` calls |
| Our own MIT code: `gfopenrtg/library/minigl` (an existing AmiCradle minigl.library 29), `library/modules/gl`, `include/gfopengpu/virgl.h` | Cross-check of the table (our `minigl_api.txt` lists the same 205 entries), and the mapping in the plan |

Not read: any implementation file (`.c`, `.s`, back end, shader or V3D file) of PiStorm3D, MiniGL, ogles2 or WarpMiniGL, and the SDK's import-library sources (`client/*.c`, `client/*.s`). Where behaviour depends on those, this document says so and names a black-box test.

## 2. Terms

- **Client**: a program (or a game's renderer DLL) built against the SDK. It links an import library from the SDK that provides `MiniGLOpen()` and `MiniGLClose()`, and every `gl*`/`glu*`/`glut*`/`mgl*` call it makes is a small inline function that calls through the dispatch table.
- **Table**: the dispatch table the library hands out. A header of five longwords, then one function pointer per entry.
- **Entry n**: the n-th function pointer, counted from 1.
- **Current context**: a `GLcontext` value stored in a variable that the library owns and the table points to.

## 3. The library

| Property | Value |
|---|---|
| File and node name | `minigl.library` (in `LIBS:`) |
| Version a client asks `OpenLibrary` for | 28 for clients built with 28.x headers, 29 with 29.x headers, **30 with the 30.0 headers**. A client may override it on its own compile line. |
| Version we must report | **30 or more.** (Our existing GFOpenRTG library reports 29, so a client built with the 30.0 SDK would fail to open it. See the plan.) |
| Library vectors | The four standard ones (Open -6, Close -12, Expunge -18, Reserved -24) and **one** function at **-30** |
| The function at -30 | Called with A6 = the library base and no other arguments. Returns the address of the table in D0. |

Facts about -30 that come only from black-box use, not from a published file: the import library's `MiniGLOpen()` calls it once, right after `OpenLibrary`, and keeps the pointer for the life of the program. Our existing library answers in D0 only and runs SDK-built demos, which supports this reading. **Test B1** (section 11) confirms it with the RTCW binary and a vbcc-built client. Put the pointer in A0 as well as D0; it costs one instruction and covers a compiler that reads A0.

**Opening and closing.** `MiniGLOpen()` opens the library, fetches the table, and checks it (section 4.2). If a check fails it closes the library and returns FALSE. `MiniGLClose()` closes the library. A client that ends without deleting its context still calls `MiniGLClose()` in most ports, and some don't, so the library must survive both.

**Resident state.** One library serves every client in the system. The `mglChoose*`/`mglProhibit*` requests and the current-context variable are library-wide in the published design (the AutoDoc says so). The library must put every `mglChoose*` and `mglProhibit*` setting back to its default (section 9.3) in three places: when a context is deleted, when a new client opens the library, and (for the close-desktop proposal) when a display opens. Otherwise one program's choice leaks into the next.

## 4. The dispatch table

### 4.1 Layout

All fields are 32 bits, big-endian, with no padding.

| Offset | Field | Value we hand out |
|---|---|---|
| 0 | ABI version | **5** |
| 4 | Size of the table in bytes | **840** (20-byte header + 205 entries × 4) |
| 8 | Back-end flags | Bits 0-2 are defined by the SDK: 1 = stub, 2 = classic (Warp3D), 4 = PiStorm3D. Our existing library sets bit 16. Use bit 16 and no SDK bit. No client is known to read this field (**test B2**). |
| 12 | Reserved | 0 |
| 16 | Address of the current-context variable (`GLcontext *`) | The variable the library reads and clients write (4.3) |
| 20 + 4·(n−1) | Entry n, n = 1..205 | A function pointer. **Never NULL.** An entry we don't support still points to a function that records an error or does nothing (section 9). |

### 4.2 The checks a client makes

- **ABI version: equality.** A client refuses any table whose version is not exactly the one in its headers. ABI 5 clients refuse 3 and 4, and the reverse. (ABI 3 and 4 clients used MiniGL's old positional enum numbers, so we do not support them.)
- **Size: "at least".** A client refuses a table whose size field is smaller than the table size its own headers define. Clients built with the 29.0 headers know 204 entries (836 bytes). Clients built with 29.1 or later headers know 205 entries (840 bytes). A larger table always passes.
- **Rules that follow.** New entries may only be appended. Nothing is ever moved or removed. The ABI version stays 5 while appending. A future entry 206+ must not be required by any existing client.

### 4.3 The current context

- Every `gl*` call a client makes reads the current-context variable *through the pointer at offset 16* and passes the value as the first argument of the entry. A NULL value is passed as it is (most clients never call GL without a context). Entries that take a context **must accept NULL** and do nothing, rather than crash.
- The client's `mglCreateContext`, `mglCreateContextFromID`, `mglCreateContextFromWindow` and `mglCreateContextFromBitMap` call the entry, and **if the result is not NULL** write it into the variable themselves. The library should also set the variable itself on success (harmless, and needed by static-style callers).
- The client's `mglDeleteContext()` takes no argument. If the variable is not NULL it copies it, clears the variable to NULL, and then calls the entry with the old value. So the entry is called with a context that is **no longer current**, and the library must not rely on the variable inside it.
- `gluLookAt` and `gluPerspective` are called by the client only when the variable is not NULL. Their entries take no context argument and must use the current one.
- Clients never read or write inside the context. `GLcontext` is opaque to them (in the 29.1 and later headers; **test B3** checks this by poisoning everything but the first longword). We can lay it out as we like.

**AmiCradle choice: one table per task.** Because clients use only the pointer they were given, the library may hand each task its own copy of the table, with its own current-context variable. That lets two MiniGL programs run at once without sharing a current context (the published library cannot do this). Key the copies by `FindTask(NULL)` in the -30 function, and free them on the matching Close.

## 5. Calling convention

- Every entry is an ordinary C function called through a pointer, in the default convention of m68k AmigaOS GCC and vbcc: **arguments on the stack**, pushed right to left, popped by the caller. **D0** holds the result. D0, D1, A0 and A1 are scratch. D2-D7 and A2-A6 must be preserved. No register is set up by the caller: the library cannot assume A4 (small data) or A6 points anywhere, and must set them itself if it needs them.
- `int`, `long`, pointers and every GL integer type are 32 bits on the stack. `GLfloat`/`GLclampf` are 4-byte IEEE singles, **not promoted to double** (the client has a prototype in scope). `GLdouble`/`GLclampd` are 8-byte IEEE doubles, big-endian, occupying two stack longwords. `GLubyte` and `GLboolean` arguments take a full longword slot (the value in the low byte for `GLubyte`, the whole longword for `GLboolean`, see 6).
- No entry returns a float or a double, so the FPU and soft-float clients call the same table the same way.
- Callbacks the library calls (idle, key, special key, mouse, and the GLUT ones) are called with the same convention, on the task that called the main loop. They receive no context.
- **Test B4**: a client compiled with GCC `-m68020 -msoft-float`, one with `-m68040 -m68881`, and one with vbcc, all through the same table.

## 6. Types

| Type | Size | Note |
|---|---|---|
| `GLenum`, `GLbitfield`, `GLint`, `GLuint`, `GLsizei` | 4 | `GLsizei` is a `long` |
| **`GLboolean`** | **4** | **An `unsigned int`, not the usual byte.** Every array of `GLboolean` has 4-byte elements: `glGetBooleanv`'s output, `glAreTexturesResident`'s output, `glEdgeFlagv`'s input and edge-flag arrays read as `GLboolean`. |
| `GLbyte`/`GLubyte`, `GLshort`/`GLushort` | 1, 2 | |
| `GLfloat`, `GLclampf` | 4 | |
| `GLdouble`, `GLclampd` | 8 | |
| `GLcontext` | 4 | Opaque pointer |
| `GLUquadricObj *` | 4 | Opaque pointer, owned by the library |
| `MGLLockInfo` | 24 | Six longwords in this order: width, height, depth, pixel format, buffer address (pointer), bytes per row |
| `MGLScreenMode` | 96 | Four `GLint`s (mode id, width, height, bits per pixel), then an 80-byte name |
| `MGLspecial` | 4 (enum) | 0-9 = F1-F10, 10 = cursor up, 11 = down, 12 = left, 13 = right |
| Idle function | | `void (void)` |
| Key function | | `void (char)`: a character from `IDCMP_VANILLAKEY` |
| Special key function | | `void (MGLspecial)` |
| Mouse function | | `void (GLint x, GLint y, GLbitfield buttons)` |
| Screen-mode callback | | `GLboolean (MGLScreenMode *)` |
| GLU callback | | `void (*)()` |

## 7. Constants

### 7.1 OpenGL tokens: standard values

From 29.1 on, the SDK's `GL_*` tokens carry OpenGL's own values. We checked the 30.0 header: of its 360 numeric `GL_*` definitions, **356 are equal to the Khronos value of the same name** and none differs. The remaining four are not in Khronos's headers:

| Name in the SDK | Value | What it is |
|---|---|---|
| (a base number) | 0x7000 | Start of MiniGL's own range |
| polygon offset (MiniGL's name without `_FILL`) | 0x7001 | MiniGL-only token. Treat it like `GL_POLYGON_OFFSET_FILL` in `glEnable`/`glIsEnabled` (**test B5**). |
| texture binding, spelled the other way round | 0x8069 | Same value as `GL_TEXTURE_BINDING_2D` |
| a misspelt texture-coordinate array pointer | 0x8092 | Same value as `GL_TEXTURE_COORD_ARRAY_POINTER` |

So **an implementation takes every `GL_*` value from Khronos's headers** (MIT-style licence), plus the MiniGL range below. The SDK also defines the buffer-object tokens (`GL_ARRAY_BUFFER` and the rest) at their standard values, but there are no buffer-object entries in the table.

GLU tokens are GLU 1.3's (100000 and up, for example 100000 smooth normals, 100900 invalid enum, 100103 the error callback). GLUT tokens are GLUT 3.7's (display-mode bits 0, 2, 0x10 ...; `glutGet` 100-123, 200-203, 500-504; elapsed time 700).

### 7.2 MiniGL's own tokens

| Value | Meaning | Where it is used |
|---|---|---|
| 0x7010 | lock mode: automatic | `mglLockMode` |
| 0x7011 | lock mode: manual (the default) | `mglLockMode` |
| 0x7012 | lock mode: smart | `mglLockMode` |
| 0x7013, 0x7014 | **withdrawn** (old flat fan and flat strip primitives) | `glBegin` must record `GL_INVALID_OPERATION` for them |
| 0x7015 | perspective mapping (a capability) | `glEnable`/`glDisable`/`glIsEnabled`: state only |
| 0x7016 | "w is one" hint | `glHint` target: may be ignored |
| 0x7017 | Z offset (a capability) | enables `mglSetZOffset`'s offset |
| 0x7018 | colour array in B,G,R,A byte order | `glColorPointer` type (size 4) |
| 0x7019 | colour array in A,R,G,B byte order | `glColorPointer` type (size 4) |
| 0x701A | packed 16-bit 5-6-5 | `glTexImage2D`/`glTexSubImage2D` type |
| 0x701B | packed 16-bit 4-4-4-4 | the same |
| 0x701C | fixed-point transform hint | `glHint` target: may be ignored |
| 0x701D | array transformations (a capability, on by default) | see 9.1, "little-endian arrays" |

Other MiniGL constants: mouse buttons left 1, right 2, middle 4. Screen mode "best" 0xFFFFFFFF, "window" 0. Z-buffer bits 16 and 32.

## 8. The entries, in table order

The signature column is ours: the return type, then the argument types. Every entry takes the context as its first argument **except** 81 and 82 (`gluLookAt`, `gluPerspective`), 91 and 92 (`mglCreateContext`, `mglCreateContextFromID`), 118-128 and 136 (the `mglChoose*`/`mglProhibit*` family and screen modes), 152 and 153 (create from a window or a bitmap), 169-200 (GLU quadrics and errors, GLUT) and 202 (`gluQuadricCallback`). Entry 168 (`gluBuild2DMipmaps`) does take the context.

| # | Offset | Serves | Signature |
|---|---|---|---|
| 1 | 20 | `glActiveTextureARB` | `void (GLcontext, GLenum)` |
| 2 | 24 | `glAlphaFunc` | `void (GLcontext, GLenum, GLclampf)` |
| 3 | 28 | `glArrayElement` | `void (GLcontext, GLint)` |
| 4 | 32 | `glBegin` | `void (GLcontext, GLenum)` |
| 5 | 36 | `glBindTexture` | `void (GLcontext, GLenum, GLuint)` |
| 6 | 40 | `glBlendFunc` | `void (GLcontext, GLenum, GLenum)` |
| 7 | 44 | `glClear` | `void (GLcontext, GLbitfield)` |
| 8 | 48 | `glClearColor` | `void (GLcontext, GLclampf, GLclampf, GLclampf, GLclampf)` |
| 9 | 52 | `glClearDepth` | `void (GLcontext, GLclampd)` |
| 10 | 56 | `glColor3fv` | `void (GLcontext, GLfloat*)` |
| 11 | 60 | `glColor3ubv` | `void (GLcontext, GLubyte*)` |
| 12 | 64 | `glColor4f` | `void (GLcontext, GLfloat, GLfloat, GLfloat, GLfloat)` |
| 13 | 68 | `glColor4fv` | `void (GLcontext, GLfloat*)` |
| 14 | 72 | `glColor4ub` | `void (GLcontext, GLubyte, GLubyte, GLubyte, GLubyte)` |
| 15 | 76 | `glColor4ubv` | `void (GLcontext, GLubyte*)` |
| 16 | 80 | `glColorMask` | `void (GLcontext, GLboolean, GLboolean, GLboolean, GLboolean)` |
| 17 | 84 | `glColorPointer` | `void (GLcontext, GLint, GLenum, GLsizei, const GLvoid*)` |
| 18 | 88 | `glColorTable` | `void (GLcontext, GLenum, GLenum, GLint, GLenum, GLenum, GLvoid*)` |
| 19 | 92 | `glCullFace` | `void (GLcontext, GLenum)` |
| 20 | 96 | `glDeleteTextures` | `void (GLcontext, GLsizei, const GLuint*)` |
| 21 | 100 | `glDepthFunc` | `void (GLcontext, GLenum)` |
| 22 | 104 | `glDepthMask` | `void (GLcontext, GLboolean)` |
| 23 | 108 | `glDepthRange` | `void (GLcontext, GLclampd, GLclampd)` |
| 24 | 112 | `glDisableClientState` | `void (GLcontext, GLenum)` |
| 25 | 116 | `glDrawArrays` | `void (GLcontext, GLenum, GLint, GLsizei)` |
| 26 | 120 | `glDrawBuffer` | `void (GLcontext, GLenum)` |
| 27 | 124 | `glDrawElements` | `void (GLcontext, GLenum, GLsizei, GLenum, const GLvoid*)` |
| 28 | 128 | `glEnableClientState` | `void (GLcontext, GLenum)` |
| 29 | 132 | `glEnd` | `void (GLcontext)` |
| 30 | 136 | `glFinish` | `void (GLcontext)` |
| 31 | 140 | `glFlush` | `void (GLcontext)` |
| 32 | 144 | `glFogf` | `void (GLcontext, GLenum, GLfloat)` |
| 33 | 148 | `glFogfv` | `void (GLcontext, GLenum, GLfloat*)` |
| 34 | 152 | `glFrontFace` | `void (GLcontext, GLenum)` |
| 35 | 156 | `glFrustum` | `void (GLcontext, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble)` |
| 36 | 160 | `glGenTextures` | `void (GLcontext, GLsizei, GLuint*)` |
| 37 | 164 | `glGetBooleanv` | `void (GLcontext, GLenum, GLboolean*)` |
| 38 | 168 | `glGetError` | `GLenum (GLcontext)` |
| 39 | 172 | `glGetFloatv` | `void (GLcontext, GLenum, GLfloat*)` |
| 40 | 176 | `glGetIntegerv` | `void (GLcontext, GLenum, GLint*)` |
| 41 | 180 | `glGetString` | `const GLubyte* (GLcontext, GLenum)` |
| 42 | 184 | `glHint` | `void (GLcontext, GLenum, GLenum)` |
| 43 | 188 | `glIsEnabled` | `GLboolean (GLcontext, GLenum)` |
| 44 | 192 | `glLoadIdentity` | `void (GLcontext)` |
| 45 | 196 | `glLoadMatrixd` | `void (GLcontext, const GLdouble*)` |
| 46 | 200 | `glLoadMatrixf` | `void (GLcontext, const GLfloat*)` |
| 47 | 204 | `glLockArrays` | `void (GLcontext, GLuint, GLsizei)` |
| 48 | 208 | `glMatrixMode` | `void (GLcontext, GLenum)` |
| 49 | 212 | `glMultMatrixd` | `void (GLcontext, const GLdouble*)` |
| 50 | 216 | `glMultMatrixf` | `void (GLcontext, const GLfloat*)` |
| 51 | 220 | `glMultiTexCoord2fARB` | `void (GLcontext, GLenum, GLfloat, GLfloat)` |
| 52 | 224 | `glMultiTexCoord2fvARB` | `void (GLcontext, GLenum, GLfloat*)` |
| 53 | 228 | `glNormal3f` | `void (GLcontext, GLfloat, GLfloat, GLfloat)` |
| 54 | 232 | `glOrtho` | `void (GLcontext, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble)` |
| 55 | 236 | `glPixelStorei` | `void (GLcontext, GLenum, GLint)` |
| 56 | 240 | `glPointSize` | `void (GLcontext, GLfloat)` |
| 57 | 244 | `glPolygonMode` | `void (GLcontext, GLenum, GLenum)` |
| 58 | 248 | `glPopMatrix` | `void (GLcontext)` |
| 59 | 252 | `glPushMatrix` | `void (GLcontext)` |
| 60 | 256 | `glReadPixels` | `void (GLcontext, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, GLvoid*)` |
| 61 | 260 | `glRotated` | `void (GLcontext, GLdouble, GLdouble, GLdouble, GLdouble)` |
| 62 | 264 | `glRotatef` | `void (GLcontext, GLfloat, GLfloat, GLfloat, GLfloat)` |
| 63 | 268 | `glRotatefEXT` | `void (GLcontext, GLfloat, const GLint)` |
| 64 | 272 | `glRotatefEXTs` | `void (GLcontext, GLfloat, GLfloat, const GLint)` |
| 65 | 276 | `glScaled` | `void (GLcontext, GLdouble, GLdouble, GLdouble)` |
| 66 | 280 | `glScalef` | `void (GLcontext, GLfloat, GLfloat, GLfloat)` |
| 67 | 284 | `glScissor` | `void (GLcontext, GLint, GLint, GLsizei, GLsizei)` |
| 68 | 288 | `glShadeModel` | `void (GLcontext, GLenum)` |
| 69 | 292 | `glTexCoord2f` | `void (GLcontext, GLfloat, GLfloat)` |
| 70 | 296 | `glTexCoord2fv` | `void (GLcontext, GLfloat*)` |
| 71 | 300 | `glTexCoord4f` | `void (GLcontext, GLfloat, GLfloat, GLfloat, GLfloat)` |
| 72 | 304 | `glTexCoord4fv` | `void (GLcontext, GLfloat*)` |
| 73 | 308 | `glTexCoordPointer` | `void (GLcontext, GLint, GLenum, GLsizei, const GLvoid*)` |
| 74 | 312 | `glTexEnvi` | `void (GLcontext, GLenum, GLenum, GLint)` |
| 75 | 316 | `glTexGeni` | `void (GLcontext, GLenum, GLenum, GLenum)` |
| 76 | 320 | `glTexImage2D` | `void (GLcontext, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*)` |
| 77 | 324 | `glTexParameteri` | `void (GLcontext, GLenum, GLenum, GLint)` |
| 78 | 328 | `glTexSubImage2D` | `void (GLcontext, GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*)` |
| 79 | 332 | `glTranslated` | `void (GLcontext, GLdouble, GLdouble, GLdouble)` |
| 80 | 336 | `glTranslatef` | `void (GLcontext, GLfloat, GLfloat, GLfloat)` |
| 81 | 340 | `gluLookAt` | `void (GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat, GLfloat)` |
| 82 | 344 | `gluPerspective` | `void (GLfloat, GLfloat, GLfloat, GLfloat)` |
| 83 | 348 | `glUnlockArrays` | `void (GLcontext)` |
| 84 | 352 | `glVertex2fv` | `void (GLcontext, GLfloat*)` |
| 85 | 356 | `glVertex3fv` | `void (GLcontext, GLfloat*)` |
| 86 | 360 | `glVertex4f` | `void (GLcontext, GLfloat, GLfloat, GLfloat, GLfloat)` |
| 87 | 364 | `glVertex4fv` | `void (GLcontext, GLfloat*)` |
| 88 | 368 | `glVertexPointer` | `void (GLcontext, GLint, GLenum, GLsizei, const GLvoid*)` |
| 89 | 372 | `glViewport` | `void (GLcontext, GLint, GLint, GLsizei, GLsizei)` |
| 90 | 376 | `mglClearPointer` | `void (GLcontext)` |
| 91 | 380 | `mglCreateContext` | `void * (int, int, int, int)` |
| 92 | 384 | `mglCreateContextFromID` | `void * (GLint, GLint*, GLint*)` |
| 93 | 388 | `mglDeleteContext` | `void (GLcontext)` |
| 94 | 392 | `mglDrawMultitexBuffer` | `void (GLcontext, GLenum, GLenum, GLenum)` |
| 95 | 396 | `mglEnableSync` | `void (GLcontext, GLboolean)` |
| 96 | 400 | `mglExit` | `void (GLcontext)` |
| 97 | 404 | `mglGetInputWindowHandle` | `void * (GLcontext)` |
| 98 | 408 | `mglGetWindowHandle` | `void * (GLcontext)` |
| 99 | 412 | `mglIdleFunc` | `void (GLcontext, IdleFn)` |
| 100 | 416 | `mglKeyFunc` | `void (GLcontext, KeyHandlerFn)` |
| 101 | 420 | `mglLockBack` | `GLboolean (GLcontext, MGLLockInfo*)` |
| 102 | 424 | `mglLockDisplay` | `GLboolean (GLcontext)` |
| 103 | 428 | `mglLockMode` | `void (GLcontext, GLenum)` |
| 104 | 432 | `mglMainLoop` | `void (GLcontext)` |
| 105 | 436 | `mglMinTriArea` | `void (GLcontext, GLfloat)` |
| 106 | 440 | `mglMouseFunc` | `void (GLcontext, MouseHandlerFn)` |
| 107 | 444 | `mglPrintMatrix` | `void (GLcontext, int)` |
| 108 | 448 | `mglPrintMatrixStack` | `void (GLcontext, int)` |
| 109 | 452 | `mglResizeContext` | `GLboolean (GLcontext, GLsizei, GLsizei)` |
| 110 | 456 | `mglSetPointer` | `void (GLcontext)` |
| 111 | 460 | `mglSetState` | `void (GLcontext, GLenum, GLboolean)` |
| 112 | 464 | `mglSetZOffset` | `void (GLcontext, GLfloat)` |
| 113 | 468 | `mglSpecialFunc` | `void (GLcontext, SpecialHandlerFn)` |
| 114 | 472 | `mglSwitchDisplay` | `void (GLcontext)` |
| 115 | 476 | `mglTexMemStat` | `void (GLcontext, GLint*, GLint*)` |
| 116 | 480 | `mglUnlockDisplay` | `void (GLcontext)` |
| 117 | 484 | `mglWriteShotPPM` | `void (GLcontext, char*)` |
| 118 | 488 | `mglChooseGuardBand` | `void (GLboolean)` |
| 119 | 492 | `mglChooseMtexBufferSize` | `void (int)` |
| 120 | 496 | `mglChooseNumberOfBuffers` | `void (int)` |
| 121 | 500 | `mglChoosePixelDepth` | `void (int)` |
| 122 | 504 | `mglChooseTextureBufferSize` | `void (int)` |
| 123 | 508 | `mglChooseVertexBufferSize` | `void (int)` |
| 124 | 512 | `mglChooseWindowMode` | `void (GLboolean)` |
| 125 | 516 | `mglGetSupportedScreenModes` | `GLint (MGLScreenModeCallback)` |
| 126 | 520 | `mglProhibitAlphaFallback` | `void (GLboolean)` |
| 127 | 524 | `mglProhibitMipMapping` | `void (GLboolean)` |
| 128 | 528 | `mglProposeCloseDesktop` | `void (GLboolean)` |
| 129 | 532 | `glPolygonOffset` | `void (GLcontext, GLfloat, GLfloat)` |
| 130 | 536 | `glClientActiveTextureARB` | `void (GLcontext, GLenum)` |
| 131 | 540 | `glInterleavedArrays` | `void (GLcontext, GLenum, GLsizei, const GLvoid*)` |
| 132 | 544 | `glMultiDrawArrays` | `void (GLcontext, GLenum, const GLint*, const GLsizei*, GLsizei)` |
| 133 | 548 | `glBlendEquation` | `void (GLcontext, GLenum)` |
| 134 | 552 | `glBlendFuncSeparate` | `void (GLcontext, GLenum, GLenum, GLenum, GLenum)` |
| 135 | 556 | `glIsTexture` | `GLboolean (GLcontext, GLuint)` |
| 136 | 560 | `mglChooseZBufferDepth` | `void (int)` |
| 137 | 564 | `glLineWidth` | `void (GLcontext, GLfloat)` |
| 138 | 568 | `glTexGenfv` | `void (GLcontext, GLenum, GLenum, const GLfloat*)` |
| 139 | 572 | `glCopyTexImage2D` | `void (GLcontext, GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)` |
| 140 | 576 | `glCopyTexSubImage2D` | `void (GLcontext, GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)` |
| 141 | 580 | `glAreTexturesResident` | `GLboolean (GLcontext, GLsizei, const GLuint*, GLboolean*)` |
| 142 | 584 | `glEdgeFlag` | `void (GLcontext, GLboolean)` |
| 143 | 588 | `glEdgeFlagPointer` | `void (GLcontext, GLsizei, const GLvoid*)` |
| 144 | 592 | `glEdgeFlagv` | `void (GLcontext, const GLboolean*)` |
| 145 | 596 | `glGetDoublev` | `void (GLcontext, GLenum, GLdouble*)` |
| 146 | 600 | `glGetPointerv` | `void (GLcontext, GLenum, GLvoid**)` |
| 147 | 604 | `glIndexi` | `void (GLcontext, GLint)` |
| 148 | 608 | `glIndexiv` | `void (GLcontext, const GLint*)` |
| 149 | 612 | `glIndexPointer` | `void (GLcontext, GLenum, GLsizei, const GLvoid*)` |
| 150 | 616 | `glPrioritizeTextures` | `void (GLcontext, GLsizei, const GLuint*, const GLclampf*)` |
| 151 | 620 | `glReadBuffer` | `void (GLcontext, GLenum)` |
| 152 | 624 | `mglCreateContextFromWindow` | `void * (struct Window*)` |
| 153 | 628 | `mglCreateContextFromBitMap` | `void * (struct BitMap*)` |
| 154 | 632 | `glLightf` | `void (GLcontext, GLenum, GLenum, GLfloat)` |
| 155 | 636 | `glLightfv` | `void (GLcontext, GLenum, GLenum, const GLfloat*)` |
| 156 | 640 | `glMaterialf` | `void (GLcontext, GLenum, GLenum, GLfloat)` |
| 157 | 644 | `glMaterialfv` | `void (GLcontext, GLenum, GLenum, const GLfloat*)` |
| 158 | 648 | `glLightModelf` | `void (GLcontext, GLenum, GLfloat)` |
| 159 | 652 | `glLightModelfv` | `void (GLcontext, GLenum, const GLfloat*)` |
| 160 | 656 | `glGetLightfv` | `void (GLcontext, GLenum, GLenum, GLfloat*)` |
| 161 | 660 | `glGetMaterialfv` | `void (GLcontext, GLenum, GLenum, GLfloat*)` |
| 162 | 664 | `glGenLists` | `GLuint (GLcontext, GLsizei)` |
| 163 | 668 | `glDeleteLists` | `void (GLcontext, GLuint, GLsizei)` |
| 164 | 672 | `glIsList` | `GLboolean (GLcontext, GLuint)` |
| 165 | 676 | `glNewList` | `void (GLcontext, GLuint, GLenum)` |
| 166 | 680 | `glEndList` | `void (GLcontext)` |
| 167 | 684 | `glCallList` | `void (GLcontext, GLuint)` |
| 168 | 688 | `gluBuild2DMipmaps` | `GLint (GLcontext, GLenum, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*)` |
| 169 | 692 | `gluErrorString` | `const GLubyte * (GLenum)` |
| 170 | 696 | `gluNewQuadric` | `GLUquadricObj * (void)` |
| 171 | 700 | `gluDeleteQuadric` | `void (GLUquadricObj*)` |
| 172 | 704 | `gluQuadricNormals` | `void (GLUquadricObj*, GLenum)` |
| 173 | 708 | `gluQuadricTexture` | `void (GLUquadricObj*, GLboolean)` |
| 174 | 712 | `gluQuadricDrawStyle` | `void (GLUquadricObj*, GLenum)` |
| 175 | 716 | `gluQuadricOrientation` | `void (GLUquadricObj*, GLenum)` |
| 176 | 720 | `gluCylinder` | `void (GLUquadricObj*, GLdouble, GLdouble, GLdouble, GLint, GLint)` |
| 177 | 724 | `gluSphere` | `void (GLUquadricObj*, GLdouble, GLint, GLint)` |
| 178 | 728 | `gluDisk` | `void (GLUquadricObj*, GLdouble, GLdouble, GLint, GLint)` |
| 179 | 732 | `glutInit` | `void (int*, char**)` |
| 180 | 736 | `glutInitDisplayMode` | `void (unsigned int)` |
| 181 | 740 | `glutInitWindowSize` | `void (int, int)` |
| 182 | 744 | `glutInitWindowPosition` | `void (int, int)` |
| 183 | 748 | `glutCreateWindow` | `int (const char*)` |
| 184 | 752 | `glutMainLoop` | `void (void)` |
| 185 | 756 | `glutDisplayFunc` | `void (void (*)(void))` |
| 186 | 760 | `glutIdleFunc` | `void (void (*)(void))` |
| 187 | 764 | `glutKeyboardFunc` | `void (void (*)(unsigned char key, int x, int y))` |
| 188 | 768 | `glutReshapeFunc` | `void (void (*)(int width, int height))` |
| 189 | 772 | `glutSwapBuffers` | `void (void)` |
| 190 | 776 | `glutPostRedisplay` | `void (void)` |
| 191 | 780 | `glutGet` | `int (GLenum)` |
| 192 | 784 | `glutGameModeString` | `void (const char*)` |
| 193 | 788 | `glutEnterGameMode` | `int (void)` |
| 194 | 792 | `glutLeaveGameMode` | `void (void)` |
| 195 | 796 | `glutGameModeGet` | `int (GLenum)` |
| 196 | 800 | `glutSolidCube` | `void (GLdouble)` |
| 197 | 804 | `glutSolidSphere` | `void (GLdouble, GLint, GLint)` |
| 198 | 808 | `glutSolidCone` | `void (GLdouble, GLdouble, GLint, GLint)` |
| 199 | 812 | `glutSolidTorus` | `void (GLdouble, GLdouble, GLint, GLint)` |
| 200 | 816 | `glutSolidDodecahedron` | `void (void)` |
| 201 | 820 | `glNormalPointer` | `void (GLcontext, GLenum, GLsizei, const GLvoid*)` |
| 202 | 824 | `gluQuadricCallback` | `void (GLUquadricObj*, GLenum, MGLUfuncptr)` |
| 203 | 828 | `glColorMaterial` | `void (GLcontext, GLenum, GLenum)` |
| 204 | 832 | `glTexEnvfv` | `void (GLcontext, GLenum, GLenum, const GLfloat*)` |
| 205 | 836 | `glNormal3fv` | `void (GLcontext, GLfloat*)` |

205 entries. An implementation built from this list should be checked against it mechanically (a host test that compares each entry's offset and the table size with this section).

### 8.1 Calls that have no entry of their own

A client's headers turn these into calls of other entries. The library never sees them as such, which matters for the argument values it receives:

| The program writes | The library receives |
|---|---|
| `glEnable(cap)`, `glDisable(cap)` | entry 111 with `GL_TRUE` or `GL_FALSE` |
| `glColor3f(r,g,b)` | entry 12 with alpha 1.0 |
| `glColor3ub(r,g,b)` | entry 14 with alpha 255 |
| `glVertex2f(x,y)`, `glVertex3f(x,y,z)` | entry 86 with z = 0 and/or w = 1 |
| `glTexEnvf`, `glTexEnviv` | entry 74, the float **converted to an integer** (or the first array element) |
| `glTexParameterf` | entry 77, the float **converted to an integer** |
| `glFogi` | entry 32, the integer **converted to a float**. So `GL_FOG_MODE` arrives as, for example, 9729.0 for `GL_LINEAR`, and entry 32 must accept an enum carried as a float. |
| `glColorTableEXT` | entry 18 |
| `glNormal3fv(v)` (29.0 headers) | entry 53 with the three components. With 29.1+ headers it is entry 205. |
| `mglCreateContext...`, `mglDeleteContext()` | see 4.3 |
| `gluLookAt`, `gluPerspective` | entries 81, 82, only when there is a current context |

Calls in a game's own code that are not in this table (for example `glStencilFunc`, `glClipPlane`, `glTexParameterfv`) are not reachable by a client at all: the RTCW port defines them away. A library that offers more GL than MiniGL changes nothing for existing clients, except through `glGetString`/`glGetIntegerv`/`glIsEnabled` answers (9.1).

## 9. Behaviour

### 9.1 The `gl*` entries

Unless this section says otherwise, each `gl*` entry does what OpenGL 1.1 says, with these extensions: `ARB_multitexture` (two units), `EXT_compiled_vertex_array` (lock/unlock), blend equation and separate blend factors (`EXT_blend_minmax`/`subtract`/`func_separate` semantics), `glMultiDrawArrays`, paletted textures with a shared palette (`EXT_paletted_texture`, `EXT_shared_texture_palette`), and the `GL_ADD` texture function (`EXT_texture_env_add`). Errors are recorded and kept until `glGetError` reads them, as GL says.

What clients are known to depend on, or what the published documentation says differs from GL 1.1. Items marked **(black box)** are not certain and have a test in section 11.

**Strings and limits**
- `glGetString(GL_VERSION)` returns exactly `"1.1"`.
- `glGetString(GL_EXTENSIONS)` must contain, as substrings: `GL_ARB_multitexture`, `GL_EXT_compiled_vertex_array`, `GL_EXT_paletted_texture`, `GL_EXT_shared_texture_palette`, `GL_EXT_color_table`. The published library writes the first as `GL_MGL_ARB_multitexture`, so that a `strstr` test finds it while an exact-word test does not. Follow that by default (**black box**: whether a game behaves differently with the plain name). Add `GL_EXT_texture_env_add` only when `GL_ADD` is implemented. RTCW reads the string but decides multitexture from `glGetIntegerv`.
- `glGetString(GL_VENDOR)`/`GL_RENDERER`: free text. Use our own name. Quake-family engines print it.
- `glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB)` returns 2. RTCW enables multitexture on this answer.
- `glGetIntegerv(GL_MAX_TEXTURE_SIZE)`: the published library answers 1024. We may answer more, but some engines size their textures from it, which changes memory use and upload time. Answer 1024 by default, with a setting to raise it.
- Integer queries of colours: the published library rounds 0..1 to 0 or 1 instead of scaling to the integer range. GL's scaling is the safer choice (**black box**: whether a game reads colours as integers at all).

**Capabilities (`glEnable`, entry 111)**
- An unknown capability records `GL_INVALID_ENUM` and changes nothing.
- MiniGL refuses, with `GL_INVALID_ENUM`, several legal GL 1.1 capabilities (stencil test, clip planes, normalize, smooth lines and polygons, 1D textures, auto normal). We may support them, because supporting more is invisible to a client that never asks. But a client that does ask and then checks `glGetError` sees a difference (**black box**).
- Perspective mapping (0x7015) and shared texture palette: state only.
- MiniGL's Z offset (0x7017) enables the `mglSetZOffset` offset. If both it and `GL_POLYGON_OFFSET_FILL` are on, the polygon-offset values win.
- `GL_TEXTURE_2D` applies to the active (server) texture unit.
- **Little-endian arrays (0x701D).** On by default. A program that disables it promises that its vertex arrays already hold **little-endian** floats and R,G,B,A bytes, and that the arrays stay unchanged until the frame has been drawn (they must be locked). That is a PiStorm3D-only fast path: the published library reads such arrays with no conversion. We must either honour the promise (read those arrays as little-endian) or ignore the switch, which would turn the geometry into garbage. Honour it. No known game uses it (**black box**).

**Primitives and arrays**
- All ten GL 1.1 primitive types. 0x7013/0x7014 are refused (7.2).
- `glDrawElements` index types: unsigned byte, unsigned short, unsigned int.
- Vertex pointer: 2, 3 or 4 components, float (also short, int and double, per GL). Colour pointer: 3 or 4 components, unsigned byte or float, and MiniGL's two byte orders (7.2, size 4). Texture coordinates per client-active unit. Normals (entry 201).
- `glInterleavedArrays`: all 14 GL 1.1 formats.
- `glLockArrays(first, count)`/`glUnlockArrays`: between the two, the program promises the locked range of the enabled arrays does not change, so the library may convert it once and reuse it for every draw until unlock. RTCW locks once per surface batch and draws with `glDrawElements` (80-110 draws, 25,000-31,000 indices a frame in its first level).
- Mode switching inside `glBegin`/`glEnd` and errors follow GL 1.1.
- The vertex-buffer size from `mglChooseVertexBufferSize` must never limit correctness. The published library silently truncates a `glBegin` block longer than the buffer. We must not truncate.

**Textures**
- Target `GL_TEXTURE_2D` only (no 1D, no proxy). Border must be 0, otherwise `GL_INVALID_VALUE`.
- Source formats: `GL_RGBA`, `GL_RGB`, `GL_LUMINANCE_ALPHA`, `GL_LUMINANCE`, `GL_ALPHA`, `GL_COLOR_INDEX`, and the GL 1.0 component counts 3 and 4 used as a *format*. Types: unsigned byte, and MiniGL's two packed 16-bit types (7.2). Accepting GL 1.1's other types too is harmless.
- `internalformat`: accept GL 1.1's names, the sized formats, and the GL 1.0 counts 1-4 (Quake-family engines pass 3 and 4). Whether the result has alpha follows GL's base-format rules.
- Mipmaps: the program supplies every level. A level above 0 must have the size the base implies (`GL_INVALID_VALUE` otherwise). Nothing is generated except by `gluBuild2DMipmaps`. `mglProhibitMipMapping` changes nothing.
- `glTexSubImage2D` between two draws of one frame must affect only the later draw (GL's order). This is what lightmap updates rely on.
- `glCopyTexImage2D`/`glCopyTexSubImage2D` copy from the colour buffer as drawn so far in the current frame.
- `glColorTable`: a palette of up to 256 entries applied at upload time to `GL_COLOR_INDEX` textures. Changing the palette after upload does not change textures already uploaded (the published library behaves this way; GL's paletted textures would recolour them). Matching MiniGL is the safe choice.
- `glAreTexturesResident` writes 4-byte booleans (section 6) and returns `GL_TRUE`.

**Texture environment**
- Per unit: `GL_MODULATE`, `GL_REPLACE`, `GL_DECAL`, `GL_BLEND` (with the environment colour from entry 204) and `GL_ADD`.
- Unit 1 combines with the result of unit 0, as in `ARB_multitexture`.

**Fragments**
- Depth test, the eight functions and the write mask. Alpha test. All fifteen blend factors. Colour mask. Scissor.
- `glPolygonOffset(factor, units)`: GL's meaning. `mglSetZOffset` (entry 112) is a separate, older offset in normalised depth (9.2).
- `glBlendEquation`: add, subtract, reverse subtract, min, max. (Our existing library draws all but adding as adding; real support is harmless.)
- Fog: linear, exp and exp2. The published library evaluates fog per vertex. Per-fragment fog is allowed by GL, and the difference is visible only in large polygons (**black box**: compare a fogged Quake level with the published library's screenshots).
- Lighting: 8 lights, materials, colour material (entry 203), light model ambient, local viewer. Two-sided lighting is optional (MiniGL has none).
- `GL_DITHER` is on by default and may be ignored.

**Matrices**
- Modelview stack depth at least 32, projection and texture at least 2 (GL's minimums; our existing library has 32, 8 and 8 per unit).
- `glRotatefEXT(angle, axis)` and `glRotatefEXTs(sine, cosine, axis)` rotate about one coordinate axis. Axis 0, 1 or 2 selects x, y or z (**black box**: confirm the numbering with a test program that draws an arrow after each). The angle is in degrees, as in `glRotatef`. The second form gives the sine and cosine directly.

**Reading back**
- `glReadPixels`: `GL_RGBA`/`GL_RGB` unsigned bytes from the colour buffer and the depth component, with the frame drawn so far. GL's origin is the bottom-left corner.
- `glFinish` returns when everything drawn so far is complete. `glFlush` may do nothing visible.

### 9.2 The `mgl*` entries

The behaviour below is what the published AutoDoc documents, restated in our words. Where we deliberately differ, the row says "**AmiCradle**".

| Entry | Must do |
|---|---|
| `mglCreateContext(x, y, w, h)` | Open a display w × h and make a context for it. Fullscreen (a screen of its own, 32-bit) unless window mode was chosen; then a Workbench window with a drag bar and depth gadget, no close or size gadget. x, y become the initial viewport origin, and the viewport is (x, y, w, h). Depth range 0..1. **Clear colour after creation: opaque white** (MiniGL's documented default, unlike GL's black; **black box** whether any game relies on it). Fullscreen asks for an exact w × h mode; NULL if there is none (**AmiCradle**: fall back to the nearest larger mode and centre, since ACRTG offers any size). The display's pointer is blanked. NULL on failure. |
| `mglCreateContextFromWindow(win)` | Render into the inner area of a window the program owns. It is never closed by the library. NULL if the inner area is empty. Window mode is ignored. |
| `mglCreateContextFromBitMap(bm)` | Render into a 32-bit bitmap the program owns. `mglSwitchDisplay` finishes the frame and copies it into the bitmap but shows nothing. No window: the window getters return NULL and the main loop returns at once. |
| `mglCreateContextFromID(id, &w, &h)` | Return NULL and write 0 into w and h when their pointers are not NULL (the published library has no mode IDs). **AmiCradle** may implement it properly for ids from entry 125, but NULL is the compatible answer. |
| `mglDeleteContext(ctx)` | Free everything the context owns, close what the library opened (not what the program gave it), clear the current-context variable if it holds ctx, and reset every `mglChoose*`/`mglProhibit*` request to its default. NULL is ignored. |
| `mglResizeContext(ctx, w, h)` | Fullscreen: reopen the display at the new size, keeping textures and GL state, and set the viewport and depth range again (not the clear colour). `GL_FALSE` if the size could not be opened (the context then has no display until a later resize succeeds). w or h ≤ 0: `GL_INVALID_VALUE`, no change. Windowed and bitmap contexts: do nothing and return `GL_TRUE`. |
| `mglSwitchDisplay(ctx)` | End the frame and show it: everything drawn since the last call becomes visible. Window: copied into the window. Bitmap: written into the bitmap. Fullscreen: shown on the screen. Waits for the vertical blank first only if sync is on (below) or more than one buffer was asked for. Must release any display lock the program took with `mglLockBack`. |
| `mglEnableSync(ctx, on)` | Off by default. On: `mglSwitchDisplay` waits for the beam to leave the visible area (fullscreen only). |
| `mglLockMode(ctx, mode)` | Accept the three modes (7.2). Manual is the default. They change nothing visible: AmiCradle never holds a display lock between frames, so no mode has to bracket anything (and RTCW calls it with "automatic" right after creating the context). |
| `mglLockDisplay(ctx)` | Set a flag and return `GL_TRUE`. Rendering never needs it. |
| `mglUnlockDisplay(ctx)` | Release a lock taken by `mglLockBack` (if any) and clear the flag. Always safe. |
| `mglLockBack(ctx, info)` | Finish drawing, then lock the drawing buffer and fill `info` (24 bytes, section 6): width, height, depth 32, pixel format 0 (meaning 8-bit R,G,B,A), the address and the bytes per row. `GL_FALSE` if a lock is already held, with the address NULL and the bytes per row 0 in `info`. `info` may be NULL. The program may read and write the pixels until `mglUnlockDisplay`. |
| `mglGetWindowHandle(ctx)`, `mglGetInputWindowHandle(ctx)` | Both return the window that receives the context's input: the program's or the library's window, and for fullscreen a borderless backdrop window opened on the screen for that purpose. NULL for a bitmap context. No IDCMP flags are set on it; the program adds its own. RTCW takes this window and reads its keyboard and mouse itself. |
| `mglMainLoop(ctx)` | Add vanilla-key, raw-key, mouse-move and mouse-button IDCMP to the input window and loop until `mglExit`. Each pass: dispatch every waiting message (key function for vanilla keys; special-key function for presses of F1-F10 and the cursor keys; mouse function for moves and button changes, with x, y in GL window coordinates, y up from the bottom, and the button mask from the message's qualifier), then call the idle function. Without an idle function, sleep on the window's port. Return at once if there is no input window. |
| `mglExit(ctx)` | Make the main loop return after its current pass. |
| `mglIdleFunc`, `mglKeyFunc`, `mglMouseFunc`, `mglSpecialFunc` | Set the handler. NULL removes it. |
| `mglSetPointer(ctx)` / `mglClearPointer(ctx)` | Blank the pointer over the input window / restore it. Must be safe to call at any time while a context exists (with MiniGL, calling it froze the machine because of a held lock; we hold none). |
| `mglSetState(ctx, cap, on)` | The function behind `glEnable`/`glDisable` (9.1). |
| `mglSetZOffset(ctx, d)` | Set the old-style depth offset used while capability 0x7017 is on. d is a delta in normalised depth, about −0.0009 in practice, meaning "nudge this coplanar decal in front". The published library maps it to a fixed polygon-offset of about −2 units rather than a literal depth delta. Do the same (**black box**: decals in a Quake port with and without it). |
| `mglMinTriArea(ctx, a)` | Store. It may be ignored (the published library ignores it). |
| `mglDrawMultitexBuffer(ctx, src, dst, env)` | Set the blend function (src, dst) and unit 1's texture environment (`GL_MODULATE`, `GL_DECAL` or `GL_REPLACE`; anything else: `GL_INVALID_ENUM`, nothing changed) for the drawing that **follows**. Draws nothing. (Classic MiniGL drew a buffered second pass here, after the geometry. ABI 5 clients call it before.) |
| `mglTexMemStat(ctx, &cur, &peak)` | Write two byte counts. Either pointer may be NULL. The values are informative only. |
| `mglWriteShotPPM(ctx, name)` | Write what is on show (the last finished frame, the drawable only) to a binary P6 PPM file. No error is reported. |
| `mglPrintMatrix`, `mglPrintMatrixStack` | Debug output. May do nothing. |
| `mglChooseWindowMode(on)` | Default off (fullscreen). Read by `mglCreateContext` only. |
| `mglChooseNumberOfBuffers(n)` | Default 1. 2 or 3: the frame waits for the vertical blank. Windowed and bitmap contexts are always single. |
| `mglChoosePixelDepth(bits)` | Default 24. Accepted and ignored: the context always renders 32-bit colour (RTCW passes its colour depth, 16). |
| `mglChooseZBufferDepth(bits)` | 16, or anything else meaning 32 (the default). No stencil either way. |
| `mglChooseVertexBufferSize(n)` | Default 1024. A hint only (9.1). |
| `mglChooseMtexBufferSize(n)` | Default 4096. A hint only. |
| `mglChooseTextureBufferSize(n)` | Default 4096: the number of texture names. Names at or above it are refused by bind, delete and is-texture. Values below 1 or above 1,048,576 are ignored. |
| `mglChooseGuardBand(on)`, `mglProhibitAlphaFallback(on)` | Defaults off. Stored, no effect. |
| `mglProhibitMipMapping(on)` | Default on. Stored, no effect. |
| `mglProposeCloseDesktop(on)` | Default off. Fullscreen only: close the Workbench while the context is open if possible, and reopen it after. Consumed when the display opens. |
| `mglGetSupportedScreenModes(cb)` | The published library returns "best" (0xFFFFFFFF) without calling cb. **AmiCradle** may list real modes, calling cb with each until it returns `GL_TRUE`, and return that mode's id. The compatible answer is "best". |

### 9.3 GLU and GLUT entries

- GLU: GLU 1.3 behaviour for the matrix helpers, `gluBuild2DMipmaps` (any size: scale to a power of two, then build every level), `gluErrorString` (GL and GLU codes) and the quadrics (sphere, cylinder, disk, in every draw style, with normals and texture coordinates, the error callback through entry 202).
- GLUT: GLUT 3.7 behaviour for a single window or game mode. `glutInit` must work with the client's argc/argv. `glutCreateWindow` makes the context (the client then has no current-context write of its own, so the library sets the variable). `glutSwapBuffers` is `mglSwitchDisplay`. `glutGet` answers window size and position, the init values and the elapsed time in ms. Game mode is fullscreen at the size from the game-mode string.

## 10. What is unknown

| # | Unknown | How to find out |
|---|---|---|
| U1 | Exactly how the SDK's import library calls -30 (registers read besides D0) | B1 |
| U2 | Whether any client reads the back-end flags | B2 |
| U3 | Whether any ABI 5 client touches the inside of a context | B3 |
| U4 | Whether game builds exist that were compiled with 30.0 headers (asking for version 30), or which version each binary asks for | Read the version argument from each game binary's `OpenLibrary` call (break on `OpenLibrary` in a lab with the Cradle's debugger) |
| U5 | Axis numbering of `glRotatefEXT`/`glRotatefEXTs` | B6 |
| U6 | Which games disable the little-endian-arrays capability (0x701D) | B7 (log every `glEnable`/`glDisable` cap a game uses) |
| U7 | Whether games depend on MiniGL's refusals (stencil, clip planes) being errors | B7 |
| U8 | Whether games depend on the white default clear colour, per-vertex fog, or the 1024 texture limit | B8: run each game against the published screenshots/videos |
| U9 | The exact mapping of `mglSetZOffset`'s delta | B9 |
| U10 | Whether a `GL_EXTENSIONS` string with plain `GL_ARB_multitexture` changes any game's path | B7 with both strings |

## 11. Black-box tests

All run in a lab copy of an instance (never on Dale's own instances or the PiStorm card). Each needs only our library and the published binaries.

- **B1, the -30 call.** A trace build of our library logs D0/D1/A0/A1 at -30 and after `MiniGLOpen()` returns. Run the RTCW port, a gcc-built and a vbcc-built test client.
- **B2, back-end flags.** Run every test client with the field at 0, at bit 16, and at each SDK bit. Any difference means a client reads it.
- **B3, opaque context.** Our library allocates the context with a guard pattern in every byte after the first longword and checks it at each entry for a run of RTCW's first level.
- **B4, calling convention.** One test client in three builds (5).
- **B5, token 0x7001.** Log whether any client passes it, and to which entry.
- **B6, `glRotatefEXT`.** A test client draws an arrow after `glRotatefEXT(90, k)` for k = 0, 1, 2; compare with `glRotatef`.
- **B7, call census.** A trace build counts every entry, every capability, every `glHint`/`glTexEnv`/`glTexParameter` value and every `glGetString`/`glGetIntegerv` query, per game. This drives which corners get implemented first.
- **B8, picture comparison.** Fixed camera positions in RTCW, Quake 2 and Heretic II against reference pictures (from the PiStorm3D release's own published screenshots where they exist, otherwise from our softpipe/Mesa route).
- **B9, decal offset.** Quake 1/2 decals and RTCW's wall marks with the Z-offset capability on, swept over the mapping constant.

## 12. Licence notes for Dale (not legal advice)

1. **Code.** PiStorm3D and MiniGL 1.2 are under the Hyperion MiniGL licence (AmigaOS only, modifications must be published). Nothing of theirs may be in AmiCradle. This spec copies no code, header text or comment. The implementer must work from this spec, the OpenGL 1.1 specification and our own code, and must not read PiStorm3D/MiniGL/ogles2/WarpMiniGL sources.
2. **The interface.** Binary compatibility needs the table order, the types, the ABI number, the sizes and the constant values. These are interface facts. Courts have generally treated independent re-implementation of an interface as lawful: *Google LLC v. Oracle America* (US Supreme Court, 2021) held that copying an API's declarations for a new implementation was fair use, and *SAS Institute v. World Programming* (CJEU C-406/10, 2012) held that a program's functionality, programming language and data-file formats are not protected by copyright as such. That is not a guarantee in every country, and this is not legal advice.
3. **The constants.** The `GL_*` values are OpenGL's and come from Khronos's headers (MIT-style). Only the MiniGL-specific values in 7.2, the type sizes and the table layout come from the MiniGL SDK.
4. **The name.** `minigl.library` must be used, because clients pass it to `OpenLibrary`. "MiniGL" may be associated with Hyperion. Describe our library as "minigl.library-compatible" (for example "AmiCradle GL 1.1 for minigl.library programs"), not as MiniGL, and use no Hyperion or PiStorm3D names or logos in its version string, documents or `GL_VENDOR`.
5. **The existing GFOpenRTG minigl.library.** Its README says the 29.1 SDK and its source repository were references "for the interface only". By the stricter rule of this task (implementers read no MiniGL source at all), Dale may want that history reviewed too, since IDEA 5 builds on that library.
6. **Recommendation.** Dale decides whether to get a lawyer's opinion before shipping, with the Hyperion name as the main point to ask about.
