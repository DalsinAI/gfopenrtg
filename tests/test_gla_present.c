/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GL module's OS 3.2 present (library/modules/gl/gla/os3/gla_present_os3.c)
 * on the PC, with stub AmigaOS headers (tests/gla_present-include) and a
 * fake GFOpenGPU. By default the fake carries out a batch only when it is
 * waited for (the worst case), so a batch that reads its frame after the
 * next frame was drawn into it puts a wrong frame on the screen. The GLA
 * core's side is modelled: after each present, buf_after_show (swap the
 * back and front textures after a frame left running; with one buffer,
 * settle) and a settle when the buffer goes.
 *
 *   test_gla_present DOUBLEBUF OPENGPU_REVISION TARGET [GPU_MS DRAW_MS]
 *     TARGET: screen (present_gpu), window (WritePixelArray), mixed (a
 *     screen, every other frame virgl's, written straight in)
 *     GPU_MS: the fake GPU is a thread that carries out each batch GPU_MS
 *     after it is sent; the renderer takes DRAW_MS a frame; the run is timed.
 *
 * Prints the counts and whether every frame reached the screen whole and in
 * order; exit 0 when it did. waits_locked: waits with the bitmap locked;
 * waits_own: waits for a batch sent for the same frame, before the next one
 * (waits_own_in_present: inside present itself). tests/run.sh checks them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <exec/types.h>
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>
#include <gfopengpu/gfopengpu.h>
#include <gfopengpu/build.h>
#include <gfopenrtg/gfopenrtg.h>
#include "gla/gla_core.h"
#include "gla/os3/gla_present_os3.h"

#define W 64
#define H 48
#define FRAMES 12
#define MAXJOBS 64

struct Library *CyberGfxBase, *OpenGPUBase, *DOSBase;
static struct Library cgx_lib = { 41, 0 }, ogpu_lib = { 0, 23 }, ortg_lib = { 0, 30 };
struct gla_stats gla_counts;

static struct BitMap bitmap;
static struct RastPort rastport;
static uint32_t screen_px[W * H];

static int in_present, lock_held, locks, waits, waits_locked, waits_own_in_present, waits_own, wpa_calls;
static int max_lock_held_at_return, direct_frames;
static unsigned long frame_first_fence;
static int displayed[FRAMES * 2], ndisplayed;
static int gpu_ms, draw_ms;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static uint32_t pattern(int frame, int x, int y) { return 0xff000000u | (uint32_t)(frame * 0x010203 + x * 7 + y * 131); }

static int frame_shown(const uint32_t *px)
{
    for (int fr = 0; fr < FRAMES; fr++) {
        int ok = 1;
        for (int y = 0; y < H && ok; y++)
            for (int x = 0; x < W && ok; x++)
                if (px[y * W + x] != pattern(fr, x, y)) ok = 0;
        if (ok) return fr;
    }
    return -1;
}

static void record_screen(void) { if (ndisplayed < FRAMES * 2) displayed[ndisplayed++] = frame_shown(screen_px); }

/* ---- AmigaOS, faked ---- */
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version) { (void)version; return strcmp(name, "gfopenrtg.library") ? NULL : &ortg_lib; }
void CloseLibrary(struct Library *lib) { (void)lib; }
void InitRastPort(struct RastPort *rp) { memset(rp, 0, sizeof *rp); }
LONG SetVar(CONST_STRPTR name, CONST_STRPTR buffer, LONG size, ULONG flags) { (void)name; (void)buffer; (void)size; (void)flags; return 1; }

APTR LockBitMapTags(struct BitMap *bm, ...)
{
    va_list ap;
    ULONG tag;
    (void)bm;
    va_start(ap, bm);
    while ((tag = va_arg(ap, ULONG)) != TAG_DONE) {
        ULONG *p = (ULONG *)va_arg(ap, ULONG);
        if (tag == LBMI_BASEADDRESS) *p = (ULONG)(uintptr_t)screen_px;
        else if (tag == LBMI_BYTESPERROW) *p = W * 4;
        else if (tag == LBMI_PIXFMT) *p = PIXFMT_ARGB32;
    }
    va_end(ap);
    lock_held++;
    locks++;
    return (APTR)&bitmap;
}
void UnLockBitMap(APTR lock) { (void)lock; lock_held--; }
ULONG GetCyberMapAttr(struct BitMap *bm, ULONG attr) { (void)bm; return attr == CYBRMATTR_WIDTH ? W : H; }
ULONG WritePixelArray(APTR src, int sx, int sy, int stride, struct RastPort *rp, int x, int y, int w, int h, int fmt)
{
    (void)rp; (void)fmt;
    for (int j = 0; j < h; j++)
        memcpy(&screen_px[(y + j) * W + x], (UBYTE *)src + (sy + j) * stride + sx * 4, (size_t)w * 4);
    wpa_calls++;
    record_screen();
    return (ULONG)(w * h);
}

