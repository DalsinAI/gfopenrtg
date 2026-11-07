/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_HL_H
#define HOSTLIB_HL_H
/* hostlib: one C source, two builds (HOSTLIB-ABI.md, docs/design/host-run-libraries.md).
 * Copyright (c) 2026 AmiCradle Team. MIT.
 *
 * A hostlib function is portable C that reaches guest memory only through
 * the accessors below, by 32-bit guest addresses (hl_addr). It is built
 * twice from the same file:
 *   - for the 68k (the default), into the library that calls it: the
 *     accessors are plain loads and stores, and HL_TAG puts the ACMF tag
 *     (common/amiga/ac_magic.h) in front of the function, so AmiCradle's
 *     JIT may run the host build in its place and every other 68k runs
 *     this code. Nothing here needs the runtime; the library runs anywhere.
 *   - for the host (HL_HOST), into the runtime (gfosmotherboards
 *     amiga-native-bodies, which vendors hostlib/ by commit): the accessors
 *     read and write guest memory big-endian, and only inside the ranges
 *     the function declared with HL_NEED_R and HL_NEED_W.
 *
 * Every function:
 *   HL_TAG(name, id)                 the tag, once, before the function
 *   HL_FN(name) { ... }              uint32_t name(hl_addr block): one
 *                                    parameter block in guest memory, big-
 *                                    endian longs, and a 32-bit result
 *   HL_NEED_R(at, n), HL_NEED_W(at, n)  every range it will read or write,
 *                                    declared before any access to it; the
 *                                    host build declines the call (the 68k
 *                                    code runs it) when a range isn't plain
 *                                    memory, wraps or is too big
 *   HL_DECLARED()                    after the last HL_NEED_W and before the
 *                                    first write (verification's dry run
 *                                    stops here, with the ranges known)
 *   if (...) HL_DECLINE();           an input the host leaves to the 68k
 *                                    code (which goes on as it always has)
 * The rules are HOSTLIB-ABI.md's, section 3.
 */
#include <stdint.h>

typedef uint32_t hl_addr;

#ifndef HL_HOST
/* ---- the 68k build ---------------------------------------------------- */
/* the tag's header: amicradle-guest's own; in a repository that vendors
 * hostlib/ (hostlib/sync.sh), the copy beside this file (-DHL_VENDORED) */
#ifdef HL_VENDORED
#include "ac_magic.h"
#else
#include "../common/amiga/ac_magic.h"
#endif
#define HL_TAG(name, id) uint32_t name(hl_addr block); AC_MAGIC(name, id)
#define HL_FN(name) AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(name)(hl_addr block)
#define HL_NEED_R(at, n) ((void)0)
#define HL_NEED_W(at, n) ((void)0)
#define HL_DECLARED() ((void)0)
#define HL_DECLINE() ((void)0)
#define HL_RD8(a) (*(const uint8_t *)(uintptr_t)(hl_addr)(a))
#define HL_RD16(a) (*(const uint16_t *)(uintptr_t)(hl_addr)(a))
#define HL_RD32(a) (*(const uint32_t *)(uintptr_t)(hl_addr)(a))
#define HL_WR8(a, v) (*(uint8_t *)(uintptr_t)(hl_addr)(a) = (uint8_t)(v))

#else
/* ---- the host build --------------------------------------------------- *
 * The file that includes a hostlib source for the host defines, first:
 *   uint8_t *hl_host_mem(void)                     guest address 0 on the host
 *   int hl_host_plain(hl_addr at, uint32_t n, int write)   1: all of it RAM (or ROM, to read)
 *   int hl_host_aliased(hl_addr at, uint32_t n)    1: some of it may show other memory
 *   void hl_host_written(hl_addr at, uint32_t n)   about to be written (the JIT's code under it)
 */
#define HL_RANGES 8
/* why the host gave a call back to the 68k code (jit_bodies.h's MD_* numbers) */
enum { HL_OK = 0, HL_WHY_MEMORY = 4, HL_WHY_SIZE = 5, HL_WHY_OVERLAP = 6, HL_WHY_INPUT = 8 };
#ifndef HL_MAX
#define HL_MAX (16u << 20)              /* the most one range may be (jit_bodies.h's MAGIC_MAX) */
#endif
struct hl_ctx {
    uint8_t *mem;
    int dry;                            /* verification: declare the ranges, then stop */
    int why;                            /* HL_OK, or why the call is the 68k code's */
    uint32_t nr, rlo[HL_RANGES], rn[HL_RANGES];
    uint32_t nw, wlo[HL_RANGES], wn[HL_RANGES];
    uint64_t bytes;                     /* declared, for the cost */
};

