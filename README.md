# GFOpenRTG

Our own RTG system for AmigaOS 3.x: `gfopenrtg.library`, GFOpenGPU, Warp3D,
several monitors, the window look and one prefs app. It is for real Amigas
and PiStorms as well as AmiCradle, where its board (ACRTG) and GFOpenGPU's
driver on the x86 and ARM64 cores make the desktop fast without emulating drawing loops.

The design is `DESIGN.md`: the parts, several monitors, AGA as a pseudo card,
the drivers (named the OS 4 way), `gfopenrtg.library`, GFOpenGPU, Warp3D, the OS 4
and MorphOS behaviours, the window look, the prefs app, the phases and what
phase 0 measured.

Status, 4 October 2026: designed; phase 0 (measuring what programs call)
done; phase 1 (several RTG monitors) built in AmiCradle; `gfopenrtg.library`
is next.

6 October 2026: GFOpenGPU G1 started: the stream (`include/gfopengpu/`), the
core every back end shares (`library/opengpu/ogpu_core.c`) with its tests
and golden scene, and `gfopengpu.library` 0.1 with the CPU back end.

## Licence and credit

GFOpenRTG is free software under the MIT licence (`LICENSE`): anyone may use
it, change it, fork it and ship it, commercially too (Team, 4 October 2026:
"I want anyone to be able to run with it, fork it etc").

GFOpenRTG was created by the AmiCradle Team, for AmiCradle.
The licence's one condition carries that credit: the copyright notice
("Copyright (c) 2026 AmigaChrome Team") and the licence text must stay with
every copy and every fork. We also ask, as a courtesy rather than a
condition, that a fork or a port say in its documentation or About window
that it is based on GFOpenRTG by the AmiCradle Team.

## What is here

- `gfopenrtg.readme`, `tools/libcount.readme`: the Aminet readmes (We fill
  in the uploader at upload time).

- `tools/libcount.c`: LibCount, phase 0's measuring tool. It counts the calls
  to every function of the libraries it is given, with OS-friendly
  SetFunction() stubs that pass every call on. It runs on any Amiga with
  OS 2.04 or later.

      LibCount graphics.library intuition.library layers.library SECONDS 30 TO RAM:counts

- `tools/libcount_report.py`: names the counted offsets from the NDK's FD
  files and prints a table sorted by calls.
- `tools/build.sh`: builds LibCount with bebbo's m68k-amigaos-gcc (amiga-gcc)
  and NDK 3.2: set `STOVE` to the toolchain's install folder (the one holding
  `prefix/bin`), or `CC` to the compiler itself. Output goes to `build/`.
  `libcount_report.py` reads the NDK's FD files; give their folder with `--fd`.
- `library/modes.c`, `library/modes.h`: phase 2's first part, the display
  database's mode table. It builds each monitor's modes once (Standard: the
  popular monitor sizes; All: with the Amiga-shaped ones), each in 8, 16 and
  32-bit, and gives their ModeIDs. The library answers NextDisplayInfo,
  FindDisplayInfo and BestModeIDA from it. Plain C, with no Amiga headers.
- `library/openrtg_lib.c`: `gfopenrtg.library` 0.1. It finds the ACRTG
  boards, one per RTG monitor (the serial number is the monitor), and builds
  each monitor's mode table once. Its calls (`include/proto/gfopenrtg.h`,
  `library/gfopenrtg_lib.sfd`) give the monitor count, each listed mode, a mode
  by ModeID, the best mode for a size, the Standard or All choice, and a
  board's address. It doesn't claim the boards yet; Picasso96's acrtg.card
  drives them until GFOpenRTG opens screens of its own.
- `library/displaydb.c`: the display database (0.2). graphics.library's
  NextDisplayInfo, FindDisplayInfo, GetDisplayInfoData and ModeNotAvailable
  answer for GFOpenRTG's ModeIDs from the mode table, chained OS-friendly and
  passing everything else through.
- `tools/openrtg_cmd.c`: `C:GFOpenRTG`.
  - `GFOpenRTG` lists the monitors.
  - `GFOpenRTG MODES [MONITOR n]` lists their modes.
  - `GFOpenRTG ALL|STANDARD [MONITOR n]` switches the list.
  - `GFOpenRTG ACTIVATE [FORCE] | OFF` puts GFOpenRTG's modes in the display
    database, or takes them out. It refuses while Picasso96 runs; FORCE is
    for tests only.
  - `GFOpenRTG LISTDB` lists every mode the display database has, as ScreenMode
    prefs sees them.
