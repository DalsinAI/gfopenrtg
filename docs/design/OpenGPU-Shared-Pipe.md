# GFOS graphics: one shared pipe (OpenGPU and OpenRTG)

Team 5, 23 Nov 2026. How our 3D and screen libraries stop duplicating each other, with Mesa's Gallium as the model. Written from gfopenrtg's main (a6cd53e) and its notes `Warp3D-Virgl-Route.md` and `MiniGL-Compatible-Plan.md`, with two outside reviews taken in. A proposal: nothing here is built yet.

## 0. The principle

**Every feature has exactly one owner (section 6). If a second implementation appears, the change that adds it names which of the two will be deleted, and when.** Most rot comes from "just put this helper here", repeated a hundred times; the ownership matrix is the architecture, and a new feature starts by saying which row it belongs to.

## 1. The three rules

1. **Only the shared pipe encodes virgl,** and Mesa's own virgl driver inside GL.module. Two encoders, both accounted for, never a third.
2. **Only OpenRTG presents pixels** to a screen, a window or a bitmap, and only from an `ORTG_Surface`. That includes Mesa's frames: format conversion, flipping and the rectangle walk exist once.
3. **Only the shared pipe owns GPU resources and bound GPU state; only gfopengpu chooses back ends and discovers capabilities.** A front end owns its API's state and nothing below it.

Each rule is a mechanical CI check (section 7, phase 0).

## 2. Why no new rendering API

virgl's command protocol is Gallium's interface written as words: every command is a `pipe_context` call, every object a Gallium state object or resource. When our libraries emit virgl, they already speak Mesa's middle layer. A new `OGPU_Draw`/`OGPU_CreateTexture` API would be another way to say the same thing. What is missing is one shared place that produces it.

## 3. The layers

| Layer | What it is | Who may call it |
|---|---|---|
| **Front ends** | Warp3D.library, minigl.library, tinygl.library, GL.module (Mesa), OGLES3, EGL, graphics.library's OGFX calls | Programs |
| **gfopengpu runtime** (its LVOs) | `OGPU_Query`, `OGPU_Submit`, `OGPU_Wait`, `OGPU_ModuleOpen`/`Close`, contexts. No draw, texture, shader or present calls. | Front ends |
| **The shared pipe** (in gfopengpu, or a module it loads) | Pipe contexts, state objects and their cache, the fixed-function shader generator, resources, frame arenas, the virgl and stream3d emitters | Our own front ends only, through a versioned table |
| **Back ends** | virgl on the host GPU, llvmpipe, the 68k core (`ogpu_3d.c`) | gfopengpu only |
| **OpenRTG** | Screens, windows, bitmaps, modes, composition, scanout, `ORTG_Surface`, `ORTG_Present` | Everything that shows pixels |

## 4. The shared pipe

### The pipe context

