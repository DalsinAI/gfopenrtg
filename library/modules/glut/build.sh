#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GLUT on GFOpenGPU (freeglut's interface) for AmigaOS 3.2, with the GCC 16
# stove: libglut.a, on GLA and an Intuition window.
#   library/modules/glut/build.sh [OUT_DIR]     (default build/)
# Makes OUT_DIR/glut/, shaped as the kit is: include/GL/glut.h and
# freeglut.h (freeglut 3.6.0's headers, with GLUT_ES_PROFILE added to
# freeglut_ext.h), lib/libglut.a, lib/pkgconfig/glut.pc and LICENSE.txt
# (freeglut's COPYING).
#
# libglut.a is glut_os3.c (the Team's: the window, its events and GLUT's
# state) and freeglut's own fg_geometry.c, fg_teapot.c, fg_font.c,
# fg_font_data.c and fg_stroke_*.c, built unchanged against fg_internal.h
# and fg_gl2.h here in place of freeglut's. freeglut's headers include
# GL/glu.h, so OUT_DIR/glu (library/modules/glu/build.sh) comes first.
#
# KIT gives GL's and GLA's headers (the developer kit, or a stove's
# prefix/m68k-amigaos); NDK the OS 3.2 NDK's include directory, when the
# compiler has no proto/ of its own. freeglut's tarball is
# TARBALLS/freeglut-3.6.0.tar.gz, fetched when it isn't there and checked
# against UPSTREAM.json's sha256.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}
KIT=${KIT:-$STOVE16/m68k-amigaos}
NDK=${NDK:-}
OUT=${1:-$REPO/build}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
TARBALLS=${TARBALLS:-$OUT/tarballs}
CC=${CC:-$STOVE16/bin/m68k-amigaos-gcc}
AR=${AR:-$STOVE16/bin/m68k-amigaos-ar}
OBJDUMP=${OBJDUMP:-$STOVE16/bin/m68k-amigaos-objdump}
eval "$(python3 - "$HERE/UPSTREAM.json" <<'PY'
import json, sys
u = json.load(open(sys.argv[1]))["freeglut"]
print(f"FILE='{u['file']}' DIR='{u['dir']}' SHA='{u['sha256']}' URL='{u['url']}' VERSION='{u['version']}'")
PY
)"
[ -f "$KIT/include/GL/gl.h" ] && [ -f "$KIT/include/gla/gla_core.h" ] || { echo "glut: no GL/gl.h and gla/ in $KIT/include (give KIT)" >&2; exit 1; }
[ -f "$OUT/glu/include/GL/glu.h" ] || { echo "glut: no $OUT/glu (library/modules/glu/build.sh first)" >&2; exit 1; }
mkdir -p "$TARBALLS"
if [ ! -f "$TARBALLS/$FILE" ]; then
    curl -sSfL -o "$TARBALLS/$FILE.part" "$URL" && mv "$TARBALLS/$FILE.part" "$TARBALLS/$FILE"
fi
echo "$SHA  $TARBALLS/$FILE" | sha256sum -c --quiet - || { echo "glut: $FILE's sha256 is not UPSTREAM.json's" >&2; exit 1; }

W=$OUT/glut-work
S=$W/$DIR
mkdir -p "$W"
[ -d "$S" ] || tar xzf "$TARBALLS/$FILE" -C "$W"

G=$OUT/glut
rm -rf "$G"
mkdir -p "$G/include/GL" "$G/lib/pkgconfig"
for h in glut.h freeglut.h freeglut_std.h freeglut_ext.h freeglut_ucall.h; do
    cp "$S/include/GL/$h" "$G/include/GL/$h"
done
# GLES contexts, which freeglut has no profile for: this kit's addition.
sed -i 's|^#define\tGLUT_COMPATIBILITY_PROFILE          0x0002$|&\
#define GLUT_ES_PROFILE                     0x0004  /* GFOpenGPU: GLES 2 or 3 (glutInitContextVersion) */|' "$G/include/GL/freeglut_ext.h"
grep -q GLUT_ES_PROFILE "$G/include/GL/freeglut_ext.h" || { echo "glut: freeglut_ext.h has no GLUT_COMPATIBILITY_PROFILE line to add GLUT_ES_PROFILE after" >&2; exit 1; }

# freeglut's files beside the stand-ins, so their #include "fg_internal.h"
# finds ours, not freeglut's.
F=$W/src
rm -rf "$F"
mkdir -p "$F" "$W/obj"
FG="fg_geometry.c fg_teapot.c fg_font.c fg_font_data.c fg_stroke_roman.c fg_stroke_mono_roman.c"
for f in $FG fg_teapot_data.h; do cp "$S/src/$f" "$F/"; done
cp "$HERE/fg_internal.h" "$HERE/fg_gl2.h" "$F/"

# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access. GCC 16 also gets
# -fno-tree-loop-distribute-patterns (its loop-shift miscompile). Hard float,
# as libGL.a is.
CF="-m68040 -m68881 -mnobitfield -O2 -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -fomit-frame-pointer -noixemul -DGL_GLEXT_PROTOTYPES -DNDEBUG"
INC="-I$G/include -I$OUT/glu/include -I$KIT/include${NDK:+ -I$NDK}"
OBJS=
for f in $FG; do
    o=$W/obj/${f%.c}.o
    "$CC" $CF -w $INC -c -o "$o" "$F/$f"
    OBJS="$OBJS $o"
done
"$CC" $CF -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers $INC -I"$F" -c -o "$W/obj/glut_os3.o" "$HERE/glut_os3.c"
OBJS="$OBJS $W/obj/glut_os3.o"
# No 68881 instruction the 68040 leaves to software.
for o in $OBJS; do
    if "$OBJDUMP" -d "$o" | grep -Eq '\sf(sin|cos|tan|atan|etox|logn|log10|log2|intrz|int|mod|rem)[.a-z]*\s'; then
        echo "libglut.a: $o has FPU instructions the 68040 lacks" >&2
        exit 1
    fi
done
# GCC 16's float-to-int store through the FPCR register (tools/fpcr_check.py).
python3 "$REPO/tools/fpcr_check.py" "$OBJDUMP" $OBJS >/dev/null || { python3 "$REPO/tools/fpcr_check.py" "$OBJDUMP" $OBJS; exit 1; }
"$AR" rcs "$G/lib/libglut.a" $OBJS
cp "$S/COPYING" "$G/LICENSE.txt"
cat > "$G/lib/pkgconfig/glut.pc" <<PC
# GFOpenGPU developer kit. The kit's prefix is two levels up from this file:
# the kit's own, or a stove's prefix/m68k-amigaos.
prefix=\${pcfiledir}/../..
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: glut
Description: GLUT (freeglut's interface) on GFOpenGPU's GLA, in an Intuition window
Version: $VERSION
Requires: glu gl
Libs: -L\${libdir} -lglut
Cflags: -I\${includedir}
PC
echo "$G/lib/libglut.a ($(wc -c < "$G/lib/libglut.a") bytes)"
