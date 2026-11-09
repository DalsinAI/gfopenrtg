#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# minigl.library's PC tests: its files built 32-bit (MGL_HOST) under
# AddressSanitizer and UBSan with a drawing area in memory, the calls going
# through the table as a program's do; and the golden scene.
#   tests/run_minigl.sh [-update]
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/.."
OUT=${OUT:-$ROOT/build/tests}
M="$ROOT/library/minigl"
O="$ROOT/library/opengpu"
mkdir -p "$OUT"
cc -m32 -std=gnu99 -DMGL_HOST -I"$HERE/host-include" -I"$ROOT/include" -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -o "$OUT/test_minigl" "$HERE/test_minigl.c" \
    "$M/mgl_math.c" "$M/mgl_state.c" "$M/mgl_matrix.c" "$M/mgl_vertex.c" "$M/mgl_pipe.c" "$M/mgl_texture.c" \
    "$M/mgl_out.c" "$M/mgl_list.c" "$M/mgl_glu.c" "$M/mgl_glut.c" "$M/mgl_choose.c" "$M/mgl_table.c" "$M/mgl_static.c" \
    "$M/mgl_vg_enc.c" "$M/mgl_vg_frame.c" "$M/mgl_vg_state.c" \
    "$O/ogpu_core.c" "$O/ogpu_dwt.c" "$O/ogpu_3d.c" "$O/ogpu_build.c" "$O/ogpu_build3d.c" -lm
"$OUT/test_minigl" "$HERE/golden/minigl-scene.txt" ${1:-} -o "$OUT/minigl-scene.ppm"
# The interface files are what the spec makes, and the table is laid out as
# the spec's section 8 says (its own reading of the spec, then offsetof).
python3 "$M/gen_api.py" --check
python3 "$M/gen_api.py" --expect "$OUT/mgl_spec_expect.h"
cc -m32 -std=gnu99 -DMGL_HOST -I"$HERE/host-include" -I"$ROOT/include" -I"$OUT" -Wall -Wextra -Werror -Wno-unused-parameter \
    -o "$OUT/test_minigl_abi" "$HERE/test_minigl_abi.c"
"$OUT/test_minigl_abi" "$ROOT/docs/design/MiniGL-Compatible-Spec.md"
# The virgl route (IDEA 5) against virglrenderer, when ACVIRGL_LIB names a
# libvirglrenderer.so.1: the same test, 32-bit, as the child of
# minigl_vg_bridge (64-bit), which carries out its request blocks.
if [ -n "${ACVIRGL_LIB:-}" ]; then
    cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -o "$OUT/minigl_vg_bridge" "$HERE/minigl_vg_bridge.c" "$ROOT/host/virgl/acvirgl.c" -ldl
    MGL_VIRGL=1 ASAN_OPTIONS=detect_leaks=0 "$OUT/minigl_vg_bridge" "$OUT/test_minigl" -o "$OUT/minigl-scene-virgl.ppm"
else
    echo "no ACVIRGL_LIB; minigl's virgl route is not run"
fi
