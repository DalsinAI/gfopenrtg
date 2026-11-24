/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * OGPU_Query's capabilities (library/opengpu/ogpu_caps.c, stage MQ of docs/design/OpenGPU-Shared-Pipe-Plan.md)
 * against a pretend host: the blocks word for word, discovery once however many questions come, a host reset,
 * every way discovery can fail, and every answer docs/design/OGPU-Query.md writes down, FULL, PARTIAL and NONE,
 * for each back end.
 *
 * Built 32-bit with VG_HOST (the blocks in the Amiga's order), as the pipe's tests are; it also builds 64-bit
 * (x86-64, ARM64: GF-91's native modules), since the caps' address is one the pretend host maps.
 *   tests/caps/test_caps.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../library/opengpu/ogpu_caps.h"
#include "../../include/gfopengpu/caps.h"
#include "../../include/gfopengpu/stream3d.h"
#include "../../library/pipe/virgl_cmd.h"

static int failures, checks;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---- the pretend host ------------------------------------------------------------------------------------------ */

static struct {
    int fail_block;               /* this block (1, 2, ...) doesn't run */
    ULONG protocol, flags;
    int set1, set2;               /* the caps sets it has */
    LONG caps_result;             /* -1: the bytes asked for */
    ULONG caps[300];              /* what it writes, by word */
    int blocks;
    ULONG first[8], first_words, caps_set, caps_bytes;
    ULONG *room;                  /* where ROOM_AT reaches: the struct's caps (its address is the gate's business) */
} H;

/* The Amiga address the test gives for the caps: the pretend host maps it, as a native module's gate would, so the
 * test runs the same on a 32-bit build, a 64-bit one and an m68k one. */
#define ROOM_AT 0x00F00000u

static LONG run(void *ctx, ULONG *blk, ULONG bytes)
{
    ULONG at = 0, words = bytes / 4, i;
    (void)ctx;
    H.blocks++;
    if (H.blocks == 1) {
        H.first_words = words;
        for (i = 0; i < words && i < 8; i++) H.first[i] = VG_GETW(blk + i);
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
        case ACV_CAPSET_INFO: {
            ULONG set = VG_GETW(blk + at + 2), has = (set == 1 && H.set1) || (set == 2 && H.set2);
            VG_PUTW(blk + at + 1, 0);
            VG_PUTW(blk + at + 3, has ? set : 0);
            VG_PUTW(blk + at + 4, has ? (set == 2 ? 1024 : 308) : 0);
            break;
        }
        case ACV_CAPS: {
            ULONG *room = VG_GETW(blk + at + 4) == ROOM_AT ? H.room : 0, asked = VG_GETW(blk + at + 5);
            if (!room) { VG_PUTW(blk + at + 1, (ULONG)-1); break; }   /* an address it can't reach */
            H.caps_set = VG_GETW(blk + at + 2);
            H.caps_bytes = asked;
            for (i = 0; i < asked / 4; i++) VG_PUTW(room + i, H.caps[i]);
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

static struct ogpu_caps *the_caps;

static void fmt_bit(ULONG mask_at, ULONG vf) { H.caps[mask_at + vf / 32] |= 1u << (vf % 32); }

/* a GPU host on desktop GL with the usual formats */
static void host(ULONG flags)
{
    memset(&H, 0, sizeof H);
    H.protocol = ACV_PROTOCOL; H.flags = flags; H.set1 = H.set2 = 1; H.caps_result = -1;
    H.room = the_caps->caps;
    /* sampler: B8G8R8A8, B8G8R8X8, B5G6R5, B5G5R5A1, B4G4R4A4, A8, L8, L8A8, R8G8B8A8 (not A8R8G8B8) */
    fmt_bit(1, 1); fmt_bit(1, 2); fmt_bit(1, 7); fmt_bit(1, 5); fmt_bit(1, 6);
    fmt_bit(1, 10); fmt_bit(1, 9); fmt_bit(1, 12); fmt_bit(1, 67);
    /* render: B8G8R8A8, B8G8R8X8, B5G6R5 */
    fmt_bit(17, 1); fmt_bit(17, 2); fmt_bit(17, 7);
    /* depth and stencil: Z16, Z24X8, Z24S8 (no Z32, no S8 alone) */
    fmt_bit(33, 16); fmt_bit(33, 21); fmt_bit(33, 19);
    H.caps[72] = 0x7FF; H.caps[89] = 32; H.caps[98] = 0x55; H.caps[121] = 16384;
}

/* the renderer string in caps set 2 (bytes 696-759), as virglrenderer fills it on a little-endian host: the
 * host's ACV_CAPS puts each word in the Amiga's order, so a word's value is its four bytes read little-endian */
static void renderer(const char *name)
{
    ULONG k;
    for (k = 0; k < 64; k++) H.caps[174 + k / 4] &= ~(0xFFu << (8 * (k % 4)));
    for (k = 0; k < 63 && name[k]; k++) H.caps[174 + k / 4] |= (ULONG)(unsigned char)name[k] << (8 * (k % 4));
}

#define ANS(q)  OGPU_ANSWER(q)
#define BE(q)   OGPU_BACKEND(q)
#define GPU(q)  OGPU_ONGPU(q)

int main(void)
{
    static struct ogpu_caps c;
    ULONG q, word[8];
    int i;
    the_caps = &c;

    /* ---- discovery: the blocks, once ---- */
    host(ACV_HOST_GPU | ACV_HOST_PRESENT);
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(c.looked && c.virgl && c.set == 2, "found virgl with caps set 2");
    CHECK(H.blocks == 2, "two blocks: HELLO with the set's size, then the caps (%d)", H.blocks);
    CHECK(H.first_words == 8, "the first block is 8 words (%lu)", (unsigned long)H.first_words);
    word[0] = ACV_HDR(ACV_HELLO, 3); word[1] = 0; word[2] = 0;
    word[3] = ACV_HDR(ACV_CAPSET_INFO, 5); word[4] = 0; word[5] = 2; word[6] = 0; word[7] = 0;
    CHECK(!memcmp(H.first, word, sizeof word), "the first block: HELLO, ACV_CAPSET_INFO 2 (no context)");
    CHECK(H.caps_set == 2 && H.caps_bytes == OGPU_CAPS_WORDS * 4, "set 2, bounded by the cache (%lu bytes)", (unsigned long)H.caps_bytes);
    CHECK(c.host_flags == (ACV_HOST_GPU | ACV_HOST_PRESENT) && c.words == OGPU_CAPS_WORDS, "the flags and %d words", OGPU_CAPS_WORDS);
    CHECK(c.discoveries == 1, "one discovery");
    for (i = 0; i < 1000; i++) {
        ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
        (void)ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32);
    }
    CHECK(c.discoveries == 1 && H.blocks == 2, "a thousand questions later: still one HELLO (%lu, %d blocks)",
          (unsigned long)c.discoveries, H.blocks);

    /* a host reset: the next question looks again */
    ogpu_caps_reset(&c);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(c.discoveries == 2 && H.blocks == 4 && c.virgl, "after a reset: one more discovery");

    /* ---- back ends ---- */
    host(ACV_HOST_GPU | ACV_HOST_PRESENT);
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    q = ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_REAL_GPU, 0);
    CHECK(ANS(q) == OGPU_FULL && BE(q) == 1 && GPU(q), "a real GPU: FULL from the driver, on the GPU");
    q = ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_LLVM_PIPE, 0);
    CHECK(ANS(q) == OGPU_NONE && BE(q) == OGPU_BACKEND_CPU, "no llvmpipe on a GPU host: NONE (the CPU's word)");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_VIRGL, OGPU_OP_LLVM_PIPE, 0)) == OGPU_NONE, "virgl on a GPU says NONE to LLVM_PIPE");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_VIRGL_PRESENT, 0)) == OGPU_FULL, "ACV_PRESENT");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_VIRGL_GLES, 0)) == OGPU_NONE, "not a GLES host");
    q = ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_CPU_CORE, 0);
    CHECK(ANS(q) == OGPU_FULL && BE(q) == OGPU_BACKEND_CPU && !GPU(q), "the 68k core: FULL, from the CPU");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_LLVMPIPE, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_NONE,
          "OGPU_DRV_LLVMPIPE on a GPU host: NONE to everything");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_ACRTG, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_NONE,
          "the driver's stream route has no capability ops");
    CHECK(ANS(ogpu_caps_answer(&c, 99, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_NONE, "an unknown back end: NONE");

    /* ---- textures: FULL where the host samples it as it is, PARTIAL where the pipe converts ---- */
    q = ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32);
    CHECK(ANS(q) == OGPU_FULL && BE(q) == 1 && GPU(q), "ARGB32 texture: FULL on the GPU");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_RGB565)) == OGPU_FULL, "RGB565: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_RGBA4444)) == OGPU_FULL, "RGBA4444: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_L8)) == OGPU_FULL, "L8: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_LA88)) == OGPU_FULL, "LA88: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_RGBA32)) == OGPU_FULL, "RGBA32: FULL (R8G8B8A8)");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_BGRA32)) == OGPU_PARTIAL,
          "BGRA32: PARTIAL (no A8R8G8B8, converted to B8G8R8A8)");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ABGR32)) == OGPU_PARTIAL, "ABGR32: PARTIAL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_CLUT8)) == OGPU_PARTIAL, "CLUT8: PARTIAL (its palette)");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, 200)) == OGPU_NONE, "an unknown format: NONE");

    /* render targets and depth */
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_RENDER_TARGET, OGPU_FMT_RGB565)) == OGPU_FULL, "an RGB565 target: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_RENDER_TARGET, OGPU_FMT_RGBA4444)) == OGPU_PARTIAL, "an RGBA4444 target: PARTIAL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_DEPTH_STENCIL, OGPU_FMT_Z16)) == OGPU_FULL, "Z16: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_DEPTH_STENCIL, OGPU_FMT_Z24)) == OGPU_FULL, "Z24: FULL (Z24X8)");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_DEPTH_STENCIL, OGPU_FMT_Z32)) == OGPU_PARTIAL, "Z32: PARTIAL (8 bits fewer)");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_DEPTH_STENCIL, OGPU_FMT_Z24S8)) == OGPU_FULL, "Z24S8: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_DEPTH_STENCIL, OGPU_FMT_S8)) == OGPU_PARTIAL, "S8: PARTIAL (inside Z24S8)");

    /* fixed function and the rest */
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_FF_MULTITEXTURE, 0)) == OGPU_FULL, "multitexture: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_FF_CHROMA, 0)) == OGPU_PARTIAL, "chroma: PARTIAL (one key, not a range)");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_LOGIC_OP, 0)) == OGPU_FULL, "logic ops on desktop GL: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_VIRGL, OGPU_OP_PRESENT_WINDOW, 0)) == OGPU_NONE, "presentation: NONE until M8");

    /* limits */
    CHECK(ogpu_caps_limit(&c, OGPU_OP_MAX_TEXTURE_SIZE) == 16384, "max texture: caps word 121");
    CHECK(ogpu_caps_limit(&c, OGPU_OP_MAX_VERTEX_ATTRIBS) == 32, "vertex attributes: caps word 89");
    CHECK(ogpu_caps_limit(&c, OGPU_OP_MAX_TEXTURE_UNITS) == 8, "texture units: the pipe's 8");
    CHECK(ogpu_caps_limit(&c, 0x80FF) == 0, "an unknown limit: 0");

    /* ---- llvmpipe on GLES ---- */
    host(ACV_HOST_GLES);
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    q = ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32);
    CHECK(ANS(q) == OGPU_FULL && BE(q) == 1 && !GPU(q), "llvmpipe: FULL from the driver, not on a GPU");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_LLVMPIPE, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_FULL, "OGPU_DRV_LLVMPIPE answers");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_VIRGL, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_NONE, "OGPU_DRV_VIRGL doesn't");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_VIRGL_GLES, 0)) == OGPU_FULL, "GLES host");
    q = ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_LOGIC_OP, 0);
    CHECK(ANS(q) == OGPU_FULL && BE(q) == OGPU_BACKEND_CPU, "no logic ops on GLES: the CPU core's answer instead");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_LLVMPIPE, OGPU_OP_LOGIC_OP, 0)) == OGPU_NONE, "llvmpipe itself: NONE to logic ops");

    /* ---- the CPU alone: no virgl ---- */
    host(0);
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 0, 0, 0, ROOM_AT);
    CHECK(c.looked && !c.virgl && H.blocks == 0 && c.discoveries == 0, "no virgl: nothing sent");
    q = ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32);
    CHECK(ANS(q) == OGPU_FULL && BE(q) == OGPU_BACKEND_CPU && !GPU(q), "ARGB32 texture on the 68k core: FULL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_RGBA4444)) == OGPU_PARTIAL, "RGBA4444 on the core: PARTIAL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_Z16)) == OGPU_NONE, "Z16 as a texture: NONE");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_DEPTH_STENCIL, OGPU_FMT_Z24S8)) == OGPU_PARTIAL, "Z24S8 on the core: PARTIAL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_RENDER_TARGET, OGPU_FMT_CLUT8)) == OGPU_PARTIAL, "a CLUT8 target: PARTIAL");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_FF_LIGHTING, 0)) == OGPU_NONE, "lighting on the core: NONE");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TGSI, 0)) == OGPU_NONE, "TGSI on the core: NONE");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_REAL_GPU, 0)) == OGPU_NONE, "no GPU");
    CHECK(ogpu_caps_limit(&c, OGPU_OP_MAX_TEXTURE_SIZE) == 2048 && ogpu_caps_limit(&c, OGPU_OP_MAX_TEXTURE_UNITS) == OGPU_TEX_UNITS,
          "the core's limits: 2048, OGPU_TEX_UNITS units");
    ogpu_caps_init(&c, 0);
    ogpu_caps_discover(&c, 0, 0, 0, ROOM_AT);
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_CPU_CORE, 0)) == OGPU_NONE
          && ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_NONE
          && ogpu_caps_limit(&c, OGPU_OP_MAX_TEXTURE_SIZE) == 0, "a build with no 3D core: NONE and 0");

    /* ---- failures: nothing promised, and no second try until a reset ---- */
    host(ACV_HOST_GPU);
    H.protocol = 99;
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(!c.virgl && c.discoveries == 1, "a host on another protocol: no virgl");
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(H.blocks == 1, "and not asked again");
    CHECK(BE(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_BACKEND_CPU, "the CPU answers instead");

    host(ACV_HOST_GPU);
    H.fail_block = 1;
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(!c.virgl, "the first block doesn't run: no virgl");

    host(ACV_HOST_GPU);
    H.fail_block = 2;
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(c.virgl && !c.set, "the caps block doesn't run: virgl, no caps");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_REAL_GPU, 0)) == OGPU_FULL, "the flags still answer");
    CHECK(BE(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_BACKEND_CPU,
          "but no format is promised from virgl");
    CHECK(ogpu_caps_limit(&c, OGPU_OP_MAX_TEXTURE_SIZE) == 2048, "and the limits are the core's");

    host(ACV_HOST_GPU);
    H.caps_result = 0;
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(c.virgl && !c.set, "ACV_CAPS answers 0 bytes: no caps");

    /* set 1 only: three blocks, and no limits from it */
    host(ACV_HOST_GPU);
    H.set2 = 0;
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(c.set == 1 && H.blocks == 3 && H.caps_set == 1 && H.caps_bytes == 308, "set 1: three blocks, 308 bytes");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_TEXTURE_2D, OGPU_FMT_ARGB32)) == OGPU_FULL, "set 1's masks answer");
    CHECK(c.words == 77, "set 1 is 77 words (%lu)", (unsigned long)c.words);
    CHECK(ogpu_caps_limit(&c, OGPU_OP_MAX_TEXTURE_SIZE) == 0 && ogpu_caps_limit(&c, OGPU_OP_MAX_VERTEX_ATTRIBS) == 16,
          "set 1: no texture size (word 121 is past it: 0, not known), attributes GL 3's least");

    host(ACV_HOST_GPU);
    H.set1 = H.set2 = 0;
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(c.virgl && !c.set && H.blocks == 2, "no caps sets at all: two blocks, no caps asked");

    /* ---- a host that leaves ACV_HOST_GPU out of HELLO: the renderer string says it ---- */
    host(ACV_HOST_PRESENT);
    renderer("AMD Radeon RX 460 Graphics (radeonsi, polaris11, ACO, DRM 3.61)");
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(ogpu_caps_renderer_on_chip(&c) == 1, "a Radeon's renderer string: a graphics chip");
    CHECK(c.host_flags == (ACV_HOST_GPU | ACV_HOST_PRESENT), "HELLO without ACV_HOST_GPU, a chip's renderer: GPU (%lx)",
          (unsigned long)c.host_flags);
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_REAL_GPU, 0)) == OGPU_FULL, "... and REAL_GPU answers FULL");
    host(ACV_HOST_PRESENT);
    renderer("llvmpipe (LLVM 19.1.7, 256 bits)");
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(ogpu_caps_renderer_on_chip(&c) == 0 && c.host_flags == ACV_HOST_PRESENT, "llvmpipe: no chip");
    CHECK(ANS(ogpu_caps_answer(&c, OGPU_DRV_BEST, OGPU_OP_LLVM_PIPE, 0)) == OGPU_FULL, "... and LLVM_PIPE answers FULL");
    host(ACV_HOST_PRESENT);
    renderer("zink Vulkan 1.3(llvmpipe (LLVM 19.1.7, 256 bits) (MESA_LLVMPIPE))");
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(ogpu_caps_renderer_on_chip(&c) == 0 && !(c.host_flags & ACV_HOST_GPU), "zink on lavapipe: no chip");
    host(ACV_HOST_PRESENT);
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(ogpu_caps_renderer_on_chip(&c) == -1 && c.host_flags == ACV_HOST_PRESENT, "no renderer string: as HELLO said");
    host(ACV_HOST_PRESENT);
    renderer("AMD Radeon RX 460 Graphics");
    H.set2 = 0;
    ogpu_caps_init(&c, 1);
    ogpu_caps_discover(&c, 1, run, 0, ROOM_AT);
    CHECK(c.set == 1 && c.host_flags == ACV_HOST_PRESENT, "caps set 1 has no renderer string: as HELLO said");

    printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
