# GFOS graphics: one shared pipe (OpenGPU and OpenRTG)

Team 5, 23 Nov 2026. How our 3D and screen libraries stop duplicating each other, with Mesa's Gallium as the model. Written from gfopenrtg's main (a6cd53e) and its notes `Warp3D-Virgl-Route.md` and `MiniGL-Compatible-Plan.md`. A proposal: nothing here is built yet.

## 1. The three rules

1. **Only the shared pipe encodes virgl,** and Mesa's own virgl driver inside GL.module. Two encoders, both accounted for, never a third.
2. **Only OpenRTG presents pixels** to a screen, a window or a bitmap, and only from an `ORTG_Surface`.
3. **Only the shared pipe owns GPU resources and cached GPU state; only gfopengpu chooses back ends and discovers capabilities.** A front end owns its API's state and nothing below it.

Each rule is a CI check (section 6, step 1). Everything else in this note follows from them.

## 2. Why no new rendering API

virgl's command protocol is Gallium's interface written as words: every command is a `pipe_context` call, every object a Gallium state object or resource. When our libraries emit virgl, they already speak Mesa's middle layer. A new `OGPU_Draw`/`OGPU_CreateTexture` API would be another way to say the same thing. What is missing is one shared place that produces it.

## 3. The layers

| Layer | What it is | Who may call it |
|---|---|---|
| **Front ends** | Warp3D.library, minigl.library, tinygl.library, GL.module (Mesa), OGLES3, EGL, graphics.library's OGFX calls | Programs |
| **gfopengpu runtime** (its LVOs) | `OGPU_Query`, `OGPU_Submit`, `OGPU_Wait`, `OGPU_ModuleOpen`/`Close`, contexts. No draw, texture, shader or present calls. | Front ends |
| **The shared pipe** (in gfopengpu, or a module it loads) | State objects and their cache, the fixed-function shader generator, resources, frame arenas, the virgl and stream3d emitters | Our own front ends only, through a versioned table |
| **Back ends** | virgl on the host GPU, virgl on llvmpipe, the 68k core (`ogpu_3d.c`), the host core (ACRTG.gpu) and its Vulkan version | gfopengpu only |
| **OpenRTG** | Screens, windows, bitmaps, modes, composition, scanout, `ORTG_Surface`, `ORTG_Present` | Everything that shows pixels |

## 4. The shared pipe

**It owns, and nothing else may:**
- **State objects:** blend, depth-stencil-alpha, rasterizer, vertex layout, sampler; cached by key.
- **The fixed-function shader generator:** OpenGL 1.1's equations (two texture units, fog, alpha, lighting, texgen, colour material), prebuilt for common cases, generated for the rest. It writes TGSI because virglrenderer takes TGSI; it is the only code that knows, so that can change later.
- **Resources:** textures, mip chains, depth buffers, rename-on-write, reference counts.
- **Frame arenas:** vertex, index and command rings; overflow never drops geometry; at most about 2 ms per block.
- **Two emitters:** virgl (through `include/gfopengpu/virgl_cmd.h`) for every accelerated back end, stream3d for the 68k core. A machine with no host has no virgl renderer, so both are needed. Which one a frame takes is decided here and nowhere else, so it is tested once for every front end.

**Its interface is a versioned table.** Our front ends ship and update separately, so a library may meet an older or newer pipe than it was built with. gfopengpu hands our libraries `struct OGPUPipeOps` with `version` first (`OGPU_PIPE_VERSION 1`); a library asking for a newer version than the pipe has gets NULL; new calls are only added at the end; no library reaches past the table. The table has no present (rule 2). It is internal: programs never get it, so it is never a public API to freeze.

**Mesa keeps its own state tracker,** which is why we carry it. It shares the lower half: its virgl goes out only through `OGPU_Submit`, and its frames reach the screen only through `ORTG_Present`. There are exactly two state trackers: Mesa's, and the pipe's for our fixed-function APIs.

## 5. Front ends, OpenRTG and capabilities

### Front ends

| Front end | Keeps | Uses |
|---|---|---|
| minigl.library | ABI 5 table, GL 1.1 state, display lists, GLU, GLUT, `mgl*` calls | the pipe |
| Warp3D.library | its API and state, lock and unlock, direct mode, culling, points and lines as quads, Z and stencil read and write | the pipe |
| tinygl.library | nothing of its own: it forwards to GL.module (the model for the rest) | GL.module |
| GL.module | all of Mesa | the runtime, `ORTG_Present` |
| OGLES3, EGL | contexts and surfaces | GL.module, OpenRTG |

