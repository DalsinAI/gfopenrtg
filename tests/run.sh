#!/bin/sh
# Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# OpenRTG's host tests, and a check that the library's C builds for the 68k.
#   tests/run.sh      (STOVE or CC68K as in tools/build.sh; the 68k check is skipped without it)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../build/tests}
mkdir -p "$OUT"
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$OUT/test_modes" "$HERE/test_modes.c" "$HERE/../library/modes.c"
"$OUT/test_modes"
# With no size asked for, the mode table is exactly 0.16's (0.17: the Native modes come only with one).
cc -std=c99 -O2 -Wall -Wextra -Werror -o "$OUT/modes_golden" "$HERE/modes_golden.c" "$HERE/../library/modes.c"
"$OUT/modes_golden" "$HERE/golden/modes-table.txt"
# screens.c's pixel conversions (library/pixconv.h) against the per-pixel code they replace.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$HERE/host-include" \
    -o "$OUT/test_pixconv" "$HERE/test_pixconv.c"
"$OUT/test_pixconv"
if command -v m68k-linux-gnu-gcc >/dev/null && command -v qemu-m68k >/dev/null; then   # and big-endian, as on the Amiga
    m68k-linux-gnu-gcc -std=c99 -O2 -fno-delete-null-pointer-checks -m68040 -static -Wall -Wextra -Werror -I"$HERE/host-include" \
        -o "$OUT/test_pixconv-m68k" "$HERE/test_pixconv.c"
    qemu-m68k -cpu m68040 "$OUT/test_pixconv-m68k" 10000
fi
# ScrollRaster (library/scroll_plan.h, what screens.c runs) on a model of a layer's ClipRects, against what it must give.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_scroll_plan" "$HERE/test_scroll_plan.c"
"$OUT/test_scroll_plan"
if command -v m68k-linux-gnu-gcc >/dev/null && command -v qemu-m68k >/dev/null; then   # and on a 68040, where long is 32 bits
    m68k-linux-gnu-gcc -std=c99 -O2 -fno-delete-null-pointer-checks -m68040 -static -Wall -Wextra -Werror \
        -o "$OUT/test_scroll_plan-m68k" "$HERE/test_scroll_plan.c"
    qemu-m68k -cpu m68040 "$OUT/test_scroll_plan-m68k"
fi
# Draw and PolyDraw on 8-bit bitmaps (openrtg.library 0.25, library/ortg_line.h) against
# graphics.library's (OpenGfx's core): FRST_DOT, linpatcnt, JAM2, INVERSVID, through OpenGPU's LINE too.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$HERE/host-include" \
    -o "$OUT/test_lines" "$HERE/test_lines.c" "$HERE/../library/opengpu/ogpu_core.c" "$HERE/../library/opengpu/ogpu_build.c"
"$OUT/test_lines"
# ... and big-endian on a 68040, when a Linux m68k compiler and qemu are installed.
if command -v m68k-linux-gnu-gcc >/dev/null && command -v qemu-m68k >/dev/null; then
    m68k-linux-gnu-gcc -std=c99 -O2 -fno-delete-null-pointer-checks -m68040 -static -Wall -Wextra -Werror -I"$HERE/host-include" \
        -o "$OUT/test_lines-m68k" "$HERE/test_lines.c" "$HERE/../library/opengpu/ogpu_core.c" "$HERE/../library/opengpu/ogpu_build.c"
    qemu-m68k -cpu m68040 "$OUT/test_lines-m68k" >"$OUT/test_lines-m68k.txt"
    tail -1 "$OUT/test_lines-m68k.txt"
fi
OGPU="$HERE/../library/opengpu"
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_opengpu" "$HERE/test_opengpu.c" "$HERE/golden_scenes.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
"$OUT/test_opengpu" "$HERE/golden/opengpu-g1.txt" "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-v12.txt"
# Text's strip (library/text_strip.h): the same pixels as 0.24's glyph-at-a-time Text.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -I"$HERE/host-include" \
    -o "$OUT/test_text_strip" "$HERE/test_text_strip.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
"$OUT/test_text_strip"
# COMPOSITE_PD (v1.4, pixman.library's composites) against its per-pixel reference.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_composite_pd" "$HERE/test_composite_pd.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
"$OUT/test_composite_pd"
# Stream v1.6 (paths, strokes, gradients, patterns, blur, blend modes) against its references.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -o "$OUT/test_vector" "$HERE/test_vector.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c" -lm
"$OUT/test_vector"
# The GL module's OS 3.2 present (library/modules/gl/gla/os3/gla_present_os3.c) on a fake
# OpenGPU: every frame reaches the screen whole and in order, and a screen's present neither
# waits with the bitmap locked nor, with a back buffer, for the batch it has just sent
# (opengpu.library 0.19: it waits as before). Pairs with openamigamesa #23.
cc -std=gnu99 -O1 -g -Wall -Wextra -Werror -Wno-unused-parameter -fsanitize=address,undefined -fno-sanitize-recover=undefined \
    -I"$HERE/gla_present-include" -I"$HERE/../include" -I"$HERE/../library/modules/gl" \
    -o "$OUT/test_gla_present" "$HERE/test_gla_present.c" "$HERE/../library/modules/gl/gla/os3/gla_present_os3.c" -lpthread
