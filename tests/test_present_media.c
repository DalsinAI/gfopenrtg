/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ORTG_Present's MEDIA kind (library/present_media.h, what screens.c's
 * prs_media runs), against a fake host: the frame lives in the fake host's
 * own memory, as ACM_HOLD leaves it, and the fake ACM_SHOW writes it into
 * the target as the runtime's broker does (gfosmotherboards
 * acmedia_service.c; its own tests hold the real one to the ABI).
 *
 * Zero-copy, tested as zero-copy: everything the 68k side touches (the
 * bitmaps, the rectangles, the block) is in one arena standing for guest
 * memory, and after a present the frame's pixels are found in it only
 * inside the window's visible pieces, written by the host; the 68k side
 * never has a buffer of the frame's. A window's pieces on one bitmap go as
 * one ACM_SHOW, another bitmap (a smart refresh window's hidden part) as
 * another; a target the host can't write goes to the bounce. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../library/present_media.h"

static int failures, checks;
static void check(int ok, const char *what)
{
    checks++;
    if (!ok) { failures++; printf("FAIL: %s\n", what); }
}

/* ---- the fake host: its frame (8 x 4 here), never in the arena ---- */
#define FW 8
#define FH 4
#define FRAME 0x00401C01UL          /* a frame handle as ACM_HOLD answers: generation, hold, slot */
static unsigned char host_frame[FH][FW][4];
static int shows, answer;           /* answer: what ACM_SHOW says, 0 = write */
static unsigned long last_blk[PRM_SHOW_LONGS];
static void host_pixel(int x, int y, unsigned char *p) { p[0] = 0xff; p[1] = (unsigned char)(0x80 | x); p[2] = (unsigned char)(0x40 | y); p[3] = 0x5a; }
static long fake_show(unsigned long *blk)
{
    unsigned long i, n = blk[7], pitch = blk[2], rows = blk[4];
    int bpp = blk[3] == PRM_PIX_ARGB32 ? 4 : blk[3] == PRM_PIX_RGB16 ? 2 : 0;
    long ox = (short)(blk[5] >> 16), oy = (short)(blk[5] & 0xffff), done = 0;
    const short *r = (const short *)blk[6];
    unsigned char *t = (unsigned char *)blk[1];
    shows++;
    memcpy(last_blk, blk, sizeof last_blk);
    if (answer) return answer;
    if (blk[0] != FRAME) return -3;                             /* ACM_E_NOSUCH */
    if (!bpp) return PRM_E_FORMAT;
    for (i = 0; i < n; i++, r += 4) {
        long x0 = r[0], y0 = r[1], x1 = r[2], y1 = r[3], x, y;
        if (x0 < ox) x0 = ox;
        if (y0 < oy) y0 = oy;
        if (x1 > ox + FW - 1) x1 = ox + FW - 1;
        if (y1 > oy + FH - 1) y1 = oy + FH - 1;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= (long)(pitch / (unsigned long)bpp)) x1 = (long)(pitch / (unsigned long)bpp) - 1;
        if (y1 >= (long)rows) y1 = (long)rows - 1;
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) {
                const unsigned char *s = host_frame[y - oy][x - ox];
                unsigned char *d = t + y * pitch + x * bpp;
                if (bpp == 4) memcpy(d, s, 4);
                else { d[0] = (unsigned char)((s[1] & 0xf8) | s[2] >> 5); d[1] = (unsigned char)((s[2] & 0x1c) << 3 | s[3] >> 3); }
                done++;
            }
    }
    return done;
}

/* ---- guest memory: one arena ---- */
#define ARENA (1 << 16)
static unsigned char *arena;
#define SCR_OFF 0x1000              /* a 32 x 16 ARGB32 screen, pitch 128 */
#define SCR_W 32
#define SCR_H 16
#define HID_OFF 0x3000              /* a smart refresh layer's 16 x 8 hidden part, ARGB32, pitch 64 */
#define CTX_OFF 0x8000
#define R16_OFF 0x5000              /* a 32 x 16 RGB16 screen, pitch 64 */

