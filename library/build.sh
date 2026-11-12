#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# gfopenrtg.library, cybergraphics.library and gfopengpu.library (bare: no startup code or C
# library; the RomTag's stub comes first), C:GFOpenRTG and its monitor driver, with the os32 stove (bebbo's m68k-amigaos-gcc);
# gfopengpu.library and its GFOpenGfx parts with GFOpenAmigaGCC 16.2.0-open4 or -open5 (OPENGPU_STOVE, below).
#   library/build.sh [OUT_DIR]     (default build/)
# (gfopengpu.library twice: OUT_DIR/gfopengpu.library for the disk, OUT_DIR/rom/gfopengpu.library
# for the GF ROM, without GFOpenGfx's patches and core; OGPU_ROM=1 makes OUT_DIR/gfopengpu.library
# the ROM's, and builds no other)
# or, with `library/build.sh module [OUT_DIR]` (OUT_DIR: $OUT, else build/), only
# the ROM modules, in OUT_DIR/modules, built for size (OPT=, -Os by default):
#   gfopenrtg.library.module        gfopenrtg.library
#   cybergraphics.library.module  cybergraphics.library
#   gfopenrtg.monitor.module        the monitor driver's job, before DOS (openrtg_monitor_rom.c)
# Every build of these three fails if one keeps data or BSS: they run from a ROM.
set -eu
TARGET=all
[ "${1:-}" != module ] || { TARGET=module; shift; }
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
NM=${NM:-${CC%gcc}nm}
if [ "$TARGET" = module ]; then OUT=${1:-${OUT:-$ROOT/build}}; else OUT=${1:-$ROOT/build}; fi
# gfopengpu.library is built with GFOpenAmigaGCC 16.2.0-open4, -open5 or -open6
# (open5 changed only libpthread and the names, not the compiler; open6's
# m68k-amigaos code is open5's, it adds the m68k-aros target), and with nothing
# else: GCC 6.5 (the os32 stove) miscompiles the CPU core's stroke walk, so
# strokes painted with gradients or patterns came out wrong (gfopenrtg
# #159; tests/amiga/vector's OpenGPUVectorCRC shows the core against the
# host). OPENGPU_STOVE holds prefix/bin; OPENGPU_CC overrides the compiler.
OPENGPU_GCC="16.2.0-open6 16.2.0-open5 16.2.0-open4"   # any of these
OPENGPU_STOVE=${OPENGPU_STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OPENGPU_CC=${OPENGPU_CC:-$OPENGPU_STOVE/prefix/bin/m68k-amigaos-gcc}
# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
mkdir -p "$OUT"
# Soft float: -m68040 alone, linked through tools/m68k-soft-m68040.specs so
# GCC takes libnix's soft libm020 and libgcc, not libm881 (FPU code, and
# values returned in fp0 where soft-float callers read d0/d1).
SOFT="-specs=$ROOT/tools/m68k-soft-m68040.specs"
# gfopenrtg.library and cybergraphics.library keep what they write in memory they
# allocate, its address in A4 (library/ortg_state.h): -ffixed-a4 on all their code.
# $1 the file, $2 the optimisation, the rest its sources
build_ortg() {
    out=$1 opt=$2; shift 2
    "$CC" ${ORTG_CPU:--m68040} $SOFT -mnobitfield $opt -ffixed-a4 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -nostartfiles -nostdlib -I"$ROOT/include" -o "$out" "$@" -lgcc
    echo "$out ($(wc -c < "$out") bytes)"
}
ORTG_SRC="$HERE/openrtg_lib.c $HERE/modes.c $HERE/displaydb.c $HERE/screens.c $HERE/pixels.c $HERE/uaegfx.c $HERE/opengpu/ogpu_build.c $HERE/string.c $HERE/ortg_stub.c"
CGX_SRC="$ROOT/compat/cybergraphics_lib.c $HERE/ortg_stub.c"
# nothing writable in the file, and nothing undefined
rom_clean() {
    python3 "$ROOT/tools/hunk_rw_check.py" "$1"
    data=$("$NM" "$1" | awk 'NF == 3 && $2 ~ /^[dDbBcC]$/ {print $3}' || true)
    [ -z "$data" ] || { echo "$1 keeps data: $data" >&2; exit 1; }
    und=$("$NM" -u "$1" || true)
    [ -z "$und" ] || { echo "undefined in $1: $und" >&2; exit 1; }
}
if [ "$TARGET" = module ]; then
    OPT=${OPT:--Os}
    # The ROM modules stay as the ROM build has them (-m68020) until the ROM
    # side moves them; everything else here is built for the 68040.
    ORTG_CPU=-m68020 SOFT=
    mkdir -p "$OUT/modules"
    # shellcheck disable=SC2086
    build_ortg "$OUT/modules/gfopenrtg.library.module" "$OPT" $ORTG_SRC
    # shellcheck disable=SC2086
    build_ortg "$OUT/modules/cybergraphics.library.module" "$OPT" $CGX_SRC
    "$CC" -m68020 $OPT -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror \
        -nostartfiles -nostdlib -I"$ROOT/include" -o "$OUT/modules/gfopenrtg.monitor.module" "$HERE/openrtg_monitor_rom.c" -lgcc
    echo "$OUT/modules/gfopenrtg.monitor.module ($(wc -c < "$OUT/modules/gfopenrtg.monitor.module") bytes)"
    for m in gfopenrtg.library cybergraphics.library gfopenrtg.monitor; do rom_clean "$OUT/modules/$m.module"; done
    python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/modules/gfopenrtg.library.module" "$OUT/modules/cybergraphics.library.module" "$OUT/modules/gfopenrtg.monitor.module"
    sha256sum "$OUT"/modules/gfopenrtg.library.module "$OUT"/modules/cybergraphics.library.module "$OUT"/modules/gfopenrtg.monitor.module
    exit 0
fi
# shellcheck disable=SC2086
build_ortg "$OUT/gfopenrtg.library" -O2 $ORTG_SRC
# shellcheck disable=SC2086
build_ortg "$OUT/cybergraphics.library" -O2 $CGX_SRC
rom_clean "$OUT/gfopenrtg.library"
rom_clean "$OUT/cybergraphics.library"
# The 3D rasteriser (Warp3D's and SDL's TRIANGLES), when its files are there.
D3= D3SRC=
if [ -f "$HERE/opengpu/ogpu_3d.c" ]; then D3=-DOGPU_WITH_3D; D3SRC="$HERE/opengpu/ogpu_3d.c"; fi
# GFOpenGfx (library/ogfx): the patches' glue and its leaves. The leaves are AC090
# magic functions: stack arguments, and no loop turned into a call (ogfx_leaves.c).
# GFOpenGfx's drawing core comes from gfopengfx at a pinned commit (one copy).
opengpu_gcc_ok() { v=$("$OPENGPU_CC" --version 2>/dev/null | head -1); for g in $OPENGPU_GCC; do case "$v" in *"OpenAmigaGCC $g)"*) return 0 ;; esac; done; return 1; }
if ! opengpu_gcc_ok; then
    echo "gfopengpu.library needs GFOpenAmigaGCC $OPENGPU_GCC (OPENGPU_STOVE or OPENGPU_CC); $OPENGPU_CC is:" >&2
    "$OPENGPU_CC" --version 2>&1 | head -1 >&2 || true
    exit 1
