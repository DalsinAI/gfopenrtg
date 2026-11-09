# Provenance of minigl.library's interface

## What was rebuilt clean-room, 9 November 2026

On Dale's decision of 9 November 2026, the interface layer of this library
was rebuilt clean-room. The earlier interface files (version 29) had been
written with the minigl.library 29.1 SDK and its source repository as
interface references; all of them were deleted without being read again and
written new. The person who wrote the new files did not read, download or
search for any MiniGL, PiStorm3D, ogles2 or WarpMiniGL header, document or
source, nor the deleted files.

Written new, from the documents below only:

| File | What it is |
|---|---|
| `include/mgl/mgltypes.h` | The types (GLboolean is four bytes), MGLLockInfo, MGLScreenMode, MGLspecial, the callback types |
| `include/mgl/mgltokens.h` | MiniGL's own tokens (spec 7.2), GLU 1.3's and GLUT 3.7's |
| `include/mgl/gltokens.h` | OpenGL's tokens, made by `gen_api.py --tokens` from the Khronos headers (Mesa's `GL/gl.h` and `GL/glext.h`, MIT) |
| `include/mgl/gl.h`, `glu.h`, `glut.h`, `include/proto/minigl.h`, `include/clib/minigl_protos.h`, `include/libraries/minigl.h` | The public headers |
| `include/libraries/minigl_dispatch.h` | The dispatch table: 5 longwords, then 205 entries in the spec's order, 840 bytes (made by `gen_api.py`) |
| `include/mgl/minigl_calls.h` | The calls a program makes through the table, the current-context handling of spec 4.3 and the client-side rewrites of spec 8.1 (glFogi as a float, glTexParameterf and glTexEnvf as integers, glEnable/glDisable as mglSetState, ...) (made by `gen_api.py`) |
| `library/minigl/gen_api.py` | The generator: reads the spec's section 8 table and nothing else for the table |
| `library/minigl/mgl_table.c`, `mgl_entries.h`, `mgl_static.c` | The table the library hands out (entries that take a context ignore a NULL one), the prototypes behind it, libmgl.a (made by `gen_api.py`) |
| `library/minigl/mgl_lib.c` | The library: the -30 vector answering the table in D0 and A0, the current-context variable, version 30, preferences, the mglChoose*/mglProhibit* defaults at each open |
| `library/minigl/mgl_choose.c` | The mgl* calls that need no display |
| `library/minigl/client/minigl_open.c` | libminigl.a: MiniGLOpen (the -30 call and the ABI checks: version equal to 5, size at least 840) and MiniGLClose |
| `tests/test_minigl_abi.c` | The ABI test: reads the spec's table itself and checks every offset and the size |

Kept from the version 29 library, the Team's own MIT code (the GL state
machine, matrices, vertices and arrays, the pipeline, textures, the batch,
display lists, GLU, GLUT, the display): `mgl_state.c`, `mgl_matrix.c`,
`mgl_vertex.c`, `mgl_pipe.c`, `mgl_texture.c`, `mgl_out.c`, `mgl_list.c`,
`mgl_glu.c`, `mgl_glut.c`, `mgl_display.c`, `mgl_math.c`, `mgl_internal.h`.
They were read for reuse and changed where the spec says otherwise (the mgl*
behaviour of spec 9.2, the strings and limits of 9.1, MiniGL's capabilities).
In them, these looked as if they came from the SDK and were rewritten:

- `glRotatefEXT`'s axis was decoded as a bit mask of named constants from
  the old header; it is now axis 0, 1 or 2 as the spec says (black-box test
  B6 still to confirm).
- MGLLockInfo carried Warp3D's pixel-format numbers; it now carries the
  spec's layout (depth 32, format 0 = bytes R, G, B, A).
- Parameter and field names that looked like the old header's (`lockMode`,
  `BSrc`/`BDst`/`TexEnv`, `CallbackFn`, `closeme`, `Current`/`Peak`,
  `sin_an`/`cos_an`/`xyz`, `bit_depth`/`mode_name`, `MGLKEY_*`, `MGL_SM_*`,
  `MGL_BUTTON_*`, the `LIB_mgl*` function names) were renamed.
- The `GL_MGL_ARGB` extension name, which the spec does not list, was
  dropped from `GL_EXTENSIONS`.

## The documents used

1. `docs/design/MiniGL-Compatible-Spec.md` (the clean-room specification,
   9 November 2026): the library name and version, the -30 vector, the
   table's order, types and size, the ABI checks, the current-context rules,
   the type sizes, MiniGL's own token values, and every mgl* behaviour.
2. The Khronos OpenGL headers (Mesa 26.2.4's `include/GL/gl.h` and
   `include/GL/glext.h`, MIT): the names and values of the `GL_*` tokens.
3. The OpenGL 1.1 specification, GLU 1.3 and GLUT 3.7: the gl*, glu* and
   glut* behaviour and the GLU and GLUT token values.
4. AmiCradle's own MIT code in this repository.

The names of MiniGL's own tokens and types that the spec describes but does
not name (`MGL_LOCK_MANUAL`, `MGL_KEY_UP`, `MGL_MOUSE_LEFT`,
`MGL_SCREENMODE_BEST`, MGLLockInfo's fields ...) are ours. Programs built
with the published SDK do not depend on them: they depend only on the values
and the layout, which come from the spec.

## The rule for future contributors

Change this library's interface from the spec and the Khronos documents
only:

- Read `docs/design/MiniGL-Compatible-Spec.md`, the OpenGL 1.1
  specification, GLU 1.3/GLUT 3.7 documentation, the Khronos (Mesa) GL
  headers, and AmiCradle's own code.
- Do not read, download or search for any MiniGL, PiStorm3D, ogles2 or
  WarpMiniGL source, header, document or SDK, at any version.
- When behaviour is unclear, add it to the spec's section 10 and find it with
  a black-box test (spec section 11), run in a lab.
- The table is changed only by changing the spec's section 8 and running
  `gen_api.py`; entries are only ever appended (spec 4.2). The tests
  (`tests/run_minigl.sh`) check that the generated files are what the spec
  makes and that every offset is the spec's.
- Commits say where a behaviour came from: the spec (section), the GL
  specification, or a black-box test (named).
- The library is described as "minigl.library-compatible"; no Hyperion or
  PiStorm3D name or logo goes in its strings, documents or `GL_VENDOR`.
