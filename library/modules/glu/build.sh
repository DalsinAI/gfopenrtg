#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GLU on GFOpenGPU's GL: SGI's libGLU (Mesa's glu 9.0.3, SGI Free Software
# License B 2.0) built for AmigaOS 3.2 as libGLU.a, with the GCC 16 stove.
#   library/modules/glu/build.sh [OUT_DIR]     (default build/)
# Makes OUT_DIR/glu/, shaped as the kit is: include/GL/glu.h, lib/libGLU.a,
# lib/pkgconfig/glu.pc and LICENSE.txt (glu.h's notice, which is every
# source file's). All of GLU is there: the matrix helpers (gluPerspective,
# gluLookAt, gluProject ...), the mipmaps (gluBuild2DMipmaps, gluScaleImage),
# the quadrics, the tessellator and NURBS. NURBS is C++: it is built without exceptions or RTTI, and
# glu_cxx.cc gives it operator new and delete on malloc, so a C program links
# -lGLU with gcc and no libstdc++.
#
# GL's headers (GL/gl.h) come from KIT: the developer kit, or a stove's
# prefix/m68k-amigaos. The glu tarball is TARBALLS/glu-9.0.3.tar.xz, fetched
# from UPSTREAM.json's url when it isn't there, and checked by its SHA-256.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE16=${STOVE16:-$HOME/AmiCradle/stoves/os32-gcc16/prefix}
KIT=${KIT:-$STOVE16/m68k-amigaos}
OUT=${1:-$REPO/build}
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
TARBALLS=${TARBALLS:-$OUT/tarballs}
CC=${CC:-$STOVE16/bin/m68k-amigaos-gcc}
CXX=${CXX:-$STOVE16/bin/m68k-amigaos-g++}
AR=${AR:-$STOVE16/bin/m68k-amigaos-ar}
NM=${NM:-$STOVE16/bin/m68k-amigaos-nm}
OBJDUMP=${OBJDUMP:-$STOVE16/bin/m68k-amigaos-objdump}
OBJCOPY=${OBJCOPY:-$STOVE16/bin/m68k-amigaos-objcopy}
JOBS=${JOBS:-2}
eval "$(python3 - "$HERE/UPSTREAM.json" <<'PY'
import json, sys
u = json.load(open(sys.argv[1]))["glu"]
print(f"FILE='{u['file']}' DIR='{u['dir']}' SHA='{u['sha256']}' URL='{u['url']}' VERSION='{u['version']}'")
PY
)"
[ -f "$KIT/include/GL/gl.h" ] || { echo "glu: no GL/gl.h in $KIT/include (give KIT: the kit, or a stove's m68k-amigaos)" >&2; exit 1; }
mkdir -p "$TARBALLS"
if [ ! -f "$TARBALLS/$FILE" ]; then
    curl -sSfL -o "$TARBALLS/$FILE.part" "$URL" && mv "$TARBALLS/$FILE.part" "$TARBALLS/$FILE"
fi
echo "$SHA  $TARBALLS/$FILE" | sha256sum -c --quiet - || { echo "glu: $FILE's sha256 is not UPSTREAM.json's" >&2; exit 1; }

W=$OUT/glu-work
S=$W/$DIR
O=$W/obj
mkdir -p "$W"
[ -d "$S" ] || tar xJf "$TARBALLS/$FILE" -C "$W"
mkdir -p "$O"

# -fno-delete-null-pointer-checks on every 68k compile: address 0 is memory on
# an Amiga, and without it GCC puts TRAP #7 (Software Failure 80000027) where
# it proves a pointer null, in place of the access. GCC 16 also gets
# -fno-tree-loop-distribute-patterns (its loop-shift miscompile). Hard float,
# as libGL.a is.
CPU="-m68040 -m68881 -mnobitfield"
COMMON="$CPU -O2 -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns -fomit-frame-pointer -noixemul -DLIBRARYBUILD -DNDEBUG"
INC="-I$S/include -I$S/src/include -I$S/src/libnurbs/internals -I$S/src/libnurbs/interface -I$S/src/libnurbs/nurbtess -I$KIT/include"
CFLAGS="$COMMON -w $INC"
CXXFLAGS="$COMMON -w -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit $INC"

