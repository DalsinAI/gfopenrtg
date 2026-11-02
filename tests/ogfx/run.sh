#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GFOpenGfx's tests, brought from amigachrome-guest (tests/m68k) with GFOpenGfx:
#  1. the leaves and the compositing reference on the host, 32-bit (addresses
#     are longs, as on the Amiga), at -O0 and -O2, under UBSan;
#  2. the same under qemu-m68k with a Linux m68k GCC, when there is one
#     (test_ogfx_leaves.sh, test_ogfx_composite.sh), and GFOpenGfx with and
#     without graphics.library 47.101's dispatch, disk and ROM builds
#     (test_ogfx_dispatch.sh);
#  3. with the os32 stove: the leaves' tags and that they call nothing, as
#     library/build.sh builds them, and that GFOpenGfx in gfopengpu.library keeps
#     no writable data (it may run from ROM), in the disk build and the ROM
#     build (-DOGPU_ROM_NO_OGFX);
#  4. a patch entry (ogfx_tramp.h), disassembled.
# OpenGfxCheck (tools/ogfx_check.c) checks the rest on an Amiga.
#   tests/ogfx/run.sh      (STOVE or CC68K as in tests/run.sh)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
GFX=$HERE/../../library/ogfx
OUT=${OUT:-$HERE/../../build/tests}/ogfx
mkdir -p "$OUT"
fail=0

# 1. the host, 32-bit
if echo 'int main(void){return sizeof(void *) != 4;}' | cc -m32 -x c - -o "$OUT/m32" 2>/dev/null && "$OUT/m32"; then
    for opt in -O0 -O2; do
        cc -m32 -std=gnu99 $opt -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined \
            -include "$HERE/host_magic.h" -o "$OUT/leaves$opt" "$HERE/ogfx_leaves_test.c" "$GFX/ogfx_leaves.c"
        echo "leaves, host $opt: $("$OUT/leaves$opt")" || fail=1
        cc -m32 -std=gnu99 $opt -g -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined \
            -o "$OUT/composite$opt" "$HERE/ogfx_composite_test.c" "$GFX/ogfx_composite.c"
        echo "composite, host $opt: $("$OUT/composite$opt")" || fail=1
    done
else
    echo "no 32-bit host compiler (cc -m32); the host run is skipped"
fi

# 2. big-endian under qemu-m68k (each script skips itself without its compiler)
sh "$HERE/test_ogfx_leaves.sh" || fail=1
sh "$HERE/test_ogfx_composite.sh" || fail=1
sh "$HERE/test_ogfx_dispatch.sh" || fail=1

