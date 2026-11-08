#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GL.module's shader cache (gla/gla_shader_cache.c) on this machine, without
# building Mesa: it and tests/test_shader_cache.c, with Mesa's BLAKE3 and
# CRC-32 from a patched source tree (mesa/build.sh's, or SRC).
#   tests/shader_cache.sh [SRC]
set -eu
TOP=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC=${1:-${SRC:-${GL_WORK:-$TOP/build}/mesa-26.2.4}}
[ -f "$SRC/src/util/disk_cache.h" ] || { echo "shader cache: no Mesa tree at $SRC (run mesa/build.sh, or give one)"; exit 1; }
grep -q GLA_DISK_CACHE "$SRC/src/util/disk_cache.h" || { echo "shader cache: $SRC is without patch 0012"; exit 1; }
W=${TMPDIR:-/tmp}/gla-shader-cache-test.$$
mkdir -p "$W"
trap 'rm -rf "$W"' EXIT
echo '#define GLA_CACHE_BUILD "test"' > "$W/gla_cache_build.h"
B3=$SRC/src/util/blake3
INC="-I$W -I$SRC/src -I$SRC/src/util -I$SRC/include"
# Mesa's own files as Mesa builds them (warnings aren't ours to fix); BLAKE3 portable.
for f in "$SRC/src/util/mesa-blake3.c" "$SRC/src/util/crc32.c" "$B3/blake3.c" "$B3/blake3_dispatch.c" "$B3/blake3_portable.c"; do
    cc -std=c11 -D_GNU_SOURCE -O1 -w -DBLAKE3_NO_SSE2 -DBLAKE3_NO_SSE41 -DBLAKE3_NO_AVX2 -DBLAKE3_NO_AVX512 \
        -DBLAKE3_USE_NEON=0 $INC -c "$f" -o "$W/$(basename "$f" .c).o"
done
cc -std=c11 -D_GNU_SOURCE -O1 -Wall -Wextra -Wno-unused-parameter -Werror $INC \
    -o "$W/test_shader_cache" "$TOP/tests/test_shader_cache.c" "$TOP/gla/gla_shader_cache.c" "$W"/*.o
TMPDIR=$W "$W/test_shader_cache"