fi
# gfopengpu.library in $1, with GFOpenGfx's ogfx_lib.c built with $2 (extra flags), the core's objects after.
# OGPU_ROM=1: only the GF ROM's build, as $OUT/gfopengpu.library (amicradle-guest's
# tools/openkickrom/trial-1m/romgpu.sh, for the ROM's gpu group): no GFOpenGfx patches and no
# drawing core (-DOGPU_ROM_NO_OGFX), for graphics.library 47.101's dispatch, which the GF ROM has.
# Otherwise the disk build, which keeps everything for OS 3.2.3 (and any graphics.library before
# 47.101), and the ROM build beside it, in $OUT/rom/.
link_gpu() {
    lib=$1 defs=$2; shift 2
    # shellcheck disable=SC2086
    "$OPENGPU_CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        $defs -I"$ROOT/include" -I"$OUT/ogfx-core" -c -o "$lib.ogfx_lib.o" "$HERE/ogfx/ogfx_lib.c"
    # -mnobitfield: GCC's bitfield instructions are slow on the AC090's JIT (opengpu_core.c's header).
    # shellcheck disable=SC2086
    "$OPENGPU_CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-toplevel-reorder -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -nostartfiles -nostdlib $SOFT -I"$ROOT/include" \
        $D3 -o "$lib" "$HERE/opengpu/opengpu_lib.c" "$HERE/opengpu/ogpu_core.c" "$HERE/opengpu/ogpu_dwt.c" $D3SRC "$HERE/opengpu/ogpu_module.c" "$HERE/opengpu/ogpu_retained.c" "$HERE/string.c" \
        "$lib.ogfx_lib.o" "$OUT/ogfx_leaves.o" "$@" -lgcc
    rm -f "$lib.ogfx_lib.o"
}
"$OPENGPU_CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns -Wall -Wextra -Werror \
    -c -o "$OUT/ogfx_leaves.o" "$HERE/ogfx/ogfx_leaves.c"
