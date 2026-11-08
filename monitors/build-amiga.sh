#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set): the chipset's monitors.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
CC=$STOVE/prefix/bin/m68k-amigaos-gcc
SOFT="-m68040 -specs=$ROOT/tools/m68k-soft-m68040.specs"
mkdir -p "$OUT"
for t in listmodes modescreen; do
    "$CC" $SOFT -noixemul -Os -fno-delete-null-pointer-checks -Wall -o "$OUT/$t" "$HERE/$t.c"
done
for c in AGA ECS; do
    "$CC" $SOFT -nostartfiles -Os -fno-toplevel-reorder -fno-delete-null-pointer-checks -fomit-frame-pointer -Wall -DCHIPSET_$c -o "$OUT/$c" "$HERE/chipset_monitor.c" -lamiga
done
