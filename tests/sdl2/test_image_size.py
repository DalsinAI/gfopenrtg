# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""SDL2_image's rung 1 in one trip (satellites/module/ogpu_image_size.c).

ogpu_image.c makes the surface from the size the file's own header gives
and asks media.decode/1 for the pixels at once, instead of a PROBE trip
and a DECODE trip, each carrying the whole file to the cores: half the
trips per picture. That holds only if the header reader gives exactly the
size and alpha the decoder will: checked here against Pillow, for the
files games load, and it must say "don't know" (PROBE as before) for what
it doesn't read. Built with ASan and UBSan, and fed every cut-short prefix
of each file."""
import io
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
CC = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")
try:
    from PIL import Image
except ImportError:          # pragma: no cover
    Image = None


def picture(mode, size=(37, 23)):
    im = Image.new(mode, size)
    px = im.load()
    for y in range(size[1]):
        for x in range(size[0]):
            v = (x * 7 + y * 13) % 256
            if mode == "RGBA":
                px[x, y] = (v, 255 - v, x % 256, (x * y) % 256)
            elif mode == "RGB":
                px[x, y] = (v, 255 - v, y % 256)
            elif mode == "LA":
                px[x, y] = (v, (x * 3) % 256)
            elif mode in ("L", "P"):
                px[x, y] = v
            elif mode == "I;16":
                px[x, y] = v * 200
    return im


def encode(im, fmt, **kw):
    b = io.BytesIO()
    im.save(b, fmt, **kw)
    return b.getvalue()


@unittest.skipUnless(CC and Image, "needs a C compiler and Pillow")
class ImageSize(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.exe = os.path.join(cls.tmp, "image_size")
        subprocess.run([CC, "-std=c99", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-function", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=undefined",
                        os.path.join(HERE, "host", "image_size_main.c"), "-o", cls.exe],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def run_on(self, data, cuts):
        path = os.path.join(self.tmp, "f")
        with open(path, "wb") as f:
            f.write(data)
        r = subprocess.run([self.exe] + (["-p"] if cuts else []) + [path],
                           capture_output=True, text=True,
                           env=dict(os.environ, ASAN_OPTIONS="detect_leaks=0"))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r.stdout.split("\n")

    def size_of(self, data):
        out = self.run_on(data, False)[0].strip()
        return None if out == "-" else tuple(int(v) for v in out.split())

    def known(self, data, alpha):
        im = Image.open(io.BytesIO(data))
        lines = self.run_on(data, True)
        self.assertEqual(lines[0], "%d %d %d" % (im.width, im.height, alpha))
        # Cut short anywhere: never another answer, never a read past the end.
        self.assertEqual([l for l in lines[1:] if l.startswith("CUT")], [])

    def unknown(self, data):
        self.assertIsNone(self.size_of(data))

    def test_jpeg(self):
        rgb = picture("RGB", (640, 480))
        self.known(encode(rgb, "JPEG", quality=80), 0)
        self.known(encode(rgb, "JPEG", progressive=True), 0)
        self.known(encode(picture("L", (1, 4000)), "JPEG"), 0)
        exif = Image.Exif()
        exif[0x0112] = 6                          # rotated: not applied
        self.known(encode(rgb, "JPEG", exif=exif.tobytes()), 0)
        self.known(encode(picture("RGB").convert("CMYK"), "JPEG"), 0)

    def test_png(self):
        self.known(encode(picture("RGB"), "PNG"), 0)
        self.known(encode(picture("RGBA"), "PNG"), 1)
        self.known(encode(picture("L"), "PNG"), 0)
        self.known(encode(picture("LA"), "PNG"), 1)
        self.known(encode(picture("I;16"), "PNG"), 0)
        self.known(encode(picture("RGB"), "PNG", transparency=(0, 255, 0)), 1)
        self.known(encode(picture("L"), "PNG", transparency=7), 1)

    def test_png_paletted_goes_probe(self):
        self.unknown(encode(picture("P"), "PNG"))

    def test_webp(self):
        from PIL import features
        if not features.check("webp"):
            self.skipTest("Pillow without WebP")
        self.known(encode(picture("RGB", (320, 200)), "WEBP", quality=70), 0)
        self.unknown(encode(picture("RGB"), "WEBP", lossless=True))
        self.unknown(encode(picture("RGBA"), "WEBP"))

    def test_others_go_probe(self):
        self.unknown(encode(picture("RGB"), "GIF"))
        self.unknown(encode(picture("RGB"), "TIFF"))
        self.unknown(encode(picture("RGB"), "BMP"))
        self.unknown(b"")
        self.unknown(b"\xff\xd8\xff\xd9")
        self.unknown(b"\x89PNG\r\n\x1a\n" + b"\0" * 40)


if __name__ == "__main__":
    unittest.main()