The **pipe context** (conceptually Mesa's `pipe_context`) is the sole owner of what is bound for drawing: the render target and depth buffer, the shaders, the blend, depth-stencil-alpha and rasterizer state, the vertex layout, and the bound textures and samplers. Every API context sits on one:

- a Warp3D context holds a pipe context;
- a MiniGL context holds a pipe context;
- neither keeps its own copy of what is bound. It keeps its API's state (what the program set) and tells the pipe context what to bind.

### What else the pipe owns, and nothing else may

- **State objects:** blend, depth-stencil-alpha, rasterizer, vertex layout, sampler; cached by key.
- **The fixed-function shader generator:** OpenGL 1.1's equations (two texture units, fog, alpha, lighting, texgen, colour material). One bug here breaks MiniGL, Warp3D and TinyGL's GL 1.x users at once, so it is built early and tested on its own (section 8). It writes TGSI because virglrenderer takes TGSI; it is the only code that knows, so that can change later. It serves the virgl route only: the 68k core keeps its own fixed-function rasteriser and takes stream3d with no shaders, so a machine with no host never pays for generation. It runs on the 68k; the common cases are prebuilt as constant text, the rest built on first use and cached per context.
- **Resources:** textures, mip chains, depth buffers, rename-on-write, reference counts. **No front end allocates GPU-visible memory.** A front end's texture is a wrapper: `struct W3DTexture { OGPUPipeResource *resource; ... API fields ... }`, never pixels, mip levels or buffers of its own.
- **Frame arenas:** vertex, index and command rings; overflow never drops geometry; at most about 2 ms per block.
- **Two emitters:** virgl (through `include/gfopengpu/virgl_cmd.h`) for every accelerated back end, stream3d for the 68k core. Which one a frame takes is decided here and nowhere else, so it is tested once for every front end.

**Its interface is a versioned table.** Our front ends ship and update separately, so a library may meet an older or newer pipe than it was built with. gfopengpu hands our libraries `struct OGPUPipeOps` with `version` first (`OGPU_PIPE_VERSION 1`); a library asking for a newer version than the pipe has gets NULL; new calls are only added at the end; no library reaches past the table. The table has no present (rule 2). It is internal: programs never get it.

**Mesa keeps its own state tracker,** which is why we carry it. The boundary is explicit: GL.module's virgl goes out only through `OGPU_Submit`, and its frames reach the screen only through `ORTG_Present`. There are exactly two state trackers: Mesa's, and the pipe's.

## 5. Front ends, OpenRTG, capabilities, back ends

### Front ends

| Front end | Keeps | Uses |
|---|---|---|
| minigl.library | ABI 5 table, GL 1.1 state, display lists, GLU, GLUT, `mgl*` calls | a pipe context |
| Warp3D.library | its API and state, lock and unlock, direct mode, culling, points and lines as quads, Z and stencil read and write | a pipe context |
| tinygl.library | nothing of its own: it forwards to GL.module (the model for the rest) | GL.module |
| GL.module | all of Mesa | the runtime, `ORTG_Present` |
| OGLES3, EGL | contexts and surfaces | GL.module, OpenRTG |

MiniGL's and Warp3D's virgl routes are already on main (`library/minigl/mgl_vg*.c`, `library/warp3d/w3d_vg*.c`), each with its own state cache, shader generator, resources, transport and present; `virgl_cmd.h` already shares their command writers. The pipe is extracted from them, step by step, each step proven against today's pixels: `OpenGPU-Shared-Pipe-Plan.md`.

### OpenRTG

- Everything shown becomes an **`ORTG_Surface`** first: a virgl colour target, a texture, a retained surface, a framebuffer, an ACMedia frame. A surface names its pixels, their format and where they live (host GPU, host memory, Amiga memory).
- **`ORTG_Present(surface, target)`** is the only present. It does the format conversion (XRGB32, ARGB32, RGB565), the flip and the window's visible rectangles. It grows out of `ORTG_PresentVirgl`; `ACV_PRESENT`, the retained present and ACRTG direct become routes inside it.
- ACMedia keeps decoding on the host; its frames are surfaces like any other, or go to the display plane when a film fills the screen.
- P96 and CyberGraphX stay stubs over OpenRTG (`uaegfx.c` already is).
- graphics.library's acceleration stays in gfopengpu, because the OGFX calls emit stream commands and moving them would teach OpenRTG opcodes it otherwise never needs. Its pixels reach the screen through OpenRTG.

### Capabilities: two levels

- **Host caps, what the back end can do:** `OGPU_Query(op, format)`, "full, partial or none". gfopengpu reads the host's virgl caps once and answers through it. The one source of truth.
- **Reported caps, what each API says:** each front end, from that answer, by its own written rules. On a Pi 5 with 16 texture units, Warp3D and MiniGL still report 2, as they always have.
- **Frozen first.** Before the shared query is wired into a front end, everything it reports today (W3D_Query and W3D_GetDriverState, MiniGL's and GL's strings and limits) is recorded on each back end and kept as the reference.

### Back ends: designated now

Every renderer we keep is a bug list, a test path and an optimisation effort for good. So the target is named now, and the measurement only confirms or changes it:

| Where | Strategic back end |
|---|---|
| 68k machines, no host | the 68k core, `ogpu_3d.c` (also the reference for what each stream3d command means) |
| Host with a usable GPU | virgl to the GPU |
| Host without one | llvmpipe, through virglrenderer |

**Deprecated, subject to the measurement:** the host copy of the 3D core in ACRTG.gpu and its Vulkan compute version (`host/vulkan/ogpu3d.comp`), and MiniGL's own CPU route. If llvmpipe matches them, they are deleted on a date the decision names. This is about 3D only: ACRTG.gpu's 2D work (the stream's 2D and composite ops that RTG and graphics.library's acceleration use) is not part of it.

One `ENV:GFOpenGPU/Driver`; the old `ENV:Warp3D/Driver`, `ENV:MiniGL/Driver` and `ENV:TinyGL/Driver` remain only as overrides. `Auto` takes virgl when `OGPU_Query(OGPU_OP_VIRGL)` says full and the host has a real GPU.

## 6. The ownership matrix

| Function | Owner | Forbidden elsewhere |
|---|---|---|
| virgl encoding | shared pipe (and Mesa's own driver) | Warp3D, MiniGL, TinyGL, EGL, OGLES3 |
| Fixed-function shader generation (TGSI) | shared pipe | Warp3D, MiniGL |
| Bound drawing state | pipe context | API contexts |
| GPU resources and their lifetime | shared pipe | every front end |
| stream3d 3D commands | shared pipe | every front end |
| Back-end choice | gfopengpu | every front end |
| Capability discovery | gfopengpu | every front end |
| Reported capabilities | each front end, by its frozen rules | gfopengpu |
| Presentation, rectangle walk, format conversion | OpenRTG | everybody else, Mesa included |
| Composition, scanout, display modes | OpenRTG | everybody else |
| graphics.library acceleration | gfopengpu (OGFX) | OpenRTG, front ends |
| Video decode | ACMedia | everybody else |
| Video presentation | OpenRTG | ACMedia |
| Warp3D semantics | Warp3D.library | the pipe, gfopengpu |
| GL semantics | minigl.library; Mesa for GL.module | the pipe, gfopengpu |
| EGL semantics | EGL | the pipe, gfopengpu |

Every object has one owner. A front end's texture refers to a pipe resource, which refers to a virgl resource or 68k memory; only the pipe frees them.

## 7. Order of work

The detailed stages, gates, build and release plan are in `OpenGPU-Shared-Pipe-Plan.md` (M0 to M11, with the capability workstream MQ). In outline:

- **Phase 0: CI enforcement,** mechanical:
  - only `library/opengpu` and the OpenRTG library may include the host headers (`acvirgl.h`, the ACRTG host header, `virglrenderer.h`) or open ACRTG;
  - only OpenRTG may export `ORTG_Present*`, and no other library exports a present;
  - only the shared pipe may include `virgl_cmd.h` (GL.module's Mesa driver uses Mesa's own protocol header, and is the listed exception);
  - only the pipe emits stream3d 3D ops (graphics.library's OGFX calls are on the allowed list for 2D);
  - no front end allocates GPU-visible buffers or keeps a state cache or shader generator of its own.
- **Phase 1: `virgl_cmd.h` and the pipe's header,** the table, state-object keys, resource handles and arena rules, before any pipe code. MiniGL and Warp3D compile against it from the start. GL.module's winsys and minigl's encoder move onto `virgl_cmd.h`; proof: the same pixels and counters in `tests/run_minigl.sh`.
- **Phase 2: the resource manager and the pipe context.**
- **Phase 3: the fixed-function shader generator,** with its own tests (section 8), before any front end depends on it.
- **Phase 4: MiniGL's virgl route on the pipe,** both emitters from the start; the pixel references start with its first scene.
- **Phase 5: Warp3D on the pipe,** under the same references.
- **Phase 6: `ORTG_Surface` and `ORTG_Present`,** then retained surfaces, GL.module's frames and ACMedia frames moved onto them.
- **Phase 7: capabilities** (the plan's MQ, which runs early, right after the pipe's interface): each front end's answers frozen, then wired to `OGPU_Query`; one `ENV:GFOpenGPU/Driver`.
- **Phase 8: the renderer elimination programme:** the measurement, then the deprecated back ends deleted on the date the decision names.

## 8. Tests and measurements

Three levels, each with reference pictures kept beside the scenes and a tolerance of 8/255 per channel with a one-pixel edge shift:

- **Level 1, APIs:** the same scene through Warp3D, MiniGL and TinyGL, wherever their APIs can say the same thing (they differ in places on purpose, such as Warp3D's fog by w; those are listed, not forced).
- **Level 2, back ends:** each scene on the 68k core, virgl on a GPU and llvmpipe.
- **Level 3, presentation:** the same surface presented into a window, a screen, a bitmap and as a video surface, all through `ORTG_Present`, in each format.

And beside them:

- **The shader generator on its own:** every texture environment pair, fog mode, light type, texgen mode and colour material case, drawn through the generated shaders and compared with the 68k core, before MiniGL uses it. Its time per new shader on a 68040, the prebuilt set's size, and the first frame's cost.
- **Screens:** open and close, modes, palette and truecolour, several monitors, bitmaps.
- **Warp3D:** `w3dcheck`, `w3darrbench`, `scenes3d.c`. **GL:** MGLTest, `tests/run_minigl.sh`, Mesa's demos, gears, texture stress, immediate mode.
- **Films:** 1080p and 4K H.264, HEVC, several streams, no copies on the way to the screen.
- **Reported capabilities** per front end and back end, against the frozen answers.
- **Deadlocks:** a screen lock, a Warp3D lock and a GL context held together, on each back end; and a 68k program holding a lock across a host round trip (a submit, a present, a readback). MiniGL already froze a machine by holding its display lock between frames.
- **Back ends:** host timedemos, and at least one workload written the 68k way that stresses the bridge (many small draws and texture updates per frame).
- Commercial games (Quake, Quake II, BlitzQuake, RTCW) need data we can't ship: labs only, never repos or CI. CI's references stay on our own tests.
