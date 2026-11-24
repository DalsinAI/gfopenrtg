/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ACMWindow: a film played into a Workbench window through ACMedia, the way
 * a window's player presents its frames (the shared pipe's plan, M8 step
 * 2.6), and what it cost. For the lab, and the pattern for ACMedia's own
 * window players.
 *   - gfopenrtg.library 0.30 or later: each due frame is held on the host
 *     (ACM_HOLD) at the window's size, presented with ORTG_Present's MEDIA
 *     kind (the host writes it straight into the window's visible pieces),
 *     then let go (ACM_RELEASE), only once ORTG_Present has returned;
 *   - older, or ENV:ACMedia/PresentOld set to 1, or OLD: the route before
 *     it: ACM_FRAME into a buffer of ours, then ORTG_WritePixels (the CPU
 *     writes it). The switch stays for one release.
 * The film is fed from the file (ACM_SRC_FEED). At the end it says how many
 * frames, in how long, and GFOpenRTG's "present media" count (frames written
 * by the host) and "present memory" count (frames bounced and written by
 * the CPU): on the new route into an RTG window the second stays 0.
 *   ACMWindow FILE/A,FRAMES/N,OLD/S,WIDTH/N,HEIGHT/N
 * Returns 0, 5 when ACMedia is not there or the film won't open, 10 on an error. */
#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <dos/rdargs.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/gfopenrtg.h>
#include <stdio.h>
#include <string.h>
#define HL_VENDORED 1
#include "hostlib/acmedia/acmedia.h"

struct Library *OpenRTGBase;
struct IntuitionBase *IntuitionBase;

static ULONG blk[8];
static int32_t acm(int32_t (*fn)(hl_addr), ULONG a0, ULONG a1, ULONG a2, ULONG a3, ULONG a4, ULONG a5, ULONG a6)
{
    blk[0] = a0; blk[1] = a1; blk[2] = a2; blk[3] = a3; blk[4] = a4; blk[5] = a5; blk[6] = a6;
    return fn((hl_addr)blk);
}

static LONG ticks(void) { struct DateStamp d; DateStamp(&d); return d.ds_Minute * 3000 + d.ds_Tick; }

