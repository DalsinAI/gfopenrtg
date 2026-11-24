#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# library/build.sh's choice of the GCC 16 stove for SDL2.module, OpenAL.module and SDL 1.2: SDL2_STOVE when given,
# else the stove gfopengpu.library's compiler (OPENGPU_CC) is in, wherever the machine keeps its stoves. The nightly
# pack on bigguy keeps them outside $HOME, and with $HOME's place as the only default SDL2.module was silently left
# out there. The lines are taken from library/build.sh itself and run with made-up stoves.
#   tests/stove/test_sdl2_stove.sh
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
B="$HERE/../../library/build.sh"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT HUP INT TERM
fails=0; checks=0
check() { checks=$((checks + 1)); if [ "$1" = "$2" ]; then echo "ok   $3"; else echo "FAIL $3: '$1', not '$2'"; fails=$((fails + 1)); fi; }
line=$(grep -E '^SDL2_STOVE=' "$B") || { echo "FAIL library/build.sh has no SDL2_STOVE= line"; exit 1; }
check "$(grep -c -F 'AmiCradle/stoves/os32-gcc16/prefix}' "$B")" 0 "no stove place of \$HOME's left in the SDL 2 lines"
mkdir -p "$T/a/prefix/bin" "$T/b/prefix/bin"
printf '#!/bin/sh\n' > "$T/a/prefix/bin/m68k-amigaos-gcc"; chmod +x "$T/a/prefix/bin/m68k-amigaos-gcc"
# from the compiler's place
got=$(HOME="$T/none" OPENGPU_CC="$T/a/prefix/bin/m68k-amigaos-gcc" sh -c "unset SDL2_STOVE; $line; echo \"\$SDL2_STOVE\"")
check "$got" "$T/a/prefix" "the stove is the one OPENGPU_CC is in"
# SDL2_STOVE wins
got=$(OPENGPU_CC="$T/a/prefix/bin/m68k-amigaos-gcc" SDL2_STOVE="$T/b/prefix" sh -c "$line; echo \"\$SDL2_STOVE\"")
check "$got" "$T/b/prefix" "SDL2_STOVE given is kept"
# a stove with no compiler is said, not passed over in silence
warn=$(grep -E '^\[ -x "\$SDL2_STOVE/bin/m68k-amigaos-gcc" \] \|\| echo' "$B") || { echo "FAIL no line says a missing stove"; exit 1; }
got=$(SDL2_STOVE="$T/b/prefix" sh -c "$warn" 2>&1 || true)
case "$got" in *"SDL2.module"*"not built"*) r=said ;; *) r=silent ;; esac
check "$r" said "a stove without the compiler is reported"
got=$(SDL2_STOVE="$T/a/prefix" sh -c "$warn" 2>&1 || true)
check "$got" "" "a stove with the compiler says nothing"
echo "test_sdl2_stove: $checks checks, $fails failed"
[ "$fails" -eq 0 ]
