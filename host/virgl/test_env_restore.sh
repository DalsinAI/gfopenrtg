#!/bin/sh
# The software try's variables come back. No GPU and no virglrenderer needed.
set -e
cd "$(dirname "$0")"
rm -rf /tmp/av-env-stubs /tmp/av-env-seen.txt /tmp/av-env-test
mkdir -p /tmp/av-env-stubs
cc -shared -fPIC -o /tmp/av-env-stubs/libvirglrenderer.so.1 test_virgl_stub.c
echo 'void av_stub_gl(void){}' | cc -shared -fPIC -o /tmp/av-env-stubs/libGL.so.1 -xc -
cc -DAV_ENV_TEST -O2 -Wall -Wextra -Werror -o /tmp/av-env-test acvirgl.c -ldl
LD_LIBRARY_PATH=/tmp/av-env-stubs /tmp/av-env-test
echo "-- seen during the two tries --"
cat /tmp/av-env-seen.txt
grep -q '^mesa=llvmpipe gallium=llvmpipe soft=1 vk=/keep/icd.json$' /tmp/av-env-seen.txt
grep -q '^mesa=zink gallium=- soft=- vk=/keep/icd.json$' /tmp/av-env-seen.txt
echo "env restore test passed"
