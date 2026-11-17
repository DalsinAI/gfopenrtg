# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
"""A module's malloc and free (library/modules/common/module_malloc.c) on
the host, against exec stand-ins (tests/modules/host).

With libnix's free (the modules before), a free of a pointer that isn't the
module's keeps the memory lock, and another task's malloc then waits for
ever: SDL2.module's audio thread under Amoebax, which stopped Mix_HaltMusic
and Mix_PlayMusic. With module_malloc.c's free that malloc comes back.
Built with ASan and UBSan."""
import os
import shutil
import subprocess
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.join(HERE, "host")
CC = os.environ.get("CC") or shutil.which("gcc") or shutil.which("cc")


def build(out, *defs):
    exe = os.path.join(out, "mm" + "".join(d.replace("-D", "_") for d in defs))
    subprocess.run([CC, "-std=gnu11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-function", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=undefined", "-I", HOST, *defs,
                    os.path.join(HOST, "test_module_malloc.c"),
                    os.path.join(HOST, "exec_shim.c"), "-o", exe, "-lpthread"],
                   check=True)
    return exe


def run(exe, *args):
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0")
    return subprocess.run([exe, *args], capture_output=True, text=True, timeout=60, env=env)


@unittest.skipUnless(CC, "no C compiler")
class ModuleMalloc(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        cls.before = build(cls.tmp, "-DLIBNIX_FREE")
        cls.after = build(cls.tmp)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_libnix_free_hangs_the_audio_thread(self):
        r = run(self.before, "audio")
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertIn("HANG", r.stdout)

    def test_module_free_lets_the_audio_thread_on(self):
        r = run(self.after, "audio")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("OK", r.stdout)

    def test_blocks(self):
        r = run(self.after)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("OK", r.stdout)


if __name__ == "__main__":
    unittest.main()