/* A visible piece of a bitmap in the arena, and where the frame's pixel 0, 0 is in that bitmap. */
struct vis { unsigned long off, pitch; long fx, fy, x0, y0, x1, y1; };
/* 1 when every frame pixel in the arena is inside one of the pieces, at its place; *seen counts them */
static int frame_only_in(const struct vis *v, int nv, long *seen)
{
    unsigned long a;
    *seen = 0;
    for (a = 0; a + 4 <= ARENA; a++) {
        const unsigned char *p = arena + a;
        int in = 0, k;
        if (!(p[0] == 0xff && (p[1] & 0xf0) == 0x80 && (p[2] & 0xf0) == 0x40 && p[3] == 0x5a)) continue;
        for (k = 0; k < nv && !in; k++) {
            long off = (long)a - (long)v[k].off, x, y;
            if (off < 0 || off % 4) continue;
            x = (off % (long)v[k].pitch) / 4; y = off / (long)v[k].pitch;
            in = x >= v[k].x0 && x <= v[k].x1 && y >= v[k].y0 && y <= v[k].y1 &&
                 p[1] == (0x80 | (x - v[k].fx)) && p[2] == (0x40 | (y - v[k].fy));
        }
        if (!in) return 0;
        (*seen)++;
    }
    return 1;
}

