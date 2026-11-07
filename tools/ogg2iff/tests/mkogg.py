#!/usr/bin/env python3
# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""Make tests/tone.ogg, ogg2iff's test input (our own tones: CC0).

    tests/mkogg.py [OUT.ogg]

2.5 s, 44100 Hz stereo: 0.5 s of silence, 1.5 s of 440 Hz on the left and
1000 Hz on the right at -6 dBFS, 0.5 s of silence. Encoded through the
system's libsndfile (Vorbis, quality 0.5), loaded with ctypes, so nothing
needs installing; the .ogg is committed so the tests don't need it at all.
"""
import ctypes, ctypes.util, math, struct, sys

RATE, CH = 44100, 2
SF_FORMAT_OGG, SF_FORMAT_VORBIS, SFM_WRITE = 0x200000, 0x0060, 0x20
SFC_SET_VBR_ENCODING_QUALITY = 0x1300


class SF_INFO(ctypes.Structure):
    _fields_ = [("frames", ctypes.c_int64), ("samplerate", ctypes.c_int),
                ("channels", ctypes.c_int), ("format", ctypes.c_int),
                ("sections", ctypes.c_int), ("seekable", ctypes.c_int)]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "tone.ogg"
    lib = ctypes.CDLL(ctypes.util.find_library("sndfile") or "libsndfile.so.1")
    lib.sf_open.restype = ctypes.c_void_p
    lib.sf_open.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(SF_INFO)]
    lib.sf_writef_float.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float), ctypes.c_int64]
    lib.sf_command.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    lib.sf_close.argtypes = [ctypes.c_void_p]
    info = SF_INFO(0, RATE, CH, SF_FORMAT_OGG | SF_FORMAT_VORBIS, 0, 0)
    f = lib.sf_open(out.encode(), SFM_WRITE, ctypes.byref(info))
    if not f:
        sys.exit("can't open %s for Vorbis through libsndfile" % out)
    q = ctypes.c_double(0.5)
    lib.sf_command(f, SFC_SET_VBR_ENCODING_QUALITY, ctypes.byref(q), ctypes.sizeof(q))
    pad, tone = RATE // 2, RATE * 3 // 2
    s = []
    for i in range(pad + tone + pad):
        if pad <= i < pad + tone:
            t = (i - pad) / RATE
            # 5 ms fades so the tone has no clicks
            env = min(1.0, (i - pad) / 220.0, (pad + tone - 1 - i) / 220.0)
            s += [0.5 * env * math.sin(2 * math.pi * 440 * t),
                  0.5 * env * math.sin(2 * math.pi * 1000 * t)]
        else:
            s += [0.0, 0.0]
    buf = (ctypes.c_float * len(s))(*s)
    lib.sf_writef_float(f, buf, len(s) // CH)
    lib.sf_close(f)


main()
