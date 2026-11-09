# minigl.library-compatible over GFOpenGPU's virgl path: implementation plan

> **Clean-room interface spec: written from published interface facts only; implementers must not read PiStorm3D/MiniGL sources.** This plan uses only `MiniGL-Compatible-Spec.md`, the OpenGL 1.1 specification, Mesa's MIT virgl protocol header and our own MIT code.

Team 5 helper, for Dale, 9 November 2026. IDEA 5 of `pistorm3d-perf-notes-20261109.md`. Estimates are marked *(estimate)*; nothing here has been built or measured yet.

## 0. The short version

- **We already have a minigl.library.** `library/minigl` in this repository is an MIT minigl.library 29 (8 October 2026) with the same ABI 5 table: 205 entries, 840 bytes. It runs the classic MiniGL demos and has its own GLU and GLUT. But it does the whole GL 1.1 front end on the 68k (transform, lighting, clipping) and sends finished triangles as GFOpenGPU 3D stream ops (`stream3d.h`), which the host draws on the PC's **CPU**. It does not use virgl or the GPU.
- **IDEA 5 is therefore a third route in that library, not a new library.** The new route, "virgl", keeps the table, the state code, the display code, GLU, GLUT and display lists. It replaces the per-vertex work and the output with:
  - a small virgl encoder of our own,
  - fixed-function shaders we write ourselves (TGSI text) that do transform, lighting, fog and texture combining on the host GPU,
  - one append-only arena per frame,
  - one request block per frame,
  - and the frame written into the screen with `ACV_PRESENT`.
- **What it buys** *(estimate)*: per draw, the 68k does a hash-free state check, a copy of the vertex data into the arena and about 10-40 command words. Today a GL 1.1 game on GL.module pays for Mesa's whole state tracker and virgl encoder on the 68k for each draw. MiniGL games also get the GPU instead of the host CPU rasteriser.
- **Effort** *(estimate)*: **about 8 weeks for one developer** on top of the existing library (section 9), including 1-2 weeks of fixing found by running games. From scratch it would be about 12-14 weeks.
- **First, two decisions for Dale:** the licence question (spec section 12), and the version number. The existing library reports 29; programs built with the 30.0 SDK ask `OpenLibrary` for 30 and would not open it. Recommendation: 30.x.

## 1. Where things are (all ours, MIT)

| Piece | Path | Used for |
|---|---|---|
| minigl.library 29 | `gfopenrtg/library/minigl/` | The base: table (`minigl_api.txt` → `gen_api.py`), state (`mgl_state.c`), matrices, textures' GL state, display/input (`mgl_display.c`), lists, GLU, GLUT, the current routes (`mgl_pipe.c` → `mgl_out.c`) |
| GL.module's virgl winsys | `gfopenrtg/library/modules/gl/gla/gla_virgl.c` | How we build ACVirgl request blocks: the block header, `ACV_RES_CREATE`, `ACV_PUT`/`GET` with a swap unit, `ACV_SUBMIT`, the context and resource ids (their addresses) |
| The transport on OS 3 | `gfopenrtg/library/modules/gl/gla/os3/gla_virgl_os3.c` | A block is one `OGPU_OP_VIRGL` command in a batch, `OGPU_Submit` + `OGPU_Wait`, the "expired fence" rule |
| The present | `gfopenrtg/library/modules/gl/gla/os3/gla_present_os3.c` | `ORTG_PresentVirgl` (gfopenrtg.library 0.15+): the host writes the frame straight into the window's visible pieces or the screen bitmap |
| The protocol | `gfopenrtg/include/gfopengpu/virgl.h` | ACVirgl commands, swap units, `ACV_PRESENT`, the host flags |
| The host side | `amicradle/vendor/openamigartg/host/virgl/acvirgl.c` | No change needed to start. Later, the depth-store skip at present (perf notes, section 4) |
| virgl's command encoding | Mesa 26.2.4 `src/virtio/virtio-gpu/virgl_protocol.h` (MIT), already fetched for GL.module | The `VIRGL_CCMD_*` and `VIRGL_OBJ_*` layouts. Include the header; write the encoder ourselves |

The spec and this plan live in `gfopenrtg/docs/design/`, next to the code they are about. `amicradle-guest` holds no GL code.

## 2. Architecture

