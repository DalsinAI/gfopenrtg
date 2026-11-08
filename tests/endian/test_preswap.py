#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""tools/preswap.py on files made here: raw layouts, a .glb and a
.gltf + .bin (interleaved and packed views, a byte MAT2's column padding,
sparse values, an image left alone, two accessors on one view), a data: URI,
and KTX 1 (16-bit RGBA with mips, an 8-bit cubemap, refusals).

    python3 tests/endian/test_preswap.py
"""
from __future__ import annotations

import json
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import preswap  # noqa: E402

failures = 0


def check(cond: bool, what: str) -> None:
    global failures
    if not cond:
        failures += 1
        print(f"FAIL {what}")


def test_raw() -> None:
    elems = [(1.5, -2.0, 3.25, 0.5, 0.75, 0xBEEF, 7) for _ in range(5)]
    le = b"HEAD" + b"".join(struct.pack("<5fHB", *e) + b"\x99" for e in elems)       # stride 24
    be = preswap.preswap_raw(le, preswap.parse_layout("f32x3,f32x2,u16,u8"), 24, 4)
    check(be[:4] == b"HEAD", "raw: offset bytes kept")
    for i, e in enumerate(elems):
        got = struct.unpack_from(">5fHB", be, 4 + 24 * i)
        check(got == e and be[4 + 24 * i + 23] == 0x99, f"raw element {i}")
    words = struct.pack("<6I", *range(1, 7))
    check(preswap.preswap_raw(words, [4], None, 0) == struct.pack(">6I", *range(1, 7)), "raw --all32")
    shorts = struct.pack("<5H", 1, 2, 0x1234, 4, 5)
    check(preswap.preswap_raw(shorts, [2], None, 0) == struct.pack(">5H", 1, 2, 0x1234, 4, 5), "raw --all16")
    try:
        preswap.preswap_raw(b"\0" * 10, [4], None, 0)
        check(False, "raw: part element not refused")
    except preswap.PreswapError:
        pass


def make_gltf() -> tuple[dict, bytes, dict]:
    """A buffer: interleaved pos+uv (stride 20), u16 indices, a byte MAT2,
    float MAT4, a sparse accessor, an 'image' and a second accessor on the
    interleaved view. Returns (json, buffer, expected values)."""
    b = bytearray()
    verts = [((i, i + 0.5, -i), (i / 4, 1 - i / 4)) for i in range(4)]
    v_off = len(b)
    for p, t in verts:
        b += struct.pack("<3f2f", *p, *t)
    i_off = len(b)
    idx = [0, 1, 2, 2, 3, 0]
    b += struct.pack("<6H", *idx)
    b += b"\0" * (-len(b) % 4)
    m2_off = len(b)                         # MAT2 of u8: columns padded to 4 bytes
    b += bytes([1, 2, 0xEE, 0xEE, 3, 4, 0xEE, 0xEE])
    m4_off = len(b)
    mat = [float(k) for k in range(16)]
    b += struct.pack("<16f", *mat)
    s_idx_off = len(b)
    b += struct.pack("<2I", 1, 3)
    s_val_off = len(b)
    b += struct.pack("<2f", 9.5, -9.5)
    img_off = len(b)
    img = b"\x89PNG\r\n\x1a\n" + bytes(range(24))
    b += img
    b += b"\0" * (-len(b) % 4)
    s16_off = len(b)                        # a u16 MAT2: columns 4 bytes, no padding
    b += struct.pack("<4H", 0x0102, 0x0304, 0x0506, 0x0708)
    doc = {
        "asset": {"version": "2.0", "extras": {"author": "test"}},
        "buffers": [{"byteLength": len(b)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": v_off, "byteLength": 80, "byteStride": 20},
            {"buffer": 0, "byteOffset": i_off, "byteLength": 12},
            {"buffer": 0, "byteOffset": m2_off, "byteLength": 8},
            {"buffer": 0, "byteOffset": m4_off, "byteLength": 64},
            {"buffer": 0, "byteOffset": s_idx_off, "byteLength": 8},
            {"buffer": 0, "byteOffset": s_val_off, "byteLength": 8},
            {"buffer": 0, "byteOffset": img_off, "byteLength": len(img)},
            {"buffer": 0, "byteOffset": s16_off, "byteLength": 8},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 0, "byteOffset": 12, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR"},
            {"bufferView": 2, "componentType": 5121, "count": 1, "type": "MAT2"},
            {"bufferView": 3, "componentType": 5126, "count": 1, "type": "MAT4"},
            {"componentType": 5126, "count": 4, "type": "SCALAR",
             "sparse": {"count": 2, "indices": {"bufferView": 4, "componentType": 5125},
                        "values": {"bufferView": 5}}},
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},     # the same bytes again
            {"bufferView": 7, "componentType": 5123, "count": 1, "type": "MAT2"},
        ],
        "images": [{"bufferView": 6, "mimeType": "image/png"}],
    }
    expect = {"verts": verts, "idx": idx, "mat": mat, "img_off": img_off, "img": img, "v_off": v_off,
              "i_off": i_off, "m2_off": m2_off, "m4_off": m4_off, "s_idx_off": s_idx_off, "s_val_off": s_val_off,
              "s16_off": s16_off}
    return doc, bytes(b), expect


def check_be(buf: bytes, e: dict, what: str) -> None:
    for k, (p, t) in enumerate(e["verts"]):
        got = struct.unpack_from(">3f2f", buf, e["v_off"] + 20 * k)
        check(got == (*map(float, p), *t), f"{what}: vertex {k}")
    check(list(struct.unpack_from(">6H", buf, e["i_off"])) == e["idx"], f"{what}: indices")
    check(buf[e["m2_off"]:e["m2_off"] + 8] == bytes([1, 2, 0xEE, 0xEE, 3, 4, 0xEE, 0xEE]), f"{what}: u8 MAT2 untouched")
    check(list(struct.unpack_from(">16f", buf, e["m4_off"])) == e["mat"], f"{what}: MAT4")
    check(struct.unpack_from(">2I", buf, e["s_idx_off"]) == (1, 3), f"{what}: sparse indices")
    check(struct.unpack_from(">2f", buf, e["s_val_off"]) == (9.5, -9.5), f"{what}: sparse values")
    check(buf[e["img_off"]:e["img_off"] + len(e["img"])] == e["img"], f"{what}: image untouched")
    check(struct.unpack_from(">4H", buf, e["s16_off"]) == (0x0102, 0x0304, 0x0506, 0x0708), f"{what}: u16 MAT2")


def test_gltf(tmp: Path) -> None:
    doc, buf, e = make_gltf()
    # .glb
    src = tmp / "m.glb"
    src.write_bytes(preswap.write_glb(json.loads(json.dumps(doc)), bytearray(buf)))
    dst = tmp / "m-be.glb"
    check(preswap.main(["gltf", str(src), str(dst)]) == 0, "glb: preswap ran")
    out_doc, out_bin = preswap.read_glb(dst.read_bytes())
    check(out_doc["asset"]["extras"] == {"author": "test", "byteOrder": "big-endian", "preswap": 1}, "glb: asset.extras")
    check(out_doc["accessors"] == doc["accessors"], "glb: accessors unchanged")
    check(len(out_bin) >= len(buf), "glb: BIN chunk")
    check_be(bytes(out_bin), e, "glb")
    check(preswap.main(["gltf", str(dst), str(tmp / "twice.glb")]) == 1, "glb: a preswapped file refused")
    # .gltf + .bin
    d2 = json.loads(json.dumps(doc))
    d2["buffers"][0]["uri"] = "m%20data.bin"
    (tmp / "m data.bin").write_bytes(buf)
    (tmp / "m.gltf").write_text(json.dumps(d2))
    check(preswap.main(["gltf", str(tmp / "m.gltf"), str(tmp / "out.gltf")]) == 0, "gltf: preswap ran")
    o = json.loads((tmp / "out.gltf").read_text())
    check(o["buffers"][0]["uri"] == "out.be.bin", "gltf: new .bin named")
    check_be((tmp / "out.be.bin").read_bytes(), e, "gltf")
    check((tmp / "m data.bin").read_bytes() == buf, "gltf: the original .bin untouched")
    # data: URI
    import base64
    d3 = json.loads(json.dumps(doc))
    d3["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(buf).decode()
    (tmp / "d.gltf").write_text(json.dumps(d3))
    check(preswap.main(["gltf", str(tmp / "d.gltf"), str(tmp / "d-out.gltf")]) == 0, "data uri: ran")
    o = json.loads((tmp / "d-out.gltf").read_text())
    check_be(base64.b64decode(o["buffers"][0]["uri"].split(",", 1)[1]), e, "data uri")
    # two accessors reading one byte range with different component sizes
    d4 = json.loads(json.dumps(doc))
    d4["accessors"].append({"bufferView": 1, "componentType": 5125, "count": 3, "type": "SCALAR"})
    try:
        preswap.preswap_gltf_json(d4, [bytearray(buf)])
        check(False, "conflicting accessors not refused")
    except preswap.PreswapError:
        pass


def ktx1(gl_type: int, type_size: int, fmt: int, w: int, h: int, faces: int, levels: list[list[bytes]], kv: bytes) -> bytes:
    out = bytearray(preswap.KTX1_ID) + struct.pack("<I", 0x04030201)
    out += struct.pack("<12I", gl_type, type_size, fmt, fmt, fmt, w, h, 0, 0, faces, len(levels), len(kv))
    out += kv
    for faces_data in levels:
        out += struct.pack("<I", len(faces_data[0]))
        for f in faces_data:
            out += f + b"\0" * ((-len(f) % 4) if faces > 1 else 0)
        out += b"\0" * (-len(out) % 4)
    return bytes(out)


def test_ktx() -> None:
    key = b"KTXorientation\0S=r,T=d\0"
    kv = struct.pack("<I", len(key)) + key + b"\0" * (-len(key) % 4)
    lv0 = struct.pack("<16H", *range(0x100, 0x110))         # 2x2 RGBA16
    lv1 = struct.pack("<4H", 0xA1B2, 0xC3D4, 0xE5F6, 0x0718)
    data = ktx1(0x1403, 2, 0x1908, 2, 2, 1, [[lv0], [lv1]], kv)
    out, _ = preswap.preswap_ktx(data)
    check(len(out) == len(data), "ktx: same size")
    check(out[12:16] == b"\x04\x03\x02\x01", "ktx: endianness field big-endian")
    hdr = struct.unpack_from(">12I", out, 16)
    check(hdr[:2] == (0x1403, 2) and hdr[5:7] == (2, 2) and hdr[10] == 2 and hdr[11] == len(kv), "ktx: header")
    check(struct.unpack_from(">I", out, 64)[0] == len(key) and out[68:68 + len(key)] == key, "ktx: key/value")
    p = 64 + len(kv)
    check(struct.unpack_from(">I", out, p)[0] == 32, "ktx: imageSize 0")
    check(struct.unpack_from(">16H", out, p + 4) == tuple(range(0x100, 0x110)), "ktx: level 0 texels")
    check(struct.unpack_from(">4H", out, p + 40) == (0xA1B2, 0xC3D4, 0xE5F6, 0x0718), "ktx: level 1 texels")
    # an 8-bit cubemap of 1x1 RGB faces (3 bytes, so cubePadding 1)
    faces = [bytes([k, k + 1, k + 2]) for k in range(0, 18, 3)]
    cube = ktx1(0x1401, 1, 0x1907, 1, 1, 6, [faces], b"")
    out, _ = preswap.preswap_ktx(cube)
    check(out[64:] == cube[64:68][::-1] + cube[68:], "ktx cubemap: imageSize swapped, texels and padding kept")
    for bad, what in ((out, "big-endian already"), (bytes(preswap.KTX2_ID) + b"\0" * 80, "KTX 2")):
        try:
            preswap.preswap_ktx(bad)
            check(False, f"ktx: {what} not refused")
        except preswap.PreswapError:
            pass


def main() -> int:
    test_raw()
    with tempfile.TemporaryDirectory() as t:
        test_gltf(Path(t))
    test_ktx()
    print(f"preswap: {failures} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
