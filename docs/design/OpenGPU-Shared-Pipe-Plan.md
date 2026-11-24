# OpenGPU shared pipe: detailed design, migration and build plan

Team 5, 23 Nov 2026. This is the working plan behind `docs/design/OpenGPU-Shared-Pipe.md` (the principles, rules and ownership matrix). That note says what the system should look like; this document says exactly what gets built, how today's code moves into it, in what order, how each step is proven, and how it is built and shipped.

Written from gfopenrtg's main at a6cd53e. It is a proposal: nothing here is built yet.

It also takes in the consolidation checklist and the review of that checklist (Dale, 01:41). Every item of the checklist is in Appendix A, against the stage that delivers it. Where the checklist's order differs from this plan, Appendix A says why.

---

## 0. A correction to the design note

The design note says MiniGL's and Warp3D's virgl routes "exist only as plans". **That is wrong.** Both are on main:

- MiniGL's route is `library/minigl/mgl_vg*.c`, about 115 KB across 8 files.
- Warp3D's route is `library/warp3d/w3d_vg*.c`, about 82 KB across 4 files.
- They already share `library/pipe/virgl_cmd.h`: the protocol numbers and the command writers.

So the shared pipe is not new code written alongside the old. It is **extracted** from code that works today, one owner at a time, and every step is proven against the pixels and counters the old code gives. The note is corrected in the same pull request as this plan.

---

## 1. Where we are

### 1.1 What exists, by layer

| Layer | Code on main | Notes |
|---|---|---|
| Runtime | `library/opengpu/opengpu_lib.c` (53 KB), `ogpu_core.c` (163 KB), `ogpu_module.c`, `ogpu_retained.c`, `ogpu_build*.c` | gfopengpu.library 0.25. LVOs: `OGPU_Query`, `OGPU_BackEndName`, `OGPU_Submit`, `OGPU_Wait`, `OGPU_ModuleOpen`/`Close`, OGFX, `OGPU_Retained*` |
| 68k 3D core | `library/opengpu/ogpu_3d.c` (82 KB) | Reads stream3d (`include/gfopengpu/stream3d.h`), the reference for what each command means |
| Host 3D | `host/vulkan/ogpu3d*.comp`, `ogpu3d_rec.c` (41 KB), `ogpu3d_spv.h` (289 KB); the ACRTG runtime's own core lives in the amicradle repo (`native/acrtg.c`) | Two host renderers of stream3d |
| virgl on the host | `host/virgl/acvirgl.c` (117 KB), `include/gfopengpu/virgl.h` (the ACV protocol) | `ACV_HELLO` … `ACV_PRESENT` (with FLIP_Y, SCALE) |
| Shared virgl writers | `library/pipe/virgl_cmd.h` (12 KB) | Protocol numbers, byte order, stateless writers, request-block helpers |
| MiniGL | `library/minigl/*` (minigl.library 29, ABI 5, 205 entries) | Two routes: its own 68k transform and lighting feeding stream3d, and `mgl_vg*` (virgl). Choice in `mgl_choose.c` |
| Warp3D | `library/warp3d/*` (Warp3D.library 5.0) | `w3d_batch.c` builds stream3d. `w3d_vg.c` translates finished batches to virgl |
| TinyGL | `library/tinygl/*` | Forwards to GL.module |
| GL.module | `library/modules/gl/` (Mesa + `gla/`) | Mesa's own virgl driver. Winsys `gla_virgl.c` / `gla_virgl_os3.c`; present `gla_present_os3.c` |
| OGLES3, EGL | `library/ogles3`, `library/egl` | On GL.module |
| SDL2, SDL3 | `library/modules/sdl2`, `sdl3` | An OpenGPU 2D renderer (`0004-opengpu-renderer.patch`, about 100 KB each) and their own presents (`SDL_os3gpu.c`) |
| OpenRTG | `library/openrtg_lib.c`, `uaegfx.c`, `compat/cybergraphics_lib.c` | gfopenrtg.library revision 29. `ORTG_PresentVirgl` (0.15), `ORTG_ShowInstead`/`ShownNow` (0.28), SCALE (0.29) |
| ACMedia | `library/hostlib/acmedia/` | Decode on the host |

### 1.2 The duplication, function by function

| Function | MiniGL (`mgl_vg*`) | Warp3D (`w3d_vg*`) | GL.module (`gla/`) | Elsewhere |
|---|---|---|---|---|
| virgl command writers | `virgl_cmd.h` (shared) | `virgl_cmd.h` (shared) | Mesa's own (allowed) | — |
| Request-block assembly | `mgl_vg_enc.c` (`vg_pre`, `vg_blk_cmd`, `vg_run`) | inside `w3d_vg.c` | `gla_virgl.c` | — |
| Transport (submit, wait, the "expired fence" rule) | `mgl_vg_os3.c` `vg_os_run` | `w3d_vg_os3.c` `os_run` | `gla_virgl_os3.c` | **3 copies of the same 15 lines** |
| HELLO, caps | `mgl_vg_frame.c` | `w3dvg_open` | `gla_virgl.c` | `acvirgl.c` answers |
| State-object caches | `mgl_vg_state.c` (`vg_cache`, 64 per kind, 5-word keys) | inside `w3d_vg.c` | Mesa | — |
| Fixed-function shaders (TGSI) | `mgl_vg_shader.c` (VS_/FS_ keys: lighting, texgen, colour material, 2 units, fog by eye distance) | `w3d_vg_shader.c` (VK_/FK_ keys: 16.16 positions, fog by w, chroma, specular, linear interpolation) | Mesa | — |
| Resources, renaming, dirty boxes | `mgl_vg_tex.c`, `mgl_vg_frame.c` (`vg_res`) | `w3d_vg.c` (textures keyed by level-0 address) | Mesa | — |
| Vertex and index rings | `mgl_vg_frame.c` (`VG_RING` 4) | `w3d_vg.c` (rings, same lesson) | Mesa | — |
| stream3d emission | MiniGL's 68k route | `w3d_batch.c` | — | — |
| Present | `mgl_vg_os3.c`: ACV_PRESENT in the frame's block for screens and bitmaps, `ORTG_PresentVirgl` for windows, readback + `WritePixelArray` | `w3d_vg.c`: ACV_PRESENT in the sync block, bitmap locked by `w3d_batch.c`, ARGB copy / `WritePixelArray` | `gla_present_os3.c`: `ORTG_PresentVirgl`; SURFACE+PIXELS batches with 4 pending slots; `WritePixelArray` | `OGPU_RetainedPresent`; SDL2/SDL3 `SDL_os3gpu.c`; ACMedia |
| Driver choice | `ENV:MiniGL/Driver` (`mgl_choose.c`) | `ENV:Warp3D/Driver` | `ENV:TinyGL/Driver` | — |
| 3D rasterisers | — | — | llvmpipe (through virglrenderer) | `ogpu_3d.c`; the ACRTG host core; Vulkan `ogpu3d.comp`; MiniGL's 68k route |

In short: **two state trackers below Mesa, two shader generators, two resource managers, three transports and six presents.** The plan takes that to one of each, Mesa aside.

---

## 2. Detailed design

### 2.1 Where the pipe lives (decision D1)

The pipe is a new directory, `library/pipe/`.

- **Stages M2 to M9: a static library**, `libogpupipe.a`. It is linked into minigl.library and Warp3D.library, with a host build (`VG_HOST`) for the PC tests. There is one copy of the source and one owner, enforced by CI. There is no new binary ABI while the code moves, which removes the "older pipe meets newer library" risk during the migration.
- **Stage M10 (optional): `LIBS:GFOpenGPU/Pipe.module`.** This is a shared module through the existing `OGPU_ModuleOpen` ABI (`gfopengpu/module.h`: A4 residency, one code copy). It only relinks: the front ends already call it through `struct OGPUPipeOps` from M2, so nothing else changes. Worth doing only if the duplicated code in RAM (about 100 KB a library) matters on 68k machines; the measurement in M10 decides.

The table is used from the first commit, even while the code is linked statically. Both libraries reach the pipe only through `ogpu_pipe_ops()`, never through its internals, so going to a module later is mechanical.

### 2.2 Source layout and where each file comes from