```
program ── table (ABI 5) ──► minigl.library
                               ├─ state (existing mgl_state.c, matrices, lists, GLU, GLUT, display)
                               ├─ route CPU / GFOpenGPU stream3d (existing, unchanged)
                               └─ route VIRGL (new)
                                    ├─ mgl_vg_state.c   GL state → keys → cached virgl objects
                                    ├─ mgl_vg_draw.c    begin/end, arrays, lock arrays → arena + DRAW_VBO
                                    ├─ mgl_vg_tex.c     textures → resources, PUT, rename, sampler views
                                    ├─ mgl_vg_shader.c  our TGSI text for each fixed-function variant
                                    ├─ mgl_vg_enc.c     the virgl command encoder (little-endian words)
                                    └─ mgl_vg_frame.c   per-frame arenas, the block, present
                                         │ one ACVirgl block per frame (or per cap, see 5)
                                         ▼
                               gfopengpu.library ── OGPU_OP_VIRGL ──► ACRTG ring ──► acvirgl.c ──► virglrenderer ──► GPU
```

Route choice, at context creation: `ENV:MiniGL/Driver` gets a new value, `Virgl`. `Auto` takes virgl when `OGPU_Query(OGPU_OP_VIRGL)` answers "full" and `ACV_HELLO` + `ACV_CTX_CREATE` succeed (as GL.module does). Otherwise it takes the existing routes. A failed route says why, on the program's output and in the runtime's log, as GL.module's softpipe notice does.

The front end changes with the route. On virgl, positions are **not** transformed on the 68k: object-space data goes into the arena and the matrices go to the vertex shader as constants. Clipping is the GPU's. Lighting and texgen are in the shader.

## 3. How calls map to virgl

### 3.1 Context creation

1. Open the display as today (`mgl_display.c`): screen, window, adopted window or bitmap.
2. `ACV_HELLO` (needs `ACV_PROTOCOL` 1; read the host flags: `ACV_HOST_PRESENT`, `ACV_HOST_GLES`), then `ACV_CTX_CREATE` with the context id = the address of our context (unique per program, as GL.module does).
3. `ACV_CAPSET_INFO` + `ACV_CAPS`: read `virgl_caps_v2` for the primitive mask (GLES hosts have no quads or polygons), the formats, and whether polygon modes, dual-source blending and the like exist. Keep a feature mask.
4. Create resources with `ACV_RES_CREATE`, each with a copy in Fast RAM (`AllocVec`):
   - colour: a 2D render target at the drawable's size, `B8G8R8A8_UNORM` (what `ACV_PRESENT` reads; check the host's format list);
   - depth: `Z32_FLOAT` or `Z16_UNORM` per `mglChooseZBufferDepth`; with stencil later, `Z24_UNORM_S8_UINT`;
   - the frame arenas (section 5): a vertex buffer, an index buffer.
5. Create the fixed objects once: surfaces for the colour and depth targets, `SET_FRAMEBUFFER_STATE`, and a default blend, DSA and rasterizer object.
6. Make sure no lock is held after creation. A held display lock between frames froze the machine with MiniGL (RTCW notes, point 1).

Resize: recreate colour, depth and their surfaces, keep everything else. Delete: `ACV_RES_UNREF` everything, `ACV_CTX_DESTROY`.

### 3.2 Per call

