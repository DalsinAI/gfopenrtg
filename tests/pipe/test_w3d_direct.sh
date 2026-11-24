#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Warp3D's direct calls to the virgl route (stage M7, part 2): tests/run_w3d_vg.sh once with every batch read back
# (w3dvg_run, as the old route still does) and once as the calls Warp3D's draw calls now make (W3D_DIRECT=1). Both
# must pass, the pictures of the two must be the same files, no random scene may be further from the CPU's picture
# with the calls, and the drawing area's lock must be taken once for each load and each present and never held when
# a call returns (the test's own checks). On llvmpipe with the machine's libvirglrenderer (ACVIRGL_LIB names
# another).
#   tests/pipe/test_w3d_direct.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/../.."
: "${ACVIRGL_LIB:=/usr/lib/x86_64-linux-gnu/libvirglrenderer.so.1}"
if [ ! -e "$ACVIRGL_LIB" ]; then echo "test_w3d_direct: no $ACVIRGL_LIB: it cannot run here"; exit 1; fi
export ACVIRGL_LIB
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
BASE=${OUT:-$ROOT/build/tests}
mkdir -p "$BASE/w3d-read" "$BASE/w3d-direct"
OUT="$BASE/w3d-read" W3D_SEEDS=1 sh "$HERE/../run_w3d_vg.sh" 100 > "$BASE/w3d-read.log" 2>&1 || { cat "$BASE/w3d-read.log"; echo "test_w3d_direct: the reader's run failed"; exit 1; }
OUT="$BASE/w3d-direct" W3D_SEEDS=1 W3D_DIRECT=1 sh "$HERE/../run_w3d_vg.sh" 100 > "$BASE/w3d-direct.log" 2>&1 || { cat "$BASE/w3d-direct.log"; echo "test_w3d_direct: the direct calls' run failed"; exit 1; }
grep -E "direct calls|held for the lock|checks, " "$BASE/w3d-direct.log"
grep -q "w3d virgl direct calls:" "$BASE/w3d-direct.log" || { echo "test_w3d_direct: the run made no direct calls"; exit 1; }
n=0; bad=0
for f in "$BASE"/w3d-read/w3d-*-virgl.ppm; do
    g="$BASE/w3d-direct/$(basename "$f")"
    n=$((n + 1))
    if ! cmp -s "$f" "$g"; then bad=$((bad + 1)); echo "differs: $(basename "$f")"; fi
done
# the benches' and fixed scenes' lines (worst channel, pixels over) must be the same too
grep -E "^w3d virgl (bench|mixed|held frame|Z read)" "$BASE/w3d-read.log" | grep -v -E " ms|/s\b" > "$BASE/w3d-read.lines" || true
grep -E "^w3d virgl (bench|mixed|held frame|Z read)" "$BASE/w3d-direct.log" | grep -v -E " ms|/s\b" > "$BASE/w3d-direct.lines" || true
[ -s "$BASE/w3d-read.lines" ] || { echo "test_w3d_direct: no scene's line to compare"; bad=$((bad + 1)); }
if ! cmp -s "$BASE/w3d-read.lines" "$BASE/w3d-direct.lines"; then bad=$((bad + 1)); diff "$BASE/w3d-read.lines" "$BASE/w3d-direct.lines" | head -20; fi
# The random scenes, seed by seed: the same scenes run, and none further from the CPU's picture with the calls than
# read back. They may be nearer: with the lock, a load of the drawing area goes to the host at once, in a block of
# its own; read back, it shares a block with the draws, and a few scenes (an area at an odd address with padded
# rows, random Z and stencil under it) then lose pixels. Those are listed.
grep -E "^  seed [0-9]+: worst" "$BASE/w3d-read.log" > "$BASE/w3d-read.seeds" || true
grep -E "^  seed [0-9]+: worst" "$BASE/w3d-direct.log" > "$BASE/w3d-direct.seeds" || true
[ -s "$BASE/w3d-read.seeds" ] || { echo "test_w3d_direct: no random scene ran"; bad=$((bad + 1)); }
paste "$BASE/w3d-read.seeds" "$BASE/w3d-direct.seeds" | awk -F'\t' '
    { split($1, a, " "); split($2, b, " ");
      scenes++;
      if (a[2] != b[2]) { print "not the same scene: " $1 " | " $2; worse++; next }
      if (b[5] + 0 > a[5] + 0) { print "further from the CPU with the calls: " $2 " (read back:" $1 ")"; worse++ }
      else if ($1 != $2) { print "nearer with the calls:" $2 " (read back:" $1 ")"; nearer++ } }
    END { printf "random scenes: %d the same or nearer (%d nearer), %d further\n", scenes - worse, nearer, worse; exit worse ? 1 : 0 }' || bad=$((bad + 1))
echo "test_w3d_direct: $n pictures compared, $bad differences"
[ "$n" -gt 0 ] && [ "$bad" -eq 0 ]
