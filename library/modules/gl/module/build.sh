#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GL.module and libGL.a for AmigaOS 3.2, on the Mesa build that mesa/build.sh
# makes (run first, for the AmigaOS cross file), with the GCC 16 stove.
# GL.module is shared: Mesa is built -fbaserel32 (the cross file), the module
# linked -resident32, so gfopengpu.library loads its code once and gives each
# program its own copy of the data (include/gfopengpu/module.h). libGL.a sets
# A4 on every call.
#   library/modules/gl/module/build.sh [OUT_DIR]      (default build/gl)
# OUT_DIR gets GL.module (for LIBS:GFOpenGPU/), libGL.a and include/ (the
# GLA headers; programs take GL's own from Mesa, $SRC/include in
# gla-link.env). GL_WORK as for mesa/build.sh. STOVE (the GCC 6.5 stove) is
# only for GLArrays' second build, when it is there.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GL=$(CDPATH= cd -- "$HERE/.." && pwd)
REPO=$(CDPATH= cd -- "$GL/../../.." && pwd)
COMMON=$REPO/library/modules/common
STUBS=$REPO/stubs/gl
STOVE16=${STOVE16:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
OUT=${1:-$REPO/build/gl}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
export PATH="$STOVE16/bin:$PATH"

(cd "$GL" && CROSS=mesa/cross/m68k-amigaos.ini sh mesa/build.sh)
B=${GL_WORK:-$GL/build}/mesa-build-m68k-amigaos
. "$B/gla-link.env"

# cybergraphics.library's calls and numbers: the Team's clean-room headers
# (gla/os3/include, from compat/cybergraphics_lib.c), not a vendor's.
CGX=$GL/gla/os3/include

W=$OUT/work
mkdir -p "$W"
# The calls: every GL entry point Mesa's glapi generated for this build, and GLA's.
"${CC%gcc}nm" -g --defined-only "$B/src/mesa/glapi/glapi/libglapi_bridge.a" | sed -n 's/^[0-9a-f]* T _\(gl[A-Z][A-Za-z0-9_]*\)$/\1/p' | sort -u > "$W/gl.names"
# Each call's argument size from Mesa's glapi XML and the GLA headers, for the stub's A4 entries.
python3 "$STUBS/gen_gl.py" --stub "$W/gl_stub_calls.s" --exports "$W/gl_exports.c" --xml "$SRC/src/mesa/glapi/glapi/gen" \
    --header "$GL/gla/gla_core.h" --header "$GL/gla/os3/gla_present_os3.h" --header "$GL/gla/os3/gla_virgl_os3.h" \
    "$W/gl.names" "$STUBS/gla.names"

# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access.
MF="-m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -noixemul -Wall -Wextra -Werror -Wno-unused-parameter -I$REPO/include -I$COMMON -I$STUBS"
# The module's own objects reach their globals through A4 too.
MODF="$MF -fbaserel32"
cd "$B"
eval "$CC $FLAGS -I$REPO/include -I$GL -I$CGX -O2 -c $GL/gla/os3/gla_present_os3.c -o gla_present_os3.o"
$CC $MODF -c -o "$W/module_start.o" "$COMMON/module_start.S"
$CC $MODF -c -o "$W/module_rt.o" "$COMMON/module_rt.c"
$CC $MODF -c -o "$W/gl_module.o" "$HERE/gl_module.c"
$CC $MODF -c -o "$W/gl_exports.o" "$W/gl_exports.c"
# module_start.o first: its first code is the module's. -nostartfiles: no
# libnix startup (module_rt.c stands in). -resident32: the data-to-data
# relocations, for each program's copy.
# --gc-sections (10 November 2026): Mesa is built -ffunction-sections
# -fdata-sections, and the sections nothing reaches are left out (about
# 0.9 MB). The stove's linker (bfd/amigaos.c, amiga_gc_sections) starts
# from every section of the first object, module_start.o, so the header,
# the entry and through it the call table (gl_exports.c) and everything
# they reach stay; libnix's .list_/.dlist_ set entries stay with any
# object that keeps a section. Don't add KEEP() in a linker script: that
# linker doesn't follow the relocations of a section already marked kept.
# -e names the first code, the module's start (no "entry symbol" warning).
$CC $MESA_LFLAGS -nostartfiles $WEAK -Wl,--gc-sections -Wl,-e,_ogpu_module_start -Wl,-Map,"$W/GL.module.map" -o "$W/GL.module.debug" "$W/module_start.o" "$W/module_rt.o" "$W/gl_module.o" "$W/gl_exports.o" \
    $GLAOBJS gla_present_os3.o $SHIM ${FIXOBJS:-} -Wl,--start-group $LIBS $OPT -Wl,--end-group $SYSLIBS -latomic -lamiga
# No float-to-int store through the FPCR register may ship (tools/fpcr_check.py),
# and the data is reached only through A4 (tools/baserel_check.py).
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$W/GL.module.debug"
python3 "$REPO/tools/baserel_check.py" "${CC%gcc}" "$W/GL.module.debug" "$W/GL.module.map" --allow "$HERE/baserel.allow"
# (strip refuses some -fbaserel32 links with a big data hunk; tools/hunk_strip.py doesn't.)
"${CC%gcc}strip" -o "$OUT/GL.module" "$W/GL.module.debug" 2>/dev/null || python3 "$REPO/tools/hunk_strip.py" "$W/GL.module.debug" "$OUT/GL.module"
echo "built $OUT/GL.module ($(wc -c < "$OUT/GL.module") bytes)"

# libGL.a: the generated entries and the loader (-ffixed-a4: it sets A4 for
# its calls into the module).
$CC $MF -c -o "$W/gl_stub_calls.o" "$W/gl_stub_calls.s"
$CC $MF -ffixed-a4 -c -o "$W/gl_stub.o" "$STUBS/gl_stub.c"
rm -f "$OUT/libGL.a"
"${CC%gcc}ar" rcs "$OUT/libGL.a" "$W/gl_stub_calls.o" "$W/gl_stub.o"
echo "built $OUT/libGL.a ($(wc -l < "$W/gl.names") GL calls, $(wc -l < "$STUBS/gla.names") GLA calls)"

# The GLA headers programs include beside GL's.
mkdir -p "$OUT/include/gla/os3"
cp "$GL/gla/gla_core.h" "$OUT/include/gla/"
cp "$GL/gla/os3/gla_present_os3.h" "$GL/gla/os3/gla_virgl_os3.h" "$OUT/include/gla/os3/"

# GLArrays (tests/glarrays.c): vertex and index data in every width, through
# libGL.a, with this stove and the GCC 6.5 one (STOVE) when it is there;
# run on an Amiga (virgl, then CPU for softpipe).
mkdir -p "$OUT/tests"
$CC -O2 -noixemul -m68040 -m68881 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -I"$SRC/include" -I"$OUT/include" \
    -o "$OUT/tests/GLArrays" "$GL/tests/glarrays.c" -L"$OUT" -lGL -lm
CC65=$STOVE/prefix/bin/m68k-amigaos-gcc
if [ -x "$CC65" ]; then
    "$CC65" -O2 -noixemul -m68040 -m68881 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -I"$SRC/include" -I"$OUT/include" \
        -o "$OUT/tests/GLArrays-gcc65" "$GL/tests/glarrays.c" -L"$OUT" -lGL -lm
fi
# GLDraws (tests/gldraws.c): 300 draws a frame three ways, timed, with the
# request blocks a frame (the virgl transport's batching, 9 Nov 2026).
$CC -O2 -noixemul -m68040 -m68881 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -I"$SRC/include" -I"$OUT/include" \
    -o "$OUT/tests/GLDraws" "$GL/tests/gldraws.c" -L"$OUT" -lGL -lm
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/tests/GLDraws"
python3 "$REPO/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/tests/"GLArrays*
echo "built $OUT/tests/GLArrays (run it on an Amiga: GLArrays, then GLArrays CPU)"
