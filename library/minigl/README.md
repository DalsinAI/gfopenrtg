# minigl.library 30 (minigl.library-compatible)

AmiCradle GL 1.1 for minigl.library programs: an MIT library that programs
built for the shared `minigl.library` (dispatch ABI 5, 29.x and 30.0
headers) open and run unchanged. Its interface was rebuilt clean-room on
9 November 2026 from `docs/design/MiniGL-Compatible-Spec.md`; see
[PROVENANCE.md](PROVENANCE.md) for what was rebuilt, from what, and the rule
for changing it.

- **The library.** `minigl.library` 30.0. Its one function, at -30, answers
  the dispatch table (5 longwords, 205 entries, 840 bytes). The front end is
  built twice (FPU and soft); the first open picks the one the machine runs.
- **Through the table.** A program includes `<proto/minigl.h>`, links
  `libminigl.a`, and calls `MiniGLOpen()` and `MiniGLClose()`.
- **Through functions.** A program includes `<mgl/gl.h>`, links `libmgl.a`,
  and calls `MGLInit()` and `MGLTerm()`.
- **Routes.** `ENV:MiniGL/Driver`: `CPU` (the 3D core on the 68k), `GFOpenGPU`
  (gfopengpu.library's 3D ops), `Virgl` (the host's GPU through GFOpenGPU's
  virgl path: `mgl_vg*.c`, docs/design/MiniGL-Compatible-Plan.md), or `Auto`
  (virgl where gfopengpu.library carries it, else as GFOpenGPU or CPU).
- **Preferences.** `ENV:MiniGL/Flip`, `ENV:MiniGL/MaxTextureSize` (default
  1024), `ENV:MiniGL/Trace`, `ENV:MINIGL_FORCE_SOFT`.

Building: `library/minigl/build.sh [OUT]` (the os32 stove). The interface
files are made by `library/minigl/build.sh api` (`gen_api.py`). PC tests:
`tests/run_minigl.sh`.
