/* Copyright (c) 2026 AmigaChrome Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * What a batch needs of a driver beyond its opcodes (opengpu.library 0.19's
 * driver_takes; plain C, so tests/test_sdl3.c checks it on the host).
 * An older driver takes a v1.2 command's opcode but not v1.7's variants of
 * it (SDL 3's blend modes, PIXELART, clamping one way, P010 and the new YUV
 * spaces), nor COMPOSITE_PD with v1.6's blend modes: those need a driver
 * that also answers PALETTE (v1.7) or PATH_FILL (v1.6), nor v1.8's aliased
 * paths unless it answers OGPU_QUERY_FLAG for them (OGPU_NEEDS_PATH_ALIASED). Nor does it take
 * v1.7's surface formats (INDEX8 as a slot, RGBA32, ABGR32): the library
 * keeps the formats a driver answers for SURFACE in. And TRIANGLES reads
 * more formats in v1.7 (targets and slot textures) than an older driver
 * draws or reads right, so ogpu_batch_takes follows each slot's format and
 * the target through the batch. */
#ifndef OGPU_VARIANT_H
#define OGPU_VARIANT_H

#include "../../include/opengpu/stream.h"
#include "../../include/opengpu/stream3d.h"

/* Word k after the header of the command at word `at` (len words, header
 * included), or 0 past its end. */