if [ "${OGPU_ROM:-0}" = 1 ]; then
    link_gpu "$OUT/gfopengpu.library" -DOGPU_ROM_NO_OGFX
else
    sh "$ROOT/tools/fetch_ogfx_core.sh" "$OUT"
    for f in gfx_core gfx_rp; do
        "$OPENGPU_CC" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
            -I"$OUT/ogfx-core" -c -o "$OUT/ogfx_$f.o" "$OUT/ogfx-core/$f.c"
    done
    link_gpu "$OUT/gfopengpu.library" "" "$OUT/ogfx_gfx_core.o" "$OUT/ogfx_gfx_rp.o"
fi
echo "$OUT/gfopengpu.library ($(wc -c < "$OUT/gfopengpu.library") bytes)"
# No writable data: it may run from ROM, so everything it keeps is in its base.
python3 "$ROOT/tools/hunk_rw_check.py" "$OUT/gfopengpu.library"
if [ "${OGPU_ROM:-0}" != 1 ]; then
    mkdir -p "$OUT/rom"
    link_gpu "$OUT/rom/gfopengpu.library" -DOGPU_ROM_NO_OGFX
    echo "$OUT/rom/gfopengpu.library ($(wc -c < "$OUT/rom/gfopengpu.library") bytes, the GF ROM's: no GFOpenGfx patches or core)"
    python3 "$ROOT/tools/hunk_rw_check.py" "$OUT/rom/gfopengpu.library"
fi
"$CC" -m68040 $SOFT -O2 -fno-delete-null-pointer-checks -Wall -Werror -Wno-pointer-sign -noixemul -I"$ROOT/include" -o "$OUT/GFOpenRTG" "$ROOT/tools/openrtg_cmd.c"
echo "$OUT/GFOpenRTG ($(wc -c < "$OUT/GFOpenRTG") bytes)"
"$CC" -m68040 $SOFT -O2 -fno-delete-null-pointer-checks -Wall -Werror -Wno-pointer-sign -noixemul -I"$ROOT/include" -o "$OUT/GFOpenRTG-Monitor" "$ROOT/tools/openrtg_monitor.c"
echo "$OUT/GFOpenRTG-Monitor ($(wc -c < "$OUT/GFOpenRTG-Monitor") bytes)"
"$HERE/warp3d/build.sh" "$OUT"        # Warp3D.library, its tools and checks
"$HERE/minigl/build.sh" "$OUT"        # minigl.library, libminigl.a, libmgl.a and MGLTest
"$HERE/tinygl/build.sh" "$OUT"        # tinygl.library (the stand-in) and libtinygl.a
"$HERE/egl/build.sh" "$OUT"           # libEGL.a: EGL 1.4 on GLA
[ ! -x "${SDL2_STOVE:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}/bin/m68k-amigaos-gcc" ] || STOVE="${SDL2_STOVE:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}" "$HERE/modules/sdl2/build.sh" "$OUT"   # SDL 2 (library/modules/sdl2), with the GCC 16 stove
[ ! -x "${SDL2_STOVE:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}/bin/m68k-amigaos-gcc" ] || STOVE="${SDL2_STOVE:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}" "$HERE/modules/openal/build.sh" "$OUT"   # OpenAL (library/modules/openal), with the GCC 16 stove
# SDL 1.2 (library/modules/sdl12: sdl12-compat on that SDL 2), when SDL 2 was built.
[ ! -f "$OUT/libSDL2.a" ] || STOVE16="${SDL2_STOVE:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}" "$HERE/modules/sdl12/build.sh" "$OUT"
# No float-to-int store through the FPCR register (tools/fpcr_check.py: GCC 16
# at -m68040 can make one; the check runs on every build, whichever stove).
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/gfopenrtg.library" "$OUT/cybergraphics.library" "$OUT/gfopengpu.library" $([ "${OGPU_ROM:-0}" = 1 ] || echo "$OUT/rom/gfopengpu.library") "$OUT/GFOpenRTG" "$OUT/GFOpenRTG-Monitor" "$OUT/minigl.library" "$OUT/tinygl.library" "$OUT/libtinygl.a" "$OUT/libEGL.a"