static inline void hl_init(struct hl_ctx *hl, int dry)
{
    hl->mem = hl_host_mem();
    hl->dry = dry;
    hl->why = HL_OK;
    hl->nr = hl->nw = 0;
    hl->bytes = 0;
}

static inline int hl_need(struct hl_ctx *hl, uint64_t at, uint64_t n, int write)
{
    if (hl->why) return 0;
    if (!n) return 1;
    if (at + n > 0x100000000ull) { hl->why = HL_WHY_INPUT; return 0; }
    if (n > HL_MAX) { hl->why = HL_WHY_SIZE; return 0; }
    if ((write ? hl->nw : hl->nr) >= HL_RANGES) { hl->why = HL_WHY_INPUT; return 0; }
    if (!hl_host_plain((hl_addr)at, (uint32_t)n, write)) { hl->why = HL_WHY_MEMORY; return 0; }
    if (write && hl_host_aliased((hl_addr)at, (uint32_t)n)) { hl->why = HL_WHY_OVERLAP; return 0; }
    if (write) { hl->wlo[hl->nw] = (uint32_t)at; hl->wn[hl->nw++] = (uint32_t)n; }
    else { hl->rlo[hl->nr] = (uint32_t)at; hl->rn[hl->nr++] = (uint32_t)n; }
    hl->bytes += n;
    return 1;
}

/* inside a declared range: a read in a read or a write range, a write in a write range */
static inline int hl_in(const struct hl_ctx *hl, hl_addr a, uint32_t n, int write)
{
    uint32_t i;
    for (i = 0; i < hl->nw; i++) if (a - hl->wlo[i] < hl->wn[i] && hl->wn[i] - (a - hl->wlo[i]) >= n) return 1;
    if (write) return 0;
    for (i = 0; i < hl->nr; i++) if (a - hl->rlo[i] < hl->rn[i] && hl->rn[i] - (a - hl->rlo[i]) >= n) return 1;
    return 0;
}
/* an access outside them gives the call back: it reads 0 and writes nothing from then on */
static inline uint32_t hl_rd(struct hl_ctx *hl, hl_addr a, uint32_t n)
{
    const uint8_t *p;
    if (hl->why || !hl_in(hl, a, n, 0)) { if (!hl->why) hl->why = HL_WHY_INPUT; return 0; }
    p = hl->mem + a;
    return n == 1 ? p[0] : n == 2 ? (uint32_t)p[0] << 8 | p[1]
                                  : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void hl_wr8(struct hl_ctx *hl, hl_addr a, uint32_t v)
{
    if (hl->why || !hl_in(hl, a, 1, 1)) { if (!hl->why) hl->why = HL_WHY_INPUT; return; }
    hl->mem[a] = (uint8_t)v;
}
/* the declared writes, to the JIT, before the first of them */
static inline void hl_declared(struct hl_ctx *hl)
{
    uint32_t i;
    for (i = 0; i < hl->nw; i++) hl_host_written(hl->wlo[i], hl->wn[i]);
}

#define HL_TAG(name, id)
#define HL_FN(name) uint32_t name##_host(struct hl_ctx *hl_, hl_addr block)
#define HL_NEED_R(at, n) do { if (!hl_need(hl_, (uint64_t)(at), (uint64_t)(n), 0)) return 0; } while (0)
#define HL_NEED_W(at, n) do { if (!hl_need(hl_, (uint64_t)(at), (uint64_t)(n), 1)) return 0; } while (0)
#define HL_DECLARED() do { if (hl_->dry) return 0; hl_declared(hl_); } while (0)
#define HL_DECLINE() do { if (!hl_->why) hl_->why = HL_WHY_INPUT; return 0; } while (0)
#define HL_RD8(a) hl_rd(hl_, (hl_addr)(a), 1)
#define HL_RD16(a) hl_rd(hl_, (hl_addr)(a), 2)
#define HL_RD32(a) hl_rd(hl_, (hl_addr)(a), 4)
#define HL_WR8(a, v) hl_wr8(hl_, (hl_addr)(a), (uint32_t)(v))
#endif

/* the block's long number n (at byte 4 n) */
#define HL_ARG(n) HL_RD32(block + 4u * (uint32_t)(n))

#endif