gla_present() {     # arguments, then counts that must be 0
    a=$1; shift
    r=$("$OUT/test_gla_present" $a) || { echo "$r"; echo "test_gla_present $a: a frame was wrong" >&2; exit 1; }
    echo "$r"
    for c in "$@"; do
        case "$r" in *" $c=0 "*) ;; *) echo "test_gla_present $a: $c is not 0" >&2; exit 1 ;; esac
    done
}
gla_present "1 23 screen" waits_locked waits_own            # a back buffer: nothing waits for its own frame
gla_present "0 23 screen" waits_locked waits_own_in_present  # one buffer: waits after present, unlocked
gla_present "1 23 window"                                   # WritePixelArray
gla_present "1 19 screen"                                   # opengpu.library 0.19: as before
gla_present "1 23 mixed" waits_locked waits_own             # virgl's frames between: kept in order
# Stream v1.8 (OGPU_PATH_ALIASED) against its exact reference, and against cairo's
# CAIRO_ANTIALIAS_NONE when cairo's headers are there (the browser's reference).
ALIASED_CAIRO=""
if pkg-config --exists cairo 2>/dev/null; then ALIASED_CAIRO="-DWITH_CAIRO $(pkg-config --cflags cairo)"; fi
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined $ALIASED_CAIRO \
    -o "$OUT/test_vector_aliased" "$HERE/test_vector_aliased.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c" \
    $(if [ -n "$ALIASED_CAIRO" ]; then pkg-config --libs cairo; fi) -lm
"$OUT/test_vector_aliased"
# Stream v1.7 (SDL 3's blend modes, PIXELART, clamping one way, PALETTE, RGBA32 and ABGR32,
# P010 and the new YUV spaces; the library's variant check) against its references, with
# the 3D rasteriser for TRIANGLES' SDL form.
cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -DOGPU_WITH_3D \
    -o "$OUT/test_sdl3" "$HERE/test_sdl3.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_3d.c" "$OGPU/ogpu_build.c" "$OGPU/ogpu_build3d.c" -lm
"$OUT/test_sdl3"
# The Vulkan back end against the core, when Vulkan's loader and headers are there
# (it skips itself when no device is found; lavapipe will do). Built with the 3D
# range (OGPU_WITH_3D), as the runtime is: TRIANGLES on the 3D shader, the rest
# of the range on the back end's core; and once without it, where the back end
# must answer no 3D.
if [ -e /usr/include/vulkan/vulkan.h ] && { ldconfig -p 2>/dev/null | grep -q libvulkan.so.1; }; then
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -DOGPU_WITH_3D \
        -o "$OUT/test_opengpu_vk" "$HERE/test_opengpu_vk.c" "$HERE/golden_scene_3d.c" "$HERE/scenes3d.c" "$HERE/../host/vulkan/ogpu_vk.c" \
        "$OGPU/ogpu_core.c" "$OGPU/ogpu_3d.c" "$OGPU/ogpu_build.c" "$OGPU/ogpu_build3d.c" -lvulkan -lm
    ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 3000 "$HERE/golden/opengpu-v11.txt" \
        "$HERE/golden/opengpu-3d.txt"
    # Again in video RAM the test owns, as the runtime's board memory is.
    OGPU_VK_HOST=1 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 500 "$HERE/golden/opengpu-v11.txt" \
        "$HERE/golden/opengpu-3d.txt"
    # And mapped with a card part (the card's own memory for its second half),
    # where the device can (sparse binding); else it says so and carries on.
    OGPU_VK_HOST=1 OGPU_VK_HOSTMEM=map OGPU_VK_CARD_AT=0x80000 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" "$HERE/golden/opengpu-g1.txt" 200 \
        "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-3d.txt"
    # And with host memory again after the card part, where the board's drivers keep the ring.
    OGPU_VK_HOST=1 OGPU_VK_HOSTMEM=map OGPU_VK_CARD_AT=0x80000 OGPU_VK_CARD_END=0xC0000 ASAN_OPTIONS=detect_leaks=0 "$OUT/test_opengpu_vk" \
        "$HERE/golden/opengpu-g1.txt" 200 "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-3d.txt"
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -o "$OUT/test_opengpu_vk-no3d" "$HERE/test_opengpu_vk.c" "$HERE/../host/vulkan/ogpu_vk.c" \
        "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c" -lvulkan
    "$OUT/test_opengpu_vk-no3d" "$HERE/golden/opengpu-g1.txt" 200 "$HERE/golden/opengpu-v11.txt"
