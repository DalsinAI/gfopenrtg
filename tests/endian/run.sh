#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# amiga_endian.h and tools/preswap.py:
#   tests/endian/run.sh
# On the host: the header's checks (C and C++, inline and with libendian),
# and preswap's. With m68k-amigaos-gcc (CC68K, or on PATH): the checks built
# for the 68000, 68020, 68040 and 68060 and libendian.a, and a look at the
# 68k code for the asm loops. With m68k-linux-gnu-gcc and qemu-m68k: the
# checks run on an emulated 68040, big-endian with the asm.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/tests/endian}
mkdir -p "$OUT"
INC="-I$ROOT/include"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined $INC \
    -o "$OUT/test_endian" "$HERE/test_endian.c"
"$OUT/test_endian"
c++ -x c++ -std=c++11 -O2 -Wall -Wextra -Werror $INC -o "$OUT/test_endian_cxx" "$HERE/test_endian.c"
"$OUT/test_endian_cxx"
cc -std=c99 -O2 -Wall -Wextra -Werror $INC -c -o "$OUT/endian_host.o" "$ROOT/library/endian/endian.c"
cc -std=c99 -O2 -Wall -Wextra -Werror -DAMIGA_ENDIAN_USE_LIB $INC -o "$OUT/test_endian_lib" "$HERE/test_endian.c" "$OUT/endian_host.o"
"$OUT/test_endian_lib"
python3 "$HERE/test_preswap.py"

CC68K=${CC68K:-$(command -v m68k-amigaos-gcc || true)}
if [ -n "$CC68K" ]; then
    OBJDUMP=${CC68K%gcc}objdump
    for cpu in 68000 68020 68040 68060; do
        "$CC68K" -m$cpu -noixemul -std=c99 -O2 -fomit-frame-pointer -Wall -Wextra -Werror $INC \
            -o "$OUT/test_endian-$cpu" "$HERE/test_endian.c"
    done
    CC="$CC68K" sh "$ROOT/library/endian/build.sh" "$OUT/m68k" >/dev/null
    "$CC68K" -m68040 -m68881 -noixemul -O2 -DAMIGA_ENDIAN_USE_LIB $INC -o "$OUT/test_endian_lib-68040" \
        "$HERE/test_endian.c" -L"$OUT/m68k" -lendian
    # the 2-words-a-long loop: rol.w #8, swap, rol.w #8, swap, on two registers
    n=$("$OBJDUMP" -d "$OUT/m68k/endian.o" | grep -c 'rolw #8,d')
    [ "$n" -ge 8 ] || { echo "libendian: no rol.w loop in ae_swap16_copy"; exit 1; }
    if "$OBJDUMP" -d "$OUT/test_endian-68000" | grep -q 'rolw #8,d'; then
        echo "68000 build has the 68020 asm"; exit 1
    fi
    echo "68k: built for 68000/020/040/060 and libendian.a (not run: no 68k here)"
fi
if command -v m68k-linux-gnu-gcc >/dev/null && command -v qemu-m68k >/dev/null; then
    m68k-linux-gnu-gcc -std=c99 -O2 -m68040 -static -Wall -Wextra -Werror $INC -o "$OUT/test_endian-linux68k" "$HERE/test_endian.c"
    qemu-m68k -cpu m68040 "$OUT/test_endian-linux68k"
fi
