#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# ogg2iff, for the PC and for the Amiga, and its tests.
#
#     tools/ogg2iff/build.sh [host|amiga|test|all] [OUT]     (default: all)
#
# host:  OUT/ogg2iff with the PC's cc (HOST_CC, default cc)
# amiga: OUT/ogg2iff.68k, m68k-amigaos-gcc -m68040 -m68881 -noixemul
#        (AMIGA_CC; default m68k-amigaos-gcc on PATH, else the Cradle's
#        GFOpenAmigaGCC 16.2 in ~/CradleLab/toolchain)
# test:  builds host, then tests/check.py against it
# OUT defaults to tools/ogg2iff/build.
#
# stb_vorbis (public domain or MIT, Sean Barrett) is third-party code, so it
# is not committed: it is fetched at the pinned commit into OUT/stb and
# checked against its sha256. STB_VORBIS=/path/stb_vorbis.c uses a copy you
# have (it must match the same sha256).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
WHAT=${1:-all}
OUT=${2:-$HERE/build}
mkdir -p "$OUT"

STB_COMMIT=4e9b368bfc071e97206ea5c6adba68dd0cbf8897   # stb_vorbis 1.22
STB_SHA256=4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636
STB_URL=https://raw.githubusercontent.com/nothings/stb/$STB_COMMIT/stb_vorbis.c

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -d' ' -f1
    else shasum -a 256 "$1" | cut -d' ' -f1
    fi
}

fetch_stb() {
    STB=$OUT/stb/stb_vorbis.c
    mkdir -p "$OUT/stb"
    if [ -f "$STB" ] && [ "$(sha256 "$STB")" = "$STB_SHA256" ]; then return; fi
    if [ -n "${STB_VORBIS:-}" ]; then
        cp "$STB_VORBIS" "$STB.part"
    elif command -v curl >/dev/null 2>&1; then
        curl -sSfL -o "$STB.part" "$STB_URL"
    else
        wget -q -O "$STB.part" "$STB_URL"
    fi
    if [ "$(sha256 "$STB.part")" != "$STB_SHA256" ]; then
        echo "stb_vorbis.c doesn't match its pinned sha256 $STB_SHA256" >&2
        rm -f "$STB.part"
        exit 1
    fi
    mv "$STB.part" "$STB"
}

host() {
    fetch_stb
    ${HOST_CC:-cc} -O2 -DNDEBUG -Wall -I"$OUT/stb" -o "$OUT/ogg2iff" "$HERE/ogg2iff.c" -lm
    echo "built $OUT/ogg2iff"
}

amiga() {
    fetch_stb
    CC=${AMIGA_CC:-}
    if [ -z "$CC" ]; then
        if command -v m68k-amigaos-gcc >/dev/null 2>&1; then CC=m68k-amigaos-gcc
        else CC=$HOME/CradleLab/toolchain/openamigagcc-16.2.0-open4-x86_64-linux/bin/m68k-amigaos-gcc
        fi
    fi
    # -m68881: real FPU code. The SDK's programs are soft float so they also
    # run on an LC040; a Vorbis decoder in soft float would be many times
    # slower, and this is an offline tool, so it wants the FPU.
    "$CC" -O2 -m68040 -m68881 -fomit-frame-pointer -DNDEBUG -noixemul -Wall -I"$OUT/stb" \
        -o "$OUT/ogg2iff.68k" "$HERE/ogg2iff.c" -lm
    echo "built $OUT/ogg2iff.68k"
}

case $WHAT in
host) host ;;
amiga) amiga ;;
test) host; python3 "$HERE/tests/check.py" "$OUT/ogg2iff" "$OUT/test" ;;
all) host; amiga; python3 "$HERE/tests/check.py" "$OUT/ogg2iff" "$OUT/test" ;;
*) echo "usage: $0 [host|amiga|test|all] [OUT]" >&2; exit 2 ;;
esac