| GL calls | virgl | Notes |
|---|---|---|
| `glClear` | `VIRGL_CCMD_CLEAR` (colour, depth) | With the scissor on, or a colour mask that isn't all on: a quad with a "clear" shader instead, as GL requires |
| `glViewport`, `glDepthRange` | `SET_VIEWPORT_STATE` (scale and translate computed on the 68k) | Only when changed |
| `glScissor` + enable | `SET_SCISSOR_STATE` + the rasterizer's scissor bit | |
| `glBlendFunc`, `glBlendFuncSeparate`, `glBlendEquation`, `glColorMask`, `GL_BLEND` | BLEND object | Cached by key (section 4) |
| `glDepthFunc`, `glDepthMask`, `GL_DEPTH_TEST`, `glAlphaFunc`, `GL_ALPHA_TEST` | DSA object (it carries the alpha test; virglrenderer emulates it where the host has none) | Cached by key |
| `glCullFace`, `glFrontFace`, `GL_CULL_FACE`, `glPolygonMode`, `glPolygonOffset`, `mglSetZOffset`, `glShadeModel`, `glLineWidth`, `glPointSize`, scissor enable | RASTERIZER object | Flat shading uses the rasterizer's flat-shade bit and provoking vertex = last (GL's rule). GLES hosts have no polygon mode: draw lines and points ourselves from the triangle indices |
| Texture env, fog, lighting enables, texgen, colour material | the shader variant (3.4) + constants | |
| Matrices | constants (`SET_CONSTANT_BUFFER`, vertex shader slot 0) | Only when changed; the modelview-projection product and, with lighting, the modelview and its inverse transpose, computed on the 68k |
| Current colour/normal/texcoord outside `glBegin` | a constant ("default attribute") when the array is off | Avoids per-vertex copies of a constant |
| `glBegin`...`glEnd`, `glDrawArrays`, `glDrawElements`, `glMultiDrawArrays`, `glArrayElement`, lists | data into the frame arena, then `SET_VERTEX_BUFFERS` (when the layout changed), `SET_INDEX_BUFFER` (when indexed) and `DRAW_VBO` | Quads, quad strips and polygons become triangle indices on the 68k when the host lacks them; line loops likewise |
| `glBindTexture` + enable on a unit | `SET_SAMPLER_VIEWS`, `BIND_SAMPLER_STATES` | Per-texture sampler view and sampler state, created once and kept |
| `glTexImage2D` | `ACV_RES_CREATE` (2D texture, the format, mip levels) + `ACV_PUT` | 3.3 |
| `glTexSubImage2D` | `ACV_PUT` of the box, or a rename (3.3) | |
| `glCopyTexImage2D`, `glCopyTexSubImage2D` | `RESOURCE_COPY_REGION` from the colour target, with the rows flipped by a `BLIT` | Stays on the host GPU. No readback: WarpMiniGL measured 130 ms a frame for the round trip |
| `glReadPixels`, `mglLockBack` | finish the frame so far, then `ACV_GET` of the box (`B8G8R8A8` → the format asked) | Slow by nature. `mglLockBack` writes the buffer back with `ACV_PUT` at `mglUnlockDisplay` |
| `glFinish` | finish the frame so far (send the block) | |
| `glFlush` | nothing | |
| `mglSwitchDisplay` | end the frame (section 6) | |
| `glColorTable` + `GL_COLOR_INDEX` uploads | expand through the palette on the 68k at upload | Rare. Only at load time |

### 3.3 Textures

- **Formats, no expansion on the 68k.** `GL_UNSIGNED_BYTE` RGBA goes as `R8G8B8A8_UNORM` (`ACV_SWAP_NONE`: bytes are bytes). RGB goes as `R8G8B8X8` after a 3→4 byte copy (GL hosts could take RGB8, GLES ones not). The packed 5-6-5 and 4-4-4-4 types go as `B5G6R5`/`A4B4G4R4` with `ACV_SWAP_16`, so the host swaps them. Luminance and luminance-alpha go as `L8`/`L8A8` where the host has them, else expanded. Never build mipmaps on the 68k except for `gluBuild2DMipmaps`, which can ask the host instead (a blit chain on the GPU).
- **Mip chains.** Level 0 creates a resource with one level. When level 1 arrives, create a resource with the full chain, copy level 0 into it on the host (`RESOURCE_COPY_REGION`), and drop the old one. Each later level is one `ACV_PUT`.
- **Updates between draws of one frame (renaming).** Every texture records the frame number and the arena position of the last draw that used it.
  - An update to a texture **not** used yet in this frame: an `ACV_PUT` placed in the block before this frame's commands.
  - An update to a texture already used in this frame: GL says earlier draws see the old texels. Make a **new resource version**, copy the old texels on the host (`RESOURCE_COPY_REGION`, all levels), `ACV_PUT` the update into the new one, and point later draws at it. The old version is released after the frame's block has run.
  - Lightmap updates in Quake-family games take the second path. On the host each version is a separate resource, so Mesa v3d never sees a write to a busy texture and does not split the job (perf notes, idea 1). Measure it in M5.
- **The order inside a block** follows GL's order: puts for not-yet-used textures, arena puts, then the frame's command stream (`ACV_SUBMIT`). Any update of a texture already used this frame triggers the rename above, so it never needs to go between two draws.

### 3.4 The fixed-function shaders (ours)

Written by us as TGSI text, from the OpenGL 1.1 specification's equations (sections 2.10-2.13 and 3.7-3.9). They are sent once per context (`CREATE_OBJECT` SHADER, then `BIND_SHADER`).

**Vertex shader variants**, selected by a key:
- position: object space × MVP (always);
- colour: the vertex colour, or the current colour from a constant;
- texture coordinates per unit (0, 1 or 2 units): pass, × texture matrix, or texgen (object linear, eye linear, sphere map);
- lighting: off, or 1-8 lights (directional, point with attenuation, spot), with colour material and local viewer (needs the normal and the modelview);
- fog coordinate: off, or the eye distance (fog factor in the fragment shader).

