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
LOG=$OUT/build.log
rc=0
STOVE=$PREFIX sh "$HERE/build.sh" "$OUT" ${TARGETS:-module stub} > "$LOG" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    grep -n 'error' "$LOG" | head -60
    tail -n 60 "$LOG"
    exit "$rc"
fi
tail -n 12 "$LOG"
echo "warnings: $(grep -c 'warning:' "$LOG" || true)"
for f in SDL3.module libSDL3.a; do
    [ -f "$OUT/$f" ] || { echo "no $f in $OUT" >&2; exit 1; }
done
"$PREFIX/bin/m68k-amigaos-ar" t "$OUT/libSDL3.a"
sha256sum "$OUT/SDL3.module" "$OUT/libSDL3.a"
rm -rf "$OUT/sdl3-work/SDL3-3.4.14" "$OUT/sdl3-work/SDL3_68k"
