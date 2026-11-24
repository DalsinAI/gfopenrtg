/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The GL module's carrier (library/modules/gl/gla/gla_carry_pipe.c) on the shared pipe, against a pretend host that
 * runs each request block as the runtime would, in order, and keeps what it saw: the GL stage of
 * docs/design/OpenGPU-Shared-Pipe-Plan.md. Mesa isn't here: the test makes the calls the virgl winsys makes
 * (gla_virgl_pipe.c) and checks what reaches the host:
 *   the open (one context, the host's flags, the caps into the caller's room);
 *   the order: a PUT made after a draw runs after it, in the same block (Mesa's calls are first in, first out);
 *   a small PUT is copied (its memory may be written again at once), a big one is read in place and has run;
 *   a GET comes after everything made so far and is done when it returns;
 *   when a block goes: the caps of one block, a fence waited for, unbatched after every command;
 *   a stream longer than one piece goes in pieces that end where a command ends;
 *   refusals counted, a refused create known at once when unbatched;
 *   the end: unrefs after the streams, the context destroyed, nothing left on the host.
 * Under AddressSanitizer, built 32-bit with VG_HOST, as the pipe's other tests.
 *   tests/pipe/test_gla_carry.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/gfopengpu/pipe.h"
#include "../../library/pipe/virgl_cmd.h"
#include "gla_carry.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* A test stream is commands as virgl's are: a header with the words that follow in its top 16 bits, then those
 * words. The first of them is a mark the host checks, so a stream cut in the middle of a command shows. */
#define MARK 0x4D41524BUL

/* ---- the pretend host ------------------------------------------------------------------------------------------- */

#define HMAX 512
static struct {
    ULONG alive[HMAX]; int nalive;
    int blocks, bad;
    char order[1024];                   /* the last block: c(reate) p(ut) s(ubmit) u(nref) g(et) d(estroy) */
    char all[4096];                     /* every block since the last reset, '|' between them */
    ULONG put_addr, put_bytes, put_first;   /* the last PUT: where from, how much, its first byte as the host read it */
    ULONG put_box[9], put_swap;
    ULONG stream_words, streams, cmds, bad_cmds;   /* SUBMITs: words, count, commands, commands without the mark */
    ULONG max_stream;
    ULONG host_flags;
    LONG create_result, get_result;
    ULONG ctx_made, ctx_gone;
} H;

static int alive(ULONG h) { int i; for (i = 0; i < H.nalive; i++) if (H.alive[i] == h) return 1; return 0; }
static void ord(char c)
{
    size_t n = strlen(H.order);
    if (n + 1 < sizeof H.order) { H.order[n] = c; H.order[n + 1] = 0; }
    n = strlen(H.all);
    if (n + 1 < sizeof H.all) { H.all[n] = c; H.all[n + 1] = 0; }
}

int ogpu_pipe_xport_ready(void) { return 1; }
void *ogpu_pipe_xport_alloc(ULONG bytes) { return calloc(1, bytes ? bytes : 4); }
void ogpu_pipe_xport_free(void *p) { free(p); }

LONG ogpu_pipe_xport_run(ULONG *blk, ULONG bytes)
{
    ULONG at = 0, words = bytes / 4, i;
    H.blocks++;
    H.order[0] = 0;
    if (H.all[0]) ord('|');
    H.order[0] = 0;
    while (at < words) {
        ULONG hdr = VG_GETW(blk + at), cmd = ACV_HDR_CMD(hdr), n = ACV_HDR_WORDS(hdr);
        if (!n) break;
        VG_PUTW(blk + at + 1, 0);
        switch (cmd) {
        case ACV_HELLO: VG_PUTW(blk + at + 1, ACV_PROTOCOL); VG_PUTW(blk + at + 2, H.host_flags); break;
        case ACV_CTX_CREATE: H.ctx_made = VG_GETW(blk + at + 2); break;
        case ACV_CAPSET_INFO: VG_PUTW(blk + at + 3, 2); VG_PUTW(blk + at + 4, 32); break;
        case ACV_CAPS: {
            ULONG *caps = (ULONG *)(unsigned long)VG_GETW(blk + at + 4), nb = VG_GETW(blk + at + 5);
            for (i = 0; i < nb / 4 && i < 8; i++) VG_PUTW(caps + i, 0xCA000000UL + i);
            VG_PUTW(blk + at + 1, nb < 32 ? nb : 32);
            break;
        }
        case ACV_RES_CREATE: {
            ULONG h = VG_GETW(blk + at + 2);
            if (H.create_result) { VG_PUTW(blk + at + 1, (ULONG)H.create_result); H.create_result = 0; ord('C'); break; }
            if (alive(h)) H.bad++;
            if (H.nalive < HMAX) H.alive[H.nalive++] = h;
            ord('c');
            break;
        }
        case ACV_RES_UNREF: {
            ULONG h = VG_GETW(blk + at + 2);
            int k, found = 0;
            for (k = 0; k < H.nalive; k++) if (H.alive[k] == h) { H.alive[k] = H.alive[--H.nalive]; found = 1; break; }
            if (!found) H.bad++;
            ord('u');
            break;
        }
        case ACV_PUT:
            if (!alive(VG_GETW(blk + at + 2))) H.bad++;
            for (i = 0; i < 9; i++) H.put_box[i] = VG_GETW(blk + at + 4 + i);
            H.put_addr = VG_GETW(blk + at + 13); H.put_bytes = VG_GETW(blk + at + 14); H.put_swap = VG_GETW(blk + at + 15);
            H.put_first = H.put_bytes ? *(const UBYTE *)(unsigned long)H.put_addr : 0;
            ord('p');
            break;
        case ACV_GET:
            if (!alive(VG_GETW(blk + at + 2))) H.bad++;
            if (H.get_result) VG_PUTW(blk + at + 1, (ULONG)H.get_result);
            else memset((void *)(unsigned long)VG_GETW(blk + at + 13), 0x5A, VG_GETW(blk + at + 14));
            ord('g');
            break;
        case ACV_SUBMIT: {
            const ULONG *s = (const ULONG *)(unsigned long)VG_GETW(blk + at + 3);
            ULONG nw = VG_GETW(blk + at + 4), k = 0;
            H.streams++;
            H.stream_words += nw;
            if (nw > H.max_stream) H.max_stream = nw;
            while (k < nw) {
                ULONG len = VG_LE(s[k]) >> 16;
                H.cmds++;
                if (!len || k + 1 + len > nw || VG_LE(s[k + 1]) != MARK) { H.bad_cmds++; break; }
                k += 1 + len;
            }
            ord('s');
            break;
        }
        case ACV_CTX_DESTROY: H.ctx_gone = VG_GETW(blk + at + 2); ord('d'); break;
        default: VG_PUTW(blk + at + 1, (ULONG)-1);
        }
        at += n;
    }
    return 0;
}

static void host(void) { ULONG f = H.host_flags; memset(&H, 0, sizeof H); H.host_flags = f; }
static void fresh(void) { H.all[0] = 0; H.order[0] = 0; H.streams = H.stream_words = H.cmds = H.bad_cmds = H.max_stream = 0; }

/* ---- the caller's side: what the winsys does ---------------------------------------------------------------------- */

static int allocs;
static void *m_alloc(void *user, unsigned long bytes) { (void)user; allocs++; return calloc(1, bytes ? bytes : 4); }
static void m_free(void *user, void *p) { (void)user; if (p) allocs--; free(p); }
static const struct gla_carry_mem mem = { m_alloc, m_free, 0 };

/* a stream of n commands, each `len` words after its header */
static ULONG *stream(ULONG n, ULONG len, ULONG *words)
{
    ULONG *s = malloc(n * (len + 1) * 4), i, k;
    for (i = 0; i < n; i++) {
        ULONG *p = s + i * (len + 1);
        *p++ = VG_LE((len << 16) | 8);
        *p++ = VG_LE(MARK);
        for (k = 1; k < len; k++) *p++ = VG_LE(i);
    }
    *words = n * (len + 1);
    return s;
}

static const unsigned long tex_shape[10] = { 2, VG_FMT_B8G8R8A8_UNORM, VG_BIND_SAMPLER_VIEW, 64, 64, 1, 1, 0, 0, 0 };
static const unsigned long buf_shape[10] = { 0, VG_FMT_R8_UNORM, VG_BIND_VERTEX_BUFFER, 1 << 20, 1, 1, 1, 0, 0, 0 };

static void box(struct gla_carry_box *b, unsigned long w, unsigned long h)
{
    memset(b, 0, sizeof *b);
    b->w = w; b->h = h; b->d = 1;
}

int main(void)
{
    struct gla_carry *c;
    struct gla_carry_res *t, *v, *r[300];
    struct gla_carry_box b;
    static ULONG caps[16];
    static UBYTE small[256], big[128 * 1024];
    unsigned long flags = 0, blocks = 0, refused = 0, f0, f1;
    ULONG *s, words;
    int caps_ok = 0, status = -1, i, blocks0;

    /* ---- the open ---- */
    host();
    H.host_flags = ACV_HOST_PRESENT;
    for (i = 0; i < 16; i++) caps[i] = 0xDEF00000UL + (ULONG)i;
    c = gla_carry_open(&mem, 0x7000, -1, caps, sizeof caps, &flags, &caps_ok, &status);
    CHECK(c != 0 && status == GLA_CARRY_OK, "the carrier opens (%d)", status);
    if (!c) return 1;
    CHECK(H.ctx_made == 0x7000 && flags == ACV_HOST_PRESENT && caps_ok, "one context, the host's flags, the caps (%lx, %lx)", (unsigned long)H.ctx_made, flags);
    CHECK(VG_GETW(caps) == 0xCA000000UL && VG_GETW(caps + 7) == 0xCA000007UL && caps[8] == 0xDEF00008UL,
          "the caps the host has, in its room; the words it doesn't have left as they were");
    gla_carry_counts(c, &blocks, &refused);
    CHECK(blocks == (unsigned long)H.blocks && H.blocks == 2 && !refused, "the open's blocks counted (%lu, %d)", blocks, H.blocks);
    CHECK(H.nalive == 0, "no buffers of the pipe's own on the host");

    /* ---- the order: create, draw, PUT, draw; one block, as made ---- */
    fresh();
    v = gla_carry_res_create(c, buf_shape);
    CHECK(v != 0 && gla_carry_res_handle(v) != 0 && H.all[0] == 0, "a create queues");
    s = stream(3, 4, &words);
    CHECK(gla_carry_submit(c, s, words) == 0, "a stream queues");
    memset(s, 0xEE, words * 4);                 /* Mesa writes its command buffer again at once */
    free(s);
    memset(small, 0x11, sizeof small);
    box(&b, sizeof small, 1);
    b.x = 512;
    CHECK(gla_carry_put(c, v, &b, small, sizeof small, ACV_SWAP_32) == 0, "a small PUT queues");
    memset(small, 0x22, sizeof small);          /* ... and the resource's memory too */
    s = stream(2, 4, &words);
    gla_carry_submit(c, s, words);
    free(s);
    f0 = gla_carry_fence(c);
    CHECK(H.all[0] == 0, "nothing sent yet");
    gla_carry_flush(c);
    CHECK(!strcmp(H.all, "csps"), "one block, in the order made: create, draws, PUT, draws (%s)", H.all);
    CHECK(H.cmds == 5 && !H.bad_cmds && H.stream_words == 25, "the streams whole, copied before Mesa wrote over them (%lu commands, %lu bad)", (unsigned long)H.cmds, (unsigned long)H.bad_cmds);
    CHECK(H.put_first == 0x11 && H.put_addr != (ULONG)(unsigned long)small, "the PUT read the bytes as they were when it was made (a copy)");
    CHECK(H.put_box[3] == 512 && H.put_box[6] == sizeof small && H.put_bytes == sizeof small && H.put_swap == ACV_SWAP_32, "the PUT's box, bytes and units");
    gla_carry_counts(c, &blocks, &refused);
    CHECK(blocks == 1 && !refused && !H.bad, "one block counted, nothing refused");

    /* ---- fences ---- */
    blocks0 = H.blocks;
    gla_carry_wait(c, f0);
    CHECK(H.blocks == blocks0, "a fence whose block has run waits for nothing");
    f0 = gla_carry_fence(c);
    gla_carry_wait(c, f0);
    CHECK(H.blocks == blocks0, "a fence made with nothing queued: nothing to send");
    s = stream(1, 4, &words);
    gla_carry_submit(c, s, words);
    free(s);
    f1 = gla_carry_fence(c);
    CHECK(f1 == f0 + 1 && H.blocks == blocks0, "a fence on queued commands");
    gla_carry_wait(c, f1);
    CHECK(H.blocks == blocks0 + 1, "... waited for: they are sent");
    gla_carry_wait(c, f1);
    CHECK(H.blocks == blocks0 + 1, "... once");

    /* ---- a big PUT: read where it is, run at once with what is queued before it ---- */
    fresh();
    s = stream(1, 4, &words);
    gla_carry_submit(c, s, words);
    free(s);
    memset(big, 0x33, sizeof big);
    box(&b, sizeof big, 1);
    blocks0 = H.blocks;
    CHECK(gla_carry_put(c, v, &b, big, sizeof big, ACV_SWAP_32) == 0, "a big PUT");
    CHECK(H.blocks == blocks0 + 1 && !strcmp(H.all, "sp") && H.put_addr == (ULONG)(unsigned long)big && H.put_first == 0x33,
          "... has run when it returns, after the draw before it, from its own memory (%s)", H.all);

    /* ---- a GET: after everything made so far, done when it returns ---- */
    fresh();
    t = gla_carry_res_create(c, tex_shape);
    s = stream(1, 4, &words);
    gla_carry_submit(c, s, words);
    free(s);
    box(&b, 8, 8);
    b.stride = 32;
    memset(big, 0, 256);
    CHECK(gla_carry_get(c, t, &b, big, 256, ACV_SWAP_NONE) == 0 && big[0] == 0x5A && big[255] == 0x5A, "a GET: the pixels are there when it returns");
    CHECK(!strcmp(H.all, "csg"), "one block: the create, the draw, then the GET (%s)", H.all);
    H.get_result = -3;
    gla_carry_counts(c, &blocks, &refused);
    CHECK(gla_carry_get(c, t, &b, big, 256, ACV_SWAP_NONE) == -3, "a GET the host refuses says so");
    H.get_result = 0;
    gla_carry_counts(c, &blocks, &refused);
    CHECK(!refused, "... to its caller, not as a queued command's refusal (%lu)", refused);

    /* ---- one block's caps: 256 commands, then it goes by itself ---- */
    fresh();
    blocks0 = H.blocks;
    for (i = 0; i < 300; i++) r[i] = gla_carry_res_create(c, tex_shape);
    CHECK(H.blocks == blocks0 + 1 && strlen(H.all) == 256, "256 creates: a block of them went (%d, %lu)", H.blocks - blocks0, (unsigned long)strlen(H.all));
    for (i = 0; i < 300; i++) gla_carry_res_destroy(c, r[i]);
    gla_carry_flush(c);
    CHECK(H.nalive == 2 && !H.bad, "300 made and let go of: the two from before left (%d)", H.nalive);
    /* ... and 384 KB of data */
    fresh();
    blocks0 = H.blocks;
    memset(big, 0x44, sizeof big);
    box(&b, 60000, 1);
    for (i = 0; i < 7; i++) gla_carry_put(c, v, &b, big, 60000, ACV_SWAP_32);
    CHECK(H.blocks == blocks0 + 1 && !strcmp(H.all, "ppppppp"), "7 PUTs of 60,000 bytes: over the data cap, the block went (%s)", H.all);
    /* ... and the copies' arena: 17 of them need more than its megabyte */
    fresh();
    blocks0 = H.blocks;
    box(&b, 64 * 1024, 1);
    for (i = 0; i < 17; i++) gla_carry_put(c, v, &b, big, 64 * 1024, ACV_SWAP_32);
    gla_carry_flush(c);
    CHECK(H.put_first == 0x44 && !H.bad && strlen(H.all) >= 17 + 2, "17 copied PUTs of 64 KB: every one read whole (%s)", H.all);

    /* ---- a refusal in a queued command is counted when its block has run ---- */
    gla_carry_counts(c, &blocks, &refused);
    H.create_result = -3;
    r[0] = gla_carry_res_create(c, tex_shape);
    CHECK(r[0] != 0, "batched: a create the host will refuse still queues");
    gla_carry_flush(c);
    gla_carry_counts(c, &blocks, &refused);
    CHECK(refused == 1, "... and is counted (%lu)", refused);
    /* (the host never made it: its unref is refused too, which the host's own count shows) */
    gla_carry_res_destroy(c, r[0]);
    gla_carry_flush(c);
    CHECK(H.bad == 1, "the unref of what was never made");
    H.bad = 0;
    gla_carry_counts(c, &blocks, &refused);

    /* ---- a stream longer than one piece: cut where a command ends ---- */
    fresh();
    s = stream(70, 1000, &words);               /* 70,070 words; a piece is 65,536 at most */
    CHECK(gla_carry_submit(c, s, words) == 0, "a long stream");
    free(s);
    gla_carry_flush(c);
    CHECK(H.stream_words == 70070 && H.cmds == 70 && !H.bad_cmds && H.max_stream <= 65536 && H.streams >= 2,
          "all of it, in pieces of whole commands (%lu words, %lu commands, %lu bad, %lu streams)",
          (unsigned long)H.stream_words, (unsigned long)H.cmds, (unsigned long)H.bad_cmds, (unsigned long)H.streams);
    fresh();
    s = stream(1, 65535, &words);               /* the longest command there is: one piece exactly */
    CHECK(gla_carry_submit(c, s, words) == 0, "the longest command");
    free(s);
    gla_carry_flush(c);
    CHECK(H.stream_words == 65536 && H.cmds == 1 && !H.bad_cmds, "... whole (%lu)", (unsigned long)H.stream_words);

    /* ---- the end ---- */
    fresh();
    s = stream(1, 4, &words);
    gla_carry_submit(c, s, words);
    free(s);
    gla_carry_res_destroy(c, t);
    gla_carry_res_destroy(c, v);
    gla_carry_close(c);
    CHECK(!strcmp(H.all, "suu|d") && H.ctx_gone == 0x7000 && H.nalive == 0 && !H.bad, "the end: the draw, the unrefs after it, the context gone (%s)", H.all);
    CHECK(allocs == 0, "all the carrier's memory given back (%d)", allocs);

    /* ---- unbatched: the program's choice, or the runtime's ---- */
    for (i = 0; i < 3; i++) {
        /* 0: the program says no; 1: the runtime says no; 2: the runtime says no, the program says yes */
        int unbatched = i < 2;
        host();
        H.host_flags = i ? ACV_HOST_NO_BATCH : 0;
        c = gla_carry_open(&mem, 0x7100, i == 0 ? 0 : i == 1 ? -1 : 1, caps, sizeof caps, &flags, &caps_ok, &status);
        CHECK(c != 0, "the carrier opens (%d)", i);
        if (!c) return 1;
        blocks0 = H.blocks;
        fresh();
        v = gla_carry_res_create(c, buf_shape);
        s = stream(1, 4, &words);
        gla_carry_submit(c, s, words);
        free(s);
        box(&b, sizeof small, 1);
        gla_carry_put(c, v, &b, small, sizeof small, ACV_SWAP_32);
        if (unbatched) {
            CHECK(H.blocks == blocks0 + 3 && !strcmp(H.all, "c|s|p") && H.put_addr == (ULONG)(unsigned long)small,
                  "unbatched (%d): a block a command, the PUT from its own memory (%s)", i, H.all);
            H.create_result = -3;
            CHECK(gla_carry_res_create(c, tex_shape) == 0, "unbatched: a create the host refuses is known at once");
            H.bad = 0;                          /* its unref, of what the host never made */
        } else
            CHECK(H.blocks == blocks0 && H.all[0] == 0, "the program's yes over the runtime's no: batched");
        gla_carry_res_destroy(c, v);
        gla_carry_close(c);
        CHECK(H.nalive == 0 && !H.bad && allocs == 0, "nothing left (%d, %d, %d)", H.nalive, H.bad, allocs);
    }

    printf("gla carrier: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
