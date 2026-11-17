"""Host test for patches/amigaos3/0022: the back end's system cursor goes on
SDL's list of cursors once.

SDL_CreateSystemCursor (SDL_mouse.c) puts what the back end's
CreateSystemCursor returns on mouse->cursors. The back end made it with
SDL_CreateCursor, which had already put it there, so the cursor's next
pointed at itself; SDL_FreeCursor of a cursor made before it walked that
loop for ever (XBoing hung as it quit, 16 Nov 2026).

The test builds the back end's two functions from the patch with a model of
SDL_mouse.c's list (SDL_CreateCursor, SDL_CreateSystemCursor and
SDL_FreeCursor, as SDL 2.32.10 has them), and the code before the patch the
same way: before, freeing hangs (caught by a step limit); after, the list
stays a list and every cursor frees."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / 'library/modules/sdl2/patches/amigaos3/0022-system-cursor-listed-once.patch'

# SDL_mouse.c's cursor list, as SDL 2.32.10 has it, with a step limit on the
# walk so a loop in the list ends the test instead of hanging it.
MODEL = r'''
#include <stdio.h>
#include <stdlib.h>
typedef struct SDL_Cursor { struct SDL_Cursor *next; void *driverdata; } SDL_Cursor;
typedef enum { SDL_SYSTEM_CURSOR_ARROW, SDL_SYSTEM_CURSOR_HAND } SDL_SystemCursor;
typedef struct { SDL_Cursor *cursors, *def_cursor, *cur_cursor;
                 SDL_Cursor *(*CreateSystemCursor)(SDL_SystemCursor); } SDL_Mouse;
static SDL_Mouse the_mouse;
static SDL_Mouse *SDL_GetMouse(void) { return &the_mouse; }
static const unsigned char default_cdata[1], default_cmask[1];
#define DEFAULT_CWIDTH 16
#define DEFAULT_CHEIGHT 16
#define DEFAULT_CHOTX 0
#define DEFAULT_CHOTY 0
static int steps_left;
static int walk_step(void)
{
    if (--steps_left < 0) { puts("LOOP"); exit(3); }
    return 1;
}
/* SDL_CreateCursor -> SDL_CreateColorCursor: the cursor goes on the list */
static SDL_Cursor *SDL_CreateCursor(const unsigned char *d, const unsigned char *m,
                                    int w, int h, int x, int y)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Cursor *cursor = calloc(1, sizeof(*cursor));
    (void)d; (void)m; (void)w; (void)h; (void)x; (void)y;
    cursor->next = mouse->cursors;
    mouse->cursors = cursor;
    return cursor;
}
'''

CORE = r'''
/* SDL_CreateSystemCursor: the driver's cursor goes on the list */
static SDL_Cursor *SDL_CreateSystemCursor(SDL_SystemCursor id)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Cursor *cursor = mouse->CreateSystemCursor(id);
    if (cursor) {
        cursor->next = mouse->cursors;
        mouse->cursors = cursor;
    }
    return cursor;
}
/* SDL_FreeCursor */
static void SDL_FreeCursor(SDL_Cursor *cursor)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Cursor *curr, *prev;
    if (!cursor || cursor == mouse->def_cursor) return;
    steps_left = 1000;
    for (prev = NULL, curr = mouse->cursors; curr && walk_step(); prev = curr, curr = curr->next) {
        if (curr == cursor) {
            if (prev) prev->next = curr->next; else mouse->cursors = curr->next;
            free(curr);
            return;
        }
    }
}
static int listed(SDL_Cursor *cursor)
{
    SDL_Cursor *c;
    steps_left = 1000;
    for (c = SDL_GetMouse()->cursors; c && walk_step(); c = c->next) if (c == cursor) return 1;
    return 0;
}
int main(void)
{
    SDL_Mouse *mouse = SDL_GetMouse();
    SDL_Cursor *own, *arrow, *hand;
    mouse->CreateSystemCursor = OS3_CreateSystemCursor;
    /* OS3_InitMouse: the default cursor */
    mouse->def_cursor = OS3_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    /* XBoing: a cursor of its own, then a system one, then frees them */
    own = SDL_CreateCursor(default_cdata, default_cmask, 16, 16, 0, 0);
    arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
    hand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
    if (arrow->next == arrow || hand->next == hand) { puts("LOOP"); return 3; }
    if (!listed(own) || !listed(arrow) || !listed(hand)) { puts("not listed"); return 1; }
    SDL_FreeCursor(own);         /* walks past both system cursors */
    SDL_FreeCursor(arrow);
    SDL_FreeCursor(hand);
    if (mouse->cursors) { puts("left on the list"); return 1; }
    /* SDL keeps the default cursor off the list (SDL_SetDefaultCursor) */
    if (listed(mouse->def_cursor)) { puts("default cursor listed"); return 1; }
    puts("OK");
    return 0;
}
'''


def function(src, signature):
    start = src.index(signature)
    end = src.index('\n}\n', start) + 3
    return src[start:end]


class SystemCursorListedOnce(unittest.TestCase):
    def sides(self):
        lines = PATCH.read_text().splitlines()
        body = [s for s in lines if not s.startswith(('---', '+++', '@@'))][2:]
        before = '\n'.join(s[1:] for s in body if s[:1] in (' ', '-')) + '\n'
        after = '\n'.join(s[1:] for s in body if s[:1] in (' ', '+')) + '\n'
        return before, after

    def run_model(self, backend):
        with tempfile.TemporaryDirectory() as t:
            src = Path(t) / 'cursor.c'
            exe = Path(t) / 'cursor'
            src.write_text(MODEL + backend + CORE)
            subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                            str(src), '-o', str(exe)], check=True)
            # a loop is reported with exit(3), leaving cursors behind on purpose
            env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
            return subprocess.run([str(exe)], capture_output=True, text=True, timeout=60, env=env)

    def test_before_the_patch_free_loops(self):
        before, _ = self.sides()
        r = self.run_model(function(before, 'static SDL_Cursor *OS3_CreateSystemCursor('))
        self.assertEqual(r.returncode, 3, r.stdout + r.stderr)
        self.assertIn('LOOP', r.stdout)

    def test_after_the_patch_every_cursor_frees(self):
        _, after = self.sides()
        backend = (function(after, 'static void OS3_UnlistCursor(') +
                   function(after, 'static SDL_Cursor *OS3_CreateSystemCursor('))
        r = self.run_model(backend)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('OK', r.stdout)


if __name__ == '__main__':
    unittest.main()
