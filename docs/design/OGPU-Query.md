# OGPU_Query: the capabilities

gfopengpu.library 0.30. Stage MQ of [the shared pipe plan](OpenGPU-Shared-Pipe-Plan.md) (2.8 and MQ). Header: `include/gfopengpu/caps.h`. Code: `library/opengpu/ogpu_caps.c`. Tests: `tests/caps/test_caps.sh`.

`OGPU_Query` is the one place any library finds out what the machine can do. MiniGL, Warp3D, EGL, OGLES3, TinyGL and the pipe stop asking the host themselves: each front end's mapping PR (MQ step 4) swaps its own HELLO and caps reading for these calls, and the ownership check's `host-caps` rule keeps it that way.

## The calls

| LVO | Call | Answers |
| --- | --- | --- |
| 30 | `OGPU_Query(op, format)` | FULL, PARTIAL or NONE from the best back end there is, as since 0.1 |
| 198 | `OGPU_QueryLimit(op)` | a number; 0 when not known |
| 204 | `OGPU_QueryDriver(drv, op, format)` | one back end's answer (`OGPU_DRV_*`) |

The answer word is the same as always:
- `OGPU_ANSWER(q)` gives `OGPU_FULL` (2), `OGPU_PARTIAL` (1) or `OGPU_NONE` (0).
- `OGPU_BACKEND(q)` gives 0 for the 68k core and 1 for the driver.
- `OGPU_ONGPU(q)` is set when the answer comes from virgl drawing on a graphics chip.

A library older than 0.30 answers NONE to every capability op, so asking is always safe. Before calling the two new functions, check `lib_Revision >= OGPU_CAPS_REVISION` (30).

## Discovery: once a boot

The capability ops are `0x8000` to `0x80FF`. No stream command may use that range.

The first capability question after the library loads does the discovery, under the driver lock, so two programs asking at once send one HELLO between them. It works like this:

1. If the driver says FULL to `OGPU_OP_VIRGL`, the library sends three request blocks through the driver:
   - `ACV_HELLO` plus `ACV_CAPSET_INFO` for set 2;
   - `ACV_CAPSET_INFO` for set 1, if the host has no set 2;
   - `ACV_CAPS`, into the library base: 128 words, which reaches `max_texture_cube_size`.
2. With no driver, or no virgl, nothing is sent and the 68k core answers alone.

Later questions send nothing.

The answers are looked up again only after a host reset: when the driver reports `OGPU_ERR_DEVICE`, the next question does a fresh discovery. `struct ogpu_caps.discoveries` counts the HELLOs, and the tests check it stays at 1 over a thousand questions.

If discovery fails, nothing from virgl is promised, and the library doesn't retry until a reset:
- **Another protocol, or the first block fails:** there is no virgl, so the CPU answers.
- **No caps came back:** the host flags still answer the back-end ops (`REAL_GPU`, `VIRGL_PRESENT` and so on), but every format and feature question gets the CPU's answer.

## What FULL and PARTIAL mean

- **FULL:** the back end does it exactly as the front end's API says. The pipe always swaps byte order (every block goes in the Amiga's order and the host turns it round), and that does not count as a conversion.
- **PARTIAL:** it works, with a difference written down below. Each front end's mapping PR says what it does with each PARTIAL.
- **NONE:** it doesn't.

## The ops

The virgl columns hold when the host answered HELLO with our protocol and gave caps. The CPU column is the 68k core (`ogpu_3d.c`), and only when the build includes it.

### Back ends (format 0)

| Op | virgl | CPU |
| --- | --- | --- |
| `OGPU_OP_REAL_GPU` 0x8001 | FULL when the host flag `ACV_HOST_GPU` is set | NONE |
| `OGPU_OP_LLVM_PIPE` 0x8002 | FULL when it isn't (llvmpipe or the like) | NONE |
| `OGPU_OP_CPU_CORE` 0x8003 | NONE | FULL |
| `OGPU_OP_VIRGL_PRESENT` 0x8004 | FULL with `ACV_HOST_PRESENT` | NONE |
| `OGPU_OP_VIRGL_GLES` 0x8005 | FULL with `ACV_HOST_GLES` | NONE |

### Formats

You can ask about the stream's formats (stream.h, 1 to 12), the 3D buffer formats (stream3d.h: `OGPU_FMT_Z16` 16, `OGPU_FMT_Z32` 17, `OGPU_FMT_S8` 18), or caps.h's own formats. caps.h's formats start at 32 so they never clash with a stream format: RGBA4444 32, RGBA5551 33, L8 34, LA88 35, Z24 36, Z24S8 37.

For virgl, each format has a virgl format that holds it as it is, and a fallback the pipe converts to:

- **FULL** when the host's caps mask has the bit for the native format.
- **PARTIAL** when it only has the fallback's bit.
- **NONE** when it has neither.

The table below uses virgl's format names without the `VIRGL_FORMAT_` prefix:

