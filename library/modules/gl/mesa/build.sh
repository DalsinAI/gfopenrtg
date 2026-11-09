#!/bin/sh
# Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# Build Mesa's softpipe, virgl and GL state tracker (the release UPSTREAM.json
# pins, checked by its SHA-256) as static libraries, then the GLA core with its
# virgl winsys, and its test.
#   mesa/build.sh [WORKDIR]                  for this machine
#   CROSS=mesa/cross/m68k-linux.ini mesa/build.sh   big-endian 68040 Linux (the test runs under qemu-m68k)
#   CROSS=mesa/cross/m68k-amigaos.ini mesa/build.sh AmigaOS 3.2 (the os32-gcc16 stove's bin/ first on PATH)
# Meson may come from a venv: the Python that runs Mesa's generators is picked
# below (PYTHON, else python3 on PATH, else /usr/bin/python3), the first that
# has mako, packaging and yaml.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOP=$(CDPATH= cd -- "$HERE/.." && pwd)
REPO=$(CDPATH= cd -- "$TOP/../../.." && pwd)          # gfopenrtg: include/gfopengpu, host/virgl
WORK=${1:-${GL_WORK:-$TOP/build}}
J=$(sed -n 's/.*"\([a-z0-9]*\)": "\(.*\)".*/\1=\2/p' "$HERE/UPSTREAM.json")
VERSION=$(echo "$J" | sed -n 's/^version=//p')
FILE=$(echo "$J" | sed -n 's/^file=//p')
SHA=$(echo "$J" | sed -n 's/^sha256=//p')
URLS=$(sed -n '/"urls"/,/]/s/.*"\(http[^"]*\)".*/\1/p' "$HERE/UPSTREAM.json")
SRC=$WORK/mesa-$VERSION
mkdir -p "$WORK"

if [ ! -f "$WORK/$FILE" ] || ! echo "$SHA  $WORK/$FILE" | sha256sum -c --status; then
    for u in $URLS; do
        # A stalled mirror gives up after 30 s (under 100 KB/s) and the next is tried:
        # a stove job has 900 s for all of it.
        curl -sSfL --connect-timeout 20 --speed-limit 102400 --speed-time 30 -o "$WORK/$FILE.part" "$u" && mv "$WORK/$FILE.part" "$WORK/$FILE" && break || true
    done
    echo "$SHA  $WORK/$FILE" | sha256sum -c --status || { echo "mesa: $FILE missing or its SHA-256 differs"; exit 1; }
