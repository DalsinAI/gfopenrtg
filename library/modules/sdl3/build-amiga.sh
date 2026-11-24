#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target
# library/modules/sdl3): SDL3.module and libSDL3.a (build.sh's module and
# stub; TARGETS= for more), with the stove's compiler. The whole log goes to
# OUT/build.log; the Cradle keeps only the end.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
# The AHI headers (build.sh's SDK_HEADERS): as library/modules/sdl2/build-amiga.sh.
if [ -z "${SDK_HEADERS:-}" ]; then
    for d in "$PREFIX/m68k-amigaos/include" "$HOME/AmiCradle/kitchen/stoves/os32/prefix/m68k-amigaos/include" \
             "$HOME/AmiCradle/stoves/os32/prefix/m68k-amigaos/include"; do
        if [ -f "$d/devices/ahi.h" ]; then SDK_HEADERS=$d; export SDK_HEADERS; break; fi
    done
fi
mkdir -p "$OUT"
# SDL's tree is built outside OUT (the Cradle lists every file in OUT).
B=${SDL3_BUILD_DIR:-${TMPDIR:-/tmp}/sdl3-build-$$}
mkdir -p "$B"
LOG=$OUT/build.log
rc=0
STOVE=$PREFIX sh "$HERE/build.sh" "$B" ${TARGETS:-module stub} > "$LOG" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    grep -n 'error' "$LOG" | head -60
    tail -n 60 "$LOG"
    rm -rf "$B"
    exit "$rc"
fi
cp "$B/SDL3.module" "$B/libSDL3.a" "$OUT/"
mkdir -p "$OUT/include" && cp -R "$B/include/SDL3" "$OUT/include/"
# With TARGETS=... tests (build-amiga-tests.sh): the test programs, for a lab.
if [ -d "$B/tests/sdl3" ]; then
    mkdir -p "$OUT/tests"
    for t in "$B"/tests/sdl3/*; do [ -f "$t" ] && cp "$t" "$OUT/tests/"; done
fi
rm -rf "$B"
tail -n 12 "$LOG"
echo "warnings: $(grep -c 'warning:' "$LOG" || true)"
for f in SDL3.module libSDL3.a; do
    [ -f "$OUT/$f" ] || { echo "no $f in $OUT" >&2; exit 1; }
done
"$PREFIX/bin/m68k-amigaos-ar" t "$OUT/libSDL3.a"
sha256sum "$OUT/SDL3.module" "$OUT/libSDL3.a"
