# Warp3D.library on the virgl route (design note)

Task 34, 10 November 2026. Written from GFOpenRTG's own source (`library/warp3d`, `library/minigl/mgl_vg*`, `library/opengpu/ogpu_3d.c`, `host/virgl/acvirgl.c`) and Warp3D's documented API only.

## Where Warp3D draws today

`Warp3D.library` 5.0 is a thin front end. Every draw call adds OGPU stream v1.2 commands (`include/gfopengpu/stream3d.h`) to the context's batch, and `w3d_batch.c` is the only file that builds them. Vertices are already converted into the stream's fixed-point layout (`struct ovtx`: x, y and u, v as 16.16, ARGB32, z as u32, w as a float, specular plus fog). Culling, points and lines as quads, and all of Warp3D's state stay in the library.

A flush (`w3d_flush`) puts SURFACE and TARGET in front and runs the batch in one of two ways:

- **W3D_OpenGPU:** `OGPU_Submit` to gfopengpu.library. On AmiCradle its back end is ACRTG.gpu, and that runs the **software** 3D core on the PC, not the graphics chip.
- **W3D_CPU:** the same core linked in, on the 68k.

So no Warp3D program has ever touched the GPU. BlitzQuake's 35 to 38 fps is the software core.

## The new route

`ENV:Warp3D/Driver` takes `Auto`, `virgl`, `GFOpenGPU` or `CPU`, the same switch as MiniGL's.

- `Auto` picks virgl when gfopengpu.library carries `OGPU_OP_VIRGL` and the host says `ACV_HOST_GPU` (a real graphics chip, not llvmpipe).
- If it doesn't, Auto falls back to GFOpenGPU or the CPU, as today.
- `virgl` asks for virgl even without a real GPU (for tests).
- `W3D_DRIVER_3DHW` and `W3D_DRIVER_BEST` get virgl when Auto would.

The route is a **translator**. `w3d_vg.c` reads the finished batch, the same words the 3D core reads, and writes ACVirgl request blocks. Warp3D's front end does not change, so every Warp3D feature reaches the GPU through one path, and the CPU core stays the reference for what each command means.

| Stream command | On the host |
|---|---|
| RENDER3D, TEXENV | Keys for cached virgl objects: blend (or logic op), depth-stencil-alpha, rasterizer (scissor, stipple), and our own TGSI shaders (environment by texture base, chroma test, specular, fog by w, the current colour). |
| TEXTURE | A host texture for each Warp3D texture (keyed by its level-0 address), uploaded with `ACV_PUT` only when its generation changes. W3D_FreeTexObj and size changes let it go. |
| TRIANGLES (lists, strips, fans) | Runs of draws with the same state become **one indexed triangle list** (16-bit indices). The vertex fetch reads the stream's own layout (SSCALED x, y, u, v; USCALED z; float w; B8G8R8A8 colours), so the 68k converts nothing new. The batch's vertex memory goes up in **one PUT**. |
| DEPTH | A host depth buffer: Z16, Z24 (for Z32), or Z24S8 when there is a stencil buffer. |
| FILL, CLEAR3D | A virgl CLEAR when the whole area is cleared, otherwise a quad inside the clip. |
| CLIP | The scissor. |

Under perspective, clip w = 1 / the vertex's w, so the host interpolates texture coordinates as the core does. Colours, specular and fog are interpolated linearly across the screen (TGSI `LINEAR`), also as the core does. GLES hosts lack noperspective and fall back to perspective interpolation.

Vertex and index buffers are rings: no block writes over data that an earlier block's draws still read. minigl.library found that virglrenderer writes buffers unsynchronized.

## Point 1: sharing MiniGL's encoder

MiniGL's `mgl_vg_enc.c` writes virgl commands through a `GLcontext` and calls its own frame code when the stream fills, so it can't be linked in as it is.

The split that works is header-only. A first small PR moves the **protocol numbers** and **stateless command writers** into `include/gfopengpu/virgl_cmd.h`. Those are the commands (viewport, scissor, framebuffer, clear, draw, vertex and index buffers, constants, shader, bind, the ACVirgl block header) and the request-block word helpers. Each writer writes at a cursor the caller has made room for.

minigl.library's encoder becomes thin wrappers over it, and Warp3D calls the same writers. The proof is that the MiniGL host virgl test (`tests/run_minigl.sh` with `ACVIRGL_LIB`) gives the same pixels and the same counters before and after.

The frame logic (rings, PUT order, present) stays in each library. MiniGL's arenas follow GL's frame, while Warp3D's follow its batches and sync points. That part is shared later only if it turns out the same.

