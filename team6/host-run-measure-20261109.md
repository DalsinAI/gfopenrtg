# Host-run measure, 9 Nov 2026

Team 6. Task 1. Lab `t6-measure-20261109`, a copy of Instance-32, on daletop.
Started with `AC_PROFILE=1` and `AC_PROFILE_BYTES=1`. The runtime is the
AC090 JIT. Eight 5-second windows, 40 seconds, 2,737 emulation samples.
The host was quiet apart from this lab: the emulation thread sat at about
3% until a workload, then 14% and 24%.

The cost is in translated guest code, not in the runtime. Across the 40
seconds, translated code was 66.8% of the emulation samples, libc 13.5%,
the runtime 10.1%, the board 2.9% and the display 2.8%. The hottest host
functions were the emulator loop (`a1200_run`, 2.7%) and the RTG line
start and end (1.3% each). Nothing on the host side is worth a hostlib
id on its own.

## What was run

1. **An archive unpack.** `SYS:Utilities/GFOpenCompress` on this copy only
   lists, extracts and tests (`l|x|t`). It extracted
   `Games:More Games/90 Original Archives/Chocolate Doom/Chocolate_DOOM.lha`
   (2.5 MB) into RAM:, 80 files, 5,262,431 bytes, in about 3 seconds.
   There was no zip in that drawer. This is the unpack the task asked for.
2. **A PNG.** Multiview opened
   `SYS:Prefs/Presets/Backdrops/background_1920_1080.png` (68,963 bytes).
   That is the largest PNG on this system. It did not rise above the
   workbench in the profile.
3. **A data load.** `Copy` of `Games:Ported/CrispyDoom/crispy-doom`
   (1,342,636 bytes) to RAM:. There is no `pak0.wad` in
   `Games:Ported/Quake/id1/`.
4. **SDL2 blits.** There is no `LIBS:sdl2.library`. The module is
   `SYS:Libs/GFOpenGPU/SDL2.module`, 1,003,264 bytes, sha256
   `8d6e95705ca6f00b37f93f2520609883acebdbada45fa805d48f47f5044eaa31`.
   The thirteen demos are at `DH1:SDL2 Demos`. OpenSprites runs from
   there and draws through that module.

   With no hint it printed
   `SDL renderer=opengpu kind=ACCELERATED` and the machine sat at about
   20% for 30 seconds. No guest block was over 4% (`$40BF2E24`, FPU
   setup). That path is not a blit.

   `SetEnv SDL_RENDER_DRIVER software` made the next start print
   `SDL renderer=software kind=SOFTWARE/CPU`. Eight windows, 40 seconds,
   35,944 emulation samples, 91% CPU. Translated code was 72.4% of that.
   The runtime was 19.6%, and 11.8 points of it were `op_addx_subx`
   (the JIT's helper for ADDX/SUBX). libc was 2.4%. The RTG line helpers
   were not in the hot list. Display was 0.5%.

## The costliest guest blocks

The extract is the window at 24% CPU (1,153 emulation samples, translated
code 86.6%). These blocks, with the first 32 bytes `AC_PROFILE_BYTES`
recorded:

| Share | Address | Where |
|------:|---------|--------|
| 15.1% | `$40B41580` | inside the loaded GFOpenCompress |
| 6.5% | `$40B41518` | the same |
| 6.3% | `$40B41846` | the same |
| 5.7% | `$40B4184E` | the same |
| 4.2% | `$00F81322` | ROM |
| 3.9% | `$00F882D8` | ROM |

The four blocks in the loaded program are about a third of that window.
`$40B41580` starts `MOVEQ #$20,D1` / `SUB.L D4,D1` / `MOVE.L D3,D0` and a
shift, which is a bit loop, not a copy. That is the unpack. The two ROM
blocks are the next cost and were not named by the runtime.

The 14% window (the copy, and the PNG open falls in the same stretch) has
no guest block over 4.7% (`$41534E12`). The copy is spread out. It is not
a candidate on this run.

The software OpenSprites window, same byte dump:

| Share | Address | Where |
|------:|---------|--------|
| 29.5% | `$40D0957A` | in the OpenSprites process, where the module runs |
| 21.4% | `$40D09824` | the same, the inner loop |
| 1.9% | `$40D0952E` | the same |
| 1.8% | `$40CC1D26` | the same |

`$40D09824` starts `MOVE.W #$00FF,D6`, then `SUBA.L D3,A6`,
`MOVE.W D6,D0` and `MULU.W` of a stack slot. A second `MULU.W` is in
the same 32 bytes. That is the shape of SDL 2.32.10's
`BlitRGBtoRGBPixelAlpha` in `src/video/SDL_blit_A.c`: the per-pixel
blend does `alpha ^ 0xFF` and a multiply, and the module is built from
that file. The bytes have not been matched to that function. There are
no symbols in the module here. The accelerated window was FPU setup,
not this loop.

## What this says for hostlib

The unpack is worth a hostlib id, and it is inside GFOpenCompress, not a
library call the ROM already exports. Inflate-on-ABI-2 (task 6, Team 3)
is the same kind of work. Do not spend a hostlib id on the RTG line
start, the emulator loop, or this PNG.

The software blit is also worth one. Half of a 91% window is two blocks
in the OpenSprites process, and the JIT is spending another 11.8% calling
out for ADDX/SUBX inside that loop. Do not hostlib `op_addx_subx`: that
helper goes away if the loop does. The accelerated renderer is not the
same work and is not a candidate.

Task 7 does not get a function yet. The likely body,
`BlitRGBtoRGBPixelAlpha`, reads a destination pixel and writes it back.
Hostlib's rule is that a call abandoned halfway is rerun from the start
on the 68k body. An in-place blend would then blend pixels that were
already blended, so the bytes would not match. A hostlib id for this has
to write a fresh buffer, or it has to be a contract that never returns
to the 68k body after the first store. Neither is written here. The 68k
function stays the specification, and the address is still not tied to
that C function by a build with symbols.