# 3. the os32 stove
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32}
CC68K=${CC68K:-$STOVE/prefix/bin/m68k-amigaos-gcc}
if [ -x "$CC68K" ]; then
    P=${CC68K%gcc}
    "$CC68K" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-lto -fno-builtin -fno-tree-loop-distribute-patterns \
        -Wall -Wextra -Werror -c -o "$OUT/ogfx_leaves.o" "$GFX/ogfx_leaves.c"
    sh "$HERE/../../tools/fetch_ogfx_core.sh" "$OUT"
    for f in gfx_core gfx_rp; do
        "$CC68K" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
            -I"$OUT/ogfx-core" -c -o "$OUT/ogfx_$f.o" "$OUT/ogfx-core/$f.c"
    done
    "$CC68K" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -I"$HERE/../../include" -I"$OUT/ogfx-core" -c -o "$OUT/ogfx_lib.o" "$GFX/ogfx_lib.c"
    "$CC68K" -m68040 -mnobitfield -O2 -fno-delete-null-pointer-checks -fomit-frame-pointer -fno-builtin -Wall -Wextra -Werror -Wno-unused-parameter \
        -DOGPU_ROM_NO_OGFX -I"$HERE/../../include" -c -o "$OUT/ogfx_lib_rom.o" "$GFX/ogfx_lib.c"
    und=$("${P}nm" -u "$OUT/ogfx_lib_rom.o" | awk '{print $2}' | grep -E '^_ogfx_rp_' | tr '\n' ' ' || true)
    [ -z "$und" ] || { echo "FAIL os32: the ROM build calls the core: $und"; fail=1; }
    und=$("${P}nm" -u "$OUT/ogfx_leaves.o" | awk '{print $2}' | grep -v '^___mulsi3$' | tr '\n' ' ' || true)
    [ -z "$und" ] || { echo "FAIL os32: the leaves call $und"; fail=1; }
    for f in "ogfx_planar_rect 00010100" "ogfx_chunky_rect 00010101" "ogfx_chunky_copy 00010102" "ogfx_planar_blit 00010103"; do
        set -- $f
        # the tag: BRA.S *+12, 'ACMF', version 1, the id, then JMP
        addr=$("${P}nm" "$OUT/ogfx_leaves.o" | awk -v s="_$1" '$3 == s { print $1 }')
        [ -n "$addr" ] || { echo "FAIL os32: no $1"; fail=1; continue; }
        bytes=$("${P}objdump" -s -j .text --start-address=0x$addr --stop-address=$(printf '0x%x' $((0x$addr + 14))) "$OUT/ogfx_leaves.o" \
            | awk '/^ [0-9a-f]+ /{ printf "%s%s%s%s", $2, $3, $4, $5 }' | cut -c1-28)
        # (bebbo's assembler makes the JMP PC-relative, $4EFA, when the body is
        # in the same section; AC090 recognises the 12-byte tag either way)
        case $bytes in "600a41434d460001${2}4ef9"|"600a41434d460001${2}4efa") ;;
            *) echo "FAIL os32: $1's tag is $bytes"; fail=1 ;; esac
    done
    # No writable data: everything GFOpenGfx keeps is in gfopengpu.library's base.
    for o in "$OUT/ogfx_leaves.o" "$OUT/ogfx_lib.o" "$OUT/ogfx_lib_rom.o" "$OUT/ogfx_gfx_core.o" "$OUT/ogfx_gfx_rp.o"; do
        rw=$("${P}objdump" -h "$o" | awk '$2 ~ /^\.(data|bss)/ && $3 != "00000000" { print $2 " " $3 }')
        [ -z "$rw" ] || { echo "FAIL os32: $(basename "$o") has writable data: $rw"; fail=1; }
    done
    echo "os32: the leaves' four tags, no calls out, no writable data in GFOpenGfx"
    # graphics.library is reached at negative offsets only: a JSR or LEA at a
    # positive offset from A6 that is one of the LVOs GFOpenGfx calls (lvo_of,
    # and the dispatch's -1062 to -1092) is GCC 6.5's lost sign (ogfx_lib.c,
    # original()). Other positive offsets from A6 are fields of a struct GCC
    # keeps in A6 there (a BitMap's Planes, exec's LibList).
    lvos='30|36|54|60|180|186|234|246|252|258|264|306|312|318|324|336|396|552|606|636|690|696|768|774|780|786|810|1056|1062|1068|1074|1080|1086|1092'
    pos=$("${P}objdump" -d "$OUT/ogfx_lib.o" "$OUT/ogfx_lib_rom.o" | grep -E "(jsr|lea) a6@\(($lvos)\)" || true)
    [ -z "$pos" ] || { echo "FAIL os32: a call at a positive offset from A6: $pos"; fail=1; }

    # 4. a patch entry
    cc -std=c99 -Wall -Wextra -Werror -o "$OUT/tramp_dump" "$HERE/tramp_dump.c"
    "$OUT/tramp_dump" > "$OUT/tramp.bin"
    dis=$("${P}objdump" -D -b binary -m m68k:68020 "$OUT/tramp.bin" | awk -F'\t' '/^ +[0-9a-f]+:/ { gsub(/ +$/, "", $3); print $3 }' | tr '\n' ';')
    want='movel a4,sp@-;moveal #305419896,a4;jsr 0xabcdef;moveal sp@+,a4;rts;nop;'
    if [ "$dis" = "$want" ]; then echo "patch entry: $dis"; else echo "FAIL patch entry: $dis"; fail=1; fi
else
    echo "no m68k compiler; the os32 checks are skipped"
fi
[ $fail = 0 ] && echo "GFOpenGfx: all tests passed"
exit $fail