else
    echo "no Vulkan headers or loader; the GPU run is skipped"
fi
# ACVirgl's host side against virglrenderer, when ACVIRGL_LIB names a libvirglrenderer.so.1
# and VIRGL_SRC its source's src/ (for virgl_protocol.h): the release amigachrome's
# scripts/build_virglrenderer.sh builds (1.3.0).
if [ -n "${ACVIRGL_LIB:-}" ] && [ -f "${VIRGL_SRC:-/nonexistent}/virgl_protocol.h" ]; then
    cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I"$VIRGL_SRC" -o "$OUT/test_acvirgl" "$HERE/test_acvirgl.c" \
        "$HERE/../host/virgl/acvirgl.c" -ldl
    "$OUT/test_acvirgl"
    # again on GLES, as a Pi's V3D may give it (skipped without libGLESv2)
    AC_VIRGL_GLES=1 "$OUT/test_acvirgl"
else
    echo "no ACVIRGL_LIB and VIRGL_SRC; the virgl run is skipped"
fi
# libSDL3.a's stub with no SDL3.module (tests/sdl3/stub_nomodule.c), when SDL3_SRC
# names SDL 3.4.14's tree (the tarball, or library/modules/sdl3/build.sh's).
if [ -f "${SDL3_SRC:-/nonexistent}/src/dynapi/SDL_dynapi_procs.h" ]; then
    cc -std=c99 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined \
        -I"$HERE/sdl3/host" -I"$HERE/host-include" -I"$HERE/../include" -I"$HERE/../stubs/sdl3" \
        -I"$SDL3_SRC/include" -I"$SDL3_SRC/src/dynapi" -I"$SDL3_SRC/src" \
        -o "$OUT/stub_nomodule" "$HERE/sdl3/stub_nomodule.c"
    "$OUT/stub_nomodule"
else
    echo "no SDL3_SRC; the SDL 3 stub test is skipped"
fi
# OpenAL.module's core and libopenal.a (tests/openal/run_host.sh): the AL and ALC
# calls, the 3D, streams and offsets, threads, and the stub with no module.
sh "$HERE/openal/run_host.sh" "$OUT/openal"
# The same tests big-endian on a 68040, when a Linux m68k compiler and qemu are installed.
if command -v m68k-linux-gnu-gcc >/dev/null && command -v qemu-m68k >/dev/null; then
    m68k-linux-gnu-gcc -std=c99 -O2 -fno-delete-null-pointer-checks -m68040 -static -Wall -Wextra -Werror \
        -o "$OUT/test_opengpu-m68k" "$HERE/test_opengpu.c" "$HERE/golden_scenes.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
    qemu-m68k -cpu m68040 "$OUT/test_opengpu-m68k" "$HERE/golden/opengpu-g1.txt" "$HERE/golden/opengpu-v11.txt" "$HERE/golden/opengpu-v12.txt"
    m68k-linux-gnu-gcc -std=c99 -O2 -fno-delete-null-pointer-checks -m68040 -static -Wall -Wextra -Werror -I"$HERE/host-include" \
        -o "$OUT/test_text_strip-m68k" "$HERE/test_text_strip.c" "$OGPU/ogpu_core.c" "$OGPU/ogpu_build.c"
    qemu-m68k -cpu m68040 "$OUT/test_text_strip-m68k"
else
    echo "no m68k-linux-gnu-gcc or qemu-m68k; the big-endian run is skipped"
fi
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC68K=${CC68K:-$STOVE/prefix/bin/m68k-amigaos-gcc}
if [ -x "$CC68K" ]; then
    "$CC68K" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -c -o "$OUT/modes-m68k.o" "$HERE/../library/modes.c"
    "$CC68K" -m68040 -O2 -fno-delete-null-pointer-checks -Wall -Wextra -Werror -fno-builtin -c -o "$OUT/ogpu_core-m68k.o" "$HERE/../library/opengpu/ogpu_core.c"
    echo "library/modes.c and library/opengpu/ogpu_core.c build for the 68k"
else
    echo "no m68k compiler; the 68k check is skipped"
fi
# OpenGfx (library/ogfx): its leaves, the compositing reference, the patch entry.
sh "$HERE/ogfx/run.sh"
# The module loader's test (Test.module and ModuleCheck, run on an Amiga), when both stoves are there.
if [ -x "${STOVE16:-$HOME/AmigaChrome/stoves/os32-gcc16/prefix}/bin/m68k-amigaos-gcc" ] && [ -x "$CC68K" ]; then
    sh "$HERE/modules/build.sh" "$OUT/modules" >/dev/null && echo "tests/modules build (ModuleCheck runs on an Amiga)"
fi
