"""Host check of SDL2.module's thread join (patches/amigaos3/0012).

The thread's end (OS3_ThreadEnded), SDL_SYS_WaitThread and
SDL_SYS_DetachThread are taken from the patch as they will be built and run
on a small Exec made of POSIX threads: Forbid() is one lock (that Wait()
gives up while it sleeps, as Exec's does), signals are per task, and a
thread's Forbid() ends with the thread, as a process's exit ends it. The
checks: a task other than the one that started a thread waits for it
(before and after it ends), threads detached before and after they end free
their data exactly once, and the waiter's signal bit is given back. The
module's own run is tests/sdl2/threadcheck.c, in a lab."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / 'library/modules/sdl2/patches/amigaos3/0012-thread-join-any-task.patch'
FILE = 'src/thread/amigaos3/SDL_systhread.c'


def new_side(patch_text, path):
    """The patched side of every hunk of one file, hunks one after another."""
    out, inside, in_hunk = [], False, False
    for line in patch_text.splitlines():
        if line.startswith('diff '):
            inside = line.endswith('b/' + path)
            in_hunk = False
            continue
        if not inside or line.startswith(('---', '+++')):
            continue
        if line.startswith('@@'):
            in_hunk = True
            out.append('')
            continue
        if in_hunk and line[:1] in (' ', '+'):
            out.append(line[1:])
        elif in_hunk and line == '':
            out.append('')
    return '\n'.join(out)


def take(text, head):
    """A definition starting with head, to its closing brace."""
    start = text.index(head)
    depth, i = 0, text.index('{', start)
    while True:
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
        i += 1
    if head.startswith('typedef'):
        end = text.index(';', end) + 1
    return text[start:end]


EXEC = r'''
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef unsigned char UBYTE; typedef signed char BYTE; typedef unsigned long ULONG;
#define MEMF_PUBLIC 1
#define MEMF_CLEAR 0x10000
#define DLOG(...) ((void)0)
struct Task {
    pthread_mutex_t m; pthread_cond_t c;
    ULONG recvd, alloc;
};
static __thread struct Task *self;
static struct Task *FindTask(void *n) { (void)n; return self; }
static void task_init(struct Task *t) {
    memset(t, 0, sizeof(*t));
    pthread_mutex_init(&t->m, NULL); pthread_cond_init(&t->c, NULL);
    t->alloc = 0xffff;  /* bits 0-15 belong to the system */
}
/* Forbid(): one lock, nested, held by one task at a time */
static pthread_mutex_t forbid_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t forbid_c = PTHREAD_COND_INITIALIZER;
static struct Task *forbid_owner; static int forbid_nest;
static void Forbid(void) {
    pthread_mutex_lock(&forbid_m);
    while (forbid_owner && forbid_owner != self) pthread_cond_wait(&forbid_c, &forbid_m);
    forbid_owner = self; forbid_nest++;
    pthread_mutex_unlock(&forbid_m);
}
static void Permit(void) {
    pthread_mutex_lock(&forbid_m);
    assert(forbid_owner == self && forbid_nest > 0);
    if (--forbid_nest == 0) { forbid_owner = NULL; pthread_cond_broadcast(&forbid_c); }
    pthread_mutex_unlock(&forbid_m);
}
static int forbid_drop(void) {  /* Wait() and the end of a process */
    int n = 0;
    pthread_mutex_lock(&forbid_m);
    if (forbid_owner == self) { n = forbid_nest; forbid_nest = 0; forbid_owner = NULL;
                                pthread_cond_broadcast(&forbid_c); }
    pthread_mutex_unlock(&forbid_m);
    return n;
}
static void forbid_take(int n) {
    if (!n) return;
    pthread_mutex_lock(&forbid_m);
    while (forbid_owner) pthread_cond_wait(&forbid_c, &forbid_m);
    forbid_owner = self; forbid_nest = n;
    pthread_mutex_unlock(&forbid_m);
}
static BYTE AllocSignal(long want) {
    int b; (void)want;
    for (b = 31; b >= 16; b--) if (!(self->alloc & (1UL << b))) {
        self->alloc |= 1UL << b;
        pthread_mutex_lock(&self->m); self->recvd &= ~(1UL << b); pthread_mutex_unlock(&self->m);
        return (BYTE)b;
    }
    return -1;
}
static void FreeSignal(BYTE b) { assert(self->alloc & (1UL << b)); self->alloc &= ~(1UL << b); }
static void Signal(struct Task *t, ULONG mask) {
    pthread_mutex_lock(&t->m); t->recvd |= mask; pthread_cond_broadcast(&t->c); pthread_mutex_unlock(&t->m);
}
static ULONG Wait(ULONG mask) {
    ULONG got; int n;
    assert((self->alloc & mask) == mask);  /* only bits this task owns */
    n = forbid_drop();
    pthread_mutex_lock(&self->m);
    while (!(self->recvd & mask)) pthread_cond_wait(&self->c, &self->m);
    got = self->recvd & mask; self->recvd &= ~got;
    pthread_mutex_unlock(&self->m);
    forbid_take(n);
    return got;
}
static void Delay(long ticks) { int n = forbid_drop(); usleep(ticks * 2000); forbid_take(n); }
static int live_blocks;
static pthread_mutex_t mem_m = PTHREAD_MUTEX_INITIALIZER;
static void *AllocVec(unsigned long n, unsigned long f) {
    (void)f; pthread_mutex_lock(&mem_m); live_blocks++; pthread_mutex_unlock(&mem_m);
    return calloc(1, n);
}
static void FreeVec(void *p) {
    pthread_mutex_lock(&mem_m); live_blocks--; pthread_mutex_unlock(&mem_m);
    memset(p, 0x5a, sizeof(void *) * 2); free(p);
}
typedef struct SDL_Thread { void *handle; } SDL_Thread;
'''

HARNESS = r'''
/* A thread: runs for run_us, then ends as OS3_ThreadMain does. */
typedef struct { SDL_Thread th; OS3_ThreadData *td; int run_us; struct Task task; } Fake;
static void *fake_main(void *p) {
    Fake *f = p;
    self = &f->task;
    usleep(f->run_us);
    OS3_ThreadEnded(f->td);
    forbid_drop();  /* the process's exit ends its Forbid() */
    return NULL;
}
static pthread_t start(Fake *f, int run_us) {
    pthread_t pt;
    task_init(&f->task);
    f->td = AllocVec(sizeof(OS3_ThreadData), MEMF_PUBLIC | MEMF_CLEAR);
    f->th.handle = f->td;
    f->run_us = run_us;
    pthread_create(&pt, NULL, fake_main, f);
    return pt;
}
/* Another task that waits for a thread it did not start */
typedef struct { Fake *f; struct Task task; int done; } Waiter;
static void *waiter_main(void *p) {
    Waiter *w = p;
    self = &w->task;
    SDL_SYS_WaitThread(&w->f->th);
    assert(w->f->th.handle == NULL);
    assert(w->task.alloc == 0xffff);  /* its bit given back */
    w->done = 1;
    return NULL;
}
int main(void) {
    static struct Task main_task;
    int round;
    task_init(&main_task); self = &main_task;
    for (round = 0; round < 300; round++) {
        Fake f; Waiter w; pthread_t pt, pw;
        int run = (round % 3) * 3000;          /* ends before, while or after the wait */
        memset(&w, 0, sizeof(w)); task_init(&w.task); w.f = &f;
        pt = start(&f, run);
        if (round % 2) usleep(4000);
        pthread_create(&pw, NULL, waiter_main, &w);
        pthread_join(pw, NULL);
        pthread_join(pt, NULL);
        assert(w.done);
        assert(live_blocks == 0);
    }
    for (round = 0; round < 300; round++) {   /* detached, before and after the end */
        Fake f; pthread_t pt;
        pt = start(&f, (round % 2) * 3000);
        if (round % 3 == 0) usleep(6000);
        SDL_SYS_DetachThread(&f.th);
        assert(f.th.handle == NULL);
        pthread_join(pt, NULL);
        assert(live_blocks == 0);
    }
    {   /* the old way round: the task that started it waits, as before */
        Fake f; pthread_t pt = start(&f, 2000);
        SDL_SYS_WaitThread(&f.th);
        pthread_join(pt, NULL);
        assert(main_task.alloc == 0xffff && live_blocks == 0);
    }
    puts("thread join: ok");
    return 0;
}
'''


class ThreadJoin(unittest.TestCase):
    def test_join_from_any_task(self):
        code = new_side(PATCH.read_text(), FILE)
        parts = [take(code, 'typedef struct {\n    struct Task *waiter;'),
                 take(code, 'static void OS3_ThreadEnded('),
                 take(code, 'void SDL_SYS_WaitThread('),
                 take(code, 'void SDL_SYS_DetachThread(')]
        # no parent signal bit left anywhere in the thread code
        self.assertNotRegex(code, r'parent_sig')
        self.assertIn('OS3_ThreadEnded(td)', code)
        with tempfile.TemporaryDirectory() as t:
            src = Path(t) / 'join.c'
            exe = Path(t) / 'join'
            src.write_text(EXEC + '\n'.join(parts) + HARNESS)
            subprocess.run(['cc', '-std=gnu11', '-O1', '-Wall', '-Werror', '-Wno-unused-function',
                            '-pthread', str(src), '-o', str(exe)], check=True)
            r = subprocess.run([str(exe)], check=True, capture_output=True, text=True, timeout=120)
            self.assertIn('thread join: ok', r.stdout)

    def test_paula_closes_in_its_thread(self):
        text = PATCH.read_text()
        code = new_side(text, 'src/audio/amigaos3/SDL_os3paula.c')
        deinit = take(code, 'static void OS3PAULA_ThreadDeinit(')
        # the port and the device are the audio thread's: closed and freed
        # there, as the thread ends, no longer in CloseDevice
        for call in ('CloseDevice((struct IORequest', 'DeleteIORequest',
                     'DeleteMsgPort'):
            self.assertIn(call, deinit)
        self.assertIn('impl->ThreadDeinit  = OS3PAULA_ThreadDeinit;', code)
        section = text[text.index('b/src/audio/amigaos3/SDL_os3paula.c'):]
        removed = [l[1:].strip() for l in section.splitlines()
                   if l.startswith('-') and not l.startswith('---')]
        self.assertIn('DeleteMsgPort(data->port);', removed)

if __name__ == '__main__':
    unittest.main()