int main(void)
{
    struct prm_ctx *c;
    int x, y, i;
    long seen;
    arena = calloc(1, ARENA);
    if (!arena) return 2;
    for (y = 0; y < FH; y++) for (x = 0; x < FW; x++) host_pixel(x, y, host_frame[y][x]);
    c = (struct prm_ctx *)(arena + CTX_OFF);            /* the 68k side's state is guest memory too */

    {   /* a window at 10, 4 of the screen, the frame at 2, 1 of it: two visible pieces, the rest under another window */
        static const struct vis vis[2] = { { SCR_OFF, SCR_W * 4, 12, 5, 12, 5, 15, 8 }, { SCR_OFF, SCR_W * 4, 12, 5, 16, 5, 19, 6 } };
        prm_init(c, FRAME, fake_show);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 12, 5, 15, 8, 12, 5);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 16, 5, 19, 6, 12, 5);
        check(shows == 0, "the pieces of one bitmap are gathered");
        prm_send(c);
        check(shows == 1 && last_blk[0] == FRAME && last_blk[1] == (unsigned long)(arena + SCR_OFF) && last_blk[2] == SCR_W * 4 &&
              last_blk[3] == PRM_PIX_ARGB32 && last_blk[4] == SCR_H && last_blk[5] == (12UL << 16 | 5) && last_blk[7] == 2,
              "one ACM_SHOW for the bitmap: the frame, the bitmap, its pitch, format and rows, the frame's place, two rectangles");
        check(c->pixels == 16 + 8, "the pixels the host wrote");
        check(frame_only_in(vis, 2, &seen) && seen == 24,
              "zero-copy: the frame's pixels are in guest memory only in the visible pieces, at their places, and only those 24");
    }
    {   /* a smart refresh window: a piece on the screen, a piece on the hidden part's own bitmap (its own origin) */
        static const struct vis vis_h[2] = { { SCR_OFF, SCR_W * 4, 20, 2, 20, 2, 23, 5 }, { HID_OFF, 16 * 4, -4, 0, 0, 0, 3, 3 } };
        memset(arena, 0, ARENA);
        shows = 0;
        prm_init(c, FRAME, fake_show);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 20, 2, 23, 5, 20, 2);
        prm_piece(c, (unsigned long)(arena + HID_OFF), 16 * 4, 8, 4, 0, 0, 3, 3, -4, 0);
        check(shows == 1, "another bitmap: what was gathered goes first");
        prm_send(c);
        check(shows == 2 && last_blk[1] == (unsigned long)(arena + HID_OFF) && last_blk[5] == (0xfffcUL << 16 | 0),
              "the hidden part's ACM_SHOW: its bitmap, the frame's place in it (x -4)");
        check(c->pixels == 16 + 16 && frame_only_in(vis_h, 2, &seen) && seen == 16 + 16,
              "the hidden part gets the frame's columns 4-7, the screen piece its columns 0-3");
    }
    {   /* RGB16: the host converts */
        memset(arena, 0, ARENA);
        prm_init(c, FRAME, fake_show);
        prm_piece(c, (unsigned long)(arena + R16_OFF), 64, 16, 2, 0, 0, 31, 15, 0, 0);
        prm_send(c);
        check(c->pixels == FW * FH && last_blk[3] == PRM_PIX_RGB16 && arena[R16_OFF] == ((0x80 & 0xf8) | 0x40 >> 5) &&
              arena[R16_OFF + 1] == (unsigned char)(((0x40 & 0x1c) << 3) | 0x5a >> 3) && arena[R16_OFF + 2 * FW] == 0,
              "RGB16: the host writes R5G6B5, the frame's size only");
    }
    {   /* more pieces than one ACM_SHOW carries */
        shows = 0;
        prm_init(c, FRAME, fake_show);
        for (i = 0; i < PRM_RECTS + 3; i++) prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 0, 0, 0, 0, 0, 0);
        prm_send(c);
        check(shows == 2 && last_blk[7] == 3 && c->pixels == PRM_RECTS + 3, "past PRM_RECTS rectangles: a second ACM_SHOW");
    }
    {   /* what goes to the bounce, and what doesn't */
        prm_init(c, FRAME, fake_show);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), 32, 16, 1, 0, 0, 3, 3, 0, 0);
        check(c->pixels == PRM_BOUNCE, "an 8-bit or planar piece: the bounce");
        shows = 0;
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 0, 0, 3, 3, 0, 0);
        prm_send(c);
        check(shows == 0 && c->pixels == PRM_BOUNCE, "after the bounce is chosen, the host is asked nothing more");
        prm_init(c, FRAME, fake_show);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 0, 0, 3, 3, 40000, 0);
        check(c->pixels == PRM_BOUNCE, "a place past a WORD: the bounce");
        answer = PRM_E_NOTAVAIL;
        prm_init(c, FRAME, fake_show);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 0, 0, 3, 3, 0, 0);
        prm_send(c);
        check(c->pixels == PRM_BOUNCE, "ACM_E_NOTAVAIL (no broker, a target not in plain memory): the bounce");
        answer = PRM_E_FORMAT;
        prm_init(c, FRAME, fake_show);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 0, 0, 3, 3, 0, 0);
        prm_send(c);
        check(c->pixels == PRM_BOUNCE, "ACM_E_FORMAT: the bounce");
        answer = 0;
        prm_init(c, FRAME ^ (1UL << 10), fake_show);
        prm_piece(c, (unsigned long)(arena + SCR_OFF), SCR_W * 4, SCR_H, 4, 0, 0, 3, 3, 0, 0);
        prm_send(c);
        check(c->pixels == -3, "a stale frame (ACM_E_NOSUCH): an error, not a bounce");
    }
    {   /* the bounce's block: the frame's part 2..5 x 1..2 into a buffer of its size */
        unsigned long blk[PRM_SHOW_LONGS];
        short rect[4];
        unsigned char *buf = arena + 0xA000;
        memset(arena, 0, ARENA);
        prm_bounce_block(blk, rect, FRAME, (unsigned long)buf, 2, 1, 5, 2);
        check(blk[2] == 16 && blk[3] == PRM_PIX_ARGB32 && blk[4] == 2 && blk[5] == (0xfffeUL << 16 | 0xffff) && blk[7] == 1 &&
              rect[0] == 0 && rect[1] == 0 && rect[2] == 3 && rect[3] == 1, "the bounce's ACM_SHOW: ARGB32, the part's size, the frame placed so the part is at 0, 0");
        check(fake_show(blk) == 8 && buf[1] == (0x80 | 2) && buf[2] == (0x40 | 1) &&
              buf[4 * 4 + 1] == (0x80 | 2) && buf[4 * 4 + 2] == (0x40 | 2) && buf[8 * 4] == 0, "the bounce's buffer holds the part, row by row");
    }
    printf("%d checks, %d failed\n", checks, failures);
    if (!failures) printf("present media cases pass\n");
    free(arena);
    return failures != 0;
}