| New file | What it owns | Extracted from |
|---|---|---|
| `include/gfopengpu/pipe.h` | `struct OGPUPipeOps`, keys, handles, constants (internal header: our libraries only) | new; keys from `mgl_vg.h` and `w3d_vg.h` |
| `library/pipe/pipe_screen.c` | One per library open: the emitter, context ids; host flags and caps read from `OGPU_Query` (from MQ; until then the HELLO and CAPS moved here) | `mgl_vg_frame.c` (`mgl_vg_open`), `w3dvg_open` |
| `library/pipe/pipe_xport_os3.c` | The transport on AmigaOS: one `OGPU_OP_VIRGL` per block, `OGPU_Submit`, `OGPU_Wait`, the expired-fence rule | `mgl_vg_os3.c` `vg_os_run`, `w3d_vg_os3.c` `os_run` (identical) |
| `library/pipe/pipe_xport_host.c` | The PC tests' transport, straight into virglrenderer | `tests/minigl_vg_bridge.c` |
| `library/pipe/pipe_block.c` | A frame's block: pre-commands (creates, PUTs), the little-endian stream, post-commands (unrefs), growth, send | `mgl_vg_enc.c` (`vg_cmd`, `vg_pre`, `vg_run`, `vg_result`), `w3d_vg.c`'s block code |
| `library/pipe/pipe_res.c` | Resources: create, rename-on-write, dirty boxes per level, `ACV_PUT`/`ACV_GET` with swap units, deferred unref, reference counts | `mgl_vg_tex.c`, `vg_res_*` in `mgl_vg_frame.c`, `w3d_vg.c`'s textures and Z/stencil buffers |
| `library/pipe/pipe_arena.c` | Vertex and index rings (never overwrite what an earlier block's draws read), the ~2 ms block budget, growth at frame start | `mgl_vg_frame.c` (`vg_need`, `vg_ring`), `w3d_vg.c` |
| `library/pipe/pipe_state.c` | State objects by key: blend, DSA, rasterizer, sampler, vertex elements; per-context caches | `mgl_vg_state.c`, `w3d_vg.c`'s caches |
| `library/pipe/pipe_ctx.c` | The pipe context: bound state, dirty bits, "everything a draw needs, sent" | `vg_prepare`, `vg_state_reset`, `struct mgl_vg`'s bound fields |
| `library/pipe/pipe_ff.c` | The fixed-function generator: one key (2.4) to TGSI text | `mgl_vg_shader.c` + `w3d_vg_shader.c`, merged |
| `library/pipe/pipe_ff_prebuilt.c` | Generated at build time: the common keys' TGSI as constant text | new (generator run on the host) |
| `library/pipe/pipe_emit_virgl.c` | Draws, clears, constants, bindings in virgl, through `virgl_cmd.h` | `mgl_vg_draw.c`'s emission, `w3d_vg.c`'s draw merging |
| `library/pipe/pipe_emit_s3d.c` | The same calls as stream3d for the 68k core and the GFOpenGPU stream route | `w3d_batch.c`'s builder |
| `library/pipe/pipe_trace.c` | Counters and `ENV:GFOpenGPU/Trace` | `struct mgl_vg` stats, `w3dvg_stats` |

What stays in the front ends:

- **MiniGL:** GL state, matrices, display lists, GLU, GLUT, its 68k transform and lighting for the stream3d route, and an adapter of about 1,000 lines (`mgl_pipe_adapt.c`) turning GL state into pipe calls.
- **Warp3D:** all of its API and state, vertex conversion to `ovtx`, culling, points and lines as quads, locks and sync points, Z/stencil read and write calls, and an adapter (`w3d_pipe_adapt.c`).

### 2.3 Core structures

The calls follow Gallium's shape: **create a state object once, bind it many times.** Dale's API sketch (01:43) is folded in here; 2.13 lists what was taken from it, what was changed, and why.

```c
/* include/gfopengpu/pipe.h  (sketch: names final, fields indicative) */
#define OGPU_PIPE_VERSION 1

typedef struct ogpu_pipe_screen OGPUPipeScreen;   /* one per library open */
typedef struct ogpu_pipe_ctx    OGPUPipeCtx;      /* one per API context: every call names it */
typedef struct ogpu_pipe_res    OGPUPipeRes;      /* a GPU-visible resource; its handle never changes */
typedef struct ogpu_pipe_cso    OGPUPipeCSO;      /* a state object: blend, DSA, rasterizer, sampler, layout */
typedef struct ogpu_pipe_shader OGPUPipeShader;   /* a fixed-function shader pair */

enum { OGPU_EMIT_VIRGL = 1, OGPU_EMIT_S3D = 2 };

struct OGPUPipeInfo {             /* what the screen found */
    ULONG emit;                   /* OGPU_EMIT_ */
    ULONG host_flags;             /* ACV_HOST_ */
    ULONG caps_bits, prim_mask;
    ULONG max_tex_size, max_tex_units;   /* host caps; front ends report their own */
};

struct OGPUBox { LONG x, y, z; ULONG w, h, d; };

struct OGPUPipeResDesc {
    ULONG target, format, bind;   /* virgl's numbers (virgl_cmd.h) */
    ULONG width, height, levels;
    ULONG flags;                  /* OGPU_RES_STREAM: rewritten every frame (rings, not renames) */
};

struct OGPUDrawInfo {
    ULONG prim, first, count;
    ULONG indexed;                /* 0, or 16 for 16-bit indices from the index arena */
    LONG  bias;
};

struct OGPUPipeOps {
    ULONG version;                /* first, always */

    /* screen: HELLO, caps, emitter, once per library open */
    OGPUPipeScreen *(*screen_open)(ULONG emit_wanted, struct OGPUPipeInfo *info);
    void  (*screen_close)(OGPUPipeScreen *);

    /* context: explicit on every call (no "current" context: libraries are shared by tasks) */
    OGPUPipeCtx *(*ctx_create)(OGPUPipeScreen *, ULONG flags);
    void  (*ctx_destroy)(OGPUPipeCtx *);

    /* resources: the only GPU-visible allocation there is */
    OGPUPipeRes *(*res_create)(OGPUPipeCtx *, const struct OGPUPipeResDesc *);
    void  (*res_update)(OGPUPipeCtx *, OGPUPipeRes *, ULONG level, const struct OGPUBox *, const void *src, ULONG stride, ULONG swap);
          /* renames inside when an earlier block still reads it: the handle stays the same */
    void  (*res_dirty)(OGPUPipeCtx *, OGPUPipeRes *, ULONG level, const struct OGPUBox *);  /* texels changed in Amiga memory */
    LONG  (*res_read)(OGPUPipeCtx *, OGPUPipeRes *, ULONG level, const struct OGPUBox *, void *dst, ULONG stride);
    void  (*res_release)(OGPUPipeCtx *, OGPUPipeRes *);   /* refcounted; freed after the last block that used it */

    /* state objects: made or found by key, refcounted; front ends never free what is under them */
    OGPUPipeCSO *(*blend_create)(OGPUPipeCtx *, const struct OGPUBlendKey *);
    OGPUPipeCSO *(*dsa_create)(OGPUPipeCtx *, const struct OGPUDsaKey *);       /* depth, stencil, alpha test */
    OGPUPipeCSO *(*raster_create)(OGPUPipeCtx *, const struct OGPURasterKey *);
    OGPUPipeCSO *(*sampler_create)(OGPUPipeCtx *, const struct OGPUSamplerKey *);
    OGPUPipeCSO *(*layout_create)(OGPUPipeCtx *, const struct OGPULayout *);
    void  (*cso_release)(OGPUPipeCtx *, OGPUPipeCSO *);

    /* the one fixed-function generator (2.4): made, prebuilt or cached by key */
    OGPUPipeShader *(*ff_shader)(OGPUPipeCtx *, const struct OGPUFFKey *);

    /* binding: the pipe context owns what is bound */
    void  (*bind_blend)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_dsa)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_raster)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_layout)(OGPUPipeCtx *, OGPUPipeCSO *);
    void  (*bind_shader)(OGPUPipeCtx *, OGPUPipeShader *);
    void  (*bind_framebuffer)(OGPUPipeCtx *, OGPUPipeRes *colour, OGPUPipeRes *depth);
    void  (*bind_texture)(OGPUPipeCtx *, ULONG unit, OGPUPipeRes *, OGPUPipeCSO *sampler);
    void  (*set_constants)(OGPUPipeCtx *, ULONG stage, const float *v, ULONG vec4s);
    void  (*set_viewport)(OGPUPipeCtx *, const float v6[6]);
    void  (*set_scissor)(OGPUPipeCtx *, const struct OGPUBox *);   /* NULL: off */
    void  (*set_blend_colour)(OGPUPipeCtx *, const float rgba[4]);
    void  (*set_stencil_ref)(OGPUPipeCtx *, ULONG front, ULONG back);

    /* geometry: the arenas */
    void *(*vtx_room)(OGPUPipeCtx *, ULONG bytes, ULONG *first);
    UWORD *(*idx_room)(OGPUPipeCtx *, ULONG count, ULONG *first);
    ULONG (*vtx_put_range)(OGPUPipeCtx *, const void *mem, ULONG bytes);  /* Warp3D: a batch's vertex memory in one PUT */

    /* frame */
    void  (*begin_frame)(OGPUPipeCtx *);
    void  (*draw)(OGPUPipeCtx *, const struct OGPUDrawInfo *);
    void  (*clear)(OGPUPipeCtx *, ULONG buffers, const float rgba[4], double depth, ULONG stencil);
    LONG  (*end_frame)(OGPUPipeCtx *, const struct OGPUPresentHook *hook, ULONG *fence);
          /* sends the frame; the hook (OpenRTG's) puts the present in the same block; fence for OGPU_Wait */
    LONG  (*flush)(OGPUPipeCtx *, ULONG *fence);   /* send what there is, no present (glFlush, a full batch) */
    void  (*stats)(OGPUPipeCtx *, struct OGPUPipeStats *);

    /* new calls only ever added here, at the end */
};

const struct OGPUPipeOps *ogpu_pipe_ops(ULONG version);  /* NULL when the pipe is older */
```

Fences are gfopengpu's own: the `ULONG` from the pipe's submits, waited for with the existing `OGPU_Wait`, `OGPU_FENCE_ALL` and the expired-fence rule. There is no second fence type.

`struct OGPUPresentHook` is how the frame's present rides in the frame's own block without the pipe knowing anything about screens (2.7):

```c
struct OGPUPresentHook {
    ULONG (*prepare)(void *user, ULONG ctx, ULONG handle, ULONG *p, ULONG room); /* words written, 0: none */
    void  (*finish)(void *user, const ULONG *blk, ULONG at);                     /* after the block ran */
    void  *user;
};
```

OpenRTG supplies the hook (`ORTG_Present` with `ORTG_PRESENT_PREPARE`, then `ORTG_PresentDone`, 2.7), so the bitmap lock, the rectangle walk and the format choice stay in OpenRTG. The pipe only reserves the words.

### 2.4 One fixed-function key

The two generators have different keys, but the features overlap almost completely. The unified key is 64 bits, in two words.

**Word 0 (vertex side):**

| Bits | Field | MiniGL today | Warp3D today |
|---|---|---|---|
| 0–1 | position input: 0 object xyzw float, 1 screen 16.16 + z u32 + w float (`ovtx`) | 0 | 1 (`VK_Z`, `VK_W`) |
| 2 | perspective by 1/w (clip w = 1/w) | — | `VK_PERSP` |
| 3 | colour from the vertex (else constant) | `VS_COL_ATTR` | `FK_GOURAUD` |
| 4–5 | colour byte order (RGBA, ARGB, BGRA) | `VS_COL_SWZ` | BGRA fixed |
| 6 | specular + fog from the vertex | — | `VK_SPEC` |
| 7–12 | unit 0 and 1: has coords, from vertex, through matrix | `VS_TC0*`, `VS_TC1*` | unit 0 from vertex |
| 13–16 | texgen unit 0, unit 1 (none, object, eye, sphere) | `VS_GEN0`, `VS_GEN1` | — |
| 17 | lighting | `VS_LIGHT` | — |
| 18 | normal from the vertex | `VS_NRM_ATTR` | — |
| 19–22 | light count | `VS_NLIGHTS` | — |
| 23–26 | colour material on, mode | `VS_COLMAT*` | — |
| 27 | local viewer | `VS_LOCALV` | — |
| 28 | normalise | `VS_NORMALIZE` | — |
| 29 | positional or spot light | `VS_LIGHT_POS` | — |
| 30 | linear (noperspective) interpolation of colours and fog | — | `VK_LINEAR` / `FK_LINEAR` |
| 31 | fog coordinate: 0 eye distance, 1 vertex w | 0 | 1 |

The spot-light mask (8 bits) stays a second argument, as in `vg_vs_text` today.

**Word 1 (fragment side):**

| Bits | Field | MiniGL today | Warp3D today |
|---|---|---|---|
| 0–2 | unit 0 environment | `FS_ENV0` (GL's) | `FK_ENV` (`OGPU_ENV_`) |
| 3–5 | unit 1 environment | `FS_ENV1` | — |
| 6–9 | unit 0 and 1 texture base (has RGB, has A) | `FS_RGB*`, `FS_A*` | `FK_BASE` (`OGPU_TEXBASE_`) |
| 10–12 | fog mode (none, linear, exp, exp2, linear step) | `FS_FOG` | `FK_FOG`, `FK_FOGEQ` |
| 13 | specular added | — | `FK_SPEC` |
| 14–15 | chroma key (off, keep range, drop range) | — | `FK_CHROMA` |
| 16–31 | reserved | | |

The environments are mapped to one enum. GL's MODULATE, REPLACE, DECAL, BLEND and ADD cover Warp3D's `OGPU_ENV_` values. Where Warp3D's meaning differs (its DECAL on an alpha texture), the mapping is a table checked in M5, not a guess.

**Constants** are one layout per stage. The vertex side keeps MiniGL's `VSC_*` slots and adds Warp3D's `VC_POS` and `VC_MISC` at the end. The fragment side merges `FSC_*` and `FC_*` (`FC_CLO` and `FC_CHI` for chroma).

**Prebuilt set:** about 40 keys covering MGLTest, the classic MiniGL demos, BlitzQuake, Quake via MiniGL, and W3DTest. They are listed from the M0 trace captures. The generator runs on the host at build time and writes `pipe_ff_prebuilt.c`, and only other keys are generated on the 68k. Its size goes in the M5 measurement.

### 2.5 A frame's life in the pipe

1. The front end makes its state objects once (`*_create`, found by key when they exist, created in the block's pre-commands when not) and binds them (`bind_*`). The pipe context records what is bound and marks it dirty. Nothing is emitted yet.
2. On `draw`, the context writes the dirty bindings (stream), uploads dirty texture boxes (pre-commands, `ACV_PUT`) and writes the draw.
3. When the arena, stream or block budget runs out, the block is sent **without a present** and the context's bound state is kept, so it carries on.
4. At a sync point the front end calls `end_frame(hook)`. The hook (OpenRTG's) writes `ACV_PRESENT` into the same block. The block runs, and the hook's `finish` unlocks and counts.
5. Unrefs of renamed or released resources go after the stream. They are freed only after the block that last used them has run.

The two front ends' sync rules are unchanged: GL's swap and finish for MiniGL; UnLockHardware, Flush, FlushFrame, WaitIdle, SetDrawRegion, DestroyContext and direct mode for Warp3D.

### 2.6 Two emitters and two vertex modes

- **virgl emitter:** everything above.
- **stream3d emitter:** the same calls written as stream3d (RENDER3D, TEXENV, TEXTURE, DEPTH, CLIP, TRIANGLES, FILL, CLEAR3D). This is the inverse of the table in `Warp3D-Virgl-Route.md`, for the 68k core and the GFOpenGPU stream route.
  - stream3d has no shaders, so it only takes **screen-space vertices** (position mode 1, `ovtx`).
  - Fixed-function features it can't express (lighting, texgen, a second unit) are refused with an error, never silently dropped.
  - MiniGL on the stream3d route keeps its 68k transform and lighting and hands the pipe screen-space vertices, as it does today.
- The emitter is fixed for a context's life. The driver choice (2.9) picks it at `ctx_create`, so it is decided in one place and tested once for every front end.

### 2.7 OpenRTG presentation

gfopenrtg.library 0.30 adds two LVOs at the end of the table:

```c
struct ORTG_PresentInfo {
    ULONG size;                   /* sizeof: the struct only grows at the end */
    /* the source */
    ULONG kind;                   /* ORTG_SURF_VIRGL, ORTG_SURF_MEMORY, ORTG_SURF_RETAINED, ORTG_SURF_MEDIA, ORTG_SURF_BITMAP */
    ULONG context, handle;        /* VIRGL: the ACVirgl context and resource (a pipe resource's handle) */
    APTR  memory;                 /* MEMORY: row 0; RETAINED: struct OGPURetained *; MEDIA: the decoder's frame; BITMAP: struct BitMap * */
    ULONG stride, format;         /* MEMORY: OGPU_FMT_ */
    UWORD width, height;          /* of the source */
    /* the target: a RastPort covers a window (its layers), a screen and a bare bitmap alike */
    struct RastPort *rp;
    LONG  x, y;                   /* where in it */
    struct Rectangle *src;        /* optional: the part of the source (damage); NULL: all */
    struct Rectangle *dst;        /* optional: a box at another size: scaled by the host (ACV_PRESENT_SCALE) */
    ULONG flags;                  /* ORTG_PRESENT_FLIP_Y, _VSYNC, _ASYNC (fence out), _PREPARE (into blk) */
    ULONG *fence;                 /* _ASYNC: the batch's fence, for OGPU_Wait */
    ULONG *blk; ULONG room;       /* _PREPARE: write the present into this request block */
    APTR  token;                  /* _PREPARE: out, for ORTG_PresentDone */
};

LONG ORTG_Present(struct ORTG_PresentInfo *info);   /* the one present: pixels shown, words written (_PREPARE), or -1 */
VOID ORTG_PresentDone(APTR token, const ULONG *blk, ULONG at);   /* _PREPARE: after the block ran */
```

- **`ORTG_Present`** is the single entry point, as the API sketch (01:43) asks: one struct, versioned by its size.
- **`ORTG_PRESENT_PREPARE`** lets a pipe frame carry its present in its own block (2.3's hook calls `ORTG_Present` with it, and `ORTG_PresentDone` after the block), so moving the present into OpenRTG costs no extra round trip. Today's MiniGL screen path and Warp3D's sync path already work this way, and they keep that speed.
- **Async presents** (today's pending slots in `gla_present_os3.c`) return their fence; the caller waits with `OGPU_Wait`, and OpenRTG settles them itself at its hazards (the CPU on the bitmap, a free).
- **Routes inside OpenRTG** (in this order, decided per call):

| Surface | Target | Route | From today's |
|---|---|---|---|
| VIRGL | Window, screen or bitmap, ARGB32 or RGB565, host has `ACV_HOST_PRESENT` | `ACV_PRESENT`, with the visible rectangles (64 a command, more commands for more) and SCALE when asked | `ORTG_PresentVirgl`, `mgl_vg_os3.c` `vg_os_present`, `w3d_vg.c` |
| VIRGL | Anything else | `ACV_GET` into a bounce buffer, then as MEMORY | `present_readback`, Warp3D's ARGB copy |
| MEMORY | ARGB32 or RGB565 RTG bitmap, gfopengpu present | SURFACE + PIXELS + FENCE batch, async with pending slots, settled at hazards | `gla_present_os3.c` `present_gpu` |
| MEMORY | Anything else | `ORTG_WritePixels` (the CPU route, clipped by layers) | `WritePixelArray` fallbacks everywhere |
| RETAINED | Any | The retained present, moved from gfopengpu | `OGPU_RetainedPresent` (kept as a wrapper) |
| MEDIA | Full screen | ACMedia's display plane | ACMedia |
| MEDIA | Window | As VIRGL or MEMORY, depending on where the frame lives | ACMedia |

- `ORTG_PresentVirgl` and `OGPU_RetainedPresent` stay for ever as small wrappers (they are public LVOs). New code may not call them, and CI checks that.
- The `GLAPresent` variable and the `ORTG_STAT_VIRGL` count move into `ORTG_DrawStats`, with one stat per route.

### 2.8 Capabilities: `OGPU_Query`, the one authority

From the capability proposal and its review (01:44, 01:45), capabilities are a pillar alongside the pipe and `ORTG_Present`, with a workstream of their own (stage MQ). `OGPU_Query` is the only place any library learns what the machine can do.

**What exists, and must not break.** `OGPU_Query(op, format)` is public since gfopengpu.library 0.1, and programs use it:

- **`op` is a stream opcode today:** "is this stream command done for this format", for example `OGPU_OP_COMPOSITE` and the 3D ones in `stream3d.h` (`OGPU_OP_TRIANGLES` is 0x0030).
- **The answer word is packed:**
  - bits 0–7: `OGPU_ANSWER`, which is `OGPU_FULL`, `OGPU_PARTIAL` or `OGPU_NONE`;
  - bits 8–15: `OGPU_BACKEND`;
  - bit 16: `OGPU_ONGPU`.
- **Formats** are `stream.h`'s `OGPU_FMT_` numbers (CLUT8 1, RGB565 2, ARGB32 3, INDEX8 4, A8 5, XRGB32 from v1.3).

So the proposal is taken in with four changes, none of which alter its meaning:

| Proposal | Here | Why |
|---|---|---|
| `OGPU_CAP_FULL`, `_PARTIAL`, `_NONE` | The existing `OGPU_FULL`, `OGPU_PARTIAL`, `OGPU_NONE`, same values; `OGPU_CAP_*` defined as aliases | Programs already compare with them |
| New `OGPU_OP_*` capability ops | The same names in a range reserved for capabilities, **0x8000–0x80FF**, which no stream command may use (a test checks `stream.h` and `stream3d.h`) | `op` is a stream opcode today, so the new ops can't share numbers with commands |
| `OGPU_FORMAT_*` | `OGPU_FMT_*`: the existing numbers kept, the new ones (RGB555, RGBA4444, RGBA5551, L8, LA88, Z16, Z24, Z32, Z24S8, S8) appended | One format namespace, already in the stream |
| A limit in the answer's high 16 bits | `ULONG OGPU_QueryLimit(ULONG op)`, a new LVO | Bits 8–16 already carry the back end and `ONGPU` |
| Answers change after `OGPU_SetBackend` | No global switch (2.9). The answers are for the best back end there is; `OGPU_QueryDriver(drv, op, format)` answers for a named one, and a front end asks with what `OGPU_Driver(api)` chose | A setting one program changes must not change another program's answers mid-run |

**Discovery happens once, in gfopengpu.library itself** (0.26). The first `OGPU_Query` of a capability op runs, once per boot (and again after a host reset):

- **virgl:** `ACV_HELLO`, `ACV_CAPSET_INFO`, `ACV_CAPS` (v2), and the renderer string. From them come formats (texture, render target, depth-stencil), limits, primitives, logic ops, TGSI, and `OGPU_OP_REAL_GPU` from `ACV_HOST_GPU`.
- **llvmpipe:** the same caps when the host has no chip (`OGPU_OP_LLVM_PIPE`); the limits are llvmpipe's.
- **CPU core:** a constant table written from `ogpu_3d.c`: what its stream3d does, its formats, its limits (`OGPU_OP_CPU_CORE`).
- **The driver's stream route** (ACRTG.gpu): what its back end answers today, unchanged.

The answers are cached in the library base. After that, a query is a table lookup.

- The pipe's screen (2.2) reads its emitter's caps through `OGPU_QueryDriver` and `OGPU_QueryLimit`; it sends no HELLO or CAPS itself.
- `pipe_screen.c` keeps only the context ids. The HELLO it sends for its own context's host flags goes too: those flags are capability ops (`OGPU_OP_VIRGL_PRESENT`, `OGPU_OP_VIRGL_GLES`).

**Which ops, first cut** (the proposal's minimal set; everything else answers `NONE` until something implements it):

| Group | Ops (0x80xx) | `format` |
|---|---|---|
| Back end | `OGPU_OP_VIRGL` (the existing op, kept), `OGPU_OP_REAL_GPU`, `OGPU_OP_LLVM_PIPE`, `OGPU_OP_CPU_CORE`, `OGPU_OP_VIRGL_PRESENT`, `OGPU_OP_VIRGL_GLES` | 0 |
| Resources | `OGPU_OP_TEXTURE_2D`, `OGPU_OP_RENDER_TARGET`, `OGPU_OP_DEPTH_STENCIL`, `OGPU_OP_BUFFER`, `OGPU_OP_MIPMAP` | format |
| Fixed function | `OGPU_OP_FF_TEXTURE_ENV`, `_FOG`, `_MULTITEXTURE`, `_LIGHTING`, `_TEXGEN`, `_ALPHA_TEST`, `_CHROMA`, `_SPECULAR` | 0 |
| Tests and blending | `OGPU_OP_BLEND`, `OGPU_OP_LOGIC_OP`, `OGPU_OP_DEPTH_TEST`, `OGPU_OP_STENCIL_TEST`, `OGPU_OP_SCISSOR` | 0 |
| Shaders | `OGPU_OP_TGSI` | 0 |
| Presentation | `OGPU_OP_PRESENT_WINDOW`, `_SCREEN`, `_BITMAP`, `OGPU_OP_VSYNC` (answered by gfopengpu from what OpenRTG registers with it) | 0 |
| Limits (`OGPU_QueryLimit`) | `OGPU_OP_MAX_TEXTURE_SIZE`, `_TEXTURE_UNITS`, `_VERTEX_ATTRIBS`, `_VIEWPORT_DIMS` | — |

The fixed-function ops answer for the back end: on virgl they are FULL where the pipe's generator covers them (2.4), and on the CPU core where `ogpu_3d.c` does.

**PARTIAL** means "there, with limits a front end must know", and each PARTIAL names its limit in `docs/design/OGPU-Query.md`. For example:

- NPOT textures up to a size;
- a blend mode done in software;
- Z24 only as Z24S8;
- logic ops on RGB565 working on 8-bit channels (the Warp3D note's known difference).

Each front end decides in its written rules whether PARTIAL is enough.

**Reported capabilities** stay each front end's, made from `OGPU_Query` by written mapping rules:

- Warp3D: every `W3D_Query` flag and `W3D_GetDriverState`;
- MiniGL: its strings, `GL_MAX_TEXTURE_SIZE`, units, extensions;
- TinyGL and GL.module: Mesa makes its own strings, but the values it is given come from here;
- EGL: config selection by `OGPU_OP_RENDER_TARGET` and `OGPU_OP_DEPTH_STENCIL` with formats.

They are frozen before any of this is wired (M0) and compared after, on every back end. On a Pi 5 with 16 units, Warp3D and MiniGL still report 2, as they always have: the mapping rule says so, not the hardware.

### 2.9 Driver choice

The variables, as the API sketch (01:43) names them:

```
ENV:GFOpenGPU/Driver     auto | virgl | llvmpipe | cpu | acrtg
ENV:MiniGL/Driver        optional override, that API only
ENV:Warp3D/Driver        optional override
ENV:TinyGL/Driver        optional override
```

| Value | Means | Emitter |
|---|---|---|
| `auto` | virgl when `OGPU_Query(OGPU_OP_VIRGL)` is FULL and the host says `ACV_HOST_GPU` and `ACV_HOST_PRESENT`; else `acrtg` while that back end exists; else `cpu` | as chosen |
| `virgl` | virgl, on a graphics chip; refused (falls to `auto`'s next) without one | virgl |
| `llvmpipe` | virgl even when the host has no chip (host software rendering; tests) | virgl |
| `cpu` | the 68k core, `ogpu_3d.c` | stream3d |
| `acrtg` | the stream route to the host's 3D core (ACRTG.gpu). Deprecated: it goes when M11's decision deletes that core, and then means `auto` | stream3d |

The old values stay as aliases: `GFOpenGPU` = `acrtg`, `CPU` = `cpu`.

gfopengpu.library 0.26 adds the one call that applies them:

```c
ULONG OGPU_Driver(CONST_STRPTR api);   /* "Warp3D", "MiniGL", "TinyGL", "GL": OGPU_DRV_VIRGL, _LLVMPIPE, _CPU, _ACRTG */
```

- The auto logic lives only here (2.13 explains why the sketch's `OGPU_SetBackend` is not a call).
- `mgl_choose.c` and Warp3D's driver code shrink to one call each.
- Each library logs the reason once to `ENV:GFOpenGPU/Trace`.

### 2.10 Tasks, locks and memory

- **Tasks:** a pipe context belongs to one API context and is used by one task at a time. A front end that allows calls from several tasks already serialises them (Warp3D's lock), and the pipe adds no lock of its own on the context.
- **The screen** is shared by the library's contexts and guarded by one `SignalSemaphore`. It is held only while ids are given out and caps are read, never across a block.
- **Lock order:** the OpenRTG bitmap lock (taken in `ORTG_Present`'s prepare) is outermost; the pipe's screen semaphore is never held while waiting on a block; no lock is ever held between frames. This is the MiniGL freeze rule, made general and tested (section 5).
- **Memory:** `AllocVec(MEMF_ANY)` (the host reaches Fast RAM by Amiga addresses).
  - Arenas start at 64 KB vertices and 16 K indices per context and grow at a frame's start, never mid-frame. Growth is capped at 4 MB, and past that the block is sent early.
  - The command stream starts at 16 KB.
  - A context with no draws for 10 seconds gives its arenas back down to the start size.

### 2.11 Errors and fallbacks

- **Commands the host refuses** are counted per block (`pre_errors` today), traced, and never retried in a loop.
- **A block that doesn't run** (transport error): the context marks itself lost. The front end gets an error at its next sync, and MiniGL and Warp3D fall back to the stream3d emitter for a new context, as they fall back today at open.
- **A present that can't go direct** falls down OpenRTG's route list (2.7). It is always correct, sometimes slower, and the route taken is counted.

### 2.12 Tracing

- One variable, `ENV:GFOpenGPU/Trace`, which replaces `ENV:MiniGL/Trace` and Warp3D's trace (both kept as aliases).
- One stats structure: blocks, words, draws, merged draws, objects made, shaders made (generated or prebuilt), PUTs and bytes, renames, presents by route, refused, declined.

### 2.13 The API sketches of 23 Nov, reconciled

Dale's sketches (01:43) proposed public `OGPU_*` calls for contexts, resources, state, shaders and drawing, an `ORTG_PresentInfo` present, and the driver variables. The ideas are taken in; the shape differs in a few places, each for a reason in the existing code.

| Sketch | In this plan | Why |
|---|---|---|
| Pipe calls as public gfopengpu entries (`OGPU_CreateContext`, `OGPU_Draw` …) | The same calls, in the internal versioned table `OGPUPipeOps` (2.3), for our libraries only | The design's rule: no new rendering API for programs, who already have Warp3D, MiniGL, GL and GLES. A public draw API would be one more front end to keep forever. The table can still become public later; going the other way is impossible |
| `OGPU_MakeCurrent` | Every call names its context | Libraries are shared by every task; a "current" context needs per-task storage and is a classic source of the wrong context drawing |
| `OGPU_Create*State` and `OGPU_Bind*State`, refcounted | Taken as they are (`*_create`, `bind_*`, `cso_release`) | Gallium's shape; the front end keeps the handle, so no hash per draw |
| `OGPU_GenerateFixedFunctionShader(ff)` | `ff_shader(ctx, key)`: made, prebuilt or cached | Taken |
| `OGPU_RenameResource` returning a new resource | Renaming inside `res_update`; the handle never changes | Front ends then can't keep a stale handle; renaming is the pipe's business (rule 3) |
| `OGPU_UpdateResource(res, data, size, level)` | `res_update(ctx, res, level, box, src, stride, swap)` | Boxed updates (Warp3D's and MiniGL's dirty boxes), and the 68k's byte order (swap units) |
| `OGPU_BeginFrame`, `OGPU_EndFrame`, `OGPU_Draw(DrawInfo)`, `OGPU_Clear` | Taken (`begin_frame`, `end_frame` with OpenRTG's hook, `draw`, `clear`), plus the arenas (`vtx_room`, `idx_room`, `vtx_put_range`) | Vertex data has to get to the host; the arenas are how |
| `struct OGPU_Fence *OGPU_Submit(ctx)`, `OGPU_WaitFence` | `end_frame`/`flush` give a `ULONG` fence for the existing `OGPU_Wait` | `OGPU_Submit(stream, words, fence)` is a public LVO since 0.1, which GL.module's winsys and every stream user call; one fence type |
| `OGPU_SetBackend`, `OGPU_GetBackend` | `ENV:GFOpenGPU/Driver` is the setting (a prefs program writes it); `OGPU_Driver(api)` is the getter | A system-wide setter lets one program change another's drawing and answers mid-run; the choice is made per context at creation |
| Driver values `auto`, `virgl`, `llvmpipe`, `cpu`, `acrtg` | Taken (2.9), with the old values as aliases | — |
| `ORTG_Present(ORTG_PresentInfo *)` | Taken: one entry point, struct versioned by its size (2.7) | — |
| `source` as a pipe resource | A source kind plus handle (virgl resource, memory, retained surface, bitmap, ACMedia frame) | OpenRTG may not depend on the pipe's internals, and half the sources are not pipe resources |
| `Window`, `Screen`, `BitMap` targets | A `RastPort` plus x, y | A RastPort is a window's (with its layers), a screen's or a bare bitmap's alike, as everywhere in graphics.library; SDL and GL.module often only have one |
| `srcRect`, `dstRect`, `flags` (flip, vsync) | Taken: `src` is the damage, `dst` at another size scales (ACV_PRESENT_SCALE); flags add ASYNC and PREPARE | PREPARE keeps the present in the frame's block (no extra round trip) |
| `ORTG_OpenScreen`, `ORTG_AllocBitMap` and friends | Not added | Intuition's `OpenScreen` and graphics.library's `AllocBitMap` already reach OpenRTG (patched, and through P96 and CyberGraphX). Second entry points would break the single-owner rule they come from |
| Not in the SDK: `virgl_cmd.h`, host enumeration, per-library presents and caps, manual frees | Agreed. `virgl_cmd.h` moves from `include/gfopengpu/` to `library/pipe/` in M7, once only the pipe uses it, so it is no longer installed with the SDK | — |
| Landing order | The same order, with MQ (capabilities) after the pipe's interface as the second review asks | — |

---

## 3. Migration plan

Every stage is one or more pull requests on gfopenrtg (and, for M11, amicradle). Each stage has:

- **a gate**, proof that nothing changed for programs;
- **a switch** that puts the old code back for one release;
- **a list of what is deleted.**

No stage both moves code and changes behaviour. Sizes: S is one PR of under 500 lines changed, M is 1–3 PRs, L is more than 3.

### M0: Baselines and freezes (S–M)

- **Capture what the code does today**, on the host tests and in a lab:
  - `tests/run_minigl.sh` with `ACVIRGL_LIB`: pixels and counters;
  - `tests/run_w3d_vg.sh`, `run_w3d_conv.sh`, `run_w3d_hlv.sh`, `run_3d.sh`, `golden_scenes`, `golden_scene_3d`, `scenes3d`;
  - GL.module `tests/run.sh`, `test_gla`;
  - OGLES3 demos;
  - `w3dcheck`, MGLTest and BlitzQuake timedemo on a copy of Instance-32 (the lab from `Warp3D-Virgl-Route.md`), each route, 3 runs.
- **Freeze** the reported capabilities per front end and back end: `tests/golden/caps/`.
- **Trace** the shader keys each test and game uses: the prebuilt list (2.4).
- **Gate:** the captures are committed under `tests/golden/pipe-m0/` and reproduce twice in a row.
- **Done on the PC tests, 23 November 2026:** `tests/pipe_m0.sh` runs `run_minigl.sh`, `run_w3d_vg.sh`, `run_w3d_conv.sh`, `run_w3d_hlv.sh` and `run_3d.sh` (virgl through `ACVIRGL_LIB`) and keeps the sha256 of each of their 32 pictures (both routes) and their counter lines in `tests/golden/pipe-m0/captures.txt`; `tests/pipe_m0.sh` with no argument checks against it. Two runs in a row on bigguy were identical once one line was left out: `run_3d.sh`'s `(auto)` bench, whose route choices depend on the machine's speed. **Still owed:** the lab half (MGLTest, `w3dcheck` and BlitzQuake on Instance-32, each route, 3 runs), the frozen caps (`tests/golden/caps/`) and the shader-key trace, which need the lab.

### M0.5: Architecture lockdown (S)

These documents come first, before any code moves:

- the ownership matrix in DESIGN.md;
- the single-owner rule in CONTRIBUTING (CONTRIBUTORS.md today);
- the boundaries: OpenGPU and OpenRTG, the pipe and the gfopengpu runtime, OpenRTG and ACMedia;
- the Mesa exception;
- the resource, lock and capability rules (2.3, 2.8, 2.10).

### M1: CI enforcement with a debt list (S)

- **Add the ownership rules** to `.github/workflows/repo-guard.yml` as a script, `tests/ownership_check.sh`, which greps includes, exports and calls:
  - host headers (`acvirgl.h`, `virglrenderer.h`, the ACRTG host header) only in `library/opengpu/`, `library/openrtg*`, `host/`, `tests/`;
  - `virgl_cmd.h` only in `library/pipe/`, plus the debt list;
  - an `ORTG_Present*` export only in gfopenrtg; `ACV_PRESENT` written only in `library/pipe/` and `library/openrtg*`, plus the debt list;
  - stream3d 3D opcodes written only in `library/pipe/`, plus the debt list (OGFX's 2D on the allowed list);
  - no `vg_cache`, `*_vs_text`, `*_fs_text` or rings outside `library/pipe/`, plus the debt list;
  - no library outside `library/opengpu/`, `library/pipe/` and `library/openrtg*` sends `ACV_HELLO`, `ACV_CAPSET_INFO` or `ACV_CAPS` (host GPU enumeration), plus the debt list;
  - no library opens ACRTG or its board directly, except gfopengpu's and OpenRTG's;
  - **the Mesa exception, checked both ways:**
    - GL.module's winsys (`gla_virgl*.c`) may reach the host only through `OGPU_Submit`/`OGPU_Wait` (no host header, no ACRTG);
    - its frames may reach the screen only through OpenRTG (from M8: `ORTG_Present*` only, and no `WritePixelArray`, `ogpu_pixels` or `LockBitMapTags` in `gla_present_os3.c`).
- **`tests/ownership.debt`** lists today's violations, file by file. CI fails if a line is **added** to it or a new violation appears, and later stages delete lines.
- **Gate:** CI green on main with the debt list. A test PR adding a fresh `#include "virgl_cmd.h"` in `library/egl` fails.

### M2: The pipe's interface, screen and transport (M)

This stage fixes the **interface only**: `pipe.h` with the whole `OGPUPipeOps` table, its handles and the ownership rules written beside each call. Behind it, only the screen and the transport are real. Nobody builds state caches or resource code against a table that is still moving: those come in M3 and M4, behind a table frozen here (any change to it after M2 is a version bump).

- Add `library/pipe/`: `pipe.h`, `pipe_screen.c`, `pipe_xport_os3.c`, `pipe_xport_host.c`, `build.sh` (m68k and host), and `tests/pipe/test_screen.c`.
- MiniGL and Warp3D use `pipe_screen` and the transport through `ogpu_pipe_ops()`.
- **Delete:** `vg_os_run` and `vg_os_open` from `mgl_vg_os3.c`; `os_run` from `w3d_vg_os3.c`; the HELLO and caps copies in `mgl_vg_frame.c` and `w3dvg_open` (moved into the screen, and from there into gfopengpu in MQ).
- **Gate:** M0's MiniGL and Warp3D host captures identical (pixels and counters), and a lab run of MGLTest and `w3dcheck` on the virgl route.
- **Switch:** none needed (pure move).
- **Done, 23 November 2026:** `include/gfopengpu/pipe.h` (the table, version 1, every member; only `screen_open`, `screen_close` and `run` filled), `library/pipe/pipe_screen.c`, `pipe_xport_os3.c`, `pipe_xport_host.c`, `build.sh` and `tests/pipe/test_screen.c` (29 checks against a pretend host: the blocks word for word, set 1, and every failure). Two changes to the sketch: `screen_open` takes a `struct OGPUPipeOpen` (the open's first block still carries the caller's first context, so the host sees the same blocks as before), and the table has `run`, the transport, which the front ends call for their own blocks until M3 to M7 move them. The pipe's memory is `AllocVec` through the transport (`ogpu_pipe_xport_alloc`). MiniGL and Warp3D open the screen and send every block through `run`; `vg_os_open`, `vg_os_run`, `vg_os_close`, Warp3D's `os_run` and the tests' two bridge clients are gone, and so are the two `host-caps` lines in `tests/ownership.debt`. Gate: `tests/pipe_m0.sh` identical; minigl.library and Warp3D.library build for the 68k.

### MQ: Capabilities (L, its own workstream)

This follows the review's order: the capability authority comes before the pipe takes anything over, because the pipe's screen and every front end read from it.

1. **Ownership and CI** (folded into M0.5 and M1): `OGPU_Query` declared the only authority; CI fails a build where MiniGL, Warp3D, EGL, OGLES3, TinyGL or the pipe sends `ACV_CAPS`, `ACV_CAPSET_INFO` or `ACV_HELLO`, reads host GPU information, or keeps its own capability table. Today's reads are on the debt list.
2. **The golden capture** (in M0, before anything is wired), as text files in `tests/golden/caps/`, one per front end and back end (68k core, the ACRTG stream route, virgl on a GPU, virgl on llvmpipe):
   - `warp3d-<backend>.txt`: every `W3D_Query` flag for every destination format, and `W3D_GetDriverState`;
   - `minigl-<backend>.txt`: the strings, extension list, `GL_MAX_TEXTURE_SIZE`, unit counts and advertised features;
   - `tinygl-<backend>.txt` and `gl-<backend>.txt`: what Mesa exposes, strings and limits;
   - `egl-<backend>.txt`: the configs `eglChooseConfig` gives for each attribute set our samples use.
3. **The infrastructure** (gfopengpu.library 0.26):
   - the reserved op range, `OGPU_QueryLimit`, `OGPU_QueryDriver`;
   - the one-time discovery for virgl, llvmpipe, the CPU core and the driver's stream route;
   - the cache in the library base, invalidated on a host reset only;
   - `docs/design/OGPU-Query.md`: every op, every PARTIAL's meaning.
4. **The mapping layers,** one PR each, each verified against its golden file on every back end:
   - Warp3D (`w3d_query.c`): `W3D_Q_TEXMAPPING` to `OGPU_OP_TEXTURE_2D` with formats, `W3D_Q_ZBUFFER` to `OGPU_OP_DEPTH_STENCIL`, `W3D_Q_MULTITEXTURE` to `OGPU_OP_FF_MULTITEXTURE`, and every other flag;
   - MiniGL: limits, `GL_ARB_multitexture`, fog and environment features, render targets;
   - EGL: configs from render-target and depth formats;
   - the pipe's screen: host flags and caps from the cache, its own HELLO and CAPS deleted.
5. **The tests:**
   - each front end's output identical to its golden file on every back end (extensions, texture support, multitexture, render targets, limits);
   - the PARTIAL tests: a fake back end in the host test answers FULL, PARTIAL or NONE for each op, and each front end's behaviour is checked against its written rule (NPOT limits, a software blend mode, a depth format only as Z24S8);
   - discovery runs once: a counter of HELLO and CAPS commands per boot is 1, however many programs open contexts.

- **Gate:** all golden files identical on all back ends; the debt list has no capability entries left.
- **Switch:** none: until a front end's mapping PR lands, it keeps its own code, so nothing changes for programs before its gate passes.

### M3: Blocks, resources and arenas, from MiniGL (M)

- `pipe_block.c`, `pipe_res.c` and `pipe_arena.c` are taken from `mgl_vg_enc.c`, `mgl_vg_frame.c` and `mgl_vg_tex.c`, with their names generalised.
- MiniGL's route calls them; `struct mgl_vg` loses its block, arena and resource fields.
- **Delete:** about 60% of `mgl_vg_enc.c`, `mgl_vg_frame.c` and `mgl_vg_tex.c`.
- **Resource lifetime tests come first,** in the stage's first PR, before any MiniGL code is removed. `tests/pipe/test_res.c` covers:
  - create, update (whole and boxed), delete;
  - rename-on-write while an earlier block still reads the old version;
  - deferred destroy: a released resource is freed only after the block that last used it has run (fence-safe);
  - reference counts across two contexts;
  - ring wrap with a block in flight;
  - render-target and depth-buffer resize.
- **Gate:** `test_res` passes; M0's MiniGL captures identical.
- **Switch:** the old files stay compiled in for one release, and `ENV:MiniGL/PipeOld=1` picks them at context creation. They are removed in M6.

### M4: The pipe context and state objects, from MiniGL (M)

- `pipe_state.c` and `pipe_ctx.c` are taken from `mgl_vg_state.c` and `vg_prepare`.
- MiniGL's adapter, `mgl_pipe_adapt.c`, turns GL state into the pipe's state objects and bindings (`*_create`, `bind_*`).
- **Delete:** `mgl_vg_state.c` (21 KB) and the bound-state fields of `struct mgl_vg`.
- **The combined lock test starts here,** as soon as the pipe has a lock (its screen semaphore), and runs in every stage after:
  - a screen lock, a Warp3D lock and a GL context held together;
  - a lock held across a submit, a present and a readback.
  Deadlocks are cheaper to fix now than in M8.
- **Gate:** M0 captures identical, with object counts no higher than before; the lock tests pass in a lab.

### M5: One fixed-function generator (L)

This is the riskiest stage, so it runs on its own and is tested before anything depends on it.

1. Write the unified key (2.4) and `pipe_ff.c` by merging the two generators. MiniGL's text is the base; Warp3D's features (16.16 positions, fog by w, chroma, specular, linear interpolation) are added as key bits.
2. **Differential test** `tests/pipe/test_ff.c`, run on the PC with virglrenderer and llvmpipe:
   - for every key in an enumerated set (all environment pairs × texture bases × fog modes × light counts 0, 1, 3, 8 with and without spots × texgen modes × colour material modes, about 20,000 keys, sampled to about 2,000 in CI and the full set nightly), draw a fixed scene with the **old** generator and the **new** one;
   - the pixels must be equal (tolerance 0); where the TGSI text is identical, say so and skip the draw;
   - the same for Warp3D's key space against `w3d_vg_shader.c`;
   - against the 68k core (`ogpu_3d.c`), the screen-space keys must match within 8/255.
3. Build the prebuilt set and measure, on a 68040 lab:
   - the time per generated shader;
   - the set's size;
   - the first frame's cost;
   - the cache hit rate over MGLTest, gears, BlitzQuake and `w3darrbench`.

   Workloads are benchmarked for MiniGL and Warp3D both. The prebuilt set covers at least: unlit textured, unlit two-unit, lit textured, lit two-unit, and each fog mode over those.
4. **The 68k path's cost, recorded as a decision.** The design says the 68k core keeps its own fixed-function rasteriser and never runs the generator. This stage proves it with numbers:
   - the generator's cost per draw if it did run on the 68k;
   - the 68k core's frame time with no generator, against M0.

   Both go in `measurements/` with the decision. If the numbers ever say otherwise, the decision is revisited there, not quietly changed in code.
5. MiniGL switches to `pipe_ff.c`. **Delete:** `mgl_vg_shader.c`.

- **Gate:** the differential test passes on the full set; M0 captures identical; the measurements are recorded in `measurements/`.

### M6: Warp3D's virgl route onto the pipe (L)

- `w3d_vg.c` (61 KB) becomes `w3d_pipe_adapt.c`. It still reads each finished stream3d batch (so Warp3D's front end does not change), but every command becomes a pipe call:
  - RENDER3D and TEXENV become state objects (`blend_create`, `dsa_create`, `raster_create`, kept by the adapter per key and bound) and `ff_shader`;
  - TEXTURE becomes `res_*` (keyed by level-0 address, as today) and `bind_texture`;
  - TRIANGLES becomes `vtx_put_range` and indexed `draw`s, with runs merged by the pipe;
  - DEPTH, CLIP, FILL and CLEAR3D map to their pipe calls;
  - Z and stencil reads and writes become `res_read` and `res_update`.
- The sync present uses the hook. Until M8 the hook is a local copy of today's code (the bitmap lock stays in `w3d_batch.c`) so this stage does not depend on M8.
- **Delete:** `w3d_vg_shader.c` (10.6 KB), most of `w3d_vg.c`, and MiniGL's `PipeOld` copies (M3).
- **Gate:**
  - M0's Warp3D captures identical (`run_w3d_vg.sh`, `scenes3d`, the CPU-2D-between-3D scene);
  - `w3dcheck`, `w3darrbench`, the texture tests (upload, delete, formats, mip levels) and render-to-texture (drawing into a bitmap later used as a texture) pass;
  - BlitzQuake timedemo within 2% of M0 on the lab.
- **Switch:** `ENV:Warp3D/PipeOld=1` for one release.

- **Done, 23 November 2026:** Warp3D's virgl route is on the pipe (`w3d_vg.c`; the pipe's context, resources, arenas,
  state objects and the one fixed-function generator), `ENV:Warp3D/PipeOld=1` keeps the old route for one release.
  The pipe's table is version 5: `blend_create2` (logic op), `dsa_create2` (stencil), `sampler_create2` (border colour),
  `room` (does a draw fit without sending) and `buf_put` (a batch's vertex memory PUT from where it is, into Warp3D's own
  ring, never renamed). MiniGL's keys are these with the extra words 0: the same objects, the same hash. A screen-space
  vertex shader is cached without the colour bit (its text doesn't depend on it).
  Gate, on llvmpipe as M0 was captured (bigguy's bridge now finds the Radeon; `LIBGL_ALWAYS_SOFTWARE=1`): the old route
  as captured; the pipe route with all 32 pictures and every counter the same but one, the stream's words (77,617 to
  78,610, +1.3%: the pipe's object caches, 64 slots with 4 probes, evict differently from Warp3D's 32-slot FIFO over the
  300 random scenes), recorded in `captures.txt`. test_res 81/0, test_screen 33/0, test_ff 397,552 keys 0 different;
  minigl.library, Warp3D.library and gfopengpu.library build for the 68k.
  Not yet deleted: `w3d_vg_shader.c`, the old half of `w3d_vg.c` and MiniGL's PipeOld copies go one release after this
  (the release that ships both PipeOld switches), so a machine can still fall back.

### The GL stage, part 1: GL.module's virgl route onto the pipe (M)

Added 23 November 2026, after M6; it does not depend on M7. GL.module is Mesa: its own virgl driver writes the stream,
so the pipe gives it no state objects, no shaders and no arenas. What moves is everything around the stream, which
`gla/gla_virgl.c` had as a third copy: the open (the hello, the context, the capability set), the request blocks, the
resources and their unrefs, PUTs and GETs, and the counters.

- `gla/gla_carry_pipe.c` (the carrier, no Mesa in it, tested on the PC against a pretend host) and
  `gla/gla_virgl_pipe.c` (Mesa's winsys calls, onto the carrier). GL.module links the pipe's files.
- The pipe's table is version 6, four members at the end and one context flag:
  - `OGPU_PIPE_CTX_NO_ARENAS`: a context whose front end sends finished streams and has no vertices of its own;
  - `res_create2`: a resource with depth, layers, samples and the host's flags (3D, arrays, cube maps, multisample);
  - `xfer`: a PUT or a GET of any box of any level, in place. A PUT is never a new version of the resource: Mesa
    knows the handle and has already written it into streams;
  - `stream_cut`: the stream so far becomes a SUBMIT among the block's pre-commands. Mesa's order is first in, first
    out (a PUT made after a draw must run after it), and the pipe's block is pre-commands, then the stream; a cut
    keeps Mesa's order inside one block, so GL's blocks are the ones the old route sent;
  - `queued`: is anything waiting to be sent.
- Batching (`ENV:GFOpenGPU/GLBatch`) is the carrier's, with the old route's limits, so a frame costs the same
  blocks. Pipelining (two blocks in flight, off by default) is not carried over: the pipe's transport waits for each
  block. It stays on the old route until it goes.
- **Gate:** M0's captures identical (MiniGL and Warp3D must not see version 6); `test_gla` draws the same pictures
  on the pipe as on the old route, byte for byte, with the same blocks a frame; ogles3's four demos look the same
  on a lab copy of Instance-32; the 68k builds.
- **Switch:** `ENV:GFOpenGPU/GLPipeOld=1` for one release; then `gla_virgl.c`'s own blocks and queue go.

- **Done, 24 November 2026:** GL.module's virgl route is on the pipe (`gla_virgl_pipe.c`, `gla_carry_pipe.c`; the
  pipe's table is version 6), `ENV:GFOpenGPU/GLPipeOld=1` keeps the old route for one release.
  Gate, in the kitchen on llvmpipe as M0 was captured: `tests/pipe_m0.sh` as captured, all 32 pictures and all 50
  counter lines (MiniGL and Warp3D ask for version 6 and use none of its new members). `test_gla`: the three scenes
  on the pipe are the old route's pictures byte for byte, and softpipe's; 300-draw frames cost the same request
  blocks a frame on the pipe as on the old route, a command apart and batched (immediate 4.7 and 2.0,
  glBufferSubData 903.0 and 5.0, glTexSubImage2D 1202.3 and 6.0), with no command refused. test_res 110/0,
  test_gla_carry 49/0 (new: the carrier against a pretend host), test_screen 33/0, test_ff 547,552 keys 0 different.
  GL.module (11,920,140 bytes), libogpupipe.a, minigl.library and gfopengpu.library build for the 68k.
  `tests/pipe/test_m0_gate.sh` and `tests/gl/test_gla.sh` are new: the gate and GL's PC test as the kitchen runs
  them.
  Not carried over: pipelining (off by default; the old route still has it). Not yet deleted: `gla_virgl.c`'s own
  blocks and queue, one release after this.

### The GL stage, part 2: minigl.library as a face of GL.module (M)

Added 24 November 2026. MiniGL keeps its ABI (the -30 call, dispatch ABI 5, 840 bytes, 205 entries, the context as each call's first argument) and its own files that are not a renderer: displays and input, the `mglChoose*` family, GLU and GLUT. Its GL calls become GL.module's: 105 are forwarded by a generated thunk each, 2 more with their answer cut to MiniGL's four-byte GLboolean, and 98 are functions (MiniGL's own calls, GLU, GLUT, and the GL calls whose arguments differ: booleans, paletted textures and MiniGL's texel types, `glRotatefEXT`, `glGetString`). `library/minigl/wrap/` holds it; `build.sh` builds it as the library's third half (`-DMGLW`, FPU).

- **Done, 24 November 2026, as an opt-in:** `ENV:MiniGL/Wrap=1` (read at the library's first open; an FPU and `LIBS:GFOpenGPU/GL.module` needed) hands out the wrapper's table. MiniGL's own renderer stays the default and stays in the library. `tests/minigl/test_wrap.sh` (1629 checks: the generated table, thunks and slots against the spec and the wrapper's files).
- **Lab, 24 November 2026** (`minigl.library` at d838e2d with GL.module of part 1, MGLTest 320x240, FRAMES 300, a lab copy on a slow disk): the same picture (96 of 76,800 pixels differ, each by one level); the wrapper 165 to 227 frames a second, MiniGL's own renderer 345 to 600 in the same runs. GL.module's own programs run at 230 to 330 there, so the cost looks to be the module's frame, not the forwarding: that is why the wrapper is not the default yet.
- **Not carried over:** a palette changed after its textures were loaded does not reach them; `MGL_UBYTE_ARGB` colour arrays are refused; `mglPrintMatrixStack` prints only the top matrix; `mglTexMemStat` answers 0.
- **Left:** GL.module's frame cost; then the wrapper becomes the default and MiniGL's renderer (`mgl_state.c`, `mgl_vertex.c`, `mgl_pipe.c`, `mgl_out.c`, `mgl_vg*.c`) goes, a release later.

### Pipe version 7: the frame's commands (S–M)

Added 24 November 2026, on Dale's yes to the design note (the pipe writes every command; no fallback switch; MiniGL's own renderer too). After M6 and M3 to M5, Warp3D and MiniGL used the pipe for their contexts, resources, arenas, state objects and shaders, but still wrote about a dozen virgl commands by hand: surfaces and the framebuffer, sampler views and their binding, the vertex and index buffers and the draw, the clear, the stencil reference, the blend colour, the stipple, Warp3D's BLIT of a 16-bit area and its own GET of the depth buffer.

- **The pipe (step 1, PR 26):** `OGPU_PIPE_VERSION` 7. The seven version 1 members never filled are real: `bind_framebuffer`, `bind_texture`, `set_blend_colour`, `set_stencil_ref`, `begin_frame`, `draw`, `clear`. Added at the end: `draw_buf` (any vertex or index buffer, with its stride), `set_stipple`, `copy` (BLIT). `library/pipe/pipe_frame.c`: a resource's surface and sampler view are the pipe's, made at its first bind (again for a renamed version) and destroyed at its last release; units' views and samplers, the buffers, the stencil reference, the blend colour and the stipple are sent only when they changed (compared as bits). A unit whose texture is let go of while bound is left as the host has it until it is bound again (Warp3D's way); MiniGL binds every unit for each draw. `tests/pipe/test_draw.sh` (24 checks) holds the pipe to the words the front ends wrote by hand.
- **Warp3D (step 2, PR 27)** and **MiniGL (step 3, PR 28):** no frame command written by hand; `mgl_vg_enc.c` is gone. The depth read (step 4) is the pipe's `xfer` GET.
- **Gate:** all 32 M0 pictures the same. Two counters moved, recorded in `captures.txt`: Warp3D's stream words 78,796 to 79,316 (+0.7%; traced with `tests/pipe/test_w3d_tally.sh` on both commits: 16 more SET_POLYGON_STIPPLE commands, +528 words, and one surface fewer made and destroyed, -8. The old Warp3D sent a stipple only when its address changed; the pipe compares the 32 rows, and the test's random scenes rewrite the stipple in place at the same address, so each new one is now sent. The first reading, destroys at close, was wrong), and MiniGL's submits 62 to 43 with the same 17,003 words (the open's surfaces now go with the first draw, not in a stream of their own).
- **The numbers (follow-up):** `include/gfopengpu/pipe_vg.h` holds the numbers a front end hands the pipe (formats, bind flags, targets, clear buffers, primitives, blend and sampler numbers), the byte order and the request block's framing; `library/pipe/virgl_cmd.h` includes it and keeps the command numbers and writers, the pipe's alone. `w3d_vg.c` and `mgl_vg.h` take `pipe_vg.h`, so their two `virgl-cmd` lines left `tests/ownership.debt` (11 to 9). `tests/pipe/test_pipe_vg.sh`: the header names no command, its numbers are the pipe's, and the two files no longer include `virgl_cmd.h`. Left for the front ends: the `acv-present` lines (M8).

### M7: stream3d emission into the pipe (L)

- `pipe_emit_s3d.c` is taken from `w3d_batch.c`'s builder.
- Warp3D's draw calls become pipe calls, and the pipe picks the emitter. On the stream3d emitter the batch is the same words as today.
- MiniGL's 68k route also emits through the pipe.
- `include/gfopengpu/virgl_cmd.h` moves to `library/pipe/virgl_cmd.h`: only the pipe uses it now, so it leaves the installed SDK headers.
- With Warp3D calling the pipe directly, the adapter of M6 no longer reads batches back. It shrinks to Warp3D state to pipe state, and the batch-reading code is **deleted**.
- **Gate:**
  - stream3d words **identical** to M0 on the CPU and GFOpenGPU routes (a word-for-word stream comparison in `tests/pipe/test_s3d_same.c`);
  - all Warp3D and MiniGL captures identical on every route;
  - the ownership debt list has no stream3d entries left.
- **Done, 24 November 2026, part 1 (the builder):** `library/pipe/pipe_emit_s3d.c` and `include/gfopengpu/pipe_s3d.h`: one builder of a stream3d batch (begin with DEPTH, clip, RENDER3D, TEXENV, TEXTURE, TRIANGLES with the joined lists, fill, CLEAR3D, SURFACE and TARGET in front at the end). It has a table of its own, `ogpu_pipe_s3d_ops`, version 1, apart from `struct OGPUPipeOps`: it needs no screen, and GL.module, which never draws this way, links neither it nor gfopengpu's builders. `w3d_batch.c` and `mgl_out.c` call it and no longer write a stream command themselves, so their two `stream3d` lines left `tests/ownership.debt`. The front ends still decide when a batch is sent and where it runs, and keep what they have sent. `virgl_cmd.h` is `library/pipe/virgl_cmd.h`, out of the SDK's headers; MiniGL's and Warp3D's virgl files still include it from there (their `virgl-cmd` debt lines stay until those files go). `tests/pipe/test_s3d_same.c` (95 checks): the front ends' old builder, kept in the test, and the emitter are given the same calls, and their streams are the same words, start and length, for a Warp3D frame, a MiniGL frame, no depth buffer, an empty batch, a full data area and an overfull batch.
- **Done, 24 November 2026, part 2 (direct calls):** design and decisions in `OpenGPU-M7-Part2-Recording-And-Lock.md` (Dale: A1, B, C2). On the graphics card route Warp3D's draw calls are calls to the route (`w3dvg_begin` ... `w3dvg_end`), which makes the pipe's calls: no batch is recorded and read back, only the vertices are kept until the batch ends. The bitmap is locked only around a send that reads or writes it (the load of a frame's first pixels, the present, by the pipe's present hook), never between two of the program's calls; `pipe.h`'s table is unchanged. A host that fails a block loses that batch's draws, and the 68k draws from there. `tests/pipe/test_w3d_direct.sh` holds the pictures against the reader's and counts the lock.
- **Item 6, the old routes out (branch `team2/old-routes-out-20261124`, held for the release after the one that ships the switches):** `ENV:Warp3D/PipeOld`, `ENV:MiniGL/PipeOld` and `ENV:GFOpenGPU/GLPipeOld` are gone, with the code they chose: the old half of `w3d_vg.c` and its reader `w3dvg_run`, MiniGL's own block, arenas, object caches and shader table, and the GL module's `gla_virgl.c` (and with it `ENV:GFOpenGPU/GLPipeline`, which only that winsys read). The two old shader generators are kept as references for `tests/pipe/test_ff.sh` in `tests/pipe/ref/`. Eleven lines left the ownership debt list. Where a paragraph above says a switch "keeps the old route for one release", that release is the one before this branch merges.
  - **For the release note:** four variables stop being read: `ENV:Warp3D/PipeOld`, `ENV:MiniGL/PipeOld`, `ENV:GFOpenGPU/GLPipeOld` and `ENV:GFOpenGPU/GLPipeline` (never on by default, but a user may have set it). Setting them does nothing from this release on.
  - **The M0 gate's Warp3D counts changed, the pictures didn't.** `tests/golden/pipe-m0/captures.txt` held the old reader's counts until now (335 blocks, 306 submits, 23.3 MB put); the direct route, the default since item 5, makes 584 blocks, 530 submits and 20.1 MB for the same scenes, because it sends at each lock instead of once a frame. This is the first time the direct route's counts are on record, not a change in what ships. On the Amiga (lab, 24 Nov, graphics card route, 320x240) W3DTest ran at 430 to 974 frames a second with this branch, inside the 600 to 950 measured for main's default route.
  - **What no longer has a second opinion:** with `test_w3d_direct.sh` gone (it compared the direct route's pictures with the reader's), the direct route's pictures are held only to the CPU core's, within tolerance, by `W3DCheck` and the M0 gate; no other graphics-card path draws the same scenes.
  - **A held frame and a changed drawing area:** a frame the host still holds (the area couldn't be locked at the present) is shown by the next present into the same area. It is let go of only in `w3d_open_area`, when the area becomes another one (or one the route can't draw into): the frame was the old area's, and an empty present would have put it into the new one.

### M8: `ORTG_Present` (L)

1. gfopenrtg.library 0.30: `ORTG_Present` and `ORTG_PresentDone` (2.7), the route table, the stats.
2. Move the presents in, one PR each, each with the old present kept behind a variable for one release:
   1. MiniGL (`vg_os_present`, `vg_os_presented`, `present_readback` go; its end of frame passes OpenRTG's hook);
   2. Warp3D (its sync present and ARGB copy go);
   3. GL.module (`gla_present_os3.c`'s three routes become one `ORTG_Present` or a hook; the pending slots move into OpenRTG);
   4. retained surfaces (`OGPU_RetainedPresent` becomes a wrapper);
   5. SDL2 and SDL3 (`SDL_os3gpu.c` presents);
   6. ACMedia window frames.
- **Dale, 24 November 2026:** each moved present keeps its old route behind a variable for one release, as the list says (a later PR takes them out).
- **Step 1, 24 November 2026:** gfopenrtg.library 0.30, `ORTG_Present` and `ORTG_PresentDone` (LVOs 0x96 and 0x9c). The struct as 2.7 has it, without the async fields for now (`fence`; the struct grows at the end): kinds VIRGL and MEMORY (MEMORY's format is an `ORTG_PIX_`, as `ORTG_WritePixels` takes); flags `FLIP_Y`, `VSYNC`, `PREPARE` and `ALPHA` (32-bit RTG targets get the frame's alpha, as Warp3D's present asks; else XRGB32 where the host has it, as MiniGL's). Routes in `library/present_plan.h`: in the caller's block (a screen or bare bitmap, prepared); `ORTG_PresentVirgl`'s way (a window, a box to stretch into, or not prepared); read back (`ACV_GET`) and written by the CPU (planar targets, no `ACV_PRESENT` on the host, or the host refused); MEMORY by the CPU. A prepared present always ends in `ORTG_PresentDone`, which presents it the other way when it could not go in the block. Two more `ORTG_DrawStats` kinds: present readback, present memory. `tests/test_present_plan.c`: the routes, and the prepared command word for word as MiniGL's and Warp3D's own presents.
- **Step 2.4, 24 November 2026:** gfopenrtg.library 0.31, kind `ORTG_SURF_RETAINED` (handle: the `struct OGPURetained *`). The retained present moved into OpenRTG (`library/present_retained.c`; the surface's layout is shared through `library/opengpu/ogpu_rs_handle.h`, not an SDK header):
  - the GPU path into GFOpenRTG's own 32 and 16-bit bitmaps only, asking `OGPU_Query` (copy, composite, palette) instead of gfopengpu's driver tables;
  - the CPU path through `ortg_write_pixels` (INDEX8 through its palette on true colour, as pens on 8 bits);
  - a batch the GPU fails is now shown the CPU way at once (before: `OGPU_RS_ERR_GPU`, nothing shown).
  `OGPU_RetainedPresent` calls `ORTG_Present` when gfopenrtg.library 0.31 is in memory; `ENV:GFOpenGPU/RetainedPresentOld` 1 (read when a surface is made) keeps gfopengpu's own present for one release. Into a Picasso96 bitmap that isn't GFOpenRTG's, the new route uses the CPU path where the old one could use the GPU. The route is `PRS_RETAINED` in `present_plan.h`, and only with GFOpenRTG's screens on: it draws through GFOpenRTG's graphics.library base, which is there only then (the first lab run, in a Picasso96 lab, crashed calling through a NULL base). Without them `ORTG_Present` returns -1 and gfopengpu.library presents its own way; also into another RTG system's bitmap of over 8 bits (Picasso96's, with GFOpenRTG's screens forced on beside it: `ortg_write_pixels` would give it pens). `tests/test_present_retained.c` (19 checks), `tests/test_present_plan.c` (+3).
- **ACMedia's rule:** decoding stays in ACMedia (OpenMedia), and its frames get no present path of their own. A decoded frame handed to `ORTG_Present` is a surface like any other:
  - while it is being presented it may not be reused or freed (the same deferred-destroy rule as pipe resources, by fence);
  - ACMedia recycles a frame buffer only after OpenRTG's finish for it;
  - the zero-copy case (the frame stays on the host and only its handle travels) is tested as such: no bytes of the frame through the 68k's memory.
- **Step 2.6, 24 November 2026 (ACMedia window frames; Dale: "Build zero-copy end to end"):** ACMedia had no window present to move (its only frame user, `akikoplus.device`, pulls frames into its own buffer), so the path is new, in three repositories:
  - amicradle-guest's hostlib: three more ACMedia services. `ACM_HOLD` (`$0001200C`) decodes a session's due frame into host memory at the size it is shown at (ARGB32) and answers a frame handle; `ACM_SHOW` (`$0001200D`) writes a held frame straight into a bitmap (RGB16 or ARGB32) inside up to 512 rectangles, clipped to the frame and the bitmap; `ACM_RELEASE` (`$0001200E`) lets it go. A session holds two frames at most; its close and a reset let them go, and a let-go frame's handle is `ACM_E_NOSUCH`, never the next frame in that place.
  - gfosmotherboards' broker (`acmedia_service.c`) runs them; `acmedia_service_test` holds them to the ABI (HOLD writes only its 12 bytes of answer into guest memory).
  - gfopenrtg.library 0.30: a third kind, `ORTG_SURF_MEDIA` (handle: the held frame; width and height: its size). An RTG target of 16 or 32 bits: the target's visible pieces (a window's layer, a smart refresh window's hidden parts) go to the host as one `ACM_SHOW` a bitmap, so the frame never passes through 68k memory; anything else (planar, 8-bit, no broker, a target the host won't write) goes to a bounce: the host writes the frame's part into a buffer of GFOpenRTG's and the CPU writes that. A prepared one is presented in `ORTG_PresentDone`. Plan in `library/present_media.h`; one more `ORTG_DrawStats` kind, present media. The caller lets the frame go only after `ORTG_Present` (or `ORTG_PresentDone`) returns, which is the "OpenRTG's finish" above: the host's write is done by then.
  - `tests/test_present_media.c` is the zero-copy test: a fake host holds the frame outside the arena that stands for guest memory, and after a present the frame's pixels are in that arena only inside the visible pieces, at their places.
  - The old route is the caller's: `tests/amiga/acmwindow` (ACMWindow, the lab's player and the pattern for ACMedia's own window players) takes the new one on 0.30 and later, and the old one (`ACM_FRAME` into its own buffer, then `ORTG_WritePixels`) on older libraries or with `ENV:ACMedia/PresentOld` set to 1, for one release.
- **Gate:**
  - Level 3 tests (section 5): every surface kind into a window, a screen, a bitmap and a covered window, in XRGB32, ARGB32 and RGB565, compared with M0's output;
  - `acvirgl_present_bench` within 2%;
  - the debt list has no present entries left.

### M9: The driver choice (S–M)

The capabilities are done in MQ, so this stage is the chooser only.

- gfopengpu.library 0.26 (with MQ's calls): `OGPU_Driver`.
- Front ends ask it; `mgl_choose.c` and Warp3D's driver code shrink to one call. `ENV:GFOpenGPU/Driver` and the aliases go in.
- **Gate:** the driver matrix test (every variable combination, with and without a GPU, with and without `ACV_PRESENT`) chooses as the table in 2.9 says; MQ's golden files still identical.

### M10: Pipe.module (optional, M)

- Measure the RAM taken by the two linked copies on a 68k lab.
- If it is worth it, build `LIBS:GFOpenGPU/Pipe.module` (shared, A4). The front ends get the table through `OGPU_ModuleOpen("Pipe", OGPU_PIPE_VERSION, &table)` and the static library stays for the host tests.
- **Gate:** the same captures; a library built against `OGPU_PIPE_VERSION` 2 meeting a version 1 module gets NULL and falls back to the stream3d route with a trace line.

### M11: Renderer elimination (M, mostly measurement)

The bake-off does not wait for the end. Its only needs are comparable pictures and stable present and query paths, so it starts **as soon as M7 has passed** (every API reaching every back end through the pipe), in parallel with M8 to M10. Only the deletions wait for the decision record.

1. Run the measurement on the 68k core, virgl on a GPU, llvmpipe, the ACRTG host core and the Vulkan compute path: timedemos (BlitzQuake, Quake via MiniGL, MGLTest scenes, `scenes3d`, `w3darrbench`), on the x86 appliance and a Pi 5 card. Run at least one 68k-style workload with many small draws and texture updates a frame.
2. Write a decision record (`docs/decisions/`) with the numbers and a deletion date. If llvmpipe is within the stated margin, delete:
   - in amicradle: the host 3D core's 3D path in `native/acrtg.c` (the 2D and composite paths stay);
   - in gfopenrtg: `host/vulkan/ogpu3d*` (the compute version), and `ogpu3d_spv.h` with it;
   - MiniGL's 68k route on machines with a host, which becomes a 68k-only route.
3. If llvmpipe is not close enough, the record says why and what would change that. Nothing is deleted.

### 3.1 What goes, in total

| Stage | Deleted (approximately) |
|---|---|
| M2 | the transport copies, HELLO/caps copies: ~4 KB |
| M3–M4 | MiniGL's block, resources, arenas, state: ~45 KB |
| M5 | `mgl_vg_shader.c` 14 KB |
| M6 | `w3d_vg_shader.c` 11 KB, most of `w3d_vg.c` ~45 KB |
| M7 | the batch-reading half of the Warp3D adapter, duplicated stream3d builders |
| M8 | presents in MiniGL, Warp3D, GL.module, SDL2/3, retained: ~25 KB |
| M11 | `host/vulkan/ogpu3d*` ~340 KB of source and SPIR-V, and the host core's 3D path, subject to the decision |

### 3.2 Order and dependencies

```
M0 ─► M0.5 ─► M1 ─► M2 ─► MQ ─► M3 ─► M4 ─► M5 ─► M6 ─► M7 ─► M11 (bake-off) ─► M11 (deletions)
                                  │                 │                     ▲
                                  └──► M8 ◄─────────┘                     │
                                        │                                 │
                                        ▼                                 │
                                       M9 ─► M10 (optional) ──────────────┘
```

- MQ's infrastructure (gfopengpu 0.26) can start in parallel with M2, since it touches only gfopengpu; its mapping PRs and the pipe screen's switch to it come after M2.
- M8's OpenRTG half (the LVOs and routes) can be built in parallel from M2, because it touches only gfopenrtg's library. Moving each present needs that front end on the pipe first.
- The bake-off runs from M7 in parallel with M8 to M10. The deletions wait for its decision record and for M8 (so no deleted renderer is still the only way some present works).

---

## 4. Build plan

### 4.1 Repositories and branches

- **gfopenrtg (DalsinAI/gfopenrtg):** everything except M11's host-core deletion.
  - One branch per stage, `team5/pipe-mN-<what>`, and a PR each, merged by Dale.
  - No stage PR is merged with the M0 gate failing.
- **amicradle:** only M11, and only after the decision record.
- **gfopenup:** apps.lock bumps when a stage changes an installed library (M2 on).

### 4.2 Build layout

```
library/pipe/
  build.sh               m68k: libogpupipe.a (-m68020 -O2, and a 68040 variant as the libraries have today)
                         host: libogpupipe_host.a (VG_HOST, 32-bit, for the PC tests)
  gen_prebuilt.sh        runs pipe_ff on the host for the prebuilt key list, writes pipe_ff_prebuilt.c
  prebuilt.keys          the key list from M0's traces
  pipe_*.c
include/gfopengpu/pipe.h
tests/pipe/
  run_pipe.sh            screen, res, ff (sampled), s3d-same, present levels
  test_*.c
tests/golden/pipe-m0/    captures
tests/golden/caps/       frozen reported caps
tests/ownership_check.sh, tests/ownership.debt
```

- `library/minigl/build.sh` and `library/warp3d/build.sh` link `libogpupipe.a` and build it first if it is missing.
- `library/opengpu/build-amiga.sh` is unchanged until M9 (the new LVO) and M10 (if the module is built).

### 4.3 Toolchain and where builds run

- **m68k:** the cross gcc and the libnix setup the libraries use today, with the same flags as `library/minigl/build.sh` and `library/warp3d/build.sh`. No new compiler.
- **Host tests:** gcc, 32-bit (`-m32`, as `MGL_HOST`/`W3D_HOST` builds now), linked with virglrenderer built for llvmpipe, so CI needs no GPU.
- **Kitchen stoves** for the m68k builds and the host tests. Every stage fits the 45-minute limit by far.
  - The only long job is M5's nightly full key set (about 20,000 draws), which runs as a bigguy task and not in CI.
  - No Python is added on the machines. `gen_prebuilt.sh` is shell around a C program built for the host.
- **Labs** for the gates that need a real machine: copies of Instance-32 for BlitzQuake and `w3dcheck`, and a 68040 lab for the generator's timing. On real hardware, the PiMiga and the CM4 for the M8 and M11 checks.

### 4.4 Versions

| Stage | gfopengpu.library | gfopenrtg.library | minigl.library | Warp3D.library | Notes |
|---|---|---|---|---|---|
| now | 0.25 | 0.29 | 29 | 5.0 | |
| M2–M5 | — | — | 30, 31 … | — | MiniGL internals only |
| M6–M7 | — | — | — | 5.1, 5.2 | Warp3D internals only |
| M8 | — | 0.30 | +1 | +1 | GL.module, SDL2/3 modules rebuilt |
| MQ | 0.26 | — | +1 | +1 | capability ops, `OGPU_QueryLimit`, `OGPU_QueryDriver`; front ends' mappings |
| M9 | 0.26 | — | +1 | +1 | `OGPU_Driver` (same library release as MQ if they land together) |
| M10 | — (a new module file) | — | +1 | +1 | Pipe.module |

Old libraries keep working with new ones:

- a minigl.library before M8 on gfopenrtg 0.30 still uses `ORTG_PresentVirgl` (a wrapper);
- a front end before M9 on gfopengpu 0.26 reads its own variable, as now.

The compatibility matrix is tested in M8 and M9 by installing the old library next to the new one in a lab.

### 4.5 Shipping

- After each gated stage that changes an installed file:
  - a gfopenrtg release (tag, LHA, readme);
  - a gfopenup apps.lock bump;
  - installed on the PiMiga and the CM4 lab for one day before the next stage's PR is merged.
- Each release note says which switch (`PipeOld`) brings back the old code, and when that switch goes away.

---

## 5. Tests, by level and stage

| Test | What | From stage | Where |
|---|---|---|---|
| Capture equality | M0's pixels and counters, MiniGL and Warp3D host virgl tests, golden scenes | M2 | CI (host) |
| `test_screen`, `test_res` | HELLO, caps, rename, deferred unref, ring wrap with a block in flight | M2, M3 | CI |
| `test_ff` | Old vs new generator, pixel-equal; screen-space keys vs the 68k core within 8/255 | M5 | CI sampled, nightly full |
| `test_s3d_same` | stream3d words identical to M0 | M7 | CI |
| **Level 1, APIs** | One scene through Warp3D, MiniGL and TinyGL where the APIs say the same; the differences on purpose (Warp3D's fog by w, its DECAL) listed in `tests/pipe/level1.differences` | M7 | CI |
| **Level 2, back ends** | Each scene on the 68k core, virgl on llvmpipe in CI, virgl on a GPU in a lab | M7 | CI and lab |
| **Level 3, presentation** | Each surface kind into window, covered window, screen, bitmap, in XRGB32, ARGB32, RGB565; scaled | M8 | CI (host OpenRTG test) and lab |
| Reported caps | Each front end, each back end, against the golden files | MQ | CI |
| PARTIAL handling | Fake back end answering FULL, PARTIAL, NONE per op; each front end follows its rule | MQ | CI |
| One discovery | HELLO and CAPS sent once per boot | MQ | Lab |
| Driver matrix | Every combination of `ENV:GFOpenGPU/Driver` and the old variables | M9 | CI |
| Deadlocks | A screen lock, a Warp3D lock and a GL context together; a lock held across a submit, a present, a readback | M4 on | Lab |
| Screens | OpenScreen, CloseScreen, mode switching, bitmap allocation, palette and truecolour modes, several monitors | M0 on (every stage) | CI (host OpenRTG test) and lab |
| Warp3D | Context creation, texture upload and delete, blending, Z buffer, multitexture, render-to-texture; `w3dcheck`, `w3darrbench` | M0 on | CI and lab |
| MiniGL | gears, MGLTest, immediate mode, texture stress | M0 on | CI and lab |
| Films | 1080p and 4K H.264, HEVC, several streams, zero-copy presentation | M8 on | Lab |
| Mesa exception | The M1 checks on GL.module's winsys and present | M1 on | CI |
| Performance | BlitzQuake, MGLTest, `scenes3d`, `acvirgl_present_bench`, small-draw workload | M0, M6, M8, M11 | Lab |
| Commercial games | Quake, Quake II, BlitzQuake, RTCW | M6, M8 | Labs only, never repos or CI |

Tolerance everywhere it isn't 0: 8/255 a channel, with a one-pixel edge shift reported separately (as `Warp3D-Virgl-Route.md` defines it).

---

## 6. Risks

| Risk | Where | What we do |
|---|---|---|
| The merged generator changes a picture subtly | M5 | Pixel-equal differential test on the full key space before anything uses it; the old generators stay as the test's oracle until M6 is gated |
| The fragment work grows (one shader with more inputs is slower on the host) | M5 | Keys only switch on what a draw uses: an unused feature is not in the TGSI. Per-key shader length is compared old vs new in the test |
| Generation on the 68k costs frames | M5 | The prebuilt set from real traces; the measurement on a 68040 is a gate, not an afterthought |
| Warp3D's batch merging loses speed in the pipe | M6 | BlitzQuake within 2% is a gate; the merger moves as it is |
| One extra round trip per present | M8 | The prepare/finish hook keeps the present inside the frame's block |
| A lock held across a host round trip freezes a machine | M6, M8 | The lock order in 2.10, and the deadlock tests as gates |
| Old and new libraries meet in the field | M8–M10 | Wrappers for old LVOs, tested old+new pairs, NULL table on a version mismatch with fallback |
| The migration stalls half-way with two of everything | all | The debt list in CI only shrinks; each stage deletes what it replaces in the same release cycle; switches expire on a stated release |
| Measurement favours keeping a renderer | M11 | Fine: the decision record says why, and the matrix keeps its owner |

---

## 7. Decisions

**Decided by Dale, 23 November 2026, 03:53:** "i think as recommended for now". All six stand as recommended. "For now" is his wording: any of them can be reopened, and a change is recorded here with its date.

| # | Decision | Decided: as recommended |
|---|---|---|
| D1 | Static library first, Pipe.module later or never | Static through M9; module only if M10's RAM measurement shows it matters |
| D2 | The pipe's shader language: TGSI only, or a path to NIR later | TGSI now (virglrenderer takes it, and both generators write it). The generator is the only code that knows, so a later change is local |
| D3 | `ORTG_PRESENT_PREPARE` and `ORTG_PresentDone` public, or internal to our libraries | Public (one more flag and one LVO), documented as for GPU libraries; programs use plain `ORTG_Present` |
| D4 | How long the `PipeOld` switches live | One release after each stage's gate passes on the PiMiga and the CM4 |
| D5 | M11's margin: how close llvmpipe must be to the host cores before they go | Within 10% on the timedemos and no worse on the small-draw workload |
| D6 | Where ACMedia's full-screen frames go: the display plane (OpenRTG) or its own path | OpenRTG's MEDIA route, so one owner presents |

When each is first needed: D1 to D4 by M2, D6 by M8, D5 by M11.
---

## 8. Definition of done

The migration is complete when every line below is true, and CI checks those it can:

- one virgl encoder below Mesa (the pipe), and Mesa's own;
- one source of capabilities: all discovery in gfopengpu, every front end and the pipe reading only `OGPU_Query`, `OGPU_QueryLimit` and `OGPU_QueryDriver`; Warp3D and MiniGL report unchanged capabilities; EGL picks the same configs; the golden capability tests pass on every back end;
- one fixed-function shader generator;
- one resource manager;
- one presentation path (`ORTG_Present`, its hook, and the wrappers over it);
- one back-end chooser (`OGPU_Driver`);
- Warp3D and MiniGL draw only through the pipe;
- P96 and CyberGraphX are over OpenRTG (already true: `uaegfx.c`, `compat/cybergraphics_lib.c`);
- no front end can bypass gfopengpu or OpenRTG (`tests/ownership.debt` is empty);
- every new graphics feature has exactly one owner, named in the matrix.

## 9. First three pull requests

1. **M0 captures** (`team5/pipe-m0-baselines`): scripts, `tests/golden/pipe-m0/`, `tests/golden/caps/`, the key traces.
2. **M1 ownership check** (`team5/pipe-m1-ownership`): `tests/ownership_check.sh`, `tests/ownership.debt`, `repo-guard.yml`.
3. **M2 skeleton** (`team5/pipe-m2-screen`): `library/pipe/` with the screen and transport; MiniGL and Warp3D on them; three transport copies deleted.

---

## Appendix A: the consolidation checklist, against this plan

Every item of the checklist (Dale, 01:41), with the stage that delivers it. `[x]` is already true on main at a6cd53e.

### Where the checklist and this plan differ, and why

| Checklist | This plan | Why |
|---|---|---|
| Phase 2: create `virgl_cmd.h`, move MiniGL's and Warp3D's writers into it | Already done | `include/gfopengpu/virgl_cmd.h` is on main, and both routes use it. M0 records it as the first pixel-identical checkpoint (MiniGL and Warp3D on the shared writers, still owning their own state and resources) |
| Phase 3 pipe objects, then Phase 4 caching, then Phase 6 resources | M2 interface only, M3 resources, M4 state, M5 shaders | The code exists in MiniGL, so the pipe is extracted from it. Resources first, because state objects and shaders refer to them. The interface is frozen in M2 before anything is built behind it, as the review asks |
| Phase 5 before Phase 6 | Resources (M3) before shaders (M5) | As above: the shader generator is the riskiest step, and it goes on its own once the ground under it is proven |
| "gfopengpu owns virgl encoding, state caching, shader generation, arenas…" | The shared pipe owns them, inside gfopengpu's tree (`library/pipe/`, linked into the libraries, a gfopengpu module later) | Same owner in the matrix. The pipe is gfopengpu's part, behind a table, so programs never see it |
| `PIPE_SURFACE`, `PIPE_PRESENT_TARGET` | `ORTG_PresentInfo` and the present hook (2.7) | Presentation belongs to OpenRTG (rule 2), so the surface type is OpenRTG's. The pipe only hands over a resource handle |
| Phase 11 after everything | From M7, in parallel with M8 to M10 | The review's point 4: the bake-off needs comparable pictures, not the whole migration |
| Lock test in the regression suite | From M4 | The review's minor note: as soon as the pipe has a lock |
| Capability freeze in Phase 10 | M0 | Frozen before anything is wired, as the review asks |
| Capabilities as Phase 10, then (second review) Phases 0A, 4A–4D | Stage MQ, right after the pipe's interface (M2) and before the pipe takes anything over (M3) | The second review's milestone order: ownership and CI, the shared encoder (already on main), `OGPU_Query` and its golden data, then the pipe |
| Golden data as `.json` | `.txt`, as `tests/golden/` already is | One format for golden data, read by the C tests without a parser |

### Phase 0: architecture lockdown (M0.5)

- [ ] Ownership matrix in the architecture document (DESIGN.md)
- [ ] Single-owner rule in the contribution guidelines
- [ ] OpenGPU / OpenRTG boundary
- [ ] Shared pipe / OpenGPU runtime boundary
- [ ] OpenRTG / OpenMedia (ACMedia) boundary
- [ ] Mesa GL.module exception policy
- [ ] Resource, lock and capability rules

The ownership lists:

- **gfopengpu (its pipe):** virgl encoding, back-end selection, capability discovery, resource lifetime, renaming, reference counting, submission, state-object caching, fixed-function shaders, command arenas. M2–M9.
- **OpenRTG:** bitmap and screen composition, display modes, presentation, scanout, the rectangle walk, clipping for presentation, pixel format conversion, monitors. Mostly true today; presentation in M8.
- **Front ends:** API semantics, validation, API-visible state, display lists, extensions.
- **Front ends must not own:** virgl encoding, resource allocation, GPU discovery, capability scanning, presentation, back-end choice. Each one becomes a line of `tests/ownership.debt` in M1 and goes in its stage.

### Phase 1: CI enforcement (M1)

Fail the build if:

- [ ] MiniGL, Warp3D, OGLES3 or EGL writes virgl commands directly (debt: MiniGL and Warp3D until M6)
- [ ] any library bypasses `ORTG_Present` (debt until M8)
- [ ] any library opens ACRTG directly
- [ ] any library enumerates the host GPU (HELLO, CAPSET, CAPS)
- [ ] the host headers are included outside `library/opengpu`, `library/pipe`, `library/openrtg*`
- [ ] the Mesa exception breaks: GL.module reaches the host other than through `OGPU_Submit`, or the screen other than through OpenRTG

### Phase 2: `virgl_cmd.h`

- [x] `include/gfopengpu/virgl_cmd.h`
- [x] MiniGL's command writers in it
- [x] Warp3D's command writers in it
- [x] Common encoding helpers (byte order, block words)
- [ ] Resource and state packet builders (M3, M4: block and object creation move into the pipe)
- [ ] Verification captured: same stream, same pixels, same counters (M0)

### Phase 3: the pipe's foundation (M2)

- [ ] Context, resource, shader, state object, arena: the `OGPUPipeOps` table and handles, frozen
- [ ] Context owns blend, raster, depth, sampler, shader state, framebuffer, textures (2.3; built in M4)
- [ ] Resource owns textures, mip chains, render targets, vertex, index and depth buffers (2.3; built in M3)

### Phase 4: shared state tracker (M4)

- [ ] Blend, rasterizer, depth, stencil, sampler and vertex layout state objects
- [ ] Hashable keys, cached reuse, reference counts
- [ ] MiniGL's state cache removed (M4)
- [ ] Warp3D's state cache removed (M6)

### Phase 5: one fixed-function generator (M5)

- [ ] Texture environments, alpha test, fog, lighting, texgen, colour material, two-unit combinations, chroma key, specular
- [ ] Prebuilt: unlit textured, unlit two-unit, lit textured, lit two-unit, the fog variants
- [ ] Generation time, cache hit rates, MiniGL and Warp3D workloads measured
- [ ] The 68k path's cost recorded with the decision (the 68k core keeps its own fixed function)

### Phase 6: shared resource manager (M3, then M6)

- [ ] Texture create, update, delete; render targets; buffers; surfaces
- [ ] Reference counts, rename-on-write, deferred and fence-safe destroy, with their tests **before** any front end stops owning textures
- [ ] MiniGL: private textures removed, wraps the pipe's resource (M3)
- [ ] Warp3D: private textures removed, wraps the pipe's resource (M6)

### Phase 7: MiniGL (M3–M5, M7)

- [ ] Keeps the GL API, GL state, display lists, GLU, GLUT
- [ ] Loses its virgl encoder (M3), shader generation (M5), resource manager and arenas (M3), back-end choice (M9)
- [ ] MGLTest, gears, texture stress, immediate mode pass

### Phase 8: Warp3D (M6, M7)

- [ ] Keeps the Warp3D API, its state model, lock and unlock, direct mode
- [ ] Loses the virgl translator (M7), TGSI generator and state cache (M6), resource management (M6)
- [ ] `w3dcheck`, `w3darrbench`, texture tests, render-to-texture pass

### Phase 9: unified presentation (M8)

- [ ] `ORTG_Present` (with `ORTG_PRESENT_PREPARE`, `ORTG_PresentDone`, async fences) for virgl render targets, retained surfaces, bitmaps, ACMedia frames, software surfaces
- [ ] Clipping, rectangle walk, format conversion, flipping, scanout submission in OpenRTG
- [ ] Present code removed from MiniGL, Warp3D, GL.module, SDL2/3, retained surfaces
- [ ] ACMedia: decode stays in ACMedia; its frames follow the resource lifetime rule

### Phase 0A and 4A–4D: capabilities (MQ)

- [ ] `OGPU_Query` declared the only authority, in the ownership matrix (M0.5)
- [ ] CI: no `ACV_CAPS`, `ACV_CAPSET_INFO`, `ACV_HELLO` or host GPU reads outside gfopengpu; no capability tables in front ends (M1)
- [ ] Golden files for Warp3D (every flag), MiniGL (strings, limits, units, features), TinyGL and GL, EGL configs, on every back end (M0)
- [ ] `OGPU_Query` capability ops in the reserved range; the existing answers, formats and stream ops untouched
- [ ] `OGPU_QueryLimit`, `OGPU_QueryDriver`
- [ ] One-time discovery: virgl (CAPSET_INFO, CAPS, renderer, formats, limits, TGSI), llvmpipe, the CPU core, the stream driver; cached; invalidated on host reset only
- [ ] Mappings: Warp3D, MiniGL, EGL, the pipe's screen, each verified against its golden files
- [ ] FULL, PARTIAL and NONE tests (NPOT limits, software blend, limited depth formats)
- [ ] The first-cut op set (2.8); the rest `NONE` until implemented

### Phase 11: back ends (M11, from M7)

- [ ] Benchmark the CPU core, virgl on a GPU, llvmpipe, ACRTG.gpu's host core, the Vulkan compute path
- [ ] Workloads: `w3darrbench`, `scenes3d`, MiniGL and Warp3D tests, bridge stress
- [ ] Strategic: 68k `ogpu_3d.c`; host GPU virgl; host software llvmpipe
- [ ] Retire the host core's 3D path and the Vulkan compute path, on the decision record's date

### The regression suite (before every merge)

The suite is section 5: screens, Warp3D, MiniGL, films, cross-back-end validation (68k reference, virgl, llvmpipe, 8/255 and one pixel), and the concurrency tests.

### Definition of done

See section 8.