# The source list is glu's own (src/meson.build).
SRCS=$(sed -n "s/^ *'\(lib[a-z]*\/[^']*\.\(c\|cc\)\)',$/\1/p" "$S/src/meson.build")
[ -n "$SRCS" ] || { echo "glu: no sources in $S/src/meson.build" >&2; exit 1; }
OBJS=
for f in $SRCS; do
    o=$O/$(echo "$f" | tr / _ | sed 's/\.cc$/.o/; s/\.c$/.o/')
    OBJS="$OBJS $o"
    [ "$o" -nt "$S/src/$f" ] && continue
    case $f in
    *.cc) echo "$CXX $CXXFLAGS -c -o $o $S/src/$f" ;;
    *) echo "$CC $CFLAGS -c -o $o $S/src/$f" ;;
    esac
done > "$W/compile.list"
xargs -P "$JOBS" -I{} sh -c '{}' < "$W/compile.list"
"$CXX" $CXXFLAGS -c -o "$O/glu_cxx.o" "$HERE/glu_cxx.cc"
OBJS="$OBJS $O/glu_cxx.o"
# libtinygl.a has five of GLU's calls of its own (tgl/glu.h). They are weak
# here, so a TinyGL program can link -lGLU too: with both, whichever library
# comes first on the line gives those five, and the rest are GLU's.
# (objcopy writes to a file of the object's own name: in place, the hunk
# unit would be named after objcopy's temporary file.)
mkdir -p "$O/weak"
W_SYMS=
for s in gluPerspective gluLookAt gluOrtho2D gluBuild2DMipmaps gluErrorString; do W_SYMS="$W_SYMS --weaken-symbol=_$s"; done
for o in project mipmap error; do
    "$OBJCOPY" $W_SYMS "$O/libutil_$o.o" "$O/weak/libutil_$o.o"
    OBJS=$(echo "$OBJS" | sed "s|$O/libutil_$o.o|$O/weak/libutil_$o.o|")
done

# No 68881 instruction the 68040 leaves to software.
for o in $OBJS; do
    if "$OBJDUMP" -d "$o" | grep -Eq '\sf(sin|cos|tan|atan|etox|logn|log10|log2|intrz|int|mod|rem)[.a-z]*\s'; then
        echo "libGLU.a: $o has FPU instructions the 68040 lacks" >&2
        exit 1
    fi
done

# GCC 16's float-to-int store through the FPCR register (tools/fpcr_check.py).
python3 "$REPO/tools/fpcr_check.py" "$OBJDUMP" $OBJS >/dev/null || { python3 "$REPO/tools/fpcr_check.py" "$OBJDUMP" $OBJS; exit 1; }

G=$OUT/glu
rm -rf "$G"
mkdir -p "$G/include/GL" "$G/lib/pkgconfig"
"$AR" rcs "$G/lib/libGLU.a" $OBJS
# Nothing from libstdc++: NURBS's C++ needs only what glu_cxx.cc gives.
"$NM" -u "$G/lib/libGLU.a" 2>/dev/null | awk 'NF==2 {print $2}' | LC_ALL=C sort -u > "$W/undef.txt"
"$NM" --defined-only "$G/lib/libGLU.a" 2>/dev/null | awk 'NF==3 {print $3}' | LC_ALL=C sort -u > "$W/def.txt"
MISSING=$(LC_ALL=C comm -23 "$W/undef.txt" "$W/def.txt" | grep -E '^__Z|^___cxa|^___gxx|^__Unwind' || true)
[ -z "$MISSING" ] || { echo "libGLU.a: needs libstdc++ for:" $MISSING >&2; exit 1; }

cp "$S/include/GL/glu.h" "$G/include/GL/glu.h"
sed -n '1,/^ \*\//p' "$S/include/GL/glu.h" | sed 's/^\/\*//; s/^ \*\/$//; s/^ \* \{0,1\}//' > "$G/LICENSE.txt"
cat > "$G/lib/pkgconfig/glu.pc" <<EOF
# GFOpenGPU developer kit. The kit's prefix is two levels up from this file:
# the kit's own, or a stove's prefix/m68k-amigaos.
prefix=\${pcfiledir}/../..
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: glu
Description: SGI's OpenGL Utility Library (Mesa's glu) on GFOpenGPU's GL
Version: $VERSION
Requires: gl
Libs: -L\${libdir} -lGLU
Cflags: -I\${includedir}
EOF
echo "$G/lib/libGLU.a ($(wc -c < "$G/lib/libGLU.a") bytes)"
