#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Stage M0 of docs/design/OpenGPU-Shared-Pipe-Plan.md: what MiniGL and Warp3D draw today, on the PC tests,
# captured so every later stage of the shared pipe can show it changed nothing.
#
#   tests/pipe_m0.sh            run the captures and compare them with tests/golden/pipe-m0/captures.txt
#   tests/pipe_m0.sh -update    run them and write captures.txt (only when a change to the pictures is meant)
#
# A capture is the sha256 of every picture the tests draw (both routes: the CPU's and virgl's) and the counter lines
# they print (blocks, submits, puts, checks, vertex and triangle counts). The virgl route runs when ACVIRGL_LIB names
# a libvirglrenderer.so.1 (as tests/run_minigl.sh); without it the virgl half is reported as not run, and a check
# against a captures.txt that has it fails. Needs a 32-bit C compiler with AddressSanitizer (gcc-multilib).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT="$HERE/.."
GOLD="$HERE/golden/pipe-m0/captures.txt"
OUT=${OUT:-$ROOT/build/pipe-m0}
UPDATE=${1:-}
TESTS="run_minigl.sh run_w3d_vg.sh run_w3d_conv.sh run_w3d_hlv.sh run_3d.sh"
rm -rf "$OUT"; mkdir -p "$OUT"
NEW="$OUT/captures.txt"
{
    echo "# OpenGPU shared pipe, stage M0: the PC tests' pictures and counters (tests/pipe_m0.sh writes this)"
    echo "# virgl: ${ACVIRGL_LIB:+run}${ACVIRGL_LIB:-not run}"
} > "$NEW.head"
: > "$NEW.body"
for t in $TESTS; do
    log="$OUT/$t.log"
    if ! OUT="$OUT/$t" sh "$HERE/$t" > "$log" 2>&1; then
        echo "pipe_m0: $t failed; the end of its log:"; tail -20 "$log"; exit 1
    fi
    # the counter lines: what each test reports when it ends, without timings, and without the auto route's
    # choices (run_3d.sh's "(auto)" bench: which commands go to the GPU depends on how fast the machine was)
    grep -E '^(ok |minigl|w3d|ogpu3d|[0-9]+ values|test_[a-z0-9_]+:)' "$log" | grep -v -E '( ms|ms |seconds|/s\b|\(auto\))' \
        | sed "s|^|counter $t: |" >> "$NEW.body" || true
    find "$OUT/$t" -type f -name '*.ppm' | sort | while read -r p; do
        echo "pixels $t/$(basename "$p") $(sha256sum "$p" | cut -d' ' -f1)"
    done >> "$NEW.body"
done
sort -o "$NEW.body" "$NEW.body"
cat "$NEW.head" "$NEW.body" > "$NEW"; rm -f "$NEW.head" "$NEW.body"
if [ "$UPDATE" = "-update" ]; then
    mkdir -p "$(dirname "$GOLD")"; cp "$NEW" "$GOLD"
    echo "pipe_m0: wrote $(grep -c '^pixels' "$GOLD") pictures and $(grep -c '^counter' "$GOLD") counter lines to tests/golden/pipe-m0/captures.txt"
    exit 0
fi
if diff -u "$GOLD" "$NEW" > "$OUT/captures.diff"; then
    echo "pipe_m0: as captured: $(grep -c '^pixels' "$NEW") pictures and $(grep -c '^counter' "$NEW") counter lines the same"
else
    echo "pipe_m0: different from tests/golden/pipe-m0/captures.txt:"; cat "$OUT/captures.diff"; exit 1
fi
