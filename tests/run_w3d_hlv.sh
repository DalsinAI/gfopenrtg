#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Warp3D.library's per-vertex conversion against hostlib_w3d_vertices, the
# same work as host code (tests/test_w3d_hlv.c), with UBSan where the
# compiler has it.
#   tests/run_w3d_hlv.sh [cases]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/.."
OUT=${OUT:-$ROOT/build/tests}
mkdir -p "$OUT"
san=
if echo 'int main(void){return 0;}' | cc -x c -fsanitize=undefined -fno-sanitize-recover=all - -o "$OUT/san$$" 2>/dev/null; then
    san="-fsanitize=undefined -fno-sanitize-recover=all"
fi
rm -f "$OUT/san$$"
for opt in -O0 -O2; do
    # shellcheck disable=SC2086
    cc -std=gnu11 $opt $san -I"$HERE/host-include" -I"$ROOT/include" -Wall -Wextra -Werror \
        -o "$OUT/test_w3d_hlv$opt" "$HERE/test_w3d_hlv.c"
    "$OUT/test_w3d_hlv$opt" ${1:-3000}
done
