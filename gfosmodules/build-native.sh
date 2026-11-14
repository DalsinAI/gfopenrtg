#!/bin/sh
# GFOpenGPU N2: portable core bodies for GFOSBOOT:'s host-platform folders.
# Manifests are made by AmiCradle's gfos_manifest.py after the 68k set.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: gfosmodules/build-native.sh OUT/gfosmodules}
X86_64_CC=${X86_64_CC:-cc}
ARM64_CC=${ARM64_CC:-aarch64-linux-gnu-gcc}
EXPORTS=$ROOT/gfosmodules/gfopengpu.exports

build_one() {
    platform=$1
    cc=$2
    dest=$OUT/$platform/default
    mkdir -p "$dest"
    "$cc" -std=c99 -O2 -fPIC -fno-builtin -fno-stack-protector \
        -Wall -Wextra -Werror -DOGPU_WITH_3D \
        -I"$ROOT/include" -I"$ROOT/library/opengpu" \
        -shared -nostdlib -Wl,--no-undefined -Wl,--build-id=none \
        -Wl,--version-script="$EXPORTS" \
        -o "$dest/gfopengpu.library" \
        "$ROOT/library/opengpu/ogpu_core.c" \
        "$ROOT/library/opengpu/ogpu_dwt.c" \
        "$ROOT/library/opengpu/ogpu_3d.c" \
        "$ROOT/library/string.c"
}

build_one arm64 "$ARM64_CC"
build_one x86_64 "$X86_64_CC"
printf '%s\n' "$OUT/arm64/default/gfopengpu.library" \
    "$OUT/x86_64/default/gfopengpu.library"
