#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# GL.module as the Amiga gets it: an OpenUp part (the layout of the OpenGPU
# part's Libs/ and Utilities/OpenUp/Licences/, with its Part script) in a
# drawer, and that drawer as an .lha, from module/build.sh's output.
#   library/modules/gl/module/package.sh GL_OUT DEST [VERSION]
# GL_OUT is module/build.sh's OUT_DIR (GL.module). DEST gets GFOpenGPU-GL/
# and GFOpenGPU-GL-VERSION.lha (when lha is on PATH). VERSION defaults to
# Mesa's version and the commit. The developer's half (libGL.a, the headers)
# is tools/make_sdk.py's.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
GL=$(CDPATH= cd -- "$HERE/.." && pwd)
REPO=$(CDPATH= cd -- "$GL/../../.." && pwd)
[ $# -ge 2 ] || { echo "usage: package.sh GL_OUT DEST [VERSION]"; exit 2; }
OUT=$(CDPATH= cd -- "$1" && pwd)
DEST=$2
MESA=$(sed -n 's/.*"version": "\(.*\)".*/\1/p' "$GL/mesa/UPSTREAM.json")
SRC=$(sed -n "s/^SRC='\(.*\)'$/\1/p" "${GL_WORK:-$GL/build}/mesa-build-m68k-amigaos/gla-link.env")
COMMIT=$(cut -c1-12 "$REPO/.commit" 2>/dev/null || git -C "$REPO" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)
VERSION=${3:-$MESA-$COMMIT}
[ -f "$OUT/GL.module" ] || { echo "package: no $OUT/GL.module (run module/build.sh)"; exit 1; }
[ -f "$SRC/docs/license.rst" ] || { echo "package: no Mesa tree at $SRC for its licence"; exit 1; }

P=$DEST/GFOpenGPU-GL
rm -rf "$P"
mkdir -p "$P/Libs/GFOpenGPU" "$P/Utilities/OpenUp/Licences"
cp "$OUT/GL.module" "$P/Libs/GFOpenGPU/GL.module"
cp "$SRC/docs/license.rst" "$P/Utilities/OpenUp/Licences/Mesa.txt"
cp "$REPO/LICENSE" "$P/Utilities/OpenUp/Licences/GFOpenGPU.txt"
# OpenUp's script: what goes where, in the Amiga's own paths.
cat > "$P/Part" <<EOF
part GFOpenGPU-GL $VERSION
; GL.module: Mesa $MESA's OpenGL 4.3/3.3 and GLES 3.1 for GFOpenGPU (virgl on the host's GPU, softpipe on the 68k), with its shader cache
copy "Libs/GFOpenGPU/GL.module" "LIBS:GFOpenGPU/GL.module"
copy "Utilities/OpenUp/Licences/Mesa.txt" "SYS:Utilities/OpenUp/Licences/Mesa.txt"
copy "Utilities/OpenUp/Licences/GFOpenGPU.txt" "SYS:Utilities/OpenUp/Licences/GFOpenGPU.txt"
EOF
cat > "$P/GFOpenGPU-GL.readme" <<EOF
Short:        Mesa's OpenGL and GLES for GFOpenGPU
Author:       AmiCradle Team
Uploader:     AmiCradle Team
Type:         util/libs
Version:      $VERSION
Requires:     AmigaOS 3.2, 68040 or 68060 with FPU, gfopengpu.library
Architecture: m68k-amigaos >= 3.2

GL.module goes in LIBS:GFOpenGPU/. Programs linked with libGL.a load it
with their first GL call. On AmiCradle, GL runs on the PC's graphics chip
through virgl (GL 4.3, GLES 3.1); elsewhere on the 68k through softpipe
(GL 3.3, GLES 3.1). ENV:GFOpenGPU/GLDriver chooses: gpu, cpu, or unset.

Shaders are compiled on the 68k, so a program with big shaders is slow
to start the first time. GL.module keeps what it compiled:

  ENV:GFOpenGPU/ShaderCache      a directory, or "off"
                                 (unset: SYS:Storage/GFOpenGPU/ShaderCache,
                                 or T:GFOpenGPU/ShaderCache when SYS: is
                                 write-protected)
  ENV:GFOpenGPU/ShaderCacheSize  the most it keeps, in KiB (4096)

Mesa $MESA is MIT (Utilities/OpenUp/Licences/Mesa.txt); the AmiCradle
Team's parts are MIT (GFOpenGPU.txt). Built from the open stack:
GFOpenAmigaGCC, libnix and OpenNDK.
EOF
[ -f "$OUT/BUILD-INFO.txt" ] && cp "$OUT/BUILD-INFO.txt" "$P/BUILD-INFO.txt"
(cd "$P" && find . -type f ! -name SHA256SUMS | sed 's|^\./||' | LC_ALL=C sort | xargs sha256sum > SHA256SUMS)
echo "packaged $P"
if command -v lha >/dev/null 2>&1; then
    L=$(CDPATH= cd -- "$DEST" && pwd)/GFOpenGPU-GL-$VERSION.lha
    rm -f "$L"
    # jlha makes archives; on some systems "lha" is lhasa, which only reads them
    T=$(command -v jlha || command -v lha)
    if (cd "$DEST" && "$T" -aq "$L" GFOpenGPU-GL) 2>/dev/null; then echo "packaged $L"; else echo "package: $T can't make archives; .lha not made"; fi
fi