**Fragment shader variants**, selected by a key:
- unit 0's function: none, modulate, replace, decal, blend, add;
- unit 1's function: none, modulate, replace, decal, blend, add, applied to unit 0's result in the **same pass** (our existing library draws unit 1 as a second pass);
- fog: none, linear, exp, exp2;
- alpha-only and luminance texture handling where the host format can't express GL's base format.

The alpha test is in the DSA object, not in the shader.

That is about 6 × 7 × 4 = 168 fragment combinations and a few dozen vertex ones, but games use few of them. RTCW and Quake need about 6-10 pairs. So **variants are generated from templates when first used**, then cached per context (the TGSI is built from fragments, about 1-3 KB of text, a one-off cost of about 1 ms on the 68k *(estimate)*). The common dozen are prebuilt as constant strings, so the first frame has no cost. The host's Mesa disk cache keeps the compiled results across runs, which must be persistent on the card (perf notes).

### 3.5 Byte order

- The command stream is little-endian words. Our encoder writes each word swapped (three instructions on the 68k: `rol.w #8 / swap / rol.w #8`), and objects that never change (default objects, shader text) are encoded once at context creation.
- Vertex and index data stay in the 68k's order. The host swaps them in `ACV_PUT`:
  - floats: `ACV_SWAP_32`;
  - colours: written by the 68k as one longword `A<<24|B<<16|G<<8|R`, which after the 32-bit swap is the bytes R, G, B, A in memory: `R8G8B8A8_UNORM` as a vertex attribute, so the **whole vertex arena is one put with `ACV_SWAP_32`**;
  - indices: a separate arena of 16-bit indices with `ACV_SWAP_16` (8-bit indices widened while copying; 32-bit ones copied to a 32-bit arena with `ACV_SWAP_32`).
- Constants go inline in the command stream (`SET_CONSTANT_BUFFER`), swapped word by word on the 68k. That is 16-80 words when a matrix or a light changes, not per draw.
- The "little-endian arrays" capability (spec 9.1, token 0x701D): when it is off, the data is already little-endian. Copy it into a separate little-endian arena that is put with `ACV_SWAP_NONE`, so nothing is swapped twice.

## 4. State caching