| OGPU format | Native (FULL) | Converted to (PARTIAL) |
| --- | --- | --- |
| ARGB32 | B8G8R8A8 | none |
| BGRA32 | A8R8G8B8 | B8G8R8A8 |
| XRGB32 | B8G8R8X8 | B8G8R8A8 |
| RGBA32 | R8G8B8A8 | B8G8R8A8 |
| ABGR32 | none | B8G8R8A8 |
| RGB565, RGB565PC | B5G6R5 | B8G8R8A8 |
| RGB555, RGB555PC, RGBA5551 | B5G5R5A1 | B8G8R8A8 |
| RGBA4444 | B4G4R4A4 | B8G8R8A8 |
| A8, L8, LA88 | A8, L8, L8A8 | B8G8R8A8 |
| CLUT8, INDEX8 | none | B8G8R8A8, through the palette |
| Z16 | Z16 | Z24X8 |
| Z24 | Z24X8 | Z24S8 |
| Z32 | Z32 | Z24X8 (8 bits fewer) |
| Z24S8 | Z24S8 | none |
| S8 | S8 | Z24S8 (inside a packed buffer) |

| Op | virgl | CPU |
| --- | --- | --- |
| `OGPU_OP_TEXTURE_2D` 0x8010, `OGPU_OP_MIPMAP` 0x8014 | the sampler mask (caps words 1 to 16) | FULL for ARGB32, RGB565, A8, INDEX8 and CLUT8, which `o3_texture` takes as they are; PARTIAL for the other colour formats, which are converted when the texture is made |
| `OGPU_OP_RENDER_TARGET` 0x8011 | the render mask (words 17 to 32) | `ogpu3d_supports(TRIANGLES)`: FULL for ARGB32 and RGB565; PARTIAL for CLUT8 (through a pen table) and SDL's 32-bit forms |
| `OGPU_OP_DEPTH_STENCIL` 0x8012 | the depth and stencil mask (words 33 to 48) | FULL for Z16, Z32 and S8; PARTIAL for Z24 (kept as Z32) and Z24S8 (two buffers, not packed) |
| `OGPU_OP_BUFFER` 0x8013 | FULL | FULL |

### Fixed function, tests and shaders (format 0)

virgl has no fixed function of its own: the pipe writes the TGSI (stage M5), so the virgl answer is what the pipe does.

| Op | virgl | CPU |
| --- | --- | --- |
| `FF_TEXTURE_ENV`, `FF_FOG`, `FF_MULTITEXTURE`, `FF_ALPHA_TEST`, `FF_SPECULAR` | FULL | FULL |
| `FF_LIGHTING`, `FF_TEXGEN` | FULL | NONE (the front ends light and generate coordinates themselves) |
| `FF_CHROMA` 0x8026 | PARTIAL: a discard against the texel, so one colour key, not a range | FULL |
| `BLEND`, `DEPTH_TEST`, `STENCIL_TEST`, `SCISSOR` | FULL | FULL |
| `LOGIC_OP` 0x8031 | FULL; NONE on a GLES host, which has no `glLogicOp` | FULL |
| `TGSI` 0x8040 | FULL | NONE |

On a GLES host, `OGPU_Query(OGPU_OP_LOGIC_OP, 0)` gives the CPU core's FULL, with `OGPU_BACKEND` 0. A front end that wants to know whether the GPU does it asks `OGPU_QueryDriver(OGPU_DRV_LLVMPIPE` or `OGPU_DRV_VIRGL, ...)` instead.

### Presentation (format 0)

`PRESENT_WINDOW`, `PRESENT_SCREEN`, `PRESENT_BITMAP` and `VSYNC` (0x8050 to 0x8053) answer NONE until OpenRTG registers its present routes with the library (stage M8).

### Limits (`OGPU_QueryLimit`)

| Op | virgl | CPU |
| --- | --- | --- |
| `MAX_TEXTURE_SIZE` 0x8080 | caps word 121 (`max_texture_2d_size`); 0 from a set-1 host | 2048: `o3_texture` takes up to 4096 a side, but a full mipmap chain (`OGPU_TEX_LEVELS` 12) stops at 2048 |
| `MAX_TEXTURE_UNITS` 0x8081 | 8, the most the pipe's fixed-function shaders use | `OGPU_TEX_UNITS` |
| `MAX_VERTEX_ATTRIBS` 0x8082 | caps word 89; 16 (GL 3's minimum) when the host doesn't say | 0 |
| `MAX_VIEWPORT_DIMS` 0x8083 | caps word 121 | 0 |

## Back ends for OGPU_QueryDriver

| `drv` | Answers |
| --- | --- |
| `OGPU_DRV_BEST` (0) | what `OGPU_Query` answers |
| `OGPU_DRV_VIRGL` (1) | virgl's answer, only when it draws on a GPU; otherwise NONE |
| `OGPU_DRV_LLVMPIPE` (2) | virgl's answer, only when there is no GPU; otherwise NONE |
| `OGPU_DRV_CPU` (3) | the 68k core, for capability ops and stream ops alike |
| `OGPU_DRV_ACRTG` (4) | the driver's own `supports()` for stream ops; NONE for capability ops (the stream route has none; it goes with M11) |

## Still to come in MQ

1. **The golden capture:** `tests/golden/caps/` holds every front end's capability output on the 68k core, the ACRTG stream route, virgl on a GPU and virgl on llvmpipe. That needs a lab Instance-32 for each back end.
2. **The mapping PRs, one per front end** (Warp3D, MiniGL, EGL, then the pipe's screen). Each is checked against its golden file on every back end, and each removes its own HELLO and caps reading and its ownership debt line.
3. **The front-end PARTIAL tests:** a fake back end answers FULL, PARTIAL or NONE for each op, and each front end's behaviour is checked against its rule.

The virgl answers above for fixed function and limits are the pipe's promises. If a golden file shows a front end doing otherwise today, the mapping PR either changes the answer here, in the same PR, or fixes the front end.
