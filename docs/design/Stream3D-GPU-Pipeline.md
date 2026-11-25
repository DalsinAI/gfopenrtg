# Stream3D on the GPU's own pipeline (design, 25 November 2026)

Dale, 25 November: "go on lets do this", after M11 showed both of the stream
route's 3D halves are software rasterisers (the host core in C, and the same
rasteriser as a Vulkan compute shader). This note is the plan for a third
half that uses the graphics chip the way a game expects: a Vulkan
**graphics** pipeline, the chip's own rasteriser, depth test and blender.

## What the probe showed

`vkprobe` (a 300-line Vulkan program, kept with this note's PR as
tools/vkprobe) drew 576 two-triangle quads a frame, each its own draw call
with a state change, into a 640x480 16-bit image, and copied the frame in
from host memory and back out each frame, as Stream3D must:

| V3D 7.1 (PiMiga, v3dv, Mesa 26.2) | fps | ms a frame |
| --- | --- | --- |
| 576 quads, 16-bit frame | 1,729 | 0.58 |
| 576 quads, 32-bit frame | 1,228 | 0.81 |
| 4,608 quads, 16-bit frame | 640 | 1.56 |

On the same Pi, SMALL (the same shape from a 68k program) is 268 fps on virgl
and 719 on the host core. The chip is not the limit. v3dv's graphics
pipelines compile at once (3 ms); the hang that keeps the Vulkan back end
off V3D (amicradle #949) is v3dv's register allocator failing on Team 4's
large compute shaders, which this path doesn't use.

## Where it sits

- **ogpu3d_gfx.c** (host/vulkan), beside ogpu3d_rec.c. ogpu_vk.c tries it
  first for TRIANGLES, then the compute shader (rec), then the C core. It
  shares ogpu_vk.c's device and queue.
- It reads the **TRIANGLES command's vertices and the 3D state**
  (RENDER3D, TEXENV, TEXTURE, DEPTH, as ogpu_3d.c has decoded them), not
  the core's per-triangle planes and spans. Triangle setup moves to the GPU.
- Selected with `AC_VK_3D=gfx` (or `OGPU_VK_3D=gfx`) while it is new. It
  becomes the default where the device has it, once it passes the tests
  below on the RX 460 and the Pi 5.

## Pictures: within tolerance

stream3d.h already says it: "3D pictures are compared with a tolerance (+-2 a
channel, edge pixels excepted): a GPU never matches an integer core bit for
bit." The C core stays the exact reference (and the 68k's). The graphics
path is tested against it with the existing scenes (tests/scenes3d.c's
random and bench scenes) within that tolerance, and in a lab with W3DCheck
(which already takes each driver's tolerance from OGPU_Driver) and MGLTest.

## The frame and buffers in Amiga memory

The target, Warp3D's Z buffer and the textures live in the Amiga's memory,
big-endian, and must be right in memory when a run returns (as on the
core). For each run of TRIANGLES into one target:

1. Rows the run draws (the triangles' bounding box, clipped) go from memory
   into a GPU image: byte-swapped on the CPU into a staging buffer where the
   format needs it, then `vkCmdCopyBufferToImage`.
2. The triangles are drawn.
3. The rows are copied back and swapped back when the GPU has finished.

Formats: RGB565PC, BGRA32 and RGBA32 match Vulkan formats as they are;
RGB565, RGB555, ARGB32, XRGB32 and ABGR32 are the same after a byte swap.
CLUT8 and INDEX8 targets are declined (the core draws them). Z16 and Z32
become D16_UNORM and D32_SFLOAT the same way. Stencil is declined in the
first cut.

Later, where the driver allows it: a linear image bound over video RAM
itself (V3D takes linear colour attachments in all three formats), so a
frame in video RAM needs no copy at all.

## What the pipeline does per draw

- **Vertex shader:** x, y (16.16 screen) to clip space; z (u32) to depth;
  under PERSPECTIVE, clip w = 1/w so texture coordinates and colours are
  perspective-correct (stream w is 1.0 at the front plane).
- **Fixed function:** viewport, scissor (the clip), depth compare and write,
  blend factors (Warp3D's numbers map to Vulkan's one to one, constant
  colour included), colour write mask.
- **Fragment shader** (one, with push constants): texture environment
  (REPLACE, DECAL, MODULATE, BLEND, ADD, SUB, OFF) with the base formats,
  specular, fog (LINEAR, EXP, EXP2, VERTEX), alpha test. Pipelines are made
  per state key (blend, depth, mask) and kept.
- **Textures:** converted to RGBA8 once and cached by a hash of their texels,
  as ogpu3d_rec.c's cache does; filter and wrap become a sampler. Mip
  levels as given.
- Declined in the first cut, so drawn by the core as today: logic ops,
  stencil, chroma test, stipple, CLUT8 and INDEX8 targets, PIXELART.

## The 68k half (step 2)

On the stream route minigl.library transforms, lights and clips each
vertex in 68k code, which caps GLQuake at about 35-45 fps whatever draws it.
Step 2 sends MiniGL's vertices untransformed with the matrices and light
state, as its virgl route already does (mgl_vg_draw.c), so the GPU does
that work too. That is a new stream command and is designed after step 1
is measured.

## Steps

1. ogpu3d_gfx.c, the hook in ogpu_vk.c and `AC_VK_3D=gfx`; a host test over
   scenes3d within tolerance (lavapipe in the kitchen, v3dv on a Pi).
2. Timed in labs: daletop (RX 460) and a Pi 5 card, W3DTest, W3DArrBench,
   SMALL, GLQuake, against virgl and the core.
3. The V3D gate in amicradle's ogpu_vk.c lets the graphics path through
   while keeping the compute shaders off.
4. MiniGL's untransformed vertices (above).