/* virgl's frame, written straight in by the "host": its handle is the frame */
LONG ORTG_PresentVirgl(struct RastPort *rp, LONG x, LONG y, const struct OpenRTGVirglFrame *f)
{
    (void)rp; (void)x; (void)y;
    for (int j = 0; j < H; j++)
        for (int i = 0; i < W; i++)
            screen_px[j * W + i] = pattern((int)f->handle, i, j);
    direct_frames++;
    record_screen();
    return W * H;
}

/* ---- GFOpenGPU, faked: a batch runs when waited for, or on the GPU thread ---- */
struct job { unsigned long src, sbpr, dst, dbpr; int x, y, w, h; unsigned long fence; int done; double due; };
static struct job built, jobs[MAXJOBS];
static int njobs;
static unsigned long next_fence = 1;

void ogpu_batch_init(struct OGPUBatch *b, void *buf, long cap) { b->buf = buf; b->cap = cap; b->words = 0; b->overflow = 0; memset(&built, 0, sizeof built); }
void ogpu_surface(struct OGPUBatch *b, int slot, unsigned long address, unsigned long bpr, int w, int h, int format)
{ (void)slot; (void)w; (void)h; (void)format; built.dst = address; built.dbpr = bpr; b->words += 6; }
void ogpu_target(struct OGPUBatch *b, int slot) { (void)slot; b->words += 2; }
void ogpu_pixels(struct OGPUBatch *b, unsigned long address, unsigned long bpr, int format, unsigned long table,
                 int x, int y, int w, int h)
{ (void)format; (void)table; built.src = address; built.sbpr = bpr; built.x = x; built.y = y; built.w = w; built.h = h; b->words += 7; }
void ogpu_fence(struct OGPUBatch *b, unsigned long id) { (void)id; b->words += 2; }
ULONG OGPU_Query(ULONG op, ULONG format) { (void)op; (void)format; return OGPU_NONE; }

static void run_job(struct job *j)
{
    for (int r = 0; r < j->h; r++)
        memcpy((UBYTE *)(uintptr_t)j->dst + (j->y + r) * j->dbpr + j->x * 4,
               (UBYTE *)(uintptr_t)j->src + r * j->sbpr, (size_t)j->w * 4);
    j->done = 1;
    record_screen();
}

LONG OGPU_Submit(APTR stream, ULONG words, ULONG *fence)
{
    (void)stream; (void)words;
    pthread_mutex_lock(&mu);
    built.fence = next_fence++;
    built.due = now() + gpu_ms;
    jobs[njobs++] = built;
    *fence = built.fence;
    pthread_cond_broadcast(&cv);
    pthread_mutex_unlock(&mu);
    return OGPU_OK;
}

LONG OGPU_Wait(ULONG fence)
{
    waits++;
    if (lock_held) waits_locked++;
    if (fence >= frame_first_fence) {   /* a batch sent for this frame */
        waits_own++;
        if (in_present) waits_own_in_present++;
    }
    pthread_mutex_lock(&mu);
    for (int i = 0; i < njobs; i++)
        if (jobs[i].fence <= fence) {
            if (gpu_ms) while (!jobs[i].done) pthread_cond_wait(&cv, &mu);
            else if (!jobs[i].done) run_job(&jobs[i]);
        }
    pthread_mutex_unlock(&mu);
    return OGPU_OK;
}

static int stop;
static void *gpu_thread(void *u)
{
    (void)u;
    pthread_mutex_lock(&mu);
    for (;;) {
        int i;
        for (i = 0; i < njobs && jobs[i].done; i++) ;
        if (i == njobs) { if (stop) break; pthread_cond_wait(&cv, &mu); continue; }
        double wait = jobs[i].due - now();
        if (wait > 0) { pthread_mutex_unlock(&mu); usleep((useconds_t)(wait * 1000)); pthread_mutex_lock(&mu); continue; }
        run_job(&jobs[i]);
        pthread_cond_broadcast(&cv);
    }
    pthread_mutex_unlock(&mu);
    return NULL;
}

