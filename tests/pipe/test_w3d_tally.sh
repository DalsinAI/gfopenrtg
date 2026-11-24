#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Warp3D's virgl stream, command by command: tests/run_w3d_vg.sh as the M0 gate runs it (llvmpipe), with the bridge
# counting each virgl command by command and object (MGL_VG_TALLY=1). For finding where a change of the gate's
# word count comes from: run it on both commits and compare. It fails only when the run fails or counts nothing.
#   tests/pipe/test_w3d_tally.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
: "${ACVIRGL_LIB:=/usr/lib/x86_64-linux-gnu/libvirglrenderer.so.1}"
if [ ! -e "$ACVIRGL_LIB" ]; then echo "test_w3d_tally: no $ACVIRGL_LIB: the virgl half cannot run here"; exit 1; fi
export ACVIRGL_LIB
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe MGL_VG_TALLY=1
OUT=${OUT:-$ROOT/build/w3d-tally}
rm -rf "$OUT"; mkdir -p "$OUT"
OUT="$OUT" sh "$ROOT/tests/run_w3d_vg.sh" > "$OUT/run.log" 2>&1 || { tail -20 "$OUT/run.log"; exit 1; }
grep -E '^minigl_vg_bridge: ([0-9]+ blocks|tally )' "$OUT/run.log" | tee "$OUT/tally.txt"
grep -q 'tally cmd' "$OUT/tally.txt"
