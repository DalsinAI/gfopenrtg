#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GFOpenGfx (library/ogfx/ogfx_lib.c) with and without graphics.library
# 47.101's drawing dispatch: ogfx_dispatch_test.c under qemu-m68k against
# stand-in exec, graphics.library and expansion.library jump tables, built
# with a Linux m68k GCC for the 68040 at -O0 and -O2, as the disk build (with
# GFOpenGfx's core, from tools/fetch_ogfx_core.sh) and as the ROM build
# (-DOGPU_ROM_NO_OGFX). The AmigaOS headers are the os32-gcc16 stove's NDK
# (NDK_INCLUDE); the library's register arguments are plain C ones here
# (OGFX_REG), the calls it makes through an LVO keep their registers.
# -O2 is -O2 -fno-inline for ogfx_lib.c: on Linux m68k A6 is the frame
# pointer, and once GCC inlines a function that loads A6 for an LVO call it
# can address a local from A6 (seen with GCC 15: the fast RectFill's request
# passed as A6-40). AmigaOS's frame pointer is A5, so the library's own
# builds don't have this.
#   sh tests/ogfx/test_ogfx_dispatch.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
top=$here/../..
gfx=$top/library/ogfx
cc=${M68K_CC:-m68k-linux-gnu-gcc}
qemu=${QEMU_M68K:-qemu-m68k}
ndk=${NDK_INCLUDE:-${OPENGPU_STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}/prefix/m68k-amigaos/ndk-include}
command -v "$cc" >/dev/null || { echo "skipped: no $cc"; exit 0; }
"$cc" -v 2>&1 | grep -q amigaos && { echo "skipped: $cc makes AmigaOS programs, which qemu-m68k doesn't run"; exit 0; }
command -v "$qemu" >/dev/null || { echo "skipped: no $qemu"; exit 0; }
[ -f "$ndk/exec/types.h" ] || { echo "skipped: no NDK headers in $ndk (NDK_INCLUDE)"; exit 0; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
sh "$top/tools/fetch_ogfx_core.sh" "$tmp" >/dev/null
fail=0
for opt in -O0 "-O2 -fno-inline"; do
    for variant in disk rom; do
        defs= core=
        if [ $variant = rom ]; then
            defs=-DOGPU_ROM_NO_OGFX
        else
            for f in gfx_core gfx_rp; do
                "$cc" -std=gnu99 $opt -m68040 -fomit-frame-pointer -fno-delete-null-pointer-checks -w -I"$tmp/ogfx-core" -idirafter "$ndk" \
                    -c -o "$tmp/$f.o" "$tmp/ogfx-core/$f.c"
            done
            core="$tmp/gfx_core.o $tmp/gfx_rp.o"
        fi
        flags="-std=gnu99 $opt -m68040 -fomit-frame-pointer -fno-delete-null-pointer-checks -Wall -Wextra -Werror -Wno-unused-parameter $defs"
        # shellcheck disable=SC2086
        "$cc" $flags "-DOGFX_REG(r,decl)=decl" -I"$top/include" -I"$tmp/ogfx-core" -idirafter "$ndk" \
            -c -o "$tmp/ogfx_lib.o" "$gfx/ogfx_lib.c"
        # shellcheck disable=SC2086
        "$cc" -std=gnu99 $opt -m68040 -fomit-frame-pointer -fno-delete-null-pointer-checks -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
            -Wall -Wextra -Werror -c -o "$tmp/ogfx_leaves.o" "$gfx/ogfx_leaves.c"
        # shellcheck disable=SC2086
        "$cc" $flags "-DOGFX_REG(r,decl)=decl" -I"$top/include" -idirafter "$ndk" -static -no-pie \
            -o "$tmp/t" "$here/ogfx_dispatch_test.c" "$tmp/ogfx_lib.o" "$tmp/ogfx_leaves.o" $core
        if out=$("$qemu" -cpu m68040 "$tmp/t"); then
            echo "ok   ${opt%% *} $variant: $out"
        else
            echo "$out" | tail -20
            echo "FAIL ${opt%% *} $variant"
            fail=1
        fi
    done
done
exit $fail
