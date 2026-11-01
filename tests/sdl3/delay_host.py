#!/usr/bin/env python3
"""SDL3.module's SDL_Delay (patches/amigaos3/0007-delay-microhz.patch) on a PC.

The patched sleep is compiled with timer.device, exec and SDL thread-local
storage stand-ins, and must:
  - wait on UNIT_MICROHZ with TR_ADDREQUEST for the time asked, rounded up
    to the microsecond, never dos.library's 1/50 s Delay() ticks;
  - give each task its own reply port and request, made at its first sleep
    and kept for the next ones;
  - free a thread's when SDL cleans up its thread-local storage, and the
    program's own task's at the program's end when SDL_Quit hasn't (and not
    twice when it has);
  - fall back to Delay() ticks, leaving nothing behind, without timer.device.
    tests/sdl3/delay_host.py [SDL_systimer.c]
With a file, its SDL_SYS_DelayNS is checked instead (SDL3_68k's own, before
the patch, fails: it sleeps in Delay() ticks)."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH = ROOT / 'library/modules/sdl3/patches/amigaos3/0007-delay-microhz.patch'

if len(sys.argv) > 1:
    text = Path(sys.argv[1]).read_text()
else:
    text = '\n'.join(line[1:] for line in PATCH.read_text().splitlines()
                     if line.startswith((' ', '+')) and not line.startswith('+++'))
start = text.find('static SDL_TLSID delay_tls;')
if start < 0:
    start = text.index('void SDL_SYS_DelayNS(Uint64 ns)')
end = text.index('\n}\n', text.index('void SDL_SYS_DelayNS(Uint64 ns)')) + 3
code = text[start:end].replace('__attribute__((destructor)) static void OS3_QuitDelayTimer',
                               'static void OS3_QuitDelayTimer')
if 'OS3_QuitDelayTimer' not in code:
    code += '\nstatic void OS3_QuitDelayTimer(void) { }\n'

shim = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint64_t Uint64;
typedef uint32_t ULONG;
typedef uint16_t UWORD;
typedef const char *CONST_STRPTR;
#define SDLCALL
#define TIMERNAME "timer.device"
#define UNIT_MICROHZ 0
#define UNIT_VBLANK 1
#define TR_ADDREQUEST 9

/* exec, for two tasks (task 0 the program's, task 1 a thread) */
static int task;
struct MsgPort { int owner; };
struct Message { struct MsgPort *mn_ReplyPort; };
struct IORequest { struct Message io_Message; void *io_Device; UWORD io_Command; };
struct timeval { ULONG tv_secs, tv_micro; };
struct timerequest { struct IORequest tr_node; struct timeval tr_time; int unit, open, magic; };
static int ports, requests, opens, doios, delays, fail_open, last_ticks;
static ULONG last_secs, last_micro;
static struct MsgPort *CreateMsgPort(void) { struct MsgPort *p = calloc(1, sizeof *p); p->owner = task; ports++; return p; }
static void DeleteMsgPort(struct MsgPort *p) { assert(p && p->owner == task); ports--; free(p); }
static struct IORequest *CreateIORequest(struct MsgPort *p, long size) {
    struct timerequest *t;
    assert(p && size == (long)sizeof(struct timerequest));
    t = calloc(1, sizeof *t); t->tr_node.io_Message.mn_ReplyPort = p; t->magic = 0x7173; requests++;
    return (struct IORequest *)t;
}
static void DeleteIORequest(struct IORequest *io) { struct timerequest *t = (struct timerequest *)io; assert(t->magic == 0x7173 && !t->open); t->magic = 0; requests--; free(t); }
static long OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags) {
    struct timerequest *t = (struct timerequest *)io;
    assert(!strcmp(name, TIMERNAME) && !flags && t->magic == 0x7173);
    if (fail_open) return -1;
    t->unit = (int)unit; t->open = 1; t->tr_node.io_Device = t; opens++;
    return 0;
}
static void CloseDevice(struct IORequest *io) { struct timerequest *t = (struct timerequest *)io; assert(t->open); t->open = 0; opens--; }
static long DoIO(struct IORequest *io) {
    struct timerequest *t = (struct timerequest *)io;
    assert(t->open && t->unit == UNIT_MICROHZ && io->io_Command == TR_ADDREQUEST);
    assert(t->tr_node.io_Message.mn_ReplyPort->owner == task);   /* the calling task's own port */
    assert(t->tr_time.tv_micro < 1000000);
    last_secs = t->tr_time.tv_secs; last_micro = t->tr_time.tv_micro; doios++;
    return 0;
}
static void Delay(ULONG ticks) { last_ticks = (int)ticks; delays++; }

/* SDL's thread-local storage, a slot for each task */
typedef struct { int value; } SDL_TLSID;
typedef void (*SDL_TLSDestructorCallback)(void *);
static void *tls_value[2];
static SDL_TLSDestructorCallback tls_destructor[2];
static int tls_calls;
static int SDL_GetAtomicInt(SDL_TLSID *id) { return id->value; }
static void *SDL_GetTLS(SDL_TLSID *id) { tls_calls++; return id->value ? tls_value[task] : NULL; }
static bool SDL_SetTLS(SDL_TLSID *id, const void *value, SDL_TLSDestructorCallback d) {
    tls_calls++; id->value = 1; tls_value[task] = (void *)value; tls_destructor[task] = d; return true;
}
static void SDL_CleanupTLS(void) {           /* a thread's end, or SDL_Quit in the program's task */
    if (tls_destructor[task] && tls_value[task]) tls_destructor[task](tls_value[task]);
    tls_value[task] = NULL; tls_destructor[task] = NULL;
}
'''

checks = r'''
#define MS 1000000ULL
static void sleep_ns(Uint64 ns) { doios = delays = 0; last_secs = last_micro = 0xDEAD; last_ticks = -1; SDL_SYS_DelayNS(ns); }

int main(void) {
    int i;
    /* nothing to free at the end when no task ever slept */
    OS3_QuitDelayTimer();
    assert(tls_calls == 0);

    /* SDL_Delay(1): a millisecond on UNIT_MICROHZ, not a 20 ms tick */
    task = 0;
    sleep_ns(1 * MS);
    if (delays) { printf("FAIL SDL_Delay(1) slept %d dos.library tick(s) of 20 ms\n", last_ticks); return 1; }
    assert(doios == 1 && last_secs == 0 && last_micro == 1000);
    sleep_ns(16 * MS); assert(doios == 1 && !delays && last_secs == 0 && last_micro == 16000);
    sleep_ns(1); assert(doios == 1 && last_micro == 1);                    /* rounded up, never shorter */
    sleep_ns(1500001); assert(doios == 1 && last_micro == 1501);
    sleep_ns(2500 * MS); assert(doios == 1 && last_secs == 2 && last_micro == 500000);
    sleep_ns(0); assert(!doios && delays == 1 && last_ticks == 0);         /* SDL_Delay(0) yields, as before */
    /* one port and request for the task, kept */
    for (i = 0; i < 100; i++) sleep_ns(MS);
    assert(ports == 1 && requests == 1 && opens == 1);

    /* a thread: its own, freed when SDL cleans up its storage */
    task = 1;
    sleep_ns(3 * MS); assert(doios == 1 && last_micro == 3000);
    assert(ports == 2 && requests == 2 && opens == 2);
    SDL_CleanupTLS();
    assert(ports == 1 && requests == 1 && opens == 1);

    /* the program ends without SDL_Quit: its task's freed */
    task = 0;
    OS3_QuitDelayTimer();
    assert(ports == 0 && requests == 0 && opens == 0);
    /* again, with SDL_Quit first: freed once */
    sleep_ns(MS); assert(ports == 1 && opens == 1);
    SDL_CleanupTLS();
    assert(ports == 0 && requests == 0 && opens == 0);
    OS3_QuitDelayTimer();
    assert(ports == 0 && requests == 0 && opens == 0);

    /* no timer.device: dos.library's ticks, as before, and nothing left behind */
    fail_open = 1;
    sleep_ns(1 * MS); assert(!doios && delays == 1 && last_ticks == 1);
    sleep_ns(45 * MS); assert(!doios && delays == 1 && last_ticks == 3);
    assert(ports == 0 && requests == 0 && opens == 0);
    puts("PASS SDL_Delay: UNIT_MICROHZ to the microsecond, a port and request for each task kept between sleeps, "
         "freed at the thread's end, SDL_Quit or the program's end, Delay() ticks without timer.device");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as d:
    src = Path(d) / 'delay.c'
    exe = Path(d) / 'delay'
    src.write_text(shim + code + checks)
    subprocess.run(['cc', '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
                    str(src), '-o', str(exe)], check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
