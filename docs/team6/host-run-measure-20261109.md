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
4. **SDL2 blits.** Not run. `LIBS:sdl2.library` is not on this copy, and
   the SDK's `examples/sprite` is source only. Task 7 stays waiting on a
   lab that has the module.

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

## What this says for hostlib

The unpack is the only workload here that is worth a hostlib id, and it
is inside GFOpenCompress, not a library call the ROM already exports.
Inflate-on-ABI-2 (task 6, Team 3) is the same kind of work. Do not spend
a hostlib id on the RTG line start, the emulator loop, or this PNG.

SDL2 blits are still unmeasured. Task 7 does not start until a lab with
`sdl2.library` and a blit demo has been profiled the same way.
