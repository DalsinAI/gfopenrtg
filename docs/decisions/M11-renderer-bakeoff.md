# Stream3D M11: the renderer bake-off (decision record)

GF-66, the shared pipe's M11 (docs/design/OpenGPU-Shared-Pipe-Plan.md, M11 and
phase 11). Status: **first numbers, no decision yet.** Nothing is deleted until
Dale says yes and a deletion date is written here.

Stream3D is the name of the whole 3D stack (Dale, 25 November): Warp3D, MiniGL
and GL through gfopengpu.library, by virgl, the stream route or the 68k.

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
| ... after the texture fix (3cf5fb0, 25 November) | 435-441 fps | not run | 499 fps (same boot) | |
| W3DArrBench SMALL NOUPDATE (no texture updates) | 551-602 fps | | 748 fps | |

Ranges are runs on 24 and 25 November; bigguy and daletop were busy with other
work, so take the spread as noise. W3DArrBench's `cpu` column is its own `CPU`
switch (it asks for `W3D_DRIVER_CPU` itself).

Pictures: W3DCheck is 183/0 under every value. The virgl scene is within the
route's tolerance on both the RX 460 (7416b6cd) and llvmpipe (72a9f3b1): worst
channel 9, 0 of 3072 pixels over 8 with a pixel's shift. MGLTest's frame-0
checksum is 9d42b7e6 on the RX 460, 69ea5427 on llvmpipe, and 124d81ad on the
stream route and the 68k. GLArrays is 14/14 on both virgl renderers.

## The stream route's two halves (25 November)

The host runs the stream route's triangles either on its C core or with the
Vulkan 3D shader. A lab start now passes `AC_VK_3D` (gfopenrtg e61a742, amicradle
PR 130), so each half was timed alone, with `ENV:GFOpenGPU/Driver` set to
`acrtg`. The runtime is a1200native built in the kitchen from that branch
(`scripts/deploy/build-native.sh`, the portable x86-64 build, not the deployed
Zen one), run in the same lab; the runtime's log confirms where the triangles
went (gpu: 39074 on the GPU; auto: 12500 on the GPU, 49328 on the core).
Three boots each, daletop's load about 10 on 12 cores.

| Program | `AC_VK_3D=core` | `AC_VK_3D=gpu` | `auto` (by cost) |
| --- | --- | --- | --- |
| W3DTest (FRAMES 300) | 333-349 fps | 405-429 fps | 366-429 fps |
| MGLTest (FRAMES 300) | 192-205 fps | 246-254 fps | 234-263 fps |
| W3DArrBench (9216 triangles a frame) | 2.50-2.55 M tri/s (one run 1.24) | 1.03-2.00 M tri/s | 1.84-1.90 M tri/s |
| W3DArrBench SMALL | 633-785 fps (one boot 160-166) | 337-363 fps | 522-548 fps |

That runtime first had no virglrenderer beside it, so `virgl` in it fell to the
stream route; with virglrenderer linked in, the same boot started with
`AC_VIRGL_DRIVER=llvmpipe` gave W3DArrBench SMALL 553 fps on virgl (744 with
NOUPDATE) against 539 on the stream route: **after the texture fix, virgl on
llvmpipe matches the stream route for many small draws.**

- The Vulkan 3D shader is quicker only for the small scenes, by about 20%
  (W3DTest, MGLTest). The host core is quicker for big arrays (about 1.25
  times) and for many small draws (about 2 times): each draw sent to the GPU is
  a submit and a wait (gpu boot: 35770 submits, 5.3 s waiting).
- `auto`'s choice by cost is worse than the core alone for both of those.
- So the Vulkan compute path costs more than it gains on this host for 68k
  games' kind of frame. Deleting it alone (keeping the host core) loses about
  20% on the small scenes.