- `library/build.sh`: builds both with the os32 stove.
- `library/build.sh module [OUT_DIR]`: GFOpenRTG for a ROM, built for size
  (`OPT=`, `-Os` by default), in `OUT_DIR/modules` (see "In a ROM" below).

- `tools/probes/`: debugging probes used while bringing up screens (AlertSpy,
  IBaseScan, ICtrlTrace, Priv1Trace, PatchScan, OSTrace, MouseProbe). They
  print to the serial port, are built by hand and are not shipped.
- `tests/run.sh`: the host tests (`tests/test_modes.c`), plus a check that
  the library's C builds for the 68k with the stove.
- `measurements/`: LibCount's raw counts from the OS 3.2.3 scratch copy with
  Picasso96, 4 October 2026: Workbench for 45 seconds, and MultiView showing
  a true-colour PNG for 30. The design's phase 0 section reads them.
- `tools/make_sdk.py` and `sdk/`: the GFOpenGPU developer kit
  (`GFOpenGPU-SDK-0.6`): SDL 2 and its satellites, OpenGL, GLES and GLA,
  Warp3D, MiniGL and gfopengpu.library's headers and link libraries,
  `sdl2-config`, pkg-config and CMake files, examples and every notice, as a
  `.tar.gz` for x86 and ARM64 cores and an `.lha` for the Amiga. Run after
  `library/build.sh` and the GL module's build:
  `tools/make_sdk.py --build build --gl build/gl OUT_DIR --archives`.
  `sdk/README` is the kit's readme and `sdk/AutoDocs-GFOpenGPU.md` its
  reference. The kit is build output and is not committed.

## In a ROM (15 October 2026)

`gfopenrtg.library` 0.18 and `cybergraphics.library` 43.3 keep no data or BSS
in their files, so the same files run from a ROM as from `LIBS:`. Every build
checks that (`tools/hunk_rw_check.py` and `nm`).

