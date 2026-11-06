#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/glquery),
# or by hand: GLQuery (glquery.c), what GL.module gives a program through
# virgl or softpipe, linked against the GCC 16 stove's libGL.a. Run it on an
# Amiga with LIBS:GFOpenGPU/GL.module: GLQuery, then GLQuery CPU.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then P=$STOVE/prefix; else P=$STOVE; fi
CC=${CC:-$P/bin/m68k-amigaos-gcc}
SYS=$P/m68k-amigaos
"$CC" --version | head -1
GL_LIB=
for base in "$SYS/lib" "$P/lib" "$SYS" "$P"; do
    [ -d "$base" ] || continue
    GL_LIB=$(find "$base" -type f -name libGL.a -print -quit 2>/dev/null || true)
    [ -z "$GL_LIB" ] || break
done
[ -n "$GL_LIB" ] || { echo "GLQuery: no libGL.a in $STOVE"; exit 5; }
echo "libGL.a: $GL_LIB"
mkdir -p "$OUT/c"
"$CC" -O2 -noixemul -m68040 -m68881 -fno-delete-null-pointer-checks -Wall -Wextra -Werror \
    -I"$SYS/include" -I"$ROOT/library/modules/gl" \
    -o "$OUT/c/GLQuery" "$HERE/glquery.c" -L"$(dirname "$GL_LIB")" -lGL -lm
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/c/GLQuery" || true
echo "$OUT/c/GLQuery ($(wc -c < "$OUT/c/GLQuery") bytes)"
sha256sum "$OUT/c/GLQuery"
