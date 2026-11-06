#!/bin/sh
# Copyright (c) 2026 Dalsin Limited / the AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
# ogles3.library on GFOpenGPU (library/ogles3): checks the generated dispatch
# table against the ABI, and fetches ladislawv's four ogles3 demos for a lab.
#   OUT=dir sh tests/ogles3/test_ogles3_demos.sh
# The demos (MIT, ogles3 1.0-beta2) are fetched at this pinned commit and
# SHA-256 into OUT/ogles3-demos, never committed. Their own ogles3.library
# (libs/, the V3D one) and their V3D shader binaries (shaders/) are left out:
# in a lab they must find the system's ogles3.library, and they compile their
# GLSL through GL.module. Their LICENSE goes with them.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=${OUT:-$ROOT/build/ogles3-test}
mkdir -p "$OUT"
COMMIT=4a5ce271ad8b3e817a6a02ab951e65ad0829d1e5
SHA=71dee3def6720f5ef84f4b8e11bd5a516f9088df154efa123a6f4988d2557901
URL="https://raw.githubusercontent.com/ladislawv/ogles3/$COMMIT/release/dist/ogles3-1.0-beta2-demos.zip"

# 1. The generator: 380 entries, the agl calls first, the arguments counted.
python3 "$ROOT/library/ogles3/gen_ogles3.py" "$OUT/gen"
python3 -I - "$OUT/gen" <<'PY'
import re, sys
g = sys.argv[1]
thunks = open(g + "/ogles3_thunks.s").read()
n = len(re.findall(r"^_ogl3_e\d+:", thunks, re.M))
assert n == 374, n
def args(name):
    m = re.search(r"\| %s\n\tmove.l\t#(\d+),%%d0\n\tmoveq\t#(\d+),%%d1" % name, thunks)
    return int(m.group(1)) // 4, int(m.group(2))
assert args("GLActiveTexture") == (6, 1)
assert args("GLClientWaitSync")[1] == 4          # sync, flags, a GLuint64: four longs
assert args("GLDispatchCompute")[1] == 3
assert args("GLClear")[1] == 1
table = open(g + "/ogles3_table.c").read()
assert table.count("(APTR)ogl3_") == 380
print("gen_ogles3: 380 entries, 374 GL calls, argument sizes right")
PY

# 2. The demos.
python3 -I - "$URL" "$SHA" "$OUT" <<'PY'
import hashlib, io, sys, urllib.request, zipfile, os
url, sha, out = sys.argv[1:4]
data = urllib.request.urlopen(url, timeout=120).read()
got = hashlib.sha256(data).hexdigest()
if got != sha:
    sys.exit("ogles3 demos: SHA-256 %s, expected %s" % (got, sha))
dest = os.path.join(out, "ogles3-demos")
n = 0
with zipfile.ZipFile(io.BytesIO(data)) as z:
    for i in z.infolist():
        parts = i.filename.split("/")
        if i.is_dir() or len(parts) < 2 or parts[0] != "ogles3-demos" or parts[1] in ("libs", "shaders"):
            continue
        if any(p in ("", "..") for p in parts) or i.filename.startswith("/"):
            sys.exit("ogles3 demos: unexpected path %r" % i.filename)
        path = os.path.join(dest, *parts[1:])
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(z.read(i))
        n += 1
print("ogles3 demos: %d files in %s" % (n, dest))
for f in ("juggler-ogles3", "boingswarm-ogles3", "boing2026-ogles3", "boingocean-ogles3", "LICENSE"):
    assert os.path.isfile(os.path.join(dest, f)), f
PY
