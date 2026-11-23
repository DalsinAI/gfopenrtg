/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The shared pipe's screen (library/pipe/pipe_screen.c, stage M2 of docs/design/OpenGPU-Shared-Pipe-Plan.md)
 * against a pretend host: the blocks it sends word for word (the ones minigl.library and Warp3D.library sent
 * before, so the host sees no change), what it reads back, and every way an open can fail.
 *
 * Built 32-bit with VG_HOST (the blocks in the Amiga's order), as the libraries' PC tests are; this file is the
 * pipe's transport (ogpu_pipe_xport_*), so no bridge is needed.
 *   tests/pipe/test_screen.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/gfopengpu/pipe.h"
#include "../../include/gfopengpu/virgl_cmd.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the pretend host ------------------------------------------------------------------------- */

static struct {
    int ready;                    /* the transport is there */
    int fail_block;               /* the block of this number (1, 2, ...) doesn't run; 0: all run */
    ULONG protocol;               /* what HELLO answers */
    LONG ctx_result;              /* what ACV_CTX_CREATE answers */
    ULONG flags;                  /* the host's flags */
    int set2;                     /* caps set 2 is there */
    LONG caps_result;             /* ACV_CAPS's answer: -1 for "the bytes asked for" */
    int blocks;                   /* blocks run */
    ULONG first[13];              /* the first block, as sent (in our order) */
    ULONG first_words;
    ULONG caps_asked_set, caps_asked_bytes;
    ULONG ctx_id, name_len;
    char name[32];
} H;

int ogpu_pipe_xport_ready(void) { return H.ready; }
void *ogpu_pipe_xport_alloc(ULONG bytes) { return calloc(1, bytes ? bytes : 4); }
void ogpu_pipe_xport_free(void *p) { free(p); }

LONG ogpu_pipe_xport_run(ULONG *blk, ULONG bytes)
{
    ULONG at = 0, words = bytes / 4, i;
    H.blocks++;
    if (H.blocks == 1) {
        H.first_words = words;
        for (i = 0; i < words && i < 13; i++) H.first[i] = VG_GETW(blk + i);
    }
    if (H.fail_block == H.blocks) return -1;
    while (at < words) {
        ULONG hdr = VG_GETW(blk + at), cmd = ACV_HDR_CMD(hdr), n = ACV_HDR_WORDS(hdr);
        if (!n) break;
        switch (cmd) {
        case ACV_HELLO:
            VG_PUTW(blk + at + 1, H.protocol);
            VG_PUTW(blk + at + 2, H.flags);
            break;
        case ACV_CTX_CREATE:
            H.ctx_id = VG_GETW(blk + at + 2);
            H.name_len = VG_GETW(blk + at + 4);
            memcpy(H.name, (const char *)(unsigned long)VG_GETW(blk + at + 3), H.name_len < 31 ? H.name_len : 31);
            VG_PUTW(blk + at + 1, (ULONG)H.ctx_result);
            break;
        case ACV_CAPSET_INFO: {
            ULONG set = VG_GETW(blk + at + 2), has = set == 1 || H.set2;
            VG_PUTW(blk + at + 1, 0);
            VG_PUTW(blk + at + 3, has ? set : 0);            /* max version */
            VG_PUTW(blk + at + 4, has ? (set == 2 ? 1024 : 308) : 0);   /* max size */
            break;
        }
        case ACV_CAPS: {
            ULONG *caps = (ULONG *)(unsigned long)VG_GETW(blk + at + 4), asked = VG_GETW(blk + at + 5);
            H.caps_asked_set = VG_GETW(blk + at + 2);
            H.caps_asked_bytes = asked;
            for (i = 0; i < asked / 4; i++) VG_PUTW(caps + i, 0x1000 + i);   /* word i of the caps */
            VG_PUTW(blk + at + 1, H.caps_result < 0 ? asked : (ULONG)H.caps_result);
            break;
        }
        default:
            VG_PUTW(blk + at + 1, (ULONG)-1);
        }
        at += n;
    }
    return 0;
}

static void host(void)
{
    memset(&H, 0, sizeof H);
    H.ready = 1; H.protocol = ACV_PROTOCOL; H.flags = ACV_HOST_GPU | ACV_HOST_PRESENT; H.set2 = 1; H.caps_result = -1;
}

/* ---- the opener ------------------------------------------------------------------------------- */

static ULONG blk[16], caps[256];
static char name[] = "minigl.library";

static OGPUPipeScreen *open_it(const struct OGPUPipeOps *ops, ULONG caps_bytes, struct OGPUPipeInfo *info)
{
    struct OGPUPipeOpen o;
    memset(blk, 0, sizeof blk); memset(caps, 0, sizeof caps);
    o.size = sizeof o; o.emit_wanted = OGPU_EMIT_VIRGL;
    o.ctx_id = 0x12345678; o.name = name; o.name_len = sizeof name - 1;
    o.blk = blk; o.caps = caps; o.caps_bytes = caps_bytes;
    return ops->screen_open(&o, info);
}

int main(void)
{
    const struct OGPUPipeOps *ops = ogpu_pipe_ops(OGPU_PIPE_VERSION);
    struct OGPUPipeInfo info;
    OGPUPipeScreen *s;
    ULONG word[64];

    /* the table */
    CHECK(ops && ops->version == OGPU_PIPE_VERSION, "the table, version %d", OGPU_PIPE_VERSION);
    CHECK(ogpu_pipe_ops(OGPU_PIPE_VERSION + 1) == 0, "no table for a later version");
    CHECK(ops->screen_open && ops->screen_close && ops->run, "M2's members are there");
    CHECK(ops->ctx_create && ops->res_create && ops->vtx_room && ops->end_frame && ops->arenas, "M3's members are there");
    CHECK(ops->blend_create && ops->bind_layout && ops->set_constants && ops->obj_new && ops->state_reset, "M4's members are there");
    CHECK(ops->ff_shader && ops->bind_shader && ops->ff_vs_consts, "M5's members are there");
    CHECK(ops->blend_create2 && ops->dsa_create2 && ops->sampler_create2 && ops->room && ops->buf_put, "M6's members are there");
    CHECK(!ops->bind_texture && !ops->draw && !ops->stats, "later stages' members are 0");

    /* an open: the blocks word for word as minigl.library and Warp3D.library sent them */
    host();
    s = open_it(ops, 256 * 4, &info);
    CHECK(s && info.status == OGPU_PIPE_OPEN_OK, "opens (%lu)", (unsigned long)info.status);
    CHECK(H.blocks == 2 && info.blocks == 2, "two blocks: the first and the caps (%d, %lu)", H.blocks, (unsigned long)info.blocks);
    CHECK(H.first_words == 13, "the first block is 13 words (%lu)", (unsigned long)H.first_words);
    word[0] = ACV_HDR(ACV_HELLO, 3); word[1] = 0; word[2] = 0;
    word[3] = ACV_HDR(ACV_CTX_CREATE, 5); word[4] = 0; word[5] = 0x12345678; word[6] = (ULONG)(unsigned long)name; word[7] = 14;
    word[8] = ACV_HDR(ACV_CAPSET_INFO, 5); word[9] = 0; word[10] = 2; word[11] = 0; word[12] = 0;
    CHECK(!memcmp(H.first, word, 13 * 4), "the first block: HELLO, ACV_CTX_CREATE, ACV_CAPSET_INFO 2");
    CHECK(H.ctx_id == 0x12345678 && H.name_len == 14 && !strcmp(H.name, "minigl.library"), "the context and its name");
    CHECK(H.caps_asked_set == 2 && H.caps_asked_bytes == 1024, "caps set 2, 1024 bytes (%lu, %lu)", (unsigned long)H.caps_asked_set, (unsigned long)H.caps_asked_bytes);
    CHECK(info.host_flags == (ACV_HOST_GPU | ACV_HOST_PRESENT), "the host's flags");
    CHECK(info.prim_mask == 0x1000 + 72 && info.caps_bits == 0x1000 + 98, "prim_mask is caps word 72, caps_bits word 98");
    CHECK(info.emit == OGPU_EMIT_VIRGL, "the virgl emitter");
    CHECK(ops->run(s, blk, 0) == 0 && H.blocks == 3, "the table's run is the transport");
    ops->screen_close(s);

    /* the caller's room bounds the caps (Warp3D: 1024 bytes; MiniGL: VG_CAPS_WORDS * 4) */
    host();
    s = open_it(ops, 200, &info);
    CHECK(s && H.caps_asked_bytes == 200, "at most the room is asked for (%lu)", (unsigned long)H.caps_asked_bytes);
    CHECK(info.prim_mask == 0x7F && info.caps_bits == 0, "short caps: prim_mask 0x7F, caps_bits 0");
    ops->screen_close(s);

    /* no set 2: set 1, in a block of its own */
    host(); H.set2 = 0;
    s = open_it(ops, 1024, &info);
    CHECK(s && H.blocks == 3 && info.blocks == 3, "set 1: three blocks (%d)", H.blocks);
    CHECK(H.caps_asked_set == 1 && H.caps_asked_bytes == 308, "caps set 1, 308 bytes (%lu, %lu)", (unsigned long)H.caps_asked_set, (unsigned long)H.caps_asked_bytes);
    CHECK(info.caps_bits == 0 && info.prim_mask == 0x1000 + 72, "set 1's 77 words: prim_mask, no caps_bits");
    ops->screen_close(s);

    /* the ways it fails */
    host(); H.ready = 0;
    CHECK(!open_it(ops, 1024, &info) && info.status == OGPU_PIPE_OPEN_NO_TRANSPORT && H.blocks == 0, "no transport: no block sent");
    host(); H.fail_block = 1;
    CHECK(!open_it(ops, 1024, &info) && info.status == OGPU_PIPE_OPEN_NO_RUN, "the first block doesn't run");
    host(); H.protocol = 2;
    CHECK(!open_it(ops, 1024, &info) && info.status == OGPU_PIPE_OPEN_PROTOCOL && info.detail == 2, "another ACVirgl");
    host(); H.ctx_result = -3;
    CHECK(!open_it(ops, 1024, &info) && info.status == OGPU_PIPE_OPEN_NO_CONTEXT && info.detail == -3, "the context refused");
    host(); H.set2 = 0; H.fail_block = 2;
    CHECK(!open_it(ops, 1024, &info) && info.status == OGPU_PIPE_OPEN_NO_RUN, "set 1's block doesn't run");
    host(); H.caps_result = 0;
    s = open_it(ops, 1024, &info);
    CHECK(s && info.status == OGPU_PIPE_OPEN_NO_CAPS && info.caps_bits == 0 && info.prim_mask == 0x7F,
          "no caps: open, the caller decides (MiniGL gives up, Warp3D goes on)");
    CHECK(info.host_flags == (ACV_HOST_GPU | ACV_HOST_PRESENT), "no caps: the flags are still there");
    ops->screen_close(s);
    host(); H.fail_block = 2;
    s = open_it(ops, 1024, &info);
    CHECK(s && info.status == OGPU_PIPE_OPEN_NO_CAPS, "the caps' block doesn't run: no caps");
    ops->screen_close(s);
    {
        struct OGPUPipeOpen o;
        memset(&o, 0, sizeof o);
        host();
        CHECK(!ops->screen_open(&o, &info) && H.blocks == 0, "an open with no size or room is refused");
        o.size = sizeof o; o.blk = blk; o.caps = caps; o.caps_bytes = 1024; o.emit_wanted = OGPU_EMIT_S3D;
        CHECK(!ops->screen_open(&o, &info) && info.status == OGPU_PIPE_OPEN_NO_TRANSPORT, "stream3d: not until M7");
    }
    ops->screen_close(0);

    printf("pipe screen: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
