# GFOS graphics: one shared pipe, like Mesa (OpenGPU / OpenRTG)

Team 5, 23 Nov 2026, for Dale. A design note on cutting duplication across our 3D and screen drivers, with Mesa as the yardstick. It is written from gfopenrtg as it is on main (a6cd53e): `library/opengpu`, `library/warp3d`, `library/minigl`, `library/tinygl`, `library/ogles3`, `library/egl`, `library/modules/gl`, `host/virgl`, `host/vulkan`, and the design notes `Warp3D-Virgl-Route.md` and `MiniGL-Compatible-Plan.md`. It started from an outside review Dale pasted (01:14), and takes in that review's second (01:20) and third (01:25) readings of this note. Proposals are marked as such. Nothing here has been built yet.

## 1. The rule, in one line

**OpenGPU is our Gallium: one way to talk to whatever draws. OpenRTG owns every pixel that reaches a screen. Every graphics API above them is a thin front end, and none may draw, encode or present by itself.**

The three rules to adopt formally (each one a CI check, section 5):

1. **Only the shared pipe encodes virgl, and Mesa's own virgl driver inside GL.module.** That is two encoders, both accounted for, and no third.
2. **Only OpenRTG presents pixels** to a screen, a window or a bitmap, and only from an `ORTG_Surface`.
3. **Only the shared pipe owns GPU resources and the state cache; only gfopengpu discovers capabilities.** Front ends own their API's state and nothing below it.

## 2. How Mesa avoids duplication, and what that means here

Mesa has four layers, and each kind of work exists in exactly one of them:

