#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target tests/amiga/vector),
# or by hand with STOVE or CC as in tools/build.sh. The CPU core is built
# with gfopengpu.library's own flags (-O2 -mnobitfield -fomit-frame-pointer):
#   OpenGPUVectorModel  tests/test_vector.c (stream v1.6) on the 68k
#   OpenGPUAliasedModel tests/test_vector_aliased.c (stream v1.8) on the 68k
#   OpenGPUGradProbe    grad_probe.c: one linear gradient, its pixels printed
#   OpenGPUVectorCRC    vector_crc.c: vector_same's random batches on this core,
#                       a CRC a round, to compare with the host's
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
OUT=${OUT:-$ROOT/build-amiga}
CC=${CC:-$STOVE/prefix/bin/m68k-amigaos-gcc}
mkdir -p "$OUT/c"
"$CC" --version | head -1
CORE="-m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -Wall -Werror"
SOFT="-specs=$ROOT/tools/m68k-soft-m68040.specs"
"$CC" $CORE $SOFT -c -o "$OUT/ogpu_core.o" "$ROOT/library/opengpu/ogpu_core.c" "$ROOT/library/opengpu/ogpu_dwt.c"
"$CC" $CORE $SOFT -c -o "$OUT/ogpu_build.o" "$ROOT/library/opengpu/ogpu_build.c"
"$CC" -m68040 $SOFT -O2 -fno-delete-null-pointer-checks -w -noixemul -o "$OUT/c/OpenGPUVectorModel" \
    "$ROOT/tests/test_vector.c" "$OUT/ogpu_core.o" "$OUT/ogpu_build.o" -lm
"$CC" -m68040 $SOFT -O2 -fno-delete-null-pointer-checks -w -noixemul -o "$OUT/c/OpenGPUAliasedModel" \
    "$ROOT/tests/test_vector_aliased.c" "$OUT/ogpu_core.o" "$OUT/ogpu_build.o" -lm
"$CC" -m68040 $SOFT -O2 -fno-delete-null-pointer-checks -Wall -Werror -noixemul -o "$OUT/c/OpenGPUGradProbe" \
    "$HERE/grad_probe.c" "$OUT/ogpu_core.o" "$OUT/ogpu_build.o"
"$CC" -m68040 $SOFT -O2 -fno-delete-null-pointer-checks -w -noixemul -o "$OUT/c/OpenGPUVectorCRC" \
    "$HERE/vector_crc.c" "$OUT/ogpu_core.o" "$OUT/ogpu_build.o"
rm -f "$OUT"/*.o
for t in OpenGPUVectorModel OpenGPUAliasedModel OpenGPUGradProbe OpenGPUVectorCRC; do
    python3 "$ROOT/tools/fpcr_check.py" "${CC%gcc}objdump" "$OUT/c/$t" || true
    echo "$OUT/c/$t ($(wc -c < "$OUT/c/$t") bytes)"
    sha256sum "$OUT/c/$t"
done