MiniGL's and Warp3D's virgl routes exist only as plans. They are built on the pipe from their first commit, so their own state caches, shader generators and encoders are never written.

### OpenRTG

- Everything shown becomes an **`ORTG_Surface`** first: a virgl colour target, a texture, a retained surface, a framebuffer, an ACMedia frame. A surface names its pixels, their format and where they live (host GPU, host memory, Amiga memory).
- **`ORTG_Present(surface, target)`** is the only present. It does the format conversion (XRGB32, ARGB32, RGB565), the flip and the window's visible rectangles. It grows out of `ORTG_PresentVirgl`; `ACV_PRESENT`, the retained present and ACRTG direct become routes inside it.
- ACMedia keeps decoding on the host; its frames are surfaces like any other, or go to the display plane when a film fills the screen.
- P96 and CyberGraphX stay stubs over OpenRTG (`uaegfx.c` already is).
- graphics.library's acceleration stays in gfopengpu (the OGFX calls emit stream commands); its pixels reach the screen through OpenRTG.

### Capabilities: two levels

- **What the back end can do:** `OGPU_Query(op, format)`, "full, partial or none". gfopengpu reads the host's virgl caps once and answers through it. The one source of truth.
- **What each API reports:** each front end, from that answer, by its own written rules. On a host with 8 texture units, Warp3D and MiniGL still report 2 and GL reports 8. The rules are tested, so a new back end never changes what an old program sees.

### Back end choice

One `ENV:GFOpenGPU/Driver`; the old `ENV:Warp3D/Driver`, `ENV:MiniGL/Driver` and `ENV:TinyGL/Driver` remain only as overrides. `Auto` takes virgl when `OGPU_Query(OGPU_OP_VIRGL)` says full and the host has a real GPU.

## 6. Who owns what

| Component | API state | GPU resources | Encodes virgl | Presents |
|---|---|---|---|---|
| minigl.library, Warp3D.library | their own | refer to pipe resources | no | no |
| tinygl.library | none | no | no | no |
| GL.module (Mesa) | Mesa's | Mesa's | yes, Mesa's driver | no |
| The shared pipe | cached GPU state only | **yes**, counted | **yes** | no |
| gfopengpu runtime | none | no | no | no (owns back ends and capability discovery) |
| OpenRTG | display state | `ORTG_Surface`s | no | **yes, only it** |

Every object has one owner. A front end's texture refers to a pipe resource, which refers to a virgl resource or 68k memory; only the pipe frees them.

## 7. Order of work

1. **The three rules as CI checks:** nothing but gfopengpu and gfopenrtg opens ACRTG; virgl words only through `virgl_cmd.h` or Mesa's driver; stream3d 3D ops only from the pipe; presenting only through gfopenrtg; no state cache or shader generator in a front end.
2. **`virgl_cmd.h`,** used by GL.module's winsys and minigl's encoder. Proof: same pixels and counters in `tests/run_minigl.sh`.
3. **The pipe, with MiniGL's virgl route as its first user:** both emitters and the versioned table from the start.
4. **Warp3D on the pipe.** Proof: `test_w3d_vg.c`'s scenes against the 68k core within 8/255 per channel.
5. **`ORTG_Surface` and `ORTG_Present`,** then retained surfaces and ACMedia frames moved onto them.
6. **Capabilities through `OGPU_Query`,** each front end's reporting rules written and tested; one `ENV:GFOpenGPU/Driver`.
7. **llvmpipe against our host core** on the Pi 5 and the x86. Every back end is maintenance for good: if llvmpipe matches, the host core and its Vulkan version go, and the core stays only for the 68k, as the reference for what stream commands mean.

## 8. Tests

- **Every scene through both emitters**, 68k core against virgl, 8/255 per channel with a one-pixel edge shift. One test set for all front ends, because only the pipe emits.
- **Screens:** open and close, modes, palette and truecolour, several monitors, bitmaps.
- **Warp3D:** contexts, textures, Z, blending, multitexture, render to texture (`w3dcheck`, `w3darrbench`, `scenes3d.c`).
- **GL:** MGLTest, `tests/run_minigl.sh`, Mesa's demos, gears, texture stress, immediate mode.
- **Films:** 1080p and 4K H.264, HEVC, several streams, no copies on the way to the screen.
- **Reported capabilities** per front end and back end, against their rules.
- **Deadlocks:** a screen lock, a Warp3D lock and a GL context held together, on each back end.
- Commercial games (Quake, Quake II, BlitzQuake, RTCW) need data we can't ship: labs only, never repos or CI.