- **What they write is in memory they allocate.** gfopenrtg.library's 0.17 had
  28,912 bytes of BSS (measured on the lab's copy): the patches' old vectors,
  the display database's records, the video RAM blocks, the pen tables and
  the rest. 0.18 keeps all of it in one block `lib_init` allocates
  (`library/ortg_state.h`). cybergraphics.library keeps its four bases in its
  own base.
- **A4 holds that block's address** in all their code: they are built
  `-ffixed-a4`, with a global register variable. Exec calling their
  functions, graphics calling a `SetFunction` patch and GFOpenGfx calling a
  provider all come in with their own A4, so each of those goes through a
  small stub in the block that loads A4 and puts the caller's back
  (`library/ortg_stub.c`).
- **`library/build.sh module`** builds three modules for an GFOpenKick ROM
  (amigachrome-guest `tools/openkickrom/trial-1m`, `RTG=`):

| Module | What | RomTag |
| --- | --- | --- |
| `gfopenrtg.library.module` | gfopenrtg.library | `RTF_AUTOINIT`, priority 0, no start class: made at its first `OpenLibrary`, as from `LIBS:` |
| `cybergraphics.library.module` | cybergraphics.library | the same |
| `gfopenrtg.monitor.module` | the monitor driver's job (`library/openrtg_monitor_rom.c`) | `RTF_COLDSTART`, priority -38 |

### gfopenrtg.monitor: why COLDSTART at -38

`DEVS:Monitors/GFOpenRTG` puts GFOpenRTG's modes in the display database,
switches its screens on and opens cybergraphics.library. In a ROM,
`gfopenrtg.monitor` does it at cold start. exec's InitCode runs the COLDSTART
modules by priority, highest first. In the 1M trial ROM's map:

- **After** romboot (105), which configures the boards, so the ACRTG board
  is in expansion's list; graphics (65); intuition (10); and syscheck (-35).
- **Before** amigachrome-guest's gfopenrtg.boot (-40), which loads its own copy
  of gfopenrtg.library into RAM unless one is in memory. With both in a ROM,
  gfopenrtg.boot finds this one's library and steps aside.
- **Before** bootmenu (-50), the first module after it that may open a
  screen (its Early Startup page); strap (-60); and dos (-120). dos's boot
  opens the Workbench screen with the boot shell's first window, and IPrefs
  opens it again on the ScreenMode prefs' mode, which by then is in the
  display database.
- Nothing else in the ROM is at -38.

Its init steps aside, keeping nothing, when Picasso96's `rtg.library` is in
memory or no ACRTG board is fitted. Otherwise it makes gfopenrtg.library and
cybergraphics.library from their RomTags (before dos, `OpenLibrary` finds
only what is in memory) and does what the monitor driver does.

### What the disk keeps doing

- **The ROM's copy is found first.** Its library is in memory before DOS, so
  `OpenLibrary("gfopenrtg.library")` gets it, and `LIBS:GFOpenRTG/cybergraphics.library`
  is found in memory by its file part (amigachrome-guest `common/openramlib`).
- **`DEVS:Monitors/GFOpenRTG` 0.8 registers nothing a second time.** It asks
  `ORTG_Screens(0)`, which from 0.18 answers 1 when the screens are on. Then
  it calls `ORTG_Screens(1)` only, which registers nothing and opens
  gfopengpu.library (below), and ends.
- **gfopengpu.library** is on the disk, so a ROM switch-on before DOS draws with
  the CPU. The first `ORTG_Screens(1)` from a process after that (the monitor
  driver, `C:GFOpenRTG SCREENS`), or the first screen a process opens once
  `LIBS:` is assigned (IPrefs' Workbench), opens it and hands the drawing to
  GFOpenGfx (`late_opengpu` in `library/screens.c`).
- **gfopengpu.library 0.16 looks for a driver in the ROM first**: a resident
  named `ACRTG.gpu` (amigachrome-guest `libraries/acrtg.gpu`,
  `build.sh module`) is opened by name, before `LIBS:GFOpenGPU/`.
- **gfopengpu.library 0.17 copies Fast RAM with MOVE16** on a 68040 or 68060:
  a COPY whose rows line up on 16 bytes, between memory lists marked Fast
  between 16 MB and 256 MB (motherboard and accelerator RAM; Zorro RAM and
  video RAM keep the long-word copy). Stream v1.5 adds `YUV_SCALED`, a part
  of a YUV picture converted straight into a rect of any size (nearest),
  which SDL 2's renderer uses for video frames.
- **gfopengpu.library 0.18 draws vectors** (stream v1.6): anti-aliased path
  fills (non-zero and even-odd) and strokes (caps, joins, miter limit,
  dashes), straight at device pixels; linear, radial and conic gradients
  and pictures under a matrix as their paint, with any Porter-Duff
  operator or W3C blend mode and an A8 clip mask; BLUR for shadows; and
  the blend modes in `COMPOSITE_PD`. The rasteriser is GFOpenFont's
  `of_raster_a8`, moved here with even-odd added.
- **gfopengpu.library 0.19 is ready for SDL 3** (stream v1.7): SDL 3's
  premultiplied blend modes, its PIXELART scaling, clamping or repeating
  each texture axis on its own, palette textures (INDEX8 with a `PALETTE`,
  so palette cycling needs no upload), the RGBA32 and ABGR32 byte orders,
  P010 video and the BT.709 full-range and BT.2020 colour spaces, and
  `SDL_RenderGeometry` straight onto 32-bit GFOpenRTG screens. A driver gets
  only the batches it draws right; the rest run on the 68k.
- **gfopengpu.library 0.23 draws paths without anti-aliasing** (stream v1.8,
  `OGPU_PATH_ALIASED`): each pixel in or out by its centre, as cairo's
  `CAIRO_ANTIALIAS_NONE`, for a browser's crisp shapes and its rectangle
  clips under a transform, so WebCore needs no cairo for them. They run on
  the library's CPU core whatever the driver, until a driver says it takes
  v1.8.
- **gfopengpu.library 0.24 says which flags a back end takes**
  (`OGPU_QUERY_FLAG` through `OGPU_Query`): a driver answers for the flags
  it carries out, such as `OGPU_PATH_ALIASED`, and gets the batches that use
  them; older libraries and drivers answer no, safely.

## Contributors

GFOpenRTG is created and maintained by [SacredTrees](https://github.com/SacredTrees) with The AmigaChrome Team, copyright AmigaChrome Team. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
