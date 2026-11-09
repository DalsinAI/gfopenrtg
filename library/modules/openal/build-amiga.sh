#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target
# library/modules/openal): OpenAL.module, libopenal.a, libopenal_static.a,
# include/AL and the tests (build.sh's module, stub, static and tests), with
# the stove's compiler. GFOpenUp's pack build runs build.sh itself; this is
# the same build, checked the way the developer kit and the GFOpenGPU part
# take it (the files make_sdk.py --openal needs).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
# build.sh and the Makefile take the stove's prefix directory
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
mkdir -p "$OUT"
# The whole log goes to OUT/build.log: the Cradle keeps only the end.
LOG=$OUT/build.log
rc=0
STOVE=$PREFIX sh "$HERE/build.sh" "$OUT" module stub static tests > "$LOG" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    tail -n 200 "$LOG"
    exit "$rc"
fi
tail -n 20 "$LOG"
echo "warnings: $(grep -c 'warning:' "$LOG" || true)"
for f in OpenAL.module libopenal.a libopenal_static.a include/AL/al.h include/AL/alc.h include/AL/alext.h; do
    [ -f "$OUT/$f" ] || { echo "no $f in $OUT" >&2; exit 1; }
done
ls -l "$OUT/OpenAL.module" "$OUT/libopenal.a" "$OUT/libopenal_static.a"
ls "$OUT/include/AL"
sha256sum "$OUT/OpenAL.module" "$OUT/libopenal.a" "$OUT/libopenal_static.a"
"$PREFIX/bin/m68k-amigaos-gcc" --version | head -1   # last, so the log's tail has it
