#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GL.module built clean room, from the open stack only: a clean export of
# this repository's commit (git archive: nothing uncommitted, no earlier
# build tree), Mesa from the pinned tarball (UPSTREAM.json, by its SHA-256)
# with mesa/patches, GFOpenAmigaGCC 16.2 with its libnix, and OpenNDK's
# headers. No vendor NDK, SDK or CyberGraphX headers (cybergraphics.library's
# are gla/os3/include's). Then module/package.sh makes the Amiga's part, and
# BUILD-INFO.txt says what it was built from.
#   library/modules/gl/module/build-open.sh WORK [COMMIT]
# WORK must not exist. COMMIT defaults to HEAD.
#   GFOPENGCC  GFOpenAmigaGCC's prefix (bin/m68k-amigaos-gcc)
#   OPENNDK    OpenNDK (Include_H/)
#   JOBS       ninja's jobs (2): Mesa's C++ takes ~1 GB a compile
#   PYTHON     a python3 with mako, packaging and yaml; meson and ninja on PATH
#   MESA_TARBALL  the tarball, if already fetched (checked by its SHA-256 all the same)
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
[ $# -ge 1 ] || { echo "usage: build-open.sh WORK [COMMIT]"; exit 2; }
WORK=$1
COMMIT=$(git -C "$REPO" rev-parse "${2:-HEAD}")
GFOPENGCC=${GFOPENGCC:-$HOME/CradleLab/toolchain/openamigagcc-16.2.0-open4-x86_64-linux}
OPENNDK=${OPENNDK:-$HOME/CradleLab/gfopensdk/OpenNDK}
JOBS=${JOBS:-2}
[ ! -e "$WORK" ] || { echo "build-open: $WORK is there already (a clean build starts from nothing)"; exit 1; }
[ -x "$GFOPENGCC/bin/m68k-amigaos-gcc" ] || { echo "build-open: no GFOpenAmigaGCC at $GFOPENGCC"; exit 1; }
[ -f "$OPENNDK/Include_H/exec/types.h" ] || { echo "build-open: no OpenNDK at $OPENNDK"; exit 1; }
command -v meson >/dev/null && command -v ninja >/dev/null || { echo "build-open: meson and ninja must be on PATH"; exit 1; }
mkdir -p "$WORK"
WORK=$(CDPATH= cd -- "$WORK" && pwd)

# 1. The clean checkout.
mkdir "$WORK/src"
git -C "$REPO" archive "$COMMIT" | tar -C "$WORK/src" -xf -
echo "$COMMIT" > "$WORK/src/.commit"

# 2. The stove: GFOpenAmigaGCC with OpenNDK as the system's headers, after
# libnix's (as a stove has the NDK in its sys-include), and ninja at JOBS.
S=$WORK/stove16
mkdir -p "$S/bin" "$WORK/tools"
for f in "$GFOPENGCC"/bin/m68k-amigaos-*; do ln -s "$f" "$S/bin/"; done
for c in gcc g++ c++; do
    [ -e "$GFOPENGCC/bin/m68k-amigaos-$c" ] || continue
    rm -f "$S/bin/m68k-amigaos-$c"
    printf '#!/bin/sh\nexec "%s" "$@" -idirafter "%s"\n' "$GFOPENGCC/bin/m68k-amigaos-$c" "$OPENNDK/Include_H" > "$S/bin/m68k-amigaos-$c"
    chmod +x "$S/bin/m68k-amigaos-$c"
done
NINJA=$(command -v ninja)
printf '#!/bin/sh\nexec "%s" -j%s "$@"\n' "$NINJA" "$JOBS" > "$WORK/tools/ninja"
chmod +x "$WORK/tools/ninja"

# 3. Mesa's tarball, where mesa/build.sh looks (it checks the SHA-256 itself).
GLW=$WORK/gl-work
mkdir -p "$GLW"
[ -n "${MESA_TARBALL:-}" ] && cp "$MESA_TARBALL" "$GLW/"

# 4. Mesa, the GLA core and GL.module (module/build.sh runs mesa/build.sh).
OUT=$WORK/out
PATH="$WORK/tools:$PATH" GL_WORK=$GLW STOVE16=$S STOVE=$WORK/no-gcc65-stove \
    sh "$WORK/src/library/modules/gl/module/build.sh" "$OUT"

# 5. What it was built from.
B=$GLW/mesa-build-m68k-amigaos
J=$WORK/src/library/modules/gl/mesa/UPSTREAM.json
{
    echo "GL.module, built clean room from the open stack"
    echo "built:      $(date -u '+%Y-%m-%d %H:%M UTC') on $(uname -srm)"
    echo "gfopenrtg:  $COMMIT ($(git -C "$REPO" log -1 --format='%ci %s' "$COMMIT"))"
    echo "mesa:       $(sed -n 's/.*"file": "\(.*\)".*/\1/p' "$J") sha256 $(sed -n 's/.*"sha256": "\(.*\)".*/\1/p' "$J")"
    echo "patches:    $(ls "$WORK/src/library/modules/gl/mesa/patches" | tr '\n' ' ')"
    echo "            sum $(cat "$GLW"/mesa-*/.gla-patched)"
    echo "compiler:   $("$GFOPENGCC/bin/m68k-amigaos-gcc" --version | head -1) ($GFOPENGCC)"
    echo "libnix:     the compiler's own (-noixemul)"
    echo "headers:    OpenNDK $OPENNDK$(git -C "$OPENNDK" log -1 --format=' (%h %ci)' 2>/dev/null || true), after libnix's"
    echo "cgx:        gla/os3/include (clean room, compat/cybergraphics_lib.c's numbers)"
    echo "meson:      $(meson --version); ninja $("$NINJA" --version) -j$JOBS; python $(${PYTHON:-python3} --version 2>&1 | cut -d' ' -f2)"
    echo "mesa flags: $(sed -n "s/^FLAGS='\(.*\)'$/\1/p" "$B/gla-link.env")"
    echo "link flags: $(sed -n "s/^MESA_LFLAGS='\(.*\)'$/\1/p" "$B/gla-link.env")"
    echo "options:    $(grep -o "\-D[a-z0-9_-]*=[^ ]*" "$WORK/src/library/modules/gl/mesa/build.sh" | tr '\n' ' ')"
    echo
    (cd "$OUT" && sha256sum GL.module libGL.a)
} > "$OUT/BUILD-INFO.txt"

# 6. The Amiga's part.
GL_WORK=$GLW sh "$WORK/src/library/modules/gl/module/package.sh" "$OUT" "$WORK/package"
cat "$OUT/BUILD-INFO.txt"