## Point 2: the present (no readback to the 68k)

The frame never comes back to the 68k.

- At each Warp3D sync point the block ends with `ACV_PRESENT`, the present stage MiniGL's route and GL.module use. The host writes the colour buffer straight into the drawing area's memory, in its format.
- With `AC_VIRGL_DIRECT` (Part B), a present into an **XRGB32/ARGB32** target with one rectangle in video RAM is written by the GPU itself.
- A **16-bit (RGB565)** target goes through the host's stage instead: the host reads back and converts on the PC, and the 68k does nothing.
- **BlitzQuake** opens a 640x480 16-bit screen, so it takes the RGB565 stage path, not Part B. It's worth measuring BlitzQuake on a 32-bit screen too, if it offers one. Part B for RGB565 would be a runtime change, which is not this PR.
- The 68k copies only where `ACV_PRESENT` can't write the area: a bitmap format other than R5G6B5 or A8R8G8B8 (Warp3D's existing ARGB32 copy and WritePixelArray), or a host without `ACV_HOST_PRESENT`.

## Point 3: Warp3D's own rules

- **Lock, unlock and flush points.** A batch that fills mid-frame is sent without a present. The frame is presented at W3D_UnLockHardware, W3D_Flush, W3D_FlushFrame, W3D_WaitIdle, W3D_SetDrawRegion (before the area moves), W3D_DestroyContext, and after each primitive when drawing is direct (not locked, not W3D_INDIRECT), as Warp3D's direct mode promises. Batches don't present between sync points.
- **Double buffering.** With one bitmap and a moving y offset, the present goes into the area at the offset the frame was drawn at, before SetDrawRegion moves it. With ChangeScreenBuffer or the program's own flips, the program syncs before it flips, so the frame is in its bitmap first.
- **CPU 2D between 3D batches.** After a present, the next draw first loads the area's pixels into the colour buffer (`ACV_PUT` from the bitmap, which is a guest-to-GPU upload). A 16-bit area goes through a B5G6R5 stage and a GPU BLIT. Anything the program drew with the CPU since the last sync is kept, in order. The load is skipped when the frame starts by filling the whole area. Its cost per frame goes in the measurements.
- **Z and stencil reads and writes** (W3D_ReadZPixel/Span, ReadStencilPixel/Span, WriteZ*, WriteStencil*, FillStencilBuffer). These flush, then copy the host's depth or stencil buffer back into Warp3D's own Z or stencil memory (`ACV_GET`, converted from the host's format), and send writes back up. They cost a round trip, but only games that use them pay it.

## Point 4: tests and tolerance

`tests/test_w3d_vg.c` runs on a PC with virglrenderer, through `minigl_vg_bridge`. It draws scenes through the 3D core and through the translator and compares the two:

- `tests/scenes3d.c`'s bench scenes (W3DTest, Cow3D and MGLTest-shaped);
- its random scenes that use only Warp3D's subset (ARGB32 textures, ARGB32 or RGB565 targets, the 3D TRIANGLES form);
- one new scene that mixes CPU 2D and GPU 3D: draw, present, the CPU writes into the bitmap, draw again, present. Its 2D pixels must be in front of and behind the right triangles.

For each scene the test reports the **worst channel difference** and how many pixels differ by more than the tolerance, with and without a one-pixel shift for edges. The tolerance is **8 of 255 a channel**. That covers bilinear filtering (the core picks a mip level once a triangle, the GPU per pixel), 8-bit fixed point against float, and dithering, which the GPU leaves out. Fog, chroma key and alpha test are reported on their own, since those are where differences show.

Known differences, documented rather than hidden:

- logic ops on an RGB565 target work on 8-bit channels, not the 565 word;
- the alpha test's EQUAL and NOTEQUAL compare floats.

## Point 5: clean room

Our own code, the documented Warp3D API (autodocs) and the NDK only. No Warp3D, WarpMiniGL or PiStorm3D source was read. virgl's numbers are virglrenderer's protocol (Mesa's MIT headers), as minigl.library's route already uses them.

## Measuring

- BlitzQuake 1.50 and 1.31, `timedemo demo1`, 640x480.
- `ENV:Warp3D/Driver` virgl, GFOpenGPU and CPU, 3 runs each, dropping the first run after boot.
- Lab: a copy of Instance-32 (open ROM, GFOpenUp 0.8.27).
- Screenshots of each route, and the runtime's virgl error count.

Builds run on origimagic only.