/* ---- the GLA core's side ---- */
static gla_present_virgl_fn reg_virgl;
static int (*reg_running)(void *);
static void (*reg_settle)(void *);
void gla_present_set_virgl(void (*present)(void *, const struct gla_frame *), gla_present_virgl_fn virgl) { (void)present; reg_virgl = virgl; }
void gla_present_set_async(void (*present)(void *, const struct gla_frame *), int (*running)(void *), void (*settle)(void *))
{ (void)present; reg_running = running; reg_settle = settle; }

int main(int argc, char **argv)
{
    int doublebuf = argc > 1 ? atoi(argv[1]) : 1, rev = argc > 2 ? atoi(argv[2]) : 23;
    const char *target_kind = argc > 3 ? argv[3] : "screen";
    int onscreen = strcmp(target_kind, "window") != 0, mixed = !strcmp(target_kind, "mixed");
    static uint32_t texA[W * H], texB[W * H];
    uint32_t *back = texA, *front = texB, *t;
    struct gla_os3_target target;
    struct gla_present hook;
    int flips = 0, identical = 1;
    pthread_t gpu;
    double t0, ms;

    gpu_ms = argc > 4 ? atoi(argv[4]) : 0;
    draw_ms = argc > 5 ? atoi(argv[5]) : 0;
    CyberGfxBase = &cgx_lib;
    OpenGPUBase = &ogpu_lib;
    ogpu_lib.lib_Revision = (UWORD)rev;
    target.rp = &rastport;
    target.bitmap = onscreen ? &bitmap : NULL;
    target.left = target.top = 0;
    gla_os3_present_init(&hook, &target);
    if (!doublebuf) back = front = texA;
    if (gpu_ms) pthread_create(&gpu, NULL, gpu_thread, NULL);
    t0 = now();

    for (int fr = 0; fr < FRAMES; fr++) {
        frame_first_fence = next_fence;
        if (draw_ms) usleep((useconds_t)draw_ms * 1000);
        in_present = 1;
        if (mixed && (fr & 1) && reg_virgl) {          /* virgl's frame: straight in */
            struct gla_virgl_frame vf;
            memset(&vf, 0, sizeof vf);
            vf.handle = (unsigned long)fr; vf.w = W; vf.h = H; vf.x1 = W; vf.y1 = H;
            reg_virgl(hook.user, &vf);
        } else {
            struct gla_frame f;
            /* the renderer draws into the back texture (softpipe writes it at
             * any time between swaps: tile evictions, the flush) */
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    back[y * W + x] = pattern(fr, x, y);
            memset(&f, 0, sizeof f);
            f.pixels = back; f.format = GLA_ARGB; f.stride = W * 4; f.w = W; f.h = H;
            f.x1 = W; f.y1 = H;
            hook.present(hook.user, &f);
        }
        in_present = 0;
        if (lock_held > max_lock_held_at_return) max_lock_held_at_return = lock_held;
        if (reg_running && reg_running(hook.user)) {    /* buf_after_show */
            if (doublebuf) { t = back; back = front; front = t; flips++; }
            else reg_settle(hook.user);
        }
    }
    frame_first_fence = next_fence;
    if (reg_settle) reg_settle(hook.user);              /* gla_buffer_destroy */
    ms = now() - t0;
    if (gpu_ms) { pthread_mutex_lock(&mu); stop = 1; pthread_cond_broadcast(&cv); pthread_mutex_unlock(&mu); pthread_join(gpu, NULL); }
    for (int i = 0; i < njobs; i++) if (!jobs[i].done) run_job(&jobs[i]);
    if (ndisplayed != FRAMES) identical = 0;
    for (int i = 0; i < ndisplayed && identical; i++) if (displayed[i] != i) identical = 0;

    printf("doublebuf=%d opengpu=0.%d %s: presents=%d locks=%d waits=%d waits_locked=%d waits_own_in_present=%d "
           "waits_own=%d lock_held_after_present=%d flips=%d wpa=%d direct=%d shown=",
           doublebuf, rev, target_kind, FRAMES, locks, waits, waits_locked, waits_own_in_present,
           waits_own, max_lock_held_at_return, flips, wpa_calls, direct_frames);
    for (int i = 0; i < ndisplayed; i++) printf("%s%d", i ? "," : "", displayed[i]);
    printf(" identical=%s", identical ? "yes" : "NO");
    if (gpu_ms) printf(" time_ms=%.0f (gpu %d ms, draw %d ms a frame)", ms, gpu_ms, draw_ms);
    printf("\n");
    return identical ? 0 : 1;
}