fi
# The Team's changes to Mesa are small patches in mesa/patches, applied to a fresh
# tree; a changed set of patches extracts the tree again.
PSUM=$(cat "$TOP"/mesa/patches/*.diff 2>/dev/null | sha256sum | cut -c1-16)
if [ "$(cat "$SRC/.gla-patched" 2>/dev/null)" != "$PSUM" ]; then
    rm -rf "$SRC" "$WORK"/mesa-build-*
    tar -C "$WORK" -xf "$WORK/$FILE"
    for p in "$TOP"/mesa/patches/*.diff; do
        [ -f "$p" ] && patch -s -d "$SRC" -p1 < "$p"
    done
    echo "$PSUM" > "$SRC/.gla-patched"
fi

PY=
for c in ${PYTHON:-} python3 /usr/bin/python3; do
    c=$(command -v "$c" 2>/dev/null) || continue
    if "$c" -c 'import mako, packaging, yaml' 2>/dev/null; then PY=$c; break; fi
done
[ -n "$PY" ] || { echo "mesa: no python3 with mako, packaging and yaml (set PYTHON)"; exit 1; }
# Mesa's meson.build tries python3.16 down to python, so name them all. Its
# find_program looks in the host machine's file: the cross file when cross
# building, so this one is passed as a second cross file then.
{
    echo "[binaries]"
    for n in python3.16 python3.15 python3.14 python3.13 python3.12 python3.11 python3.10 python3 python; do
        echo "$n = '$PY'"
    done
} > "$WORK/python.ini"

if [ -n "${CROSS:-}" ]; then
    NAME=$(basename "$CROSS" .ini); XARG="--cross-file $TOP/$CROSS"
    PYARG=--cross-file
    # The AmigaOS assembler has no GOT, so static libraries are built without PIC.
    # No C++ exceptions (nothing in this build catches one): built -fbaserel32,
    # their tables would point from the code into the data, which the hunk
    # format can't do PC-relative across hunks.
    case $NAME in *amigaos*) XARG="$XARG -Db_staticpic=false -Dcpp_eh=none" ;; esac
else
    NAME=host; XARG= PYARG=--native-file
fi
B=$WORK/mesa-build-$NAME
# GL_DRIVERS: the Gallium drivers GL.module carries (9 November 2026).
# both (the default): softpipe and virgl, as before. virgl: no softpipe, for a
# GPU ROM or a set that always has the host's GPU; with no virgl a program's
# GL fails cleanly (gla_virgl_os3.c says why). softpipe: no virgl, for a
# machine without the ACRTG board's virgl. The GLA core is told which by
# GLA_HAVE_SOFTPIPE and GLA_HAVE_VIRGL (gla_core.h), in FLAGS below.
GL_DRIVERS=${GL_DRIVERS:-both}
case $GL_DRIVERS in
    both)     GDRV=softpipe,virgl; HAVE_SP=1; HAVE_VG=1 ;;
    virgl)    GDRV=virgl;          HAVE_SP=0; HAVE_VG=1 ;;
    softpipe) GDRV=softpipe;       HAVE_SP=1; HAVE_VG=0 ;;
    *) echo "mesa: GL_DRIVERS is both, virgl or softpipe, not $GL_DRIVERS"; exit 1 ;;
esac
if [ ! -f "$B/build.ninja" ]; then
    # Only what GLA needs: softpipe and/or virgl, GL and GLES 2/3, no window systems or LLVM.
    meson setup "$B" "$SRC" $XARG $PYARG "$WORK/python.ini" --buildtype=release \
        -Dgallium-drivers=$GDRV -Dvulkan-drivers= -Dllvm=disabled -Dplatforms= \
        -Degl=disabled -Dglx=disabled -Dgbm=disabled -Dgles1=disabled -Dgles2=enabled \
        -Dopengl=true -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled \
        -Dexpat=disabled -Dzlib=disabled -Dshader-cache=disabled -Dspirv-tools=disabled
elif [ "$(cat "$B/.gl-drivers" 2>/dev/null || echo both)" != "$GL_DRIVERS" ]; then
    # A build tree made for another set: the same tree, told the new one.
    meson configure "$B" -Dgallium-drivers=$GDRV
fi
echo "$GL_DRIVERS" > "$B/.gl-drivers"
DRVLIBS=
[ $HAVE_SP = 1 ] && DRVLIBS="$DRVLIBS src/gallium/drivers/softpipe/libsoftpipe.a"
[ $HAVE_VG = 1 ] && DRVLIBS="$DRVLIBS src/gallium/drivers/virgl/libvirgl.a"
LIBS="src/mesa/libmesa.a$DRVLIBS src/gallium/auxiliary/libgallium.a src/gallium/auxiliary/libgalliumvl_stub.a
 src/compiler/glsl/libglsl.a src/compiler/glsl/glcpp/libglcpp.a src/compiler/spirv/libvtn.a
 src/compiler/nir/libnir.a src/compiler/libcompiler.a src/mesa/glapi/glapi/libglapi_bridge.a
 src/mesa/glapi/shared-glapi/libglapi.a src/util/libxmlconfig.a src/util/libmesa_util.a
 src/util/blake3/libblake3.a src/c11/impl/libmesa_util_c11.a"
OPT=$(cd "$B" && ninja -t targets all | grep -oE '^src/(util/libmesa_util_(simd|clflush|clflushopt)|mesa/libmesa_sse41)\.a' | sort -u | tr '\n' ' ')
ninja -C "$B" $LIBS $OPT
# GCC 16's -m68040 sometimes saves FPCR, around a float-to-int store, in the
# register the store indexes with (tools/fpcr_check.py). The objects that do
# are built again at -m68020 and linked ahead of the libraries.
FIXOBJS=
case $NAME in *amigaos*)
    FIXOBJS=$("$PY" "$HERE/fpcr_fix.py" "$B" "$(dirname "$(command -v m68k-amigaos-gcc)")/m68k-amigaos-objdump" "$B/fpcr-fixed" $LIBS $OPT)
    FIXOBJS=$(echo $FIXOBJS) ;;
esac

# The GLA core takes Mesa's own flags (from the state tracker's compile line).
# That includes the target's -m flags and, on AmigaOS, -noixemul, -fbaserel32,
# -fno-delete-null-pointer-checks and the shim header.
FLAGS=$("$PY" - "$B" <<'PY'
import json, shlex, sys
b = sys.argv[1]
for e in json.load(open(b + "/compile_commands.json")):
    if e["file"].endswith("state_tracker/st_context.c"):
        a, keep, i = shlex.split(e["command"]), [], 1
        while i < len(a):
            x = a[i]
            if x == "-include":
                keep += [x, a[i + 1]]; i += 1
            elif x[:2] in ("-D", "-I") or x.startswith(("-std=", "-m")) or x in ("-noixemul", "-fbaserel32", "-fno-delete-null-pointer-checks"):
                keep.append(x)
            i += 1
        print(" ".join(shlex.quote(x) for x in keep))
        break
PY
)
FLAGS="$FLAGS -DGLA_HAVE_SOFTPIPE=$HAVE_SP -DGLA_HAVE_VIRGL=$HAVE_VG"
# The cross file's link flags (-m68040, -noixemul, ...), without its libraries.
LFLAGS=
if [ -n "${CROSS:-}" ]; then
    LFLAGS=$("$PY" - "$TOP/$CROSS" <<'PY'
import ast, re, sys
for l in open(sys.argv[1]):
    m = re.match(r"\s*c_link_args\s*=\s*(\[.*\])", l)
    if m:
        print(" ".join(x for x in ast.literal_eval(m.group(1)) if not x.startswith("-l")))
PY
)
fi
# Two kinds of program on AmigaOS (8 October 2026). One that links Mesa itself
# (test_gla, GFOpenDemos.static) is -fbaserel32 like Mesa, and is linked
# -resident32 (MESA_LFLAGS): the stove's plain -fbaserel32 program start
# (nlbcrt0) hangs as the program ends, even for "int main(void) { return 0; }",
# where -resident32's (nlrcrt0) ends as it should. One that uses GL through
# libGL.a (GFOpenDemos) is an ordinary program (LFLAGS, without -fbaserel32):
# GL.module's A4 is libGL.a's business.
MESA_LFLAGS=$LFLAGS
case " $LFLAGS " in *" -fbaserel32 "*)
    MESA_LFLAGS="$LFLAGS -resident32"
    LFLAGS=$(echo " $LFLAGS " | sed 's/ -fbaserel32 / /; s/^ *//; s/ *$//') ;;
esac
CC=$(sed -n "s/^c = \[*'\{0,1\}\([^]']*\).*/\1/p" "${CROSS:+$TOP/$CROSS}" 2>/dev/null | head -1)
CC=${CC:-cc}
cd "$B"
eval "$CC $FLAGS -I$SRC/src/gallium/drivers -I$SRC/src/mesa/glapi -O2 -c $TOP/gla/gla_core.c -o gla_core.o"
eval "$CC $FLAGS -I$SRC/src/gallium/drivers -I$SRC/src/virtio -I$REPO/include -O2 -c $TOP/gla/gla_virgl.c -o gla_virgl.o"
# The shader cache (gla/gla_shader_cache.c, patch 0012): its entries are
# keyed on this build, Mesa's version and the patches' sum, as there is no
# ELF build id to take it from.
echo "#define GLA_CACHE_BUILD \"mesa-$VERSION+$PSUM\"" > gla_cache_build.h
eval "$CC $FLAGS -I. -I$TOP/gla -O2 -c $TOP/gla/gla_shader_cache.c -o gla_shader_cache.o"
SHIM= SYSLIBS="-lstdc++ -lm -lpthread -ldl" PROGOBJS=
# The host's half of virgl (ACVirgl over virglrenderer), for the tests on this machine.
HOSTV=
case $NAME in host) cc -O2 -c "$REPO/host/virgl/acvirgl.c" -o acvirgl.o && HOSTV="acvirgl.o -DGLA_TEST_VIRGL" ;; esac
# Mesa finds libdrm on a PC that has its headers and then calls it (drmGetCap
# in u_screen.c): link it there.
case $NAME in host) if grep -q HAVE_LIBDRM "$B/compile_commands.json"; then SYSLIBS="$SYSLIBS $(pkg-config --libs libdrm 2>/dev/null || echo -ldrm)"; fi ;; esac
case $NAME in *amigaos*)
    eval "$CC $FLAGS -O2 -c $TOP/mesa/amigaos/posix_shim.c -o posix_shim.o"
    # virgl's transport: OGPU_OP_VIRGL batches through gfopengpu.library
    eval "$CC $FLAGS -I$REPO/include -O2 -c $TOP/gla/os3/gla_virgl_os3.c -o gla_virgl_os3.o"
    # its notice when a program gets softpipe: the output, the runtime's log, the ACRTG board
    eval "$CC $FLAGS -O2 -c $TOP/gla/os3/gla_notice_os3.c -o gla_notice_os3.o"
    eval "$CC $FLAGS -O2 -c $REPO/library/opengpu/ogpu_build.c -o ogpu_build.o"
    SHIM="posix_shim.o gla_virgl_os3.o gla_notice_os3.o ogpu_build.o" SYSLIBS="-lstdc++ -lm -lpthread"   # libnix has no libdl
    # A -fbaserel32 program needs libnix's constructor runner done again (amigaos/initcpp.c).
    eval "$CC $FLAGS -O2 -c $TOP/mesa/amigaos/initcpp.c -o initcpp.o"
    PROGOBJS=initcpp.o ;;
