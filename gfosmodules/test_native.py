#!/usr/bin/env python3
"""N2's GFOpenGPU native bodies have the platform layout and a narrow ABI."""
from __future__ import annotations

import ctypes
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "gfosmodules" / "build-native.sh"
NAME = "gfopengpu.library"
PLATFORMS = {"arm64": 183, "x86_64": 62}
EXPORTS = {
    "ogpu_core_init",
    "ogpu_core_run",
    "ogpu_core_supports",
    "ogpu3d_init",
    "ogpu3d_is_op",
    "ogpu3d_op",
    "ogpu3d_run",
    "ogpu3d_supports",
    "ogpu_3d_run",
}


def elf_header(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:20]
    assert data[:4] == b"\x7fELF", f"{path}: not ELF"
    endian = {1: "<", 2: ">"}.get(data[5])
    assert endian, f"{path}: bad ELF byte order {data[5]}"
    return struct.unpack_from(endian + "HH", data, 16)


def dynamic_symbols(path: Path, option: str) -> set[str]:
    out = subprocess.check_output(["nm", "-D", option, str(path)], text=True)
    return {line.split()[-1] for line in out.splitlines() if line.split()}


def main() -> int:
    if shutil.which("aarch64-linux-gnu-gcc") is None:
        print("native N2: skipped (aarch64-linux-gnu-gcc is not installed)")
        return 0
    with tempfile.TemporaryDirectory(prefix="gfopengpu-native-") as td:
        out = Path(td) / "gfosmodules"
        subprocess.run([str(BUILD), str(out)], cwd=ROOT, check=True)
        assert {p.relative_to(out).as_posix() for p in out.rglob("*") if p.is_file()} == {
            "arm64/default/gfopengpu.library",
            "x86_64/default/gfopengpu.library",
        }
        for platform, machine in PLATFORMS.items():
            body = out / platform / "default" / NAME
            elf_type, elf_machine = elf_header(body)
            assert elf_type == 3, f"{body}: ELF type {elf_type}, expected ET_DYN"
            assert elf_machine == machine, f"{body}: e_machine {elf_machine}, expected {machine}"
            assert dynamic_symbols(body, "--defined-only") == EXPORTS
            assert dynamic_symbols(body, "--undefined-only") == set()
            if platform == "x86_64":
                native = ctypes.CDLL(str(body))
                native.ogpu_core_supports.argtypes = (ctypes.c_int, ctypes.c_int)
                native.ogpu_core_supports.restype = ctypes.c_int
                assert native.ogpu_core_supports(0x0010, 3) == 2  # FILL, ARGB32 -> OGPU_FULL
    print("native N2: 2 platforms, layout/ELF/export checks PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
