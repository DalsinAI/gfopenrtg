#!/bin/sh
# acvirgl says once when Mesa quietly gives llvmpipe (AC-285), and stays
# quiet on a GPU or when software was asked for. No GPU and no
# virglrenderer needed: test_virgl_stub.c stands in for virglrenderer.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
T=${TMPDIR:-/tmp}/av-quiet-$$
trap 'rm -rf "$T"' EXIT
mkdir -p "$T/stubs"
cc -shared -fPIC -o "$T/stubs/libvirglrenderer.so.1" "$HERE/test_virgl_stub.c"
echo 'void av_stub_gl(void){}' | cc -shared -fPIC -o "$T/stubs/libGL.so.1" -xc -
cc -std=gnu11 -O1 -g -Wall -Wextra -Werror -I"$HERE" -o "$T/t" "$HERE/test_quiet_software.c" "$HERE/acvirgl.c" -ldl
run() {   # run <env assignments...>: stderr into $T/err
    env -u AC_VIRGL_DRIVER -u LIBGL_ALWAYS_SOFTWARE -u GALLIUM_DRIVER -u MESA_LOADER_DRIVER_OVERRIDE \
        LD_LIBRARY_PATH="$T/stubs" "$@" "$T/t" 2 >"$T/out" 2>"$T/err"
}
fail() { echo "test_quiet_software: $1"; cat "$T/err"; exit 1; }
run AV_STUB_RENDERER="llvmpipe (LLVM 19.1.7, 128 bits)"
[ "$(grep -c 'Mesa gave llvmpipe (LLVM 19.1.7, 128 bits), not the GPU' "$T/err")" = 1 ] || fail "llvmpipe not said exactly once"
run AV_STUB_RENDERER="zink Vulkan 1.3(llvmpipe (LLVM 19.1.7, 256 bits) (MESA_LLVMPIPE))"
grep -q 'not the GPU' "$T/err" || fail "zink on lavapipe not said"
run AV_STUB_RENDERER="V3D 7.1"
! grep -q 'not the GPU' "$T/err" || fail "a GPU was called software"
grep -q '^renderer V3D 7.1$' "$T/out" || fail "renderer string lost"
run AV_STUB_RENDERER="llvmpipe (LLVM 19.1.7, 128 bits)" AC_VIRGL_DRIVER=llvmpipe
! grep -q 'not the GPU' "$T/err" || fail "said when AC_VIRGL_DRIVER asked for software"
run AV_STUB_RENDERER="llvmpipe (LLVM 19.1.7, 128 bits)" LIBGL_ALWAYS_SOFTWARE=1
! grep -q 'not the GPU' "$T/err" || fail "said when LIBGL_ALWAYS_SOFTWARE asked for software"
run AV_STUB_RENDERER="llvmpipe (LLVM 19.1.7, 128 bits)" LIBGL_ALWAYS_SOFTWARE=0
grep -q 'not the GPU' "$T/err" || fail "LIBGL_ALWAYS_SOFTWARE=0 hid it"
run AV_STUB_RENDERER="softpipe" GALLIUM_DRIVER=softpipe
! grep -q 'not the GPU' "$T/err" || fail "said when GALLIUM_DRIVER asked for software"
echo "quiet software test passed"
