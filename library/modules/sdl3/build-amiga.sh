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
rm -rf "$B"
tail -n 12 "$LOG"
echo "warnings: $(grep -c 'warning:' "$LOG" || true)"
for f in SDL3.module libSDL3.a; do
    [ -f "$OUT/$f" ] || { echo "no $f in $OUT" >&2; exit 1; }
done
"$PREFIX/bin/m68k-amigaos-ar" t "$OUT/libSDL3.a"
sha256sum "$OUT/SDL3.module" "$OUT/libSDL3.a"