static unsigned long ogpu_arg_word(const unsigned char *s, unsigned long at, unsigned len, int k)
{
    const unsigned char *p = s + (at + 1 + (unsigned long)k) * 4;
    if ((unsigned long)k + 1 >= len) return 0;
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* v1.8: a flag a driver must take, as a pseudo-opcode of the ok[] table:
 * opengpu.library 0.24 sets ok[OGPU_NEEDS_PATH_ALIASED] when the driver
 * answers OGPU_QUERY_FLAG(OGPU_OP_PATH_FILL, OGPU_PATH_ALIASED_BIT) (stream.h);
 * a driver that predates the form answers OGPU_NONE, so its batches with an
 * aliased path run on the CPU core, which a v1.6 or v1.7 driver would draw
 * anti-aliased. (0.23 called it OGPU_NEEDS_CPU and set it for no driver.)
 * 0xF0-0xFE are kept for such flags; no opcode is above 0x8F. */
#define OGPU_NEEDS_PATH_ALIASED 0x00FE
#define OGPU_NEEDS_CPU          OGPU_NEEDS_PATH_ALIASED

/* The opcode a driver must also take for the command's variant, or 0. */
static unsigned ogpu_variant_needs(const unsigned char *s, unsigned long at, unsigned op, unsigned len)
{
    unsigned long f = 0, sp;
    switch (op) {
    case OGPU_OP_FILL_BLEND: case OGPU_OP_POINTS_BLEND: case OGPU_OP_LINES_BLEND:
        f = ogpu_arg_word(s, at, len, 3);
        return (f & OGPU_BLEND_MASK) >= OGPU_BLEND_BLEND_PREMUL ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_COMPOSITE:
        f = ogpu_arg_word(s, at, len, 6);
        return (f & OGPU_COMP_PIXELART) || ((f & OGPU_COMP_BLENDMODE) && OGPU_COMP_MODE_OF(f) >= OGPU_BLEND_BLEND_PREMUL)
               ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_COMPOSITE_AFFINE:
        f = ogpu_arg_word(s, at, len, 10);
        return (f & OGPU_AFF_PIXELART) || (f & OGPU_BLEND_MASK) >= OGPU_BLEND_BLEND_PREMUL ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_YUV: case OGPU_OP_YUV_SCALED:
        f = ogpu_arg_word(s, at, len, op == OGPU_OP_YUV ? 8 : 7);
        sp = (f >> 8) & 255;
        return (f & 255) == OGPU_YUV_P010 || sp > OGPU_YUV_JPEG ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_TRIANGLES:
        if (len != 9) return 0;                 /* the 3D form: no v1.7 variants */
        f = ogpu_arg_word(s, at, len, 7);
        return (f & (OGPU_TRI_PIXELART | OGPU_TRI_CLAMP_U | OGPU_TRI_CLAMP_V))
               || (f & OGPU_TRI_BLEND_MASK) >= OGPU_TRI_BLEND_PREMUL ? OGPU_OP_PALETTE : 0;
    case OGPU_OP_COMPOSITE_PD:
        return (ogpu_arg_word(s, at, len, 0) & 255) >= OGPU_BLEND_MULTIPLY ? OGPU_OP_PATH_FILL : 0;
    case OGPU_OP_PATH_FILL: case OGPU_OP_PATH_STROKE:
        return ogpu_arg_word(s, at, len, 0) & OGPU_PATH_ALIASED ? OGPU_NEEDS_PATH_ALIASED : 0;
    }
    return 0;
}

/* The target formats TRIANGLES' SDL form drew into before v1.7, and the slot
 * texture formats both forms read right (CLUT8 as grey, its pen). */
static int ogpu_tri_v16_target(unsigned long f)
{
    return f == OGPU_FMT_ARGB32 || f == OGPU_FMT_RGB565 || f == OGPU_FMT_CLUT8;
}

static int ogpu_tri_v16_texture(unsigned long f)
{
    return f == OGPU_FMT_ARGB32 || f == OGPU_FMT_RGB565 || f == OGPU_FMT_A8 || f == OGPU_FMT_CLUT8;
}

/* Whether a driver takes a whole batch: ok[op] says it carries out op (for
 * some format), fmt[f] that it answers SURFACE in format f. A bad length
 * goes to the driver, which says so as the CPU would. A slot or target the
 * batch doesn't set itself is taken as before: each batch starts with none
 * on the CPU, and SDL's renderers set them in each batch. */
static int ogpu_batch_takes(const unsigned char *s, unsigned long words, const unsigned char *ok, const unsigned char *fmt)
{
    unsigned char slot_fmt[OGPU_MAX_SLOTS];
    unsigned long at = 0, target = OGPU_MAX_SLOTS;
    int i;
    for (i = 0; i < OGPU_MAX_SLOTS; i++) slot_fmt[i] = 0;
    while (at < words) {
        unsigned op = (unsigned)s[at * 4] << 8 | s[at * 4 + 1], len = (unsigned)s[at * 4 + 2] << 8 | s[at * 4 + 3], need;
        if (op > 255 || !ok[op]) return 0;
        if (!len || at + len > words) return 1;
        need = ogpu_variant_needs(s, at, op, len);
        if (need && !ok[need]) return 0;
        if (op == OGPU_OP_SURFACE && len >= 6) {
            unsigned long slot = ogpu_arg_word(s, at, len, 0), f = ogpu_arg_word(s, at, len, 4);
            if (f > 0 && f < OGPU_FMT_COUNT && !fmt[f]) return 0;
            if (slot < OGPU_MAX_SLOTS) slot_fmt[slot] = (unsigned char)(f < OGPU_FMT_COUNT ? f : 0);
        } else if (op == OGPU_OP_TARGET && len >= 2) {
            target = ogpu_arg_word(s, at, len, 0);
        } else if (op == OGPU_OP_TRIANGLES && (len == 9 || len == 10) && !ok[OGPU_OP_PALETTE]) {
            unsigned long tex = ogpu_arg_word(s, at, len, 6);
            if (tex < OGPU_MAX_SLOTS && slot_fmt[tex] && !ogpu_tri_v16_texture(slot_fmt[tex])) return 0;
            if (len == 9 && target < OGPU_MAX_SLOTS && slot_fmt[target] && !ogpu_tri_v16_target(slot_fmt[target])) return 0;
        }
        at += len;
    }
    return 1;
}

/* ---- 0.22: a batch the driver can't take whole, split ---------------------
 * Before 0.22 one command the driver lacked sent the whole batch to the 68k,
 * after waiting for every earlier driver batch. Now the batch is cut into
 * runs: those the driver takes go to it, the rest run on the CPU, in order.
 * Each run is preceded by the batch's state commands from before it
 * (SURFACE, TARGET, CLIP, PALETTE), so it draws where the whole batch would
 * have. Only 2D batches are split: one with a 3D or GPU API command, a bad
 * length, or a state command the driver lacks runs whole on the CPU as
 * before. */
struct ogpu_split_state {
    unsigned char slot_fmt[OGPU_MAX_SLOTS];
    unsigned long target;
};

static inline void ogpu_split_init(struct ogpu_split_state *st)
{
    int i;
    for (i = 0; i < OGPU_MAX_SLOTS; i++) st->slot_fmt[i] = 0;
    st->target = OGPU_MAX_SLOTS;
}

static inline int ogpu_is_state_op(unsigned op)
{
    return op == OGPU_OP_NOP || op == OGPU_OP_SURFACE || op == OGPU_OP_TARGET || op == OGPU_OP_CLIP
           || op == OGPU_OP_PALETTE;
}

/* ogpu_batch_takes for one well-formed command at `at`, following the slots'
 * formats and the target in st: 1 when the driver takes it. */
static inline int ogpu_cmd_takes(struct ogpu_split_state *st, const unsigned char *s, unsigned long at, unsigned op,
                          unsigned len, const unsigned char *ok, const unsigned char *fmt)
{
    unsigned need;
    int takes = 1;
    if (op > 255 || !ok[op]) takes = 0;
    need = ogpu_variant_needs(s, at, op, len);
    if (need && !ok[need]) takes = 0;
    if (op == OGPU_OP_SURFACE && len >= 6) {
        unsigned long slot = ogpu_arg_word(s, at, len, 0), f = ogpu_arg_word(s, at, len, 4);
        if (f > 0 && f < OGPU_FMT_COUNT && !fmt[f]) takes = 0;
        if (slot < OGPU_MAX_SLOTS) st->slot_fmt[slot] = (unsigned char)(f < OGPU_FMT_COUNT ? f : 0);
    } else if (op == OGPU_OP_TARGET && len >= 2) {
        st->target = ogpu_arg_word(s, at, len, 0);
    } else if (op == OGPU_OP_TRIANGLES && (len == 9 || len == 10) && !ok[OGPU_OP_PALETTE]) {
        unsigned long tex = ogpu_arg_word(s, at, len, 6);
        if (tex < OGPU_MAX_SLOTS && st->slot_fmt[tex] && !ogpu_tri_v16_texture(st->slot_fmt[tex])) takes = 0;
        if (len == 9 && st->target < OGPU_MAX_SLOTS && st->slot_fmt[st->target] && !ogpu_tri_v16_target(st->slot_fmt[st->target])) takes = 0;
    }
    return takes;
}

/* 1 when the batch may be split (above). */
static inline int ogpu_batch_splittable(const unsigned char *s, unsigned long words, const unsigned char *ok, const unsigned char *fmt)
{
    struct ogpu_split_state st;
    unsigned long at = 0;
    ogpu_split_init(&st);
    while (at < words) {
        unsigned op = (unsigned)s[at * 4] << 8 | s[at * 4 + 1], len = (unsigned)s[at * 4 + 2] << 8 | s[at * 4 + 3];
        if (!len || at + len > words) return 0;
        if ((op >= 0x0030 && op <= 0x003F) || (op >= OGPU_OP_EXT_FIRST && op <= OGPU_OP_EXT_LAST) || op > 255) return 0;
        if (!ogpu_cmd_takes(&st, s, at, op, len, ok, fmt) && ogpu_is_state_op(op)) return 0;
        at += len;
    }
    return 1;
}

/* The runs of a splittable batch, for each(user, start, end, driver): words
 * [start, end) of the batch, driver 1 for the driver's runs. Every state
 * command belongs to the run it is in; a batch of state commands alone is
 * one driver run. Returns the number of runs, or stops at (and returns)
 * the first negative each() returns. */
static inline long ogpu_batch_runs(const unsigned char *s, unsigned long words, const unsigned char *ok, const unsigned char *fmt,
                            long (*each)(void *user, unsigned long start, unsigned long end, int driver), void *user)
{
    struct ogpu_split_state st;
    unsigned long at = 0, start = 0;
    int side = -1;
    long runs = 0, r;
    ogpu_split_init(&st);
    while (at < words) {
        unsigned op = (unsigned)s[at * 4] << 8 | s[at * 4 + 1], len = (unsigned)s[at * 4 + 2] << 8 | s[at * 4 + 3];
        int t = ogpu_cmd_takes(&st, s, at, op, len, ok, fmt);
        if (!ogpu_is_state_op(op)) {
            if (side < 0) {
                side = t;
            } else if (t != side) {
                if ((r = each(user, start, at, side)) < 0) return r;
                runs++;
                start = at;
                side = t;
            }
        }
        at += len;
    }
    if ((r = each(user, start, words, side < 0 ? 1 : side)) < 0) return r;
    return runs + 1;
}

#endif