int main(void)
{
    LONG args[5] = { 0, 0, 0, 0, 0 };
    struct RDArgs *rda;
    struct Window *win = NULL;
    UBYTE *film = NULL, *buf = NULL;
    ULONG size = 0, fed = 0, want_frames = 0, w = 320, h = 180, frames = 0, drops = 0;
    ULONG src[3], target[ACM_TARGET_BYTES / 4], held[3], ev[4 * 8];
    ULONG before[ORTG_STAT_COUNT * 4], after[ORTG_STAT_COUNT * 4];
    int32_t ses = -1, r;
    int old = 0, ended = 0, rc = 10;
    LONG t0, t1;
    char v[4];
    BPTR fh;

    if (!(rda = ReadArgs((CONST_STRPTR)"FILE/A,FRAMES/N,OLD/S,WIDTH/N,HEIGHT/N", args, NULL))) { PrintFault(IoErr(), (CONST_STRPTR)"ACMWindow"); return 10; }
    if (args[1]) want_frames = *(ULONG *)args[1];
    if (args[3]) w = *(ULONG *)args[3];
    if (args[4]) h = *(ULONG *)args[4];
    old = args[2] != 0;
    if (GetVar((CONST_STRPTR)"ACMedia/PresentOld", (STRPTR)v, sizeof v, GVF_GLOBAL_ONLY) > 0 && v[0] == '1') old = 1;
    IntuitionBase = (struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library", 39);
    OpenRTGBase = OpenLibrary((CONST_STRPTR)"gfopenrtg.library", 0);
    if (!IntuitionBase || !OpenRTGBase) { printf("ACMWindow: no intuition.library 39 or gfopenrtg.library\n"); goto out; }
    if (OpenRTGBase->lib_Version == 0 && OpenRTGBase->lib_Revision < 30) old = 1;      /* no ORTG_Present */
    printf("gfopenrtg.library %u.%u; route: %s\n", OpenRTGBase->lib_Version, OpenRTGBase->lib_Revision,
           old ? "old (ACM_FRAME into our buffer, the CPU writes it)" : "new (ACM_HOLD, ORTG_Present MEDIA, the host writes it)");

    if (!(fh = Open((CONST_STRPTR)args[0], MODE_OLDFILE))) { PrintFault(IoErr(), (CONST_STRPTR)args[0]); rc = 5; goto out; }
    Seek(fh, 0, OFFSET_END); size = (ULONG)Seek(fh, 0, OFFSET_BEGINNING);
    if (size && (film = AllocVec(size, MEMF_ANY))) size = (ULONG)Read(fh, film, size);
    Close(fh);
    if (!film || !size) { printf("ACMWindow: can't read %s\n", (char *)args[0]); goto out; }
    if (old && !(buf = AllocVec(w * h * 4, MEMF_ANY))) goto out;

    src[0] = ACM_SRC_FEED; src[1] = size; src[2] = 0;
    memset(target, 0, sizeof target); target[0] = ACM_OUT_GUEST;
    ses = acm(hostlib_acm_open, (ULONG)src, sizeof src, ACM_ST_VIDEO, (ULONG)target, 0, 0, 0);
    if (ses < 0) {
        char line[80];
        acm(hostlib_acm_fault, (ULONG)ses, (ULONG)line, sizeof line, 0, 0, 0, 0);
        printf("ACMWindow: ACM_OPEN %ld: %s\n", (long)ses, ses == ACM_E_NOTAVAIL ? "no ACMedia on this machine" : line);
        rc = 5; goto out;
    }
    if (!(win = OpenWindowTags(NULL, WA_Title, (ULONG)"ACMWindow", WA_InnerWidth, w, WA_InnerHeight, h, WA_DragBar, TRUE,
                               WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW, WA_Activate, TRUE, TAG_DONE)))
        goto out;
    acm(hostlib_acm_control, (ULONG)ses, ACM_CTL_PLAY, 0, 0, 0, 0, 0);
    ORTG_DrawStats(before, ORTG_STAT_COUNT, 0);
    t0 = ticks();
    while (!ended && (!want_frames || frames < want_frames)) {
        struct IntuiMessage *m;
        int32_t n, k;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) { if (m->Class == IDCMP_CLOSEWINDOW) ended = 1; ReplyMsg((struct Message *)m); }
        if (fed < size) {
            ULONG part = size - fed > 65536 ? 65536 : size - fed;
            r = acm(hostlib_acm_feed, (ULONG)ses, (ULONG)(film + fed), part, fed + part == size ? ACM_FF_EOF : 0, 0, 0, 0);
            if (r > 0) fed += (ULONG)r;
            else if (r < 0 && r != ACM_E_AGAIN) { printf("ACMWindow: ACM_FEED %ld\n", (long)r); goto out; }
        }
        n = acm(hostlib_acm_events, (ULONG)ses, (ULONG)ev, sizeof ev, 0, 0, 0, 0);
        for (k = 0; k < n; k++) {
            ULONG type = ev[4 * k + 1] >> 16;
            if (type == ACM_EV_END) ended = 1;
            if (type == ACM_EV_ERROR) { printf("ACMWindow: the decoder says %ld\n", (long)ev[4 * k + 3]); ended = 1; }
        }
        if (!old) {
            struct ORTG_PresentInfo pi;
            int32_t f = acm(hostlib_acm_hold, (ULONG)ses, (ULONG)held, sizeof held, ACM_PIX_ARGB32, w, h, 0);
            if (f == ACM_E_AGAIN) { Delay(1); continue; }
            if (f < 0) { printf("ACMWindow: ACM_HOLD %ld\n", (long)f); goto out; }
            memset(&pi, 0, sizeof pi);
            pi.size = sizeof pi; pi.kind = ORTG_SURF_MEDIA; pi.handle = (ULONG)f;
            pi.width = (UWORD)(held[1] >> 16); pi.height = (UWORD)(held[1] & 0xffff);
            pi.rp = win->RPort; pi.x = win->BorderLeft; pi.y = win->BorderTop;
            if (ORTG_Present(&pi) < 0) drops++;
            acm(hostlib_acm_release, (ULONG)f, 0, 0, 0, 0, 0, 0);       /* only now: ORTG_Present has finished with it */
        } else {
            struct OpenRTGPixels px;                                    /* ORTG_WritePixels, as before 0.30 */
            int32_t p = acm(hostlib_acm_frame, (ULONG)ses, (ULONG)buf, w * h * 4, w * 4, ACM_PIX_ARGB32, w, h);
            if (p == ACM_E_AGAIN) { Delay(1); continue; }
            if (p < 0) { printf("ACMWindow: ACM_FRAME %ld\n", (long)p); goto out; }
            memset(&px, 0, sizeof px);
            px.data = buf; px.modulo = (long)w * 4; px.format = ORTG_PIX_ARGB; px.width = (long)w; px.height = (long)h;
            if (ORTG_WritePixels(win->RPort, win->BorderLeft, win->BorderTop, &px) < 0) drops++;
        }
        frames++;
    }
    t1 = ticks();
    ORTG_DrawStats(after, ORTG_STAT_COUNT, 0);
    {
        LONG dt = t1 - t0 > 0 ? t1 - t0 : 1;
        ULONG k = ORTG_STAT_PRESENT_MEDIA, m = ORTG_STAT_PRESENT_MEMORY;
        printf("frames %lu in %ld.%02ld s (%lu.%lu fps), %lu not presented\n", (unsigned long)frames, (long)(dt / 50), (long)(dt % 50 * 2),
               (unsigned long)(frames * 50 / (ULONG)dt), (unsigned long)(frames * 500 / (ULONG)dt % 10), (unsigned long)drops);
        printf("present media: %lu by the host (%lu pixels); present memory: %lu by the CPU (%lu pixels)\n",
               (unsigned long)(after[k * 4] - before[k * 4]), (unsigned long)(after[k * 4 + 2] - before[k * 4 + 2]),
               (unsigned long)(after[m * 4 + 1] - before[m * 4 + 1]), (unsigned long)(after[m * 4 + 3] - before[m * 4 + 3]));
    }
    rc = drops ? 10 : 0;
out:
    if (ses >= 0) acm(hostlib_acm_close, (ULONG)ses, 0, 0, 0, 0, 0, 0);
    if (win) CloseWindow(win);
    if (buf) FreeVec(buf);
    if (film) FreeVec(film);
    if (OpenRTGBase) CloseLibrary(OpenRTGBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    FreeArgs(rda);
    return rc;
}
