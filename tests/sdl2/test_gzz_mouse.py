"""Host contract for 0011: an IntuiMessage's mouse is relative to the
window's outer corner, a GimmeZeroZero window's too (OS 3.2.3), so the back
end takes the borders off a GZZ window's moves and clicks. The real pointer
needs a lab."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / 'library/modules/sdl2/patches/amigaos3/0011-gzz-mouse-position.patch'


class GzzMouseContract(unittest.TestCase):
    def added(self):
        return '\n'.join(s[1:] for s in PATCH.read_text().splitlines()
                         if s.startswith('+') and not s.startswith('+++'))

    def test_inner_mouse(self):
        added = self.added()
        start = added.index('static void OS3_InnerMouse(')
        end = added.index('\n}', start) + 2
        support = r'''
#include <assert.h>
typedef short WORD;
#define WFLG_GIMMEZEROZERO 0x00000400
struct Window { unsigned long Flags; signed char BorderLeft, BorderTop; };
'''
        main = r'''
int main(void) {
 struct Window gzz = {WFLG_GIMMEZEROZERO, 4, 22}, plain = {0, 4, 22};
 WORD x = 100, y = 100;
 OS3_InnerMouse(&gzz, &x, &y);
 assert(x == 96 && y == 78);
 x = 100; y = 100;
 OS3_InnerMouse(&plain, &x, &y);
 assert(x == 100 && y == 100);
 x = 2; y = 10;          /* on the border: SDL clips it */
 OS3_InnerMouse(&gzz, &x, &y);
 assert(x == -2 && y == -12);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as t:
            src = Path(t) / 'gzz.c'
            exe = Path(t) / 'gzz'
            src.write_text(support + added[start:end] + main)
            subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_moves_and_clicks_both_use_it(self):
        added = self.added()
        # absolute moves, and a click's own position before the button
        self.assertEqual(added.count('OS3_InnerMouse(intuiwin, &mousex, &mousey);'), 2)
        self.assertIn('OS3_HandleMouseButton(sdlwin, code);', PATCH.read_text())


if __name__ == '__main__':
    unittest.main()
