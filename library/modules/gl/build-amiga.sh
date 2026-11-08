#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# For the Cradle's stove_build (STOVE= and OUT= set, target library/modules/gl):
# GL.module, libGL.a and GLArrays, as module/build.sh builds them, with the
# stove's GCC 16, so a lab can be given them with lab_put. Mesa comes from
# the pinned tarball (mesa/UPSTREAM.json, fetched and checked by its SHA-256).
# The release build is module/build-open.sh (clean room, OpenNDK only).
# It says first what the machine has: Mesa's build wants meson, ninja and a
# python3 with mako, packaging and yaml, which it puts in a venv from PyPI
# when they are missing; and about 1 GB of memory for each compile.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmiCradle/stoves/os32-gcc16}
OUT=${OUT:-$HERE/build-amiga}
if [ -x "$STOVE/prefix/bin/m68k-amigaos-gcc" ]; then P=$STOVE/prefix; else P=$STOVE; fi
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)
W=$OUT/work
mkdir -p "$W"
START=$(date +%s)

CPUS=$(nproc 2>/dev/null || echo 2)
MEMMB=$(awk '/MemAvailable/ { print int($2 / 1024) }' /proc/meminfo 2>/dev/null || echo 2048)
JOBS=$((MEMMB / 1100)); [ "$JOBS" -ge 1 ] || JOBS=1; [ "$JOBS" -le "$CPUS" ] || JOBS=$CPUS
echo "host: $(uname -srm), $CPUS CPUs, $MEMMB MB free; ninja -j$JOBS"
"$P/bin/m68k-amigaos-gcc" --version | head -1
[ -f "$P/m68k-amigaos/include/exec/types.h" ] || [ -f "$P/m68k-amigaos/sys-include/exec/types.h" ] \
    || echo "note: the stove has no exec/types.h; GLA's OS 3 files need an NDK" >&2

# meson, ninja and a python3 with mako, packaging and yaml.
PY=
for c in ${PYTHON:-} python3 /usr/bin/python3; do
    c=$(command -v "$c" 2>/dev/null) || continue
    if "$c" -c 'import mako, packaging, yaml' 2>/dev/null; then PY=$c; break; fi
done
if [ -z "$PY" ] || ! command -v meson >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
    echo "tools: making a venv for meson, ninja, mako, packaging, pyyaml"
    python3 -m venv "$W/venv"
    "$W/venv/bin/pip" install -q meson ninja mako packaging pyyaml
    PATH=$W/venv/bin:$PATH
    PY=$W/venv/bin/python3
fi
echo "tools: meson $(meson --version), ninja $(ninja --version), $("$PY" --version)"
mkdir -p "$W/bin"
printf '#!/bin/sh\nexec "%s" -j%s "$@"\n' "$(command -v ninja)" "$JOBS" > "$W/bin/ninja"
chmod +x "$W/bin/ninja"

# Mesa, the GLA core, GL.module, libGL.a and GLArrays.
PATH="$W/bin:$PATH" PYTHON=$PY GL_WORK=$W/gl STOVE16=$P STOVE=$W/no-gcc65-stove \
    sh "$HERE/module/build.sh" "$W/out"

mkdir -p "$OUT/libs/GFOpenGPU" "$OUT/lib" "$OUT/c"
cp "$W/out/GL.module" "$OUT/libs/GFOpenGPU/GL.module"
cp "$W/out/libGL.a" "$OUT/lib/libGL.a"
cp "$W/out/tests/GLArrays" "$OUT/c/GLArrays"
rm -rf "$W"
echo "built in $(( $(date +%s) - START )) s"
sha256sum "$OUT/libs/GFOpenGPU/GL.module" "$OUT/lib/libGL.a" "$OUT/c/GLArrays"