- **Keys.** Blend (factors, equations, colour mask, enable), DSA (depth function, mask, enable, alpha function, reference, enable) and rasterizer (cull, front face, polygon mode, offset factor and units, flat shading, line width, point size, scissor enable) each pack into one to three longwords on the 68k.
- **Objects.** A small table per type (64 entries, direct-mapped on the key, no hashing of floats; the offset's two floats are compared as bits). A miss creates the object (`CREATE_OBJECT` with a handle we choose: an index in our table). A hit costs one compare. Entries are never destroyed while the context lives, which bounds them in practice: games use few combinations.
- **Binding.** Per type, the last bound handle. A draw emits `BIND_OBJECT` only when it changed.
- **Dirty bits.** One longword per context: viewport, scissor, framebuffer, constants (by range: MVP, modelview, lights, fog, texenv colour), vertex layout, sampler views per unit, shader keys. A draw with nothing dirty emits only `DRAW_VBO`, plus `SET_INDEX_BUFFER` if the index arena offset moved. One `SET_INDEX_BUFFER` per frame is enough when draws use the index-arena offset as `start`.
- **Vertex layouts.** A layout is a VERTEX_ELEMENTS object: which attributes, at which offsets, of what format. There are about ten in practice (position only, +colour, +tc0, +tc0+tc1, +normal ...). Each is created once and found by a small key.
- **Measure from the start.** Counters for draws, bound objects, created objects, words per draw, arena bytes and blocks per frame, readable with a trace variable (as `ENV:MiniGL/Trace` does today).

## 5. The per-frame arena and the block

- **Three arenas per context**, allocated at context creation and never freed mid-frame: vertices (default 4 MB), indices (1 MB) and commands (256 KB) *(sizes are estimates: RTCW's first level is about 30,000 indices = 60 KB and a few hundred KB of vertices a frame)*. Each is a bump pointer reset at `mglSwitchDisplay`. The host-side resources have the same sizes.
- **Two sets**, used alternately by even and odd frames, so the next frame can be built while the host still runs this one. That is for later, once the transport is asynchronous (option C in Design-VIRGL-Transport); until then one set is enough.
- **Overflow** (an arena or the command buffer is full mid-frame): send what is there as a block (puts of the used ranges + `ACV_SUBMIT`), reset, and carry on. **Never drop geometry.** Grow the arena at the next frame boundary (the existing GL.module rule: grow once, keep).
- **The block for a frame**, in order:
  1. `ACV_PUT` for each texture update that went before first use (3.3);
  2. `ACV_PUT` of the vertex arena's used range (`ACV_SWAP_32`) and of the index arena's (`ACV_SWAP_16`);
  3. `ACV_SUBMIT` of the command stream;
  4. `ACV_PRESENT` (section 6), or nothing for `glFinish`.

  One block a frame stops the 68k for the host's whole frame time. Design-VIRGL-Transport proposed a cap of about 2 ms per block: split the stream at a draw boundary every about 64 KB of commands or puts. With the rename rule there is no ordering hazard in a split.
- **Locked arrays (`glLockArrays`).** At lock: copy the enabled arrays' range [first, first+count) into the vertex arena once, in our interleaved layout, and remember its base. Every `glDrawElements` before the unlock copies only its indices (biased: `index_bias` = base − first) and emits `DRAW_VBO`. Positions are never copied twice per lock, which is the PiStorm3D technique, and a lock up to 65,536 vertices is one range. Without a lock, `glDrawElements` must copy the vertices its indices reach: find min/max index (one pass over the indices), copy that range, bias. `glDrawArrays` copies its range.
- **Immediate mode.** `glVertex*` appends one interleaved vertex in the current layout (only attributes in use since `glBegin`). Most MiniGL games use arrays; immediate mode only needs to be correct and not slow.

## 6. Presenting

- `mglSwitchDisplay` closes the frame and sends its block with `ACV_PRESENT` at the end. ACV_PRESENT tells the host to read the colour target back and write it, converted to the target's format (`XRGB32`, `ARGB32` or `RGB565`) and flipped (GL's rows are bottom-up), straight into Amiga memory:
  - **Window, adopted window:** through `ORTG_PresentVirgl` (gfopenrtg.library 0.15 or later), which walks the window's visible rectangles and writes only those (up to 64 per command). That is GL.module's path today.
  - **Fullscreen screen:** the screen's bitmap, one rectangle.
  - **Bitmap context:** the program's bitmap.
  - **No `ACV_PRESENT`** (an older runtime): `ACV_GET` into a Fast RAM buffer, then `WritePixelArray`. Correct but slow.
- No display lock is taken by the library between frames. `ORTG_PresentVirgl` takes what it needs inside the call.
- **Sync:** with `mglEnableSync` on, or 2-3 buffers chosen, wait for the vertical blank (`WaitTOF` or the ACRTG's blank signal) before the block's present. With one buffer: no wait.
- **Depth** is not needed after the frame. When the depth was cleared at the start of the frame, the host could skip storing it (perf notes: about 1.2 MB of writes a frame at 640×480). That needs an `acvirgl.c` change, so it is listed as a follow-up.
- Later: KMS-plane present for fullscreen (perf notes 3b) applies to this route as to GL.module.

## 7. Test plan

**On the PC (no Amiga), every commit:**
- `tests/run_minigl.sh` already builds the library on the PC under ASan/UBSan and drives the table. Add the virgl route there with `acvirgl_run` called directly (as `test_gla.c` does) on the PC's virglrenderer.
- An ABI test: the table's size (840), every entry's offset (spec section 8), the five header fields, and the type sizes (spec section 6).
- Golden pictures: the existing `tests/golden/minigl-scene.txt` scene and one small program per feature (each texture environment pair, fog modes, alpha test, blend factors, polygon offset, texgen, lights, quads and polygons, lock arrays, sub-image update between draws). Compare against the existing CPU route with GL.module's tolerance: 99% of pixels within 8 on every channel.

**On lab instances (Cradle labs; never Dale's Instance-NN, never the PiStorm card):**

| Program | What it checks | Source |
|---|---|---|
| MGLTest, MGLTestStatic, MGLGlutTest | Whole table, GLUT, both link styles | ours |
| Classic MiniGL demos (gears, cube, ring, warp, bounce, smtest, mtex) | Old-style programs, `mglLockBack`, multitexture | the classic package, local copies |
| Mesa's Open demos boing and chrome gears via our library | Display lists, sphere map | ours (already measured on the CPU route: 17.1 / 27.3 fps 68k, 36.9 / 75.4 fps GFOpenGPU at 320×240) |
| **RTCW port** (`ReturnToCastleWolfenstein_Minigl68k`) | The main target: lock arrays, multitexture, `glDrawElements`, `mglLockMode`, input window | GitHub, GPL; needs the game data (Dale's copy) |
| glquakelib, Quake 2 `ref_gl_shared`, Heretic II `ref_minigl`, ioq3 minigllib, glxsglut | The five shared-library clients named in the 29/30 SDK | binaries from their release archives, if published (**open**: availability) |
| WarpMiniGL v0.3 + FreeSpace or Payback | A Warp3D shim over our minigl.library (optional: we already have Warp3D) | LGPL binary, user's choice |

**Measurements**, per program, on the PC runtime and on the Pi 5 / CM4 cards: frames a second at 640×480, 68k µs per draw, words per draw, blocks per frame, the host's time per block, V3D jobs per frame (`V3D_DEBUG=perf`). Reference numbers to beat: RTCW `escape1` about 24 ms a frame on PiStorm3D (Pi 4), of which 2-6 ms in draws.

**The black-box tests B1-B9** of the spec, run in M1 (B1-B4) and M6 (B5-B9).

## 8. Risks

| Risk | Effect | What we do |
|---|---|---|
| Mesa v3d still splits jobs on the host (perf notes, idea 1) | Lower GPU throughput on the Pi | The rename rule (3.3) and arena sizes avoid the known causes; measure jobs per frame in M5 |
| GLES hosts (the Pi cards) lack quads, polygon mode, some formats, `GL_CLAMP`/border colour | Wrong pictures | Read the caps at creation (3.1); convert quads/polygons to triangles, lines for polygon mode, `GL_CLAMP` → clamp to edge |
| One synchronous block per frame stalls the 68k for the host's frame time | Lower fps than possible | The 2 ms cap (5); then the transport's option C (asynchronous) |
| The game's own 68k code is the bottleneck (RTCW: 40 fps on PiStorm3D with MiniGL taking only 2-6 ms) | Smaller gains than hoped | Measure before tuning; the gain is largest for games that spend their time in GL |
| Behaviour differences found only in games (spec section 10) | Visual bugs | The B7 census in M1 tells us what each game uses before we implement it |
| Licence | Unable to ship | Dale's decision, spec section 12 |

## 9. Milestones and effort *(estimates, one developer)*

| # | Milestone | Effort |
|---|---|---|
| M0 | Dale's decisions: licence, version 30, route names. Spec review. | — |
| M1 | Virgl route skeleton: transport, context, colour/depth, clear, present (window, screen, bitmap), the ABI test, B1-B4. Trace build for the B7 census, run on the target games through the existing route. | 1 week |
| M2 | Arrays and immediate mode into the arena, matrices as constants, the basic shader pair, the state objects, viewport/scissor, primitive conversion. MGLTest scene matches the golden picture (untextured parts). | 1.5 weeks |
| M3 | Textures: formats, 16-bit, mip chains, sampler states, renaming, copy-tex, palettes, read-back, `mglLockBack`. | 1.5 weeks |
| M4 | The shader generator: two-unit environments, fog, alpha test, texgen, lighting, colour material, polygon offset and Z offset. All golden programs pass. | 1.5 weeks |
| M5 | Lock arrays, index biasing, arena sizing and overflow, the dirty-bit/cache tuning, the 2 ms cap; measurements on PC, Pi 5, CM4. | 1 week |
| M6 | Games: RTCW first, then the Quake family, Heretic II; B5-B9; fixes. | 1-2 weeks |

**Total: about 7.5-8.5 weeks**, of which about 1 week is uncertainty in M6. Most of the existing library (state, display, input, lists, GLU, GLUT: roughly 60% of its 8,800 lines, *estimate*) is reused unchanged.

## 10. The clean-room rule for whoever builds this

- Read: `MiniGL-Compatible-Spec.md`, this plan, the OpenGL 1.1 specification, GLU/GLUT documentation, Mesa (MIT) for the virgl protocol, and any AmiCradle code.
- Do not read: PiStorm3D, MiniGL (any branch, any version), ogles2's library, WarpMiniGL's sources, or their SDK's import-library sources. If a behaviour is unclear, add it to the spec's section 10 and find it with a black-box test.
- The published SDK headers are needed only by the spec writer. The library is built against our own generated headers (`gen_api.py`).
- Commits say where each behaviour came from: the spec, the GL spec, or a black-box test (named).
