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
  so `OGPU_Driver` wrongly said "virgl has no graphics chip". The `llvmpipe` value
  below therefore measured virgl on the RX 460, not llvmpipe.
- **The stream route** is ACRTG.gpu's host 3D, in its own choice between the
  host core and the Vulkan compute path (`OGPU_VK_3D` unset, so `auto`). A lab
  can't set `OGPU_VK_3D`, so the two are not yet timed apart.

| Program | virgl on the RX 460 | `acrtg` (stream route) | `cpu` (68k core) |
| --- | --- | --- | --- |
| W3DTest (Warp3D, FRAMES 300; cpu 100) | 1071 fps | 429 fps | 54 fps |
| MGLTest (MiniGL, FRAMES 300; cpu 100) | 577 fps | 234 fps | 67 fps |

Pictures: W3DCheck is 183/0 under every value. Under `llvmpipe` the virgl scene
is within the route's tolerance (worst channel 9, 0 of 3072 pixels over 8 with a
pixel's shift). MGLTest's frame-0 checksum is 9d42b7e6 on virgl and 124d81ad on
the stream route and the 68k.

## What it says so far

On this host, virgl on the graphics chip is about 2.5 times the stream route for
both Warp3D and MiniGL. llvmpipe itself is not measured yet: it needs a lab
started with `AC_VIRGL_DRIVER=llvmpipe`. The plan's rule ("if llvmpipe is within
the stated margin, delete the host core's 3D path and the Vulkan compute path")
waits for that number.

## Still to measure before a decision

- The host core and the Vulkan compute path timed apart (`OGPU_VK_3D=core` and
  `gpu`). That needs a lab start that can pass that variable.
- llvmpipe on the same host (`AC_VIRGL_DRIVER=llvmpipe` at lab start).
- The x86 appliance and a Pi 5 card.
- BlitzQuake, Quake through MiniGL, `scenes3d`, `w3darrbench`, and a 68k-style
  workload of many small draws and texture updates a frame.
- GL.module's programs (GLArrays) under each value.

## The question for Dale, once those are in

M9's table (kept on 24 November) sends `auto` to the stream route on a host
whose virgl has no chip. That choice was asked while the host's missing
`ACV_HOST_GPU` made daletop look chipless; on daletop `auto` now takes virgl. If the stream route's host 3D is deleted, `auto` there
falls to the 68k unless the table changes to take llvmpipe first. So deleting
and the table go together: either keep the stream route, or delete it and let
`auto` take llvmpipe on hosts without a chip.

Team 2