| Mesa layer | What it owns | Ours today | Ours, proposed |
|---|---|---|---|
| **API front ends** (GL, GLES, Vulkan) | The API's names, arguments and errors | Warp3D.library 5.0, minigl.library 29, tinygl.library 53, ogles3, EGL, GL.module (Mesa itself), graphics.library's OGFX patches | The same libraries, made thin |
| **State tracker** (st/mesa) | Turning API state into pipe objects: blend, depth-stencil-alpha, rasterizer, vertex layouts, shaders, samplers | Mesa's own (in GL.module); minigl.library's planned `mgl_vg_state.c`; Warp3D's planned `w3d_vg.c` | **One shared state tracker** for our fixed-function APIs (section 4); Mesa keeps its own |
| **Gallium interface** (pipe_context, pipe_resource, CSO) | Resources, state objects, shaders, draws, fences | Two languages: the OGPU stream (`stream3d.h`, stream v1.2) and virgl (`OGPU_OP_VIRGL`) | **virgl for every accelerated back end, stream3d for the 68k**, both produced only inside the shared pipe |
| **Drivers** (llvmpipe, softpipe, virgl, v3d) | Running it on a device | gfopengpu back ends: the CPU core on the 68k, ACRTG.gpu (the same core on the host, a Vulkan compute version in `host/vulkan`), ACRTG virgl (`host/virgl/acvirgl.c`, the host's real GPU) | The same back ends, chosen only by gfopengpu |

The key fact: **virgl's command protocol is Gallium's interface written down as words.** Every virgl command is a pipe_context call, and every virgl object is a Gallium CSO or resource. So when Warp3D and MiniGL emit virgl, they are already speaking Mesa's middle layer. **We don't need a new OpenGPU rendering API** (`OGPU_Draw`, `OGPU_SetPipeline`, `OGPU_CreateTexture` and the like): that would be a fourth way to say the same thing. We need one shared place that produces virgl, instead of three.

## 3. Where the duplication is now

1. **Three state trackers, built or planned.** Mesa (in GL.module) has a full one on the 68k. minigl.library does GL 1.1 transform, lighting and clipping on the 68k for its existing routes, and its virgl plan adds its own state cache, shader generator and arenas. Warp3D's virgl plan adds a translator with its own state keys, TGSI shaders and rings. TinyGL already did the right thing: it has no GL of its own and forwards to GL.module (`library/tinygl/README.md`). **TinyGL is the template.**
2. **Command encoders:** GL.module's winsys (`gla_virgl.c`), minigl's `mgl_vg_enc.c`, and Warp3D's planned `w3d_vg.c`.
3. **Fixed-function shader generators:** two planned, MiniGL's (plan section 3.4) and Warp3D's (environment by texture, chroma, specular, fog). They are the same job.
4. **Several software rasterisers.** The core in `ogpu_3d.c` runs on the 68k and on the host, and `host/vulkan/ogpu3d.comp` is a Vulkan version of it. Mesa's softpipe is the CPU fallback in GL.module and TinyGL, and llvmpipe is available on the host through virglrenderer. MiniGL's CPU route is a fourth.
5. **Several presents:** `ACV_PRESENT` and `ORTG_PresentVirgl` (GL.module, MiniGL, Warp3D), `OGPU_RetainedPresent`, ACRTG direct and `acvirgl_scanout`, and ACMedia's films on the display plane. Each knows about windows, rectangles and pixel formats on its own.
6. **Capabilities read in several places.** Each virgl user does its own `ACV_HELLO`, `ACV_CAPSET_INFO` and `ACV_CAPS`; Warp3D and MiniGL each report their own; `OGPU_Query` answers only for the stream ops.
7. **Back ends chosen in several places:** `ENV:Warp3D/Driver`, `ENV:MiniGL/Driver`, `ENV:TinyGL/Driver`, each with its own `Auto`.
8. **graphics.library's acceleration lives in gfopengpu** (`OGFX_BltBitMap`, `OGFX_RectFill`, text), with no written decision about where it belongs.

## 4. The proposal

### 4.1 Three layers, and who may call what

| Layer | What it is | Who calls it |
|---|---|---|
| **1. The public runtime** (gfopengpu.library's LVOs) | `OGPU_Query`, `OGPU_Submit`, `OGPU_Wait`, `OGPU_ModuleOpen`/`Close`, and contexts. **No draw, texture, shader or present calls.** | Front-end libraries; tools that measure |
| **2. The shared pipe** (inside gfopengpu, or a module it loads like `GL.module`) | State objects and their cache, the fixed-function shader generator, resources with renaming and reference counts, frame arenas, the two emitters (virgl and stream3d) | **Our own front-end libraries only.** An internal interface (conceptually Gallium's `pipe_context`), versioned (below) but not one programs may call. |
| **3. Back ends** | 68k core, virgl on the host GPU, virgl on llvmpipe, ACRTG.gpu core and its Vulkan version, future hardware | **gfopengpu only** |

And beside them, **OpenRTG**: surfaces on screen, windows, bitmaps, modes, composition, scanout, and the one present.

**The pipe's interface is versioned,** even though it is internal. MiniGL, Warp3D and later front ends are separate libraries that ship and update on their own, so the pipe they open may be older or newer than the one they were built against. Mesa can keep Gallium unversioned only because its drivers and front ends build in one tree. Ours don't. So the pipe is a table of calls with a version first (`struct OGPUPipeOps { ULONG version; ... }`, `OGPU_PIPE_VERSION 1`), handed out by gfopengpu to our own libraries only. A front end asks for the version it needs and gets NULL from an older pipe, and new calls are only ever added at the end. A front end that reaches past the table into gfopengpu's internals fails the CI check. **The table has no present:** presenting is OpenRTG's (rule 2).

### 4.2 The shared pipe: one state tracker for our fixed-function APIs

What it owns, and nothing else may:

- **State objects:** blend, depth-stencil-alpha, rasterizer, vertex layout and sampler, cached by key (MiniGL plan section 4, written once).
- **The fixed-function shader generator:** from the OpenGL 1.1 equations (texture environments over two units, fog, alpha, lighting, texgen, colour material), prebuilt for the common cases and generated for the rest (MiniGL plan section 3.4, written once and used by Warp3D too). It writes TGSI today, because that is what virglrenderer takes. Mesa itself has moved to NIR internally, so **the generator is the only code that knows we write TGSI**, and we can change it later without touching a front end.
- **Resources:** textures, mip chains, depth buffers, the rename-on-write rule, reference counts. A Warp3D or GL texture refers to a pipe resource and never frees it itself.
- **Frame arenas:** vertex, index and command rings, overflow without dropping geometry, the 2 ms cap per block.
- **The two emitters.** virgl (through `include/gfopengpu/virgl_cmd.h`, the Warp3D note's point 1) for every accelerated back end, and stream3d for the 68k core. A real Amiga with no host has no virgl renderer, and interpreting TGSI on a 68k would be far too slow, so both are needed. **The choice between them is made inside the pipe and nowhere else.** That way the doubling is tested once, on the same scenes, rather than once per API.

**Mesa is the exception, deliberately.** GL.module *is* Mesa, and Mesa brings its own state tracker, which is why we carry it. It keeps that. It shares the lower half: its virgl goes out only through `OGPU_Submit`, into the same arenas and the same transport rules, and its frames reach the screen only through `ORTG_Present`. The shared state tracker is for our fixed-function APIs: MiniGL, Warp3D, TinyGL's GL 1.x users, and graphics.library's 3D-shaped calls.

### 4.3 The front ends, made thin

| Front end | Keeps | Hands to the pipe |
|---|---|---|
| **minigl.library** | The ABI 5 table, GL 1.1 state, display lists, GLU, GLUT, the `mgl*` calls | State objects, shaders, resources, arenas, encoding |
| **Warp3D.library** | Its API, state definitions, lock and unlock, direct (immediate) mode, culling, points and lines as quads, Z and stencil read and write | State objects, shaders, the context's resources, encoding |
| **tinygl.library** | As now: it is GL.module | Nothing new |
| **GL.module (Mesa)** | All of Mesa | Transport, arenas, present |
| **OGLES3, EGL** | Contexts and surfaces | Through GL.module and OpenRTG |

**This is build, not migrate.** MiniGL's virgl route and Warp3D's virgl translator exist only as design notes. So the saving is not removing code from them: it is building MiniGL's virgl route on the shared pipe from its first commit, then pointing Warp3D at the same pipe, and never writing the second state cache, shader generator or encoder at all.

### 4.4 Back ends: chosen by gfopengpu, never by a library

| Back end | Where it runs | For |
|---|---|---|
| **virgl, real GPU** | the host's graphics chip through virglrenderer | Pi 5, CM4, x86 appliance: the normal route |
| **virgl, llvmpipe** | the host's CPUs | cards without a usable GPU; tests |
| **CPU core** (`ogpu_3d.c`) | the 68k | real Amigas, accelerators, anything with no host |
| **ACRTG.gpu core / Vulkan compute** | the host | today's default; to be measured against llvmpipe |

`ENV:Warp3D/Driver`, `ENV:MiniGL/Driver` and `ENV:TinyGL/Driver` become one `ENV:GFOpenGPU/Driver`, with the per-API variables kept only as overrides for old setups and tests. `Auto` (virgl when `OGPU_Query(OGPU_OP_VIRGL)` says full and the host reports a real GPU) is decided in one place.

**The rasteriser question.** On a host, llvmpipe inside virglrenderer does what our host core and its Vulkan version do, and Mesa maintains it. Measure both on the Pi 5 and the x86. If llvmpipe matches, the host copy of the core and `ogpu3d.comp` can go, and the core lives on only for the 68k, as the reference for what each stream command means.

### 4.5 OpenRTG owns every pixel that reaches a screen

- **`ORTG_Surface` is the only thing presentation takes.** A virgl colour target, a texture, a retained surface, a framebuffer or an ACMedia frame becomes an `ORTG_Surface` first (a handle that names the pixels, their format and where they live: host GPU, host memory or Amiga memory), and presentation never accepts any of those things directly. That stops each new source growing its own present.
- **One present, `ORTG_Present(surface, target)`,** growing out of `ORTG_PresentVirgl`. The surface goes into a window's visible pieces, a screen or a bitmap. Format conversion (XRGB32, ARGB32, RGB565), flipping and the rectangle walk happen there and nowhere else. **There is no `OGPU_Present`:** a surface may be a pipe resource, but only OpenRTG puts it on a screen. A present in gfopengpu as well would be a sixth.
- **Films are surfaces too.** ACMedia keeps decoding on the host; its output frame becomes a surface handle that `ORTG_Present` (or the display plane, when a film fills a screen) takes.
- **P96 and CyberGraphX stay stubs over OpenRTG.** uaegfx's compatibility is already in gfopenrtg (`uaegfx.c`).
- **graphics.library: one owner, written down.** Its acceleration stays in gfopengpu (the OGFX calls), because those calls emit stream commands; anything it needs on screen goes through OpenRTG. No second patch set anywhere else.

### 4.6 Capabilities: two levels, one source

Two levels, kept apart on purpose:

- **What the hardware can do** (gfopengpu, once): `OGPU_Query(op, format)` answers "full, partial or none" for each operation and format, and gfopengpu reads the host's virgl caps once (`ACV_CAPSET_INFO`/`ACV_CAPS`) and answers for them through it. This is the single source of truth, and stronger than a fixed struct of yes/no fields.
- **What each API reports** (each front end, from that one answer, by its own fixed rules). Example: the host has 8 texture units. Warp3D and MiniGL go on reporting 2, as old programs expect, and GL.module reports 8. Each front end's rules are written down beside its code and tested, so a new back end can't quietly change what an old program sees.

### 4.7 Resources: handles in the stream, not a call each

Making, updating and freeing resources are commands inside a submitted batch, as virgl does (`ACV_RES_CREATE`, `ACV_PUT`, unref). One library call per texture or a Map/Unmap per update would cross the bridge to the host every time. Map/Unmap make sense only on the 68k core.

### 4.8 Who owns what

| Component | Owns API state | Owns GPU resources | Encodes virgl | Presents |
|---|---|---|---|---|
| minigl.library | its GL 1.1 state | no (refers to pipe resources) | no | no |
| Warp3D.library | its Warp3D state | no (refers to pipe resources) | no | no |
| tinygl.library | none (it is GL.module) | no | no | no |
| GL.module (Mesa) | Mesa's | Mesa's, through its winsys | yes, Mesa's own virgl driver | no |
| The shared pipe | none (cached GPU state only) | yes, with reference counts | yes, through `virgl_cmd.h` | no |
| gfopengpu (runtime) | none | no | no | no; it owns back ends and capability discovery |
| OpenRTG | display state | `ORTG_Surface`s | no | **yes, the only one** |

**One owner for every object.** A MiniGL or Warp3D texture refers to a pipe resource; the pipe resource refers to a virgl resource (or the 68k core's memory). Only the pipe frees either. A front end's destroy call drops its reference, and nothing else.

## 5. Order of work (proposed)

1. **The three rules in CI.** A check that no library under `library/` other than gfopengpu and gfopenrtg opens ACRTG; that virgl words are written only through `virgl_cmd.h` or by GL.module's Mesa driver; that stream3d 3D ops are emitted only by the pipe; that nothing presents except through gfopenrtg; and that no front end keeps a state cache or shader generator of its own (so there is never a third state tracker beside Mesa's and the pipe's). This stops new duplication starting today.
2. **`virgl_cmd.h`** (the Warp3D note's point 1). GL.module's winsys and minigl's existing encoder pieces use it. Proof: the same pixels and counters in `tests/run_minigl.sh` with `ACVIRGL_LIB`.
3. **The shared pipe, with MiniGL's virgl route as its first user,** built from MiniGL plan sections 3 to 6, with both emitters and the versioned table from the start.
4. **Warp3D onto the pipe** instead of its own translator. Proof: `test_w3d_vg.c`'s scenes against the 68k core, within 8/255 per channel.
5. **`ORTG_Surface` and one `ORTG_Present`,** then retained surfaces and ACMedia frames moved onto it.
6. **Capabilities through `OGPU_Query`** for virgl, each front end's reporting rules written down and tested, then the per-API variables folded into `ENV:GFOpenGPU/Driver`.
7. **The rasteriser measurement** (llvmpipe against the host core), and the retirement it allows.

## 6. Tests

Kept from the outside review's matrix: screens (open and close, modes, palette and truecolour, several monitors), bitmaps, Warp3D (contexts, textures, Z, blending, multitexture, render to texture), GL (gears, texture stress, immediate mode), and films (1080p and 4K H.264, HEVC, several streams, presentation without copies).

Changed:
- **No commercial games in repos or CI.** Quake, Quake II, BlitzQuake and RTCW need game data we may not ship, so they run only in labs on Dale's word. CI uses our own tests (`w3dcheck`, `w3darrbench`, `scenes3d.c`, `tests/run_minigl.sh`, MGLTest), Mesa's demos and gears.
- **Both emitters against one reference.** Each scene goes through the pipe to stream3d on the 68k core and to virgl, compared with the Warp3D note's tolerance (8/255 per channel, one-pixel edge shift). Because only the pipe emits, this is one test set for every front end.
- **Reported capabilities:** for each front end, what it reports on each back end, against its written rules.
- **Deadlocks:** a test that holds a screen lock, a Warp3D lock and a GL context at once, on each back end.

## 7. Risks, as they apply here

- **Hidden hardware access:** the CI check in step 1.
- **Who owns a resource:** one owner for every object (4.8); front ends never free pipe resources themselves.
- **Locking:** one pipe lock in place of several, which is why the deadlock test exists.
- **Capabilities drifting:** hardware and reported capabilities are kept apart (4.6), and each front end's reporting rules are tested.
- **Back ends multiplying:** every back end is permanent maintenance. Where two solve the same problem (llvmpipe and our host core), the measurement in step 7 decides, and the weaker one goes.
- **Migration risk:** medium, and lower than it looks, because the two biggest pieces (MiniGL's and Warp3D's virgl routes) are built on the pipe rather than moved onto it.

## 8. Compared with Mesa

| | Mesa | Ours after this |
|---|---|---|
| One driver model | yes (Gallium) | yes (gfopengpu back ends) |
| One middle layer | Gallium pipe | the shared pipe, emitting virgl or stream3d, versioned because our front ends ship apart |
| One fixed-function shader generator | yes (st/mesa) | yes (the pipe) |
| One capability source | pipe caps | `OGPU_Query`, with each API's reporting rules on top |
| One present | per window system | `ORTG_Present`, from `ORTG_Surface`s only |
| Warp3D, MiniGL, TinyGL, P96, CyberGraphX | no | yes, as thin front ends |

Our list of APIs is finite and known: Warp3D, MiniGL, TinyGL, GL, GLES and EGL, graphics.library, P96, CyberGraphX, and ACMedia's films. That is why this can be simpler than Mesa while staying GFOS's own, not "Mesa on Amiga". What keeps it simple is the three rules, checked in CI from step 1: once the shared pipe exists, nothing new is allowed to go around it.