esac
# Mesa's c11 threads name the pthread_mutexattr calls weakly; a static link must ask for them.
WEAK="-Wl,-u,pthread_mutexattr_init -Wl,-u,pthread_mutexattr_settype -Wl,-u,pthread_mutexattr_destroy"
$CC $MESA_LFLAGS -std=c99 -O2 -Wall -Wextra -Werror -I"$SRC/include" -I"$REPO" ${CROSS:+-static $WEAK} -o test_gla \
    "$TOP/tests/test_gla.c" gla_core.o gla_virgl.o gla_shader_cache.o $HOSTV $SHIM $PROGOBJS $FIXOBJS -Wl,--start-group $LIBS $OPT -Wl,--end-group \
    $SYSLIBS ${CROSS:+-latomic}
# What programs built on this (GFOpenDemos) need to link against it, as shell
# assignments: LFLAGS for a program on libGL.a; MESA_LFLAGS, FLAGS and
# PROGOBJS (libnix's constructor runner, amigaos/initcpp.c) for one that links
# Mesa itself.
q() { printf "%s='%s'\n" "$1" "$(printf %s "$2" | sed "s/'/'\\\\''/g")"; }
{ q CC "$CC"; q FLAGS "$FLAGS"; q LFLAGS "$LFLAGS"; q SRC "$SRC"; q LIBS "$LIBS"; q OPT "$OPT"
  q SHIM "$SHIM"; q SYSLIBS "$SYSLIBS"; q WEAK "$WEAK"; q GLAOBJS "gla_core.o gla_virgl.o gla_shader_cache.o"; q FIXOBJS "$FIXOBJS"
  q PROGOBJS "$PROGOBJS"; q MESA_LFLAGS "$MESA_LFLAGS"; } > gla-link.env
case $NAME in *amigaos*)
    # The unstripped test is ~20 MB of symbols; the copy for the Amiga is stripped.
    # (strip refuses a -resident32 program whose data is this big; tools/hunk_strip.py doesn't.)
    "${CC%gcc}strip" -o test_gla.stripped test_gla 2>/dev/null || python3 "$REPO/tools/hunk_strip.py" test_gla test_gla.stripped
    echo "built $B/test_gla ($B/test_gla.stripped for the Amiga)" ;;
*)  echo "built $B/test_gla" ;;
esac
