#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target
# library/modules/sdl2): SDL2.module, libSDL2.a, libSDL2_static.a and the
# tests (build.sh's module, stub, static and tests), with the stove's
# compiler. build.sh fetches the sources UPSTREAM.json pins. The satellites
# are not built here.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$ROOT/build-amiga}
# build.sh and the Makefile take the stove's prefix directory
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then PREFIX=$STOVE/prefix; else PREFIX=$STOVE; fi
# The CyberGraphX and AHI headers (build.sh's SDK_HEADERS): the stove's own
# when it has them, else the OS 3.2 stove's, which moved under kitchen/.
if [ -z "${SDK_HEADERS:-}" ]; then
    for d in "$PREFIX/m68k-amigaos/include" "$HOME/AmiCradle/kitchen/stoves/os32/prefix/m68k-amigaos/include" \
             "$HOME/AmiCradle/stoves/os32/prefix/m68k-amigaos/include"; do
        if [ -f "$d/cybergraphx/cybergraphics.h" ] && [ -f "$d/devices/ahi.h" ]; then SDK_HEADERS=$d; export SDK_HEADERS; break; fi
    done
fi
# vasm: this stove's, else an OS 3.2 stove's (the open6 stoves have none)
if [ -z "${VASM:-}" ]; then
    for v in "$PREFIX/bin/vasmm68k_mot" \
             "$HOME/AmiCradle/kitchen/stoves/os32/prefix/bin/vasmm68k_mot" \
             "$HOME/AmiCradle/stoves/os32/prefix/bin/vasmm68k_mot" \
             "$HOME/AmiCradle/kitchen/stoves/os32-gcc16/prefix/bin/vasmm68k_mot"; do
        if [ -x "$v" ]; then VASM=$v; export VASM; break; fi
    done
fi
mkdir -p "$OUT"
# The whole log goes to OUT/build.log: the Cradle keeps only the end of
# what is printed, so the end says how it went and lists every warning.
LOG=$OUT/build.log
rc=0
STOVE=$PREFIX sh "$HERE/build.sh" "$OUT" module stub static satellites > "$LOG" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then
    tail -n 200 "$LOG"
    exit "$rc"
fi
tail -n 20 "$LOG"
ls -l "$OUT"; exit 0   # lab-only: satellites
echo "warnings, each once (file:line, from $(grep -c 'warning:' "$LOG" || true) in all):"
grep 'warning:' "$LOG" | sed -e "s|$OUT/sdl2-work/||" -e "s|$HOME/||" | sort | uniq -c || true

# The window surface in video RAM goes round the back end's framebuffer
# functions as SDL calls them (SDL_os3vram.h). Besides SDL_os3video.o, which
# hands them to SDL, only SDL_os3window.o's DestroyWindow may call one
# itself (its own framebuffer's Destroy; DestroyWindow is wrapped too).
bad=0
for o in "$OUT"/sdl2-work/obj-module/video/amigaos3/*.o; do
    n=$(basename "$o")
    for s in $("$PREFIX/bin/m68k-amigaos-nm" -u "$o" | awk '/WindowFramebuffer$/ { print $2 }'); do
        echo "$n calls $s"
        case "$n:$s" in
        SDL_os3video.o:*) ;;
        SDL_os3window.o:*DestroyWindowFramebuffer) ;;
        *) bad=1 ;;
        esac
    done
done
[ "$bad" -eq 0 ] || { echo "a framebuffer function is called past the video RAM wrappers" >&2; exit 1; }

ls -l "$OUT/SDL2.module" "$OUT/libSDL2.a" "$OUT/libSDL2_static.a" "$OUT/libSDL2_test.a"
ls "$OUT/tests/sdl2"
sha256sum "$OUT/SDL2.module" "$OUT/libSDL2.a" "$OUT/libSDL2_static.a"
"$PREFIX/bin/m68k-amigaos-gcc" --version | head -1   # last, so the log's tail has it