- **Pi 5 card (amicradle-cm5, V3D 7.1, Mesa 26.2.6):** the Vulkan back end is
  off there by design (amicradle #949, V3DV's compute shaders don't finish).
  Forced on with `OGPU_VK_V3D=1`, `tests/gpu/test_opengpu_3d.sh`'s device probe
  still hadn't come back after 400 s. On the Pi the stream route is the host
  core only, and has always been.
- **x86 appliance (Ryzen 5 2400G, RX 580, RADV):** no Amiga there yet, so
  only the host side: `tests/gpu/test_opengpu_3d.sh` at amicradle e662e82, one
  frame of each bench scene on the core and on the GPU (ms a frame, the wait
  included). All 12 checks PASS, every picture byte for byte the core's.

  | Scene | core ms | GPU ms | GPU faster |
  | --- | --- | --- | --- |
  | w3dtest (320x240, 14 triangles) | 1.83 | 0.41 | 4.4x |
  | mgltest (320x240 RGB565, 14 triangles) | 5.34 | 0.37 | 14.5x |
  | cow3d (2201 triangles) | 1.95 | 1.86 | 1.0x |
  | tiny (4 triangles, 120 pixels) | 0.05 | 0.15 | 0.4x |
  | fill640 (640x480) | 11.57 | 1.07 | 10.8x |

  The GPU wins on pixels and loses on small draws (a submit costs about
  0.15 ms with its wait), the same shape as daletop's lab: a 68k game's many
  small draws are the "tiny" row, over and over.
- MGLTest's frame-0 checksum is 26ca6bed for the first program after
  minigl.library is loaded (after a boot or `Avail FLUSH`) and 124d81ad for
  every later one, on every route including the 68k and whatever the frame
  count: about 17,000 pixels in the scene's lower half differ by 1. Not the
  renderer and not gfopengpu.library (GLArrays first doesn't change it); some
  state in minigl.library outlives its first client. Not the emulator's JIT
  either: the same with `JIT_NOFPCRSPEC=1` and with `JIT_NOX87=1`. The host
  test's golden scene (tests/golden/minigl-scene.txt) is 124d81ad, so the first
  run is the wrong one. Still to find.

## GLQuake (25 November)

OpenUpQuake (id's GLQuake through minigl.library, the copy on Instance-32's
DH2:Ported/Quake, read only) in the same lab, `timedemo demo1` (969 frames) at
640x480, one run each. The lab was moved first from the DevDrive (NFS from
bigguy) to daletop's own disk: on the DevDrive the game's loading stalled the
emulator in the kernel for minutes.

| Route | seconds | fps |
| --- | --- | --- |
| virgl on the RX 460 | 6.5 | 149.2 |
| virgl on llvmpipe | 27.3 | 35.5 |
| stream route, `AC_VK_3D=gpu` | 27.5 | 35.3 |
| stream route, `auto` | 27.4 | 35.4 |
| stream route, `AC_VK_3D=core` | 40.6 | 23.9 |

- A real MiniGL game agrees with the small benches on the chip: virgl on the
  graphics chip is about 4 times anything else.
- Here the Vulkan 3D shader beats the host core by about 1.5 times (big
  textured triangles at 640x480: pixels, which the GPU is good at), and
  `auto` takes it.
- virgl on llvmpipe is level with the stream route's best, and 1.5 times the
  host core alone.
- The 68k wasn't timed (it would take minutes for 969 frames).
- At 320x240 the stream route (`auto`) gives 45.5 fps against 35.4 at
  640x480: a quarter of the pixels buys 29%. The stream route's ceiling here
  is not its pixels. On the stream route minigl.library transforms, lights
  and clips every vertex in 68k code (mgl_pipe.c) and hands over screen
  triangles, and the host sets each triangle up on its CPU, also in front of
  the Vulkan shader. On virgl the GPU does all of that (mgl_vg_draw.c).

## What the stream route's 3D is

Both halves of the stream route's 3D are software renderers. The host core
is a fixed-point C rasteriser on the host's CPU. The Vulkan 3D path is the
same rasteriser as a compute shader (Team 4, 17 October, c80554c6, "pixel for
pixel as the 3D core"): it does not use the GPU's triangle hardware, it does
the core's exact integer divides (no fast integer divide on a GPU: a float
estimate and a correction each), triangle setup stays on the CPU, and any
triangle it declines goes to the C core. Bit-exact pictures against the 68k
are what it was built for, and on this evidence it is a CPU-class renderer
wherever it runs: GLQuake is 149 fps on virgl with the RX 460's own pipeline
and about 35 on the stream route.

A Stream3D path that competes with virgl on the chip would need a Vulkan
graphics pipeline (the GPU's own rasteriser, floats) fed vertices before
transform, as virgl's route already sends them, and pictures judged within
a tolerance rather than exactly. The virgl route already works that way: each
chip draws MGLTest with its own checksum, and W3DCheck takes each driver's
tolerance from OGPU_Driver. Allowing that for a GPU path changes the M0
gate's exact captures, which is Dale's call. On Linux, Vulkan on these chips
is still a Mesa driver (v3dv, radv), a smaller part of Mesa than GL.

## Raspberry Pi 5 (25 November)

The PiMiga card (Raspberry Pi 5, V3D 7.1, Mesa 26.2.6, the card's own
/opt/amicradle runtime), with this branch's warp3d, minigl, gfopengpu,
gfopenrtg and tinygl libraries and the same test programs put in its DH0:,
then put back afterwards. The stream route on a Pi has only the host core
(the Vulkan 3D shader is gated off on V3D: its probe hangs). llvmpipe was
forced with `LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe` in the card's
native.env and `Driver=llvmpipe` (the card runtime doesn't translate
`AC_VIRGL_DRIVER`). Each GPU renderer draws MGLTest with its own checksum
(V3D afc31265, llvmpipe 69ea5427, RX 460 9d42b7e6); 124d81ad is the stream
route's and the 68k's.

| Program | virgl on V3D | stream route (host core) | virgl on llvmpipe | 68k |
| --- | --- | --- | --- | --- |
| W3DTest fps | 937.5 | 288 | 161 | 52 |
| MGLTest fps | 349-357 | 48.7 | 176 | |
| W3DArrBench M tri/s | 1.12 | 2.75 | 0.75 | |
| W3DArrBench SMALL fps | 268 | 712-719 | 260 | |
| GLQuake timedemo demo1 640x480 fps | 63.8 | 10.9 | 22.5 | |

- On the Pi the gap is much wider than on daletop: the Pi's CPU is several
  times slower, and the host core and llvmpipe both draw on it, while V3D
  does the pixels. A real game (GLQuake) is about 6 times faster on virgl
  over V3D than on the host core.
- The host core still wins big arrays (2.5 times) and many small draws
  (2.7 times), as on daletop: virgl's per-draw cost is CPU work too.
- Where the time goes (per-thread CPU over 10 s while each bench ran; the
  cards have no perf): on the stream route the host core runs on the
  emulator's own thread (acrtg.c's ring_put keeps the C core there when there
  is no Vulkan), one core at 100% and the 68k waiting while it draws. virgl
  with SMALL: the emulator's thread 85% and the GPU thread 55%, so most of
  virgl's cost per draw is Warp3D's virgl route making about ten pipe calls on
  the 68k for each change of state (w3d_vg.c's prepare), not Mesa. virgl with
  big arrays: the GPU thread 76% (virglrenderer and Mesa) and the emulator 41%.

## CM4 (25 November)

amicradle-8gb (Compute Module 4, V3D 4.2, Cortex-A72), the same libraries and
programs, put back afterwards. **The card runs an old stack** (/opt/amicradle
0.70.0, runtime of 21 November, start.sh without acd), so its Python host
(amicradle_host.py) was taking 130-180% of a core with the Amiga idle,
walking every drive; the emulator has core 3 to itself, which leaves the GPU
thread and Mesa about one core. These numbers are the old card's, to be timed
again on main's Python-free runtime (acd alone, AC-60) once that can run on a
card.

| Program | virgl on V3D 4.2 | stream route (host core) |
| --- | --- | --- |
| W3DTest fps | 88.2 | 45.3 |
| MGLTest fps | 37.7-39.3 (afc31265) | 6.25 (124d81ad) |
| W3DArrBench M tri/s | 0.094 | 0.28 |
| W3DArrBench SMALL fps | 11 | 45 |
| GLQuake timedemo demo1 640x480 fps | 6.0 | (not run: about 1 fps) |

llvmpipe was not timed on the CM4.

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
- **Most of that was one cost, now fixed.** Each W3D_UpdateTexImage sent the
  pending batch to the host and waited (Warp3D flushed so the batch couldn't
  read the new texels), and the pipe then renamed the texture (a new host
  texture and view). Small texture levels now go into the stream itself,
  copied when written, so neither happens: virgl doubles to about 440 fps,
  close to the stream route's 500 in the same boot. The draws alone are
  still a little behind the stream route (about 80%).
- So the plan's rule ("if llvmpipe is within the stated margin, delete the host
  core's 3D path and the Vulkan compute path") looks met on this host for these
  programs. For many small draws, which is what most 68k Warp3D games send,
  virgl is now within about 15% of the stream route on the graphics chip;
  llvmpipe after the fix is still to time. The stream route's `auto` timing still mixes the host core and
  Vulkan, and only one host is measured, so there is no decision yet.

## Still to measure before a decision

- The Pi cards again on main's Python-free runtime (the CM4 and PiMiga run the old Python host), llvmpipe on the CM4, and the x86 appliance's Amiga programs (only its host-side test is in).
- BlitzQuake (not found on any drive yet), Quake on the 68k, and `scenes3d`.
- Why the Vulkan 3D shader loses on small draws, and whether it can win (Dale, 25 November: later).
- GLArrays timed (it only checks pictures today).

## The question for Dale, once those are in

M9's table (kept on 24 November) sends `auto` to the stream route on a host
whose virgl has no chip. That choice was asked while the host's missing
`ACV_HOST_GPU` made daletop look chipless; on daletop `auto` now takes virgl. If the stream route's host 3D is deleted, `auto` there
falls to the 68k unless the table changes to take llvmpipe first. So deleting
and the table go together: either keep the stream route, or delete it and let
`auto` take llvmpipe on hosts without a chip.

Team 2
