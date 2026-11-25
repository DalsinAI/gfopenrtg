# M11: the renderer bake-off (decision record)

GF-66, the shared pipe's M11 (docs/design/OpenGPU-Shared-Pipe-Plan.md, M11 and
phase 11). Status: **first numbers, no decision yet.** Nothing is deleted until
Dale says yes and a deletion date is written here.

## What was measured

The same programs, from the same lab and the same boot, under each value of
`ENV:GFOpenGPU/Driver` (gfopengpu.library 0.32, M9). Each time is one run of
the program's fixed frame count, at 320x240 in 16 bits.

- **Lab:** t2-rimmer-m8-20261124, a copy of Instance-23 on GFOpenRTG.
- **Host:** daletop (Ryzen), labs muted.
- **The host draws virgl on a graphics chip**, an AMD Radeon RX 460 (radeonsi), as
  GLArrays reports. Until 25 November the host's HELLO never set `ACV_HOST_GPU`,
  so `OGPU_Driver` wrongly said "virgl has no graphics chip".
- **llvmpipe** is the same lab started with `AC_VIRGL_DRIVER=llvmpipe`, so the
  host's virgl draws on the CPU (GLArrays: "LLVMPIPE (LLVM 21.1.8, 256 bits)").
  virgl gives that name in capitals, which the chip check first missed, so
  `auto` took virgl there as if on a chip. The check now ignores case, and on
  llvmpipe `auto` takes the stream route for Warp3D and MiniGL, as the table says.
- **The stream route** is ACRTG.gpu's host 3D, in its own choice between the
  host core and the Vulkan compute path (`OGPU_VK_3D` unset, so `auto`). A lab
  start can't pass `OGPU_VK_3D`; the host also reads `AC_VK_3D`, which it can,
  once the Cradle carries that host build.

| Program | virgl on the RX 460 | virgl on llvmpipe | `acrtg` (stream route) | `cpu` (68k core) |
| --- | --- | --- | --- | --- |
| W3DTest (Warp3D, FRAMES 300; cpu 100) | 882-1071 fps | 441-750 fps | 429 fps | 54 fps |
| MGLTest (MiniGL, FRAMES 300; cpu 100) | 577-600 fps | 455-484 fps | 234 fps | 67 fps |
| W3DArrBench (9216 triangles a frame, 10 s) | 2.87 M tri/s | 1.65 M tri/s | 1.92-1.98 M tri/s | 0.53 M tri/s |
| W3DArrBench SMALL (576 one-quad draws and 9 texture updates a frame, 5 s) | 188-205 fps | 221-235 fps | 584-585 fps | 147 fps |

Ranges are runs on 24 and 25 November; bigguy and daletop were busy with other
work, so take the spread as noise. W3DArrBench's `cpu` column is its own `CPU`
switch (it asks for `W3D_DRIVER_CPU` itself).

Pictures: W3DCheck is 183/0 under every value. The virgl scene is within the
route's tolerance on both the RX 460 (7416b6cd) and llvmpipe (72a9f3b1): worst
channel 9, 0 of 3072 pixels over 8 with a pixel's shift. MGLTest's frame-0
checksum is 9d42b7e6 on the RX 460, 69ea5427 on llvmpipe, and 124d81ad on the
stream route and the 68k. GLArrays is 14/14 on both virgl renderers.

## What it says so far

- virgl on the graphics chip is about 2 to 2.5 times the stream route for
  Warp3D and MiniGL, and 1.5 times for big arrays.
- llvmpipe beats the stream route for Warp3D's small scene (1 to 1.7 times) and
  MiniGL (about 2 times), and is about 85% of it for big arrays (W3DArrBench).
- **Many small draws turn it round.** With 576 one-quad draws and 9 texture
  updates a frame (a 68k game's kind of frame), the stream route is about 3
  times virgl on the RX 460 and 2.5 times virgl on llvmpipe. virgl is little
  quicker on the chip than on llvmpipe here, so the cost is virgl's per-draw
  and per-upload work, not the renderer.
- So the plan's rule ("if llvmpipe is within the stated margin, delete the host
  core's 3D path and the Vulkan compute path") looks met on this host for these
  programs, but not for many small draws, which is what most 68k Warp3D games
  send. The stream route's `auto` timing still mixes the host core and
  Vulkan, and only one host is measured, so there is no decision yet.

## Still to measure before a decision

- The host core and the Vulkan compute path timed apart (`AC_VK_3D=core` and
  `gpu` at lab start), after the Cradle's next gfopenrtg pin.
- The x86 appliance and a Pi 5 card.
- BlitzQuake and Quake through MiniGL (neither is in the lab or the Media
  Library), and `scenes3d`.
- W3DArrBench SMALL with the draws and the texture updates apart, to see which
  of the two costs virgl most.
- GLArrays timed (it only checks pictures today).

## The question for Dale, once those are in

M9's table (kept on 24 November) sends `auto` to the stream route on a host
whose virgl has no chip. That choice was asked while the host's missing
`ACV_HOST_GPU` made daletop look chipless; on daletop `auto` now takes virgl. If the stream route's host 3D is deleted, `auto` there
falls to the 68k unless the table changes to take llvmpipe first. So deleting
and the table go together: either keep the stream route, or delete it and let
`auto` take llvmpipe on hosts without a chip.

Team 2
