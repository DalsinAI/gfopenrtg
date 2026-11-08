#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tools/glinfo),
# or by hand: GLInfo (glinfo.c), GL.module's versions, extensions, limits and
# shader compile times, through virgl and softpipe. It links the GCC 16
# stove's libGL.a, or the developer kit's when SDK= names one (an unpacked
# OpenGPU-SDK: include/ and lib/libGL.a), with CC= its compiler and NDK= the
# NDK's C headers when the compiler has none of its own. Run it on an
# Amiga with LIBS:GFOpenGPU/GL.module: GLInfo TO=RAM:glinfo.txt.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build-amiga}
if [ -n "${SDK:-}" ]; then
    CC=${CC:-m68k-amigaos-gcc}
    INC=$SDK/include
    [ -z "${NDK:-}" ] || INC="$INC $NDK"
    GL_LIB=$SDK/lib/libGL.a
else
    STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
    if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then P=$STOVE/prefix; else P=$STOVE; fi
    CC=${CC:-$P/bin/m68k-amigaos-gcc}
    SYS=$P/m68k-amigaos
    INC=$SYS/include
    GL_LIB=
    for base in "$SYS/lib" "$P/lib" "$SYS" "$P"; do
        [ -d "$base" ] || continue
        GL_LIB=$(find "$base" -type f -name libGL.a -print -quit 2>/dev/null || true)
        [ -z "$GL_LIB" ] || break
    done
fi
"$CC" --version | head -1
[ -n "$GL_LIB" ] && [ -f "$GL_LIB" ] || { echo "GLInfo: no libGL.a"; exit 5; }
echo "libGL.a: $GL_LIB"
mkdir -p "$OUT/c"
# GL is hard float: -m68881, as libGL.a was built.
"$CC" -O2 -noixemul -m68040 -m68881 -fno-delete-null-pointer-checks -fno-tree-loop-distribute-patterns \
    -Wall -Wextra -Werror $(for i in $INC; do printf -- "-I%s " "$i"; done) -o "$OUT/c/GLInfo" "$HERE/glinfo.c" -L"$(dirname "$GL_LIB")" -lGL -lm
python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/c/GLInfo" || true
echo "$OUT/c/GLInfo ($(wc -c < "$OUT/c/GLInfo") bytes)"
sha256sum "$OUT/c/GLInfo"
