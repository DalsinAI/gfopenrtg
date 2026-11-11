#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target
# library/modules/sdl12): libSDL.a and libSDL_gl.a (build.sh), on SDL 2's
# link library (library/modules/sdl2/build.sh's stub), and the test
# tests/sdl12/loadwav.c built against them, as OUT/tests/sdl12/LoadWAV.
# A program built here runs on the SDL2.module already installed.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
if [ -z "${VASM:-}" ] && [ -x "$PREFIX/bin/vasmm68k_mot" ]; then VASM=$PREFIX/bin/vasmm68k_mot; export VASM; fi
mkdir -p "$OUT"
OUT=$(CDPATH= cd -- "$OUT" && pwd)
LOG=$OUT/build.log
: > "$LOG"
fail() { tail -n 200 "$LOG"; echo "sdl12: $1 failed" >&2; exit 1; }

# sdl12-compat's tarball: from TARBALLS, or downloaded once into OUT, checked
# against UPSTREAM.json's sha256 (build.sh checks it again).
TARBALLS=${TARBALLS:-$HOME/AmiCradle-dev/upstream-tarballs}
eval "$(python3 - "$HERE/UPSTREAM.json" <<'PY'
import json, sys
u = json.load(open(sys.argv[1]))["sdl12-compat"]
print(f"FILE='{u['file']}' URL='{u['url']}'")
PY
)"
if [ ! -f "$TARBALLS/$FILE" ]; then
    TARBALLS=$OUT/tarballs
    mkdir -p "$TARBALLS"
    [ -f "$TARBALLS/$FILE" ] || curl -fsL -o "$TARBALLS/$FILE" "$URL" >> "$LOG" 2>&1 || fail "download of $FILE"
fi
export TARBALLS

STOVE=$PREFIX sh "$ROOT/library/modules/sdl2/build.sh" "$OUT" stub >> "$LOG" 2>&1 || fail "SDL 2's stub"
STOVE16=$PREFIX sh "$HERE/build.sh" "$OUT" >> "$LOG" 2>&1 || fail "build.sh"

mkdir -p "$OUT/tests/sdl12"
"$PREFIX/bin/m68k-amigaos-gcc" -m68040 -m68881 -O2 -noixemul -fno-delete-null-pointer-checks -Wall \
    -I"$OUT/sdl12/include/SDL" -o "$OUT/tests/sdl12/LoadWAV" "$ROOT/tests/sdl12/loadwav.c" \
    -L"$OUT/sdl12" -lSDL -lm -lamiga >> "$LOG" 2>&1 || fail "tests/sdl12/loadwav.c"

tail -n 5 "$LOG"
ls -l "$OUT/sdl12/libSDL.a" "$OUT/sdl12/libSDL_gl.a" "$OUT/tests/sdl12/LoadWAV"
sha256sum "$OUT/sdl12/libSDL.a" "$OUT/tests/sdl12/LoadWAV"
"$PREFIX/bin/m68k-amigaos-gcc" --version | head -1
