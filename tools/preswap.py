#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""preswap: little-endian assets to big-endian at build time, so a port on
the 68k reads them as they are and swaps nothing at run time.

    preswap.py raw  --layout f32x3,f32x2,u16 [--stride N] [--offset N] IN OUT
    preswap.py raw  --all32 | --all16 | --all64 IN OUT
    preswap.py gltf IN.glb OUT.glb
    preswap.py gltf IN.gltf OUT.gltf      (writes OUT's .bin files beside it)
    preswap.py ktx  IN.ktx OUT.ktx

raw: the file is a run of elements, each the fields of --layout (f32, i32,
u32, f16, i16, u16, f64, i64, u64, i8, u8, pad; xN repeats a field) and
--stride bytes long (default: the layout's size, the rest of a longer
stride is left alone), from --offset (default 0; the bytes before it are
copied as they are). A part element at the end is an error.

gltf: every accessor's components (sparse indices and values too) are
swapped by componentType, with the bufferView's byteStride and the column
padding of 1- and 2-byte matrices; bytes no accessor reads (images,
Draco or meshopt data) are left as they are. Two accessors over the same
bytes with the same component size are swapped once; with different sizes
it's an error. The JSON stays as it was, but for asset.extras, which gets
"byteOrder": "big-endian" (and "preswap": 1); a file that already says so
is refused. A .glb's own header and chunk headers stay little-endian, as
the glTF spec says.

ktx: a KTX 1 file is written natively big-endian: the header, the key/value
sizes and the image sizes swapped, the endianness field 0x04030201 in
big-endian order, and the texels swapped by glTypeSize (2 or 4; 1, as for
any compressed format, is left alone). KTX 2 has no endianness field (it is
always little-endian) and is refused.
"""
from __future__ import annotations

import argparse
import base64
import json
import struct
import sys
from pathlib import Path

VERSION = 1
MARK = {"byteOrder": "big-endian", "preswap": VERSION}


class PreswapError(Exception):
    pass


def swap_units(buf: bytearray, start: int, end: int, size: int) -> None:
    """Swap buf[start:end] in place as units of size bytes."""
    if size <= 1 or end <= start:
        return
    if (end - start) % size:
        raise PreswapError(f"{end - start} bytes at {start} are not whole {size}-byte units")
    fmt = {2: "H", 4: "I", 8: "Q"}[size]
    n = (end - start) // size
    vals = struct.unpack_from(f"<{n}{fmt}", buf, start)
    struct.pack_into(f">{n}{fmt}", buf, start, *vals)


class Swapper:
    """Swaps fields of a buffer once each, refusing a byte claimed twice with
    different unit sizes (two accessors reading the same bytes differently)."""

    def __init__(self, buf: bytearray, name: str):
        self.buf = buf
        self.name = name
        self.claim = bytearray(len(buf))    # per byte: 0, or 2/4/8 at a unit's first byte, 1 inside it

    def field(self, off: int, size: int) -> None:
        if size <= 1:
            return
        if off < 0 or off + size > len(self.buf):
            raise PreswapError(f"{self.name}: a {size}-byte field at {off} is past the end ({len(self.buf)} bytes)")
        c = self.claim
        if c[off] == size and all(c[off + i] == 1 for i in range(1, size)):
            return                          # the same field, already swapped
        if any(c[off + i] for i in range(size)):
            raise PreswapError(f"{self.name}: bytes {off}-{off + size - 1} are read by two accessors "
                               f"with different component sizes")
        c[off] = size
        for i in range(1, size):
            c[off + i] = 1
        self.buf[off:off + size] = self.buf[off:off + size][::-1]

    def run(self, off: int, n: int, size: int) -> None:
        """n consecutive fields of size bytes."""
        self.strided(off, n, size, [0], size)

    def strided(self, start: int, count: int, stride: int, offs: list[int], size: int) -> None:
        """Fields of size bytes at start + e * stride + o, e < count, o in offs:
        with extended slices (one per byte of a field) when none is claimed
        yet, else field by field."""
        if size <= 1 or count <= 0:
            return
        last = start + (count - 1) * stride + max(offs) + size
        if start < 0 or last > len(self.buf):
            raise PreswapError(f"{self.name}: {count} elements of {stride} bytes at {start} "
                               f"are past the end ({len(self.buf)} bytes)")
        span = (count - 1) * stride + 1
        slices = [[slice(start + o + i, start + o + i + span, stride) for i in range(size)] for o in offs]
        if any(any(self.claim[sl]) for field in slices for sl in field):
            for e in range(count):
                for o in offs:
                    self.field(start + e * stride + o, size)
            return
        head, rest = bytes([size]) * count, bytes([1]) * count
        for field in slices:
            parts = [self.buf[sl] for sl in field]
            for i, sl in enumerate(field):
                self.buf[sl] = parts[size - 1 - i]
                self.claim[sl] = head if i == 0 else rest


# ---- raw ------------------------------------------------------------------

FIELD_SIZES = {"f32": 4, "i32": 4, "u32": 4, "f16": 2, "i16": 2, "u16": 2,
               "f64": 8, "i64": 8, "u64": 8, "i8": 1, "u8": 1, "pad": 1}


def parse_layout(spec: str) -> list[int]:
    sizes: list[int] = []
    for part in spec.replace(" ", "").split(","):
        if not part:
            continue
        name, _, rep = part.partition("x")
        if name not in FIELD_SIZES:
            raise PreswapError(f"layout: unknown field {name!r} (one of {', '.join(FIELD_SIZES)})")
        count = int(rep) if rep else 1
        if count < 1:
            raise PreswapError(f"layout: {part}: a count of at least 1")
        sizes += [FIELD_SIZES[name]] * count
    if not sizes:
        raise PreswapError("layout: no fields")
    return sizes


def preswap_raw(data: bytes, sizes: list[int], stride: int | None, offset: int) -> bytes:
    buf = bytearray(data)
    elem = sum(sizes)
    stride = stride or elem
    if stride < elem:
        raise PreswapError(f"stride {stride} is less than the layout's {elem} bytes")
    body = len(buf) - offset
    if offset < 0 or body < 0:
        raise PreswapError(f"offset {offset} is past the end ({len(buf)} bytes)")
    count, rest = divmod(body, stride)
    if rest and rest < elem:
        raise PreswapError(f"{rest} bytes after the last whole element ({count} of {stride} bytes)")
    if rest:                                # the last element may end before its padding
        count += 1
    # fields as (offset in element, size), runs of one size merged
    runs: list[tuple[int, int, int]] = []
    o = 0
    for s in sizes:
        if s > 1:
            if runs and runs[-1][2] == s and runs[-1][0] + runs[-1][1] * s == o:
                runs[-1] = (runs[-1][0], runs[-1][1] + 1, s)
            else:
                runs.append((o, 1, s))
        o += s
    if stride == elem and len(runs) == 1 and runs[0][0] == 0 and runs[0][1] * runs[0][2] == elem:
        swap_units(buf, offset, offset + count * stride, runs[0][2])    # one size throughout
        return bytes(buf)
    for i in range(count):
        base = offset + i * stride
        for ro, rn, rs in runs:
            swap_units(buf, base + ro, base + ro + rn * rs, rs)
    return bytes(buf)


# ---- glTF 2.0 --------------------------------------------------------------

COMPONENT_SIZE = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
TYPE_COMPONENTS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT2": 4, "MAT3": 9, "MAT4": 16}
GLB_MAGIC, GLB_JSON, GLB_BIN = 0x46546C67, 0x4E4F534A, 0x004E4942


def element_fields(atype: str, csize: int) -> tuple[list[int], int]:
    """Offsets of an element's components and the element's size, with the
    glTF rule that each matrix column starts on 4 bytes."""
    if atype.startswith("MAT"):
        n = int(atype[3])
        col = n * csize
        col_padded = (col + 3) & ~3
        offs = [c * col_padded + r * csize for c in range(n) for r in range(n)]
        return offs, n * col_padded
    k = TYPE_COMPONENTS[atype]
    return [i * csize for i in range(k)], k * csize


def swap_view_range(sw: Swapper, start: int, count: int, stride: int, offs: list[int], csize: int, esize: int) -> None:
    if stride == esize and offs == [i * csize for i in range(len(offs))]:
        sw.run(start, count * len(offs), csize)     # tightly packed: one run
    else:
        sw.strided(start, count, stride, offs, csize)


def preswap_gltf_json(doc: dict, buffers: list[bytearray]) -> int:
    """Swaps buffers in place for every accessor; returns how many accessors."""
    asset = doc.get("asset", {})
    if str(asset.get("version", "")).split(".")[0] != "2":
        raise PreswapError(f"glTF asset.version {asset.get('version')!r}: only glTF 2.0")
    extras = asset.get("extras")
    if isinstance(extras, dict) and extras.get("byteOrder") == "big-endian":
        raise PreswapError("asset.extras.byteOrder is big-endian already: this file was preswapped")
    views = doc.get("bufferViews", [])
    swappers = [Swapper(b, f"buffer {i}") for i, b in enumerate(buffers)]

    def view_slot(vi: int) -> tuple[Swapper, int, int, int] | None:
        v = views[vi]
        if "EXT_meshopt_compression" in v.get("extensions", {}) or "KHR_meshopt_compression" in v.get("extensions", {}):
            return None                     # filled by the decoder, in the host's order
        bi = v["buffer"]
        if bi >= len(buffers) or buffers[bi] is None:
            return None                     # a fallback buffer with no data
        return swappers[bi], v.get("byteOffset", 0), v["byteLength"], v.get("byteStride", 0)

    done = 0
    for ai, acc in enumerate(doc.get("accessors", [])):
        ct = acc["componentType"]
        if ct not in COMPONENT_SIZE:
            raise PreswapError(f"accessor {ai}: componentType {ct}")
        csize = COMPONENT_SIZE[ct]
        offs, esize = element_fields(acc["type"], csize)
        if "bufferView" in acc:
            slot = view_slot(acc["bufferView"])
            if slot:
                sw, voff, vlen, vstride = slot
                stride = vstride or esize
                start = voff + acc.get("byteOffset", 0)
                if acc["count"] and acc.get("byteOffset", 0) + stride * (acc["count"] - 1) + esize > vlen:
                    raise PreswapError(f"accessor {ai}: runs past its bufferView")
                swap_view_range(sw, start, acc["count"], stride, offs, csize, esize)
        sparse = acc.get("sparse")
        if sparse:
            n = sparse["count"]
            idx, val = sparse["indices"], sparse["values"]
            isize = COMPONENT_SIZE[idx["componentType"]]
            slot = view_slot(idx["bufferView"])
            if slot:
                sw, voff, _, _ = slot
                sw.run(voff + idx.get("byteOffset", 0), n, isize)
            slot = view_slot(val["bufferView"])
            if slot:
                sw, voff, _, _ = slot
                swap_view_range(sw, voff + val.get("byteOffset", 0), n, esize, offs, csize, esize)
        done += 1
    if not isinstance(extras, dict):
        extras = {}
    extras.update(MARK)
    asset["extras"] = extras
    doc["asset"] = asset
    return done


def read_glb(data: bytes) -> tuple[dict, bytearray | None]:
    magic, version, length = struct.unpack_from("<III", data, 0)
    if magic != GLB_MAGIC or version != 2:
        raise PreswapError("not a glTF 2.0 .glb")
    pos, doc, binchunk = 12, None, None
    while pos + 8 <= min(length, len(data)):
        clen, ctype = struct.unpack_from("<II", data, pos)
        chunk = data[pos + 8:pos + 8 + clen]
        if ctype == GLB_JSON and doc is None:
            doc = json.loads(chunk.decode("utf-8"))
        elif ctype == GLB_BIN and binchunk is None:
            binchunk = bytearray(chunk)
        pos += 8 + clen
    if doc is None:
        raise PreswapError(".glb has no JSON chunk")
    return doc, binchunk


def write_glb(doc: dict, binchunk: bytearray | None) -> bytes:
    js = json.dumps(doc, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    js += b" " * (-len(js) % 4)
    out = struct.pack("<II", len(js), GLB_JSON) + js
    if binchunk is not None:
        b = bytes(binchunk) + b"\0" * (-len(binchunk) % 4)
        out += struct.pack("<II", len(b), GLB_BIN) + b
    return struct.pack("<III", GLB_MAGIC, 2, 12 + len(out)) + out


def preswap_gltf(src: Path, dst: Path) -> str:
    data = src.read_bytes()
    if data[:4] == b"glTF":
        doc, binchunk = read_glb(data)
        bufs: list = []
        for i, b in enumerate(doc.get("buffers", [])):
            if "uri" not in b:
                if i != 0 or binchunk is None:
                    bufs.append(None)
                else:
                    bufs.append(binchunk)
            else:
                bufs.append(load_uri(src.parent, b["uri"]))
        n = preswap_gltf_json(doc, bufs)
        write_external(doc, bufs, dst)
        dst.write_bytes(write_glb(doc, binchunk))
        return f"{n} accessors"
    doc = json.loads(data.decode("utf-8"))
    bufs = [load_uri(src.parent, b["uri"]) if "uri" in b else None for b in doc.get("buffers", [])]
    n = preswap_gltf_json(doc, bufs)
    write_external(doc, bufs, dst)
    dst.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return f"{n} accessors"


def load_uri(base: Path, uri: str) -> bytearray:
    if uri.startswith("data:"):
        head, _, payload = uri.partition(",")
        if not head.endswith(";base64"):
            raise PreswapError("a data: URI that isn't base64")
        return bytearray(base64.b64decode(payload))
    from urllib.parse import unquote
    return bytearray((base / unquote(uri)).read_bytes())


def write_external(doc: dict, bufs: list, dst: Path) -> None:
    """Each buffer with a uri goes to a new file beside dst (or back into its
    data: URI), and the JSON points at it."""
    for i, b in enumerate(doc.get("buffers", [])):
        if "uri" not in b or bufs[i] is None:
            continue
        if b["uri"].startswith("data:"):
            b["uri"] = "data:application/octet-stream;base64," + base64.b64encode(bytes(bufs[i])).decode("ascii")
            continue
        name = f"{dst.stem}.be{'' if i == 0 else i}.bin"
        (dst.parent / name).write_bytes(bytes(bufs[i]))
        b["uri"] = name


# ---- KTX 1 -----------------------------------------------------------------

KTX1_ID = b"\xABKTX 11\xBB\r\n\x1A\n"
KTX2_ID = b"\xABKTX 20\xBB\r\n\x1A\n"
KTX_FIELDS = ("glType", "glTypeSize", "glFormat", "glInternalFormat", "glBaseInternalFormat", "pixelWidth",
              "pixelHeight", "pixelDepth", "numberOfArrayElements", "numberOfFaces", "numberOfMipmapLevels",
              "bytesOfKeyValueData")


def preswap_ktx(data: bytes) -> tuple[bytes, str]:
    if data[:12] == KTX2_ID:
        raise PreswapError("KTX 2 is always little-endian (it has no endianness field): convert to KTX 1, or swap on load")
    if data[:12] != KTX1_ID:
        raise PreswapError("not a KTX 1 file")
    endian = struct.unpack_from("<I", data, 12)[0]
    if endian == 0x01020304:
        raise PreswapError("this KTX is big-endian already")
    if endian != 0x04030201:
        raise PreswapError(f"KTX endianness field {endian:#x}")
    h = dict(zip(KTX_FIELDS, struct.unpack_from("<12I", data, 16)))
    out = bytearray(data[:12]) + struct.pack(">I", 0x04030201) + struct.pack(">12I", *(h[k] for k in KTX_FIELDS))
    pos = 64
    kv_end = pos + h["bytesOfKeyValueData"]
    while pos < kv_end:                     # keyAndValueByteSize, key/value bytes, padding to 4
        n = struct.unpack_from("<I", data, pos)[0]
        pad = -n % 4
        out += struct.pack(">I", n) + data[pos + 4:pos + 4 + n + pad]
        pos += 4 + n + pad
    if pos != kv_end:
        raise PreswapError("KTX key/value data doesn't end where bytesOfKeyValueData says")
    tsize = h["glTypeSize"]
    if tsize not in (1, 2, 4):
        raise PreswapError(f"glTypeSize {tsize}")
    if h["glType"] == 0 and tsize != 1:
        raise PreswapError("a compressed KTX (glType 0) with glTypeSize other than 1")
    levels = max(1, h["numberOfMipmapLevels"])
    cube_faces = h["numberOfFaces"] if h["numberOfArrayElements"] == 0 else 1   # non-array cubemaps: imageSize is one face
    for _ in range(levels):
        size = struct.unpack_from("<I", data, pos)[0]
        out += struct.pack(">I", size)
        pos += 4
        for _ in range(cube_faces):
            img = bytearray(data[pos:pos + size])
            if len(img) != size:
                raise PreswapError("KTX image data cut short")
            if tsize > 1:
                swap_units(img, 0, size - size % tsize, tsize)
                if size % tsize:
                    raise PreswapError(f"an image of {size} bytes isn't whole {tsize}-byte units")
            pad = -size % 4 if cube_faces > 1 else 0             # cubePadding
            out += img + data[pos + size:pos + size + pad]
            pos += size + pad
        mpad = -pos % 4                                         # mipPadding
        out += data[pos:pos + mpad]
        pos += mpad
    if pos != len(data):
        raise PreswapError(f"{len(data) - pos} bytes after the last mip level")
    return bytes(out), f"{h['pixelWidth']}x{h['pixelHeight']}, {levels} levels, glTypeSize {tsize}"


# ---- main ------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="preswap.py", description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__.split("\n\n", 1)[1])
    sub = ap.add_subparsers(dest="mode", required=True)
    r = sub.add_parser("raw", help="a run of elements of one layout")
    g = r.add_mutually_exclusive_group(required=True)
    g.add_argument("--layout", help="fields of an element, e.g. f32x3,f32x2,u16")
    g.add_argument("--all16", action="store_true", help="every 2 bytes")
    g.add_argument("--all32", action="store_true", help="every 4 bytes")
    g.add_argument("--all64", action="store_true", help="every 8 bytes")
    r.add_argument("--stride", type=int, help="bytes from one element to the next")
    r.add_argument("--offset", type=int, default=0, help="bytes to copy as they are first (a header)")
    for m in ("gltf", "ktx"):
        sub.add_parser(m)
    for p in sub.choices.values():
        p.add_argument("src", type=Path)
        p.add_argument("dst", type=Path)
    a = ap.parse_args(argv)
    try:
        if a.src.resolve() == a.dst.resolve():
            raise PreswapError("write to another file, so the original stays little-endian")
        if a.mode == "raw":
            sizes = parse_layout(a.layout) if a.layout else [2 if a.all16 else 4 if a.all32 else 8]
            a.dst.write_bytes(preswap_raw(a.src.read_bytes(), sizes, a.stride, a.offset))
            what = a.layout or ("16" if a.all16 else "32" if a.all32 else "64") + "-bit"
        elif a.mode == "gltf":
            what = preswap_gltf(a.src, a.dst)
        else:
            out, what = preswap_ktx(a.src.read_bytes())
            a.dst.write_bytes(out)
    except (PreswapError, OSError, KeyError, ValueError, struct.error) as e:
        print(f"preswap: {a.src}: {e}", file=sys.stderr)
        return 1
    print(f"preswap: {a.src} -> {a.dst} ({what}, big-endian)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
