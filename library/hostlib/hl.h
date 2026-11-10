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
 *   HL_PTR_R(p, at, n), HL_PTR_W(p, at, n)  p: a byte pointer to [at, at + n),
 *                                    which must lie inside one declared range
 *                                    (a write range for HL_PTR_W), else the
 *                                    host gives the call back. Bytes only (no
 *                                    16- or 32-bit loads through it: the host
 *                                    is little-endian), and only inside those
 *                                    n bytes: the host checks the span once,
 *                                    not each access, so the function checks
 *                                    its own indices (HL_DECLINE) before it
 *                                    uses them. For inner loops.
 * The rules are HOSTLIB-ABI.md's, section 3.
 *
 * ABI 2 (HOSTLIB-ABI.md, section 8) adds, each optional per function:
 *   HL_TAG_LOCKED(name, id)          a locked call's tag (version $0002) in
 *                                    place of HL_TAG; hostlib.toml says
 *                                    locked = true
 *   HL_NEED_S(at, n)                 a state range: read, then changed; at
 *                                    most HL_STATE_MAX bytes in all a call,
 *                                    none overlapping a read range (the host
 *                                    declines one that does, at declare
 *                                    time). The host copies them aside at
 *                                    HL_DECLARED() and puts them back if it
 *                                    gives the call up later.
 *   HL_PART(fn) { ... }              one part of a call's work:
 *                                    void fn(block, part, parts)
 *   HL_PARALLEL(n, fn)               after HL_DECLARED(): parts 0 to n - 1.
 *                                    The host may run them on several cores;
 *                                    the 68k build runs them in order, one
 *                                    after another (nothing parallel: the
 *                                    68k side of it is a no-op). Each part
 *                                    writes only its own disjoint bytes of
 *                                    the write ranges, no state, declares
 *                                    nothing and does not decline.
 *   struct s { ... } HL_BE;          a struct in guest memory laid out as the
 *                                    68k lays it out, big-endian, guest
 *                                    pointers as hl_gptr (uint32_t); GCC's
 *                                    scalar_storage_order. Check its layout
 *                                    with HL_LAYOUT/HL_SIZE in both builds.
 *   HL_STRUCT_R(p, at), HL_STRUCT_W(p, at)  p: a pointer to such a struct
 *                                    at guest address at, all of it inside
 *                                    one declared range (a write or state
 *                                    range for _W), as HL_PTR_R/HL_PTR_W
 *   HL_THIN (defined by the build)   a thin shell's 68k build: the tags stay
 *                                    and their bodies are gone; a body
 *                                    reached anyway calls hl_thin_missing(id)
 *                                    (the shell's: Alert, log, task stopped)
 */
#include <stdint.h>
#include <string.h>

typedef uint32_t hl_addr;
typedef uint32_t hl_gptr;               /* a guest pointer inside an HL_BE struct */

#define HL_STATE_MAX 65536u             /* state ranges, all of a call's together (ABI 2) */
/* the Alert for a total function's access outside its ranges, and a thin
 * shell's body reached (HOSTLIB-ABI.md 8.2): recoverable, the id's low half;
 * the log line has the whole id */
#define HL_ALERT(id) (0x00AC0000u | ((uint32_t)(id) & 0xffffu))

/* The struct helper: the 68k build's layout, big-endian, on any GCC. packed
 * and aligned(2) give m68k GCC's layout (16- and 32-bit members at even
 * offsets) when the struct has no padding of its own: add pad members where
 * the 68k would pad, and check with HL_LAYOUT. Clang has no
 * scalar_storage_order: there, use the accessors. */
#if defined(__GNUC__) && !defined(__clang__)
#define HL_BE __attribute__((scalar_storage_order("big-endian"), packed, aligned(2)))
#endif
#define HL_LAYOUT(type, member, off) \
    _Static_assert(__builtin_offsetof(type, member) == (off), #type "." #member " is not at " #off ": the 68k layout is the contract")
#define HL_SIZE(type, n) _Static_assert(sizeof(type) == (n), #type " is not " #n " bytes: the 68k layout is the contract")

#ifndef HL_HOST
/* ---- the 68k build ---------------------------------------------------- */
/* the tag's header: amicradle-guest's own; in a repository that vendors
 * hostlib/ (hostlib/sync.sh), the copy beside this file (-DHL_VENDORED) */
#ifdef HL_VENDORED
#include "ac_magic.h"
#else
#include "../common/amiga/ac_magic.h"
#endif
#ifndef HL_THIN
#define HL_TAG(name, id) uint32_t name(hl_addr block); AC_MAGIC(name, id)
#define HL_TAG_LOCKED(name, id) uint32_t name(hl_addr block); AC_MAGIC_LOCKED(name, id)
#define HL_FN(name) AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(name)(hl_addr block)
#else
/* A thin shell (ABI 2, HOSTLIB-ABI.md 8.4): the tag, and behind it no body
 * but a call to the shell's hl_thin_missing(id), which a total function's
 * host build never lets happen: it Alerts with the id, logs a line and
 * stops the calling task (8.2). The function's own code stays in the
 * source as its specification, static inline and unused, so GCC leaves it
 * out at every optimisation level. */
uint32_t hl_thin_missing(uint32_t id);
#define HL_THIN_BODY(name, id) \
    AC_MAGIC_KEEP uint32_t AC_MAGIC_BODY(name)(hl_addr block) { (void)block; return hl_thin_missing(id); }
#define HL_TAG(name, id) uint32_t name(hl_addr block); AC_MAGIC(name, id) HL_THIN_BODY(name, id)
#define HL_TAG_LOCKED(name, id) uint32_t name(hl_addr block); AC_MAGIC_LOCKED(name, id) HL_THIN_BODY(name, id)
#define HL_FN(name) static inline __attribute__((unused)) uint32_t name##_hlspec(hl_addr block)
#define HL_PART(fn) static inline __attribute__((unused)) void fn(hl_addr block, uint32_t part, uint32_t parts)
#endif
#define HL_NEED_R(at, n) ((void)0)
#define HL_NEED_W(at, n) ((void)0)
#define HL_NEED_S(at, n) ((void)0)
#define HL_DECLARED() ((void)0)
#define HL_DECLINE() ((void)0)
#define HL_RD8(a) (*(const uint8_t *)(uintptr_t)(hl_addr)(a))
#define HL_RD16(a) (*(const uint16_t *)(uintptr_t)(hl_addr)(a))
#define HL_RD32(a) (*(const uint32_t *)(uintptr_t)(hl_addr)(a))
#define HL_WR8(a, v) (*(uint8_t *)(uintptr_t)(hl_addr)(a) = (uint8_t)(v))
#define HL_PTR_R(var, a, n) ((var) = (const uint8_t *)(uintptr_t)(hl_addr)(a))
#define HL_PTR_W(var, a, n) ((var) = (uint8_t *)(uintptr_t)(hl_addr)(a))
#define HL_STRUCT_R(var, a) ((var) = (__typeof__(var))(uintptr_t)(hl_addr)(a))
#define HL_STRUCT_W(var, a) ((var) = (__typeof__(var))(uintptr_t)(hl_addr)(a))
#ifndef HL_PART
#define HL_PART(fn) static void fn(hl_addr block, uint32_t part, uint32_t parts)
#endif
#define HL_PARALLEL(n, fn) \
    do { uint32_t hl_p_, hl_n_ = (uint32_t)(n); for (hl_p_ = 0; hl_p_ < hl_n_; hl_p_++) fn(block, hl_p_, hl_n_); } while (0)

#else
/* ---- the host build --------------------------------------------------- *
 * The file that includes a hostlib source for the host defines, first:
 *   uint8_t *hl_host_mem(void)                     guest address 0 on the host
 *   int hl_host_plain(hl_addr at, uint32_t n, int write)   1: all of it RAM (or ROM, to read)
 *   int hl_host_aliased(hl_addr at, uint32_t n)    1: some of it may show other memory
 *   void hl_host_written(hl_addr at, uint32_t n)   about to be written (the JIT's code under it)
 */
#define HL_RANGES 12
/* why the host gave a call back to the 68k code (jit_bodies.h's MD_* numbers) */
enum { HL_OK = 0, HL_WHY_MEMORY = 4, HL_WHY_SIZE = 5, HL_WHY_OVERLAP = 6, HL_WHY_INPUT = 8 };
#ifndef HL_MAX
#define HL_MAX (16u << 20)              /* the most one range may be (jit_bodies.h's MAGIC_MAX) */
#endif
/* The runtime supplies one state buffer per calling host thread and the
 * worker pool. They are declared before the inline context code uses them. */
struct hl_ctx;
uint8_t *hl_host_state_buf(void);
void hl_host_parallel(struct hl_ctx *hl, uint32_t parts,
                      void (*fn)(struct hl_ctx *, hl_addr, uint32_t, uint32_t), hl_addr block);

struct hl_ctx {
    uint8_t *mem;
    int dry;                            /* verification: declare the ranges, then stop */
    int why;                            /* HL_OK, or why the call is the 68k code's */
    uint32_t nr, rlo[HL_RANGES], rn[HL_RANGES];
    uint32_t nw, wlo[HL_RANGES], wn[HL_RANGES];
    uint32_t ns, slo[HL_RANGES], sn[HL_RANGES], soff[HL_RANGES];
    uint32_t state_bytes;
    uint8_t declared, restored;
    uint32_t rclo, rcn, wclo, wcn;      /* the range the last read and the last write fell in */
    uint64_t bytes;                     /* declared, for the cost */
};

static inline void hl_init(struct hl_ctx *hl, int dry)
{
    hl->mem = hl_host_mem();
    hl->dry = dry;
    hl->why = HL_OK;
    hl->nr = hl->nw = hl->ns = 0;
    hl->state_bytes = 0;
    hl->declared = hl->restored = 0;
    hl->rclo = hl->rcn = hl->wclo = hl->wcn = 0;
    hl->bytes = 0;
}

static inline void hl_restore_state(struct hl_ctx *hl)
{
    uint8_t *save;
    if (!hl->declared || hl->restored || !hl->ns) return;
    save = hl_host_state_buf();
    for (uint32_t i = 0; i < hl->ns; i++)
        memcpy((uint8_t *)((uintptr_t)hl->mem + hl->slo[i]), save + hl->soff[i], hl->sn[i]);
    hl->restored = 1;
}

static inline void hl_fail(struct hl_ctx *hl, int why)
{
    if (!hl->why) hl->why = why;
    hl_restore_state(hl);
}

static inline int hl_need(struct hl_ctx *hl, uint64_t at, uint64_t n, int write)
{
    if (hl->why) return 0;
    if (!n) return 1;
    if (at + n > 0x100000000ull) { hl_fail(hl, HL_WHY_INPUT); return 0; }
    if (n > HL_MAX) { hl_fail(hl, HL_WHY_SIZE); return 0; }
    if ((write ? hl->nw : hl->nr) >= HL_RANGES) { hl_fail(hl, HL_WHY_INPUT); return 0; }
    if (!hl_host_plain((hl_addr)at, (uint32_t)n, write)) { hl_fail(hl, HL_WHY_MEMORY); return 0; }
    /* An alias window can show the same physical bytes at another guest
     * address. Reject it for reads as well as writes: a texture or vertex
     * upload must not observe a concurrent write through the other window. */
    if (hl_host_aliased((hl_addr)at, (uint32_t)n)) { hl_fail(hl, HL_WHY_OVERLAP); return 0; }
    /* no read or write range over a state range (HOSTLIB-ABI.md 8.1, rules 3
     * and A3): the second of two overlapping ranges declines, before any write */
    for (uint32_t i = 0; i < hl->ns; i++)
        if (at < (uint64_t)hl->slo[i] + hl->sn[i] && hl->slo[i] < at + n) {
            hl_fail(hl, HL_WHY_INPUT); return 0;
        }
    if (write) { hl->wlo[hl->nw] = (uint32_t)at; hl->wn[hl->nw++] = (uint32_t)n; }
    else { hl->rlo[hl->nr] = (uint32_t)at; hl->rn[hl->nr++] = (uint32_t)n; }
    hl->bytes += n;
    return 1;
}

/* A state range is read and changed. It is kept separately from ordinary
 * writes so a decline after HL_DECLARED can restore it. */
static inline int hl_need_s(struct hl_ctx *hl, uint64_t at, uint64_t n)
{
    if (hl->why) return 0;
    if (!n) return 1;
    if (at + n > 0x100000000ull) { hl_fail(hl, HL_WHY_INPUT); return 0; }
    if (n > HL_MAX || (uint64_t)hl->state_bytes + n > HL_STATE_MAX) { hl_fail(hl, HL_WHY_SIZE); return 0; }
    if (hl->ns >= HL_RANGES) { hl_fail(hl, HL_WHY_INPUT); return 0; }
    if (!hl_host_plain((hl_addr)at, (uint32_t)n, 1)) { hl_fail(hl, HL_WHY_MEMORY); return 0; }
    if (hl_host_aliased((hl_addr)at, (uint32_t)n)) { hl_fail(hl, HL_WHY_OVERLAP); return 0; }
    /* nor over a read, a write or another state range (8.1 rule 3, A3) */
    for (uint32_t i = 0; i < hl->nr; i++)
        if (at < (uint64_t)hl->rlo[i] + hl->rn[i] && hl->rlo[i] < at + n) {
            hl_fail(hl, HL_WHY_INPUT); return 0;
        }
    for (uint32_t i = 0; i < hl->nw; i++)
        if (at < (uint64_t)hl->wlo[i] + hl->wn[i] && hl->wlo[i] < at + n) {
            hl_fail(hl, HL_WHY_INPUT); return 0;
        }
    for (uint32_t i = 0; i < hl->ns; i++)
        if (at < (uint64_t)hl->slo[i] + hl->sn[i] && hl->slo[i] < at + n) {
            hl_fail(hl, HL_WHY_INPUT); return 0;
        }
    hl->slo[hl->ns] = (uint32_t)at;
    hl->sn[hl->ns] = (uint32_t)n;
    hl->soff[hl->ns++] = hl->state_bytes;
    hl->state_bytes += (uint32_t)n;
    hl->bytes += n;
    return 1;
}

/* inside a declared range: a read in a read or a write range, a write in a
 * write range. The range the last access fell in is kept, and tried first. */
static inline int hl_find(struct hl_ctx *hl, hl_addr a, uint32_t n, int write)
{
    uint32_t i;
    for (i = 0; i < hl->nw; i++)
        if (a - hl->wlo[i] < hl->wn[i] && hl->wn[i] - (a - hl->wlo[i]) >= n) {
            if (write) { hl->wclo = hl->wlo[i]; hl->wcn = hl->wn[i]; }
            else { hl->rclo = hl->wlo[i]; hl->rcn = hl->wn[i]; }
            return 1;
        }
    for (i = 0; i < hl->ns; i++)
        if (a - hl->slo[i] < hl->sn[i] && hl->sn[i] - (a - hl->slo[i]) >= n) {
            if (write) { hl->wclo = hl->slo[i]; hl->wcn = hl->sn[i]; }
            else { hl->rclo = hl->slo[i]; hl->rcn = hl->sn[i]; }
            return 1;
        }
    if (write) return 0;
    for (i = 0; i < hl->nr; i++)
        if (a - hl->rlo[i] < hl->rn[i] && hl->rn[i] - (a - hl->rlo[i]) >= n) { hl->rclo = hl->rlo[i]; hl->rcn = hl->rn[i]; return 1; }
    return 0;
}
/* mem may be deliberately biased so that adding a 32-bit guest address
 * reaches the runtime's allocation. Do that addition as an integer: C
 * pointer arithmetic on the biased value is undefined, and AArch64 UBSan
 * diagnoses it even when the resulting address is valid. */
static inline uint8_t *hl_mem_at(struct hl_ctx *hl, hl_addr a)
{
    return (uint8_t *)((uintptr_t)hl->mem + (uintptr_t)a);
}
/* an access outside them gives the call back: it reads 0 and writes nothing from then on */
static inline uint32_t hl_rd(struct hl_ctx *hl, hl_addr a, uint32_t n)
{
    const uint8_t *p;
    uint32_t o = a - hl->rclo, ow = a - hl->wclo;
    if (hl->why || (!(o < hl->rcn && hl->rcn - o >= n) && !(ow < hl->wcn && hl->wcn - ow >= n))) {
        /* not in the last read's range, nor in the last write's (a read back of what it writes) */
        if (hl->why || !hl_find(hl, a, n, 0)) { hl_fail(hl, HL_WHY_INPUT); return 0; }
    }
    p = hl_mem_at(hl, a);
    return n == 1 ? p[0] : n == 2 ? (uint32_t)p[0] << 8 | p[1]
                                  : (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static inline void hl_wr8(struct hl_ctx *hl, hl_addr a, uint32_t v)
{
    uint32_t o = a - hl->wclo;
    if (hl->why || !(o < hl->wcn)) {
        if (hl->why || !hl_find(hl, a, 1, 1)) { hl_fail(hl, HL_WHY_INPUT); return; }
    }
    *hl_mem_at(hl, a) = (uint8_t)v;
}
/* [a, a + n) wholly inside one declared range (a write range for w), for HL_PTR_R/HL_PTR_W */
static inline int hl_span(struct hl_ctx *hl, hl_addr a, uint32_t n, int w)
{
    uint32_t o = a - hl->wclo, orr = a - hl->rclo;
    if (hl->why) return 0;
    if (o < hl->wcn && hl->wcn - o >= n) return 1;
    if (!w && orr < hl->rcn && hl->rcn - orr >= n) return 1;
    if (hl_find(hl, a, n, w)) return 1;
    hl_fail(hl, HL_WHY_INPUT);
    return 0;
}
/* Save state and hand every range the call may change to the JIT before the
 * first write. */
static inline void hl_declared(struct hl_ctx *hl)
{
    uint32_t i;
    uint8_t *save = hl_host_state_buf();
    for (i = 0; i < hl->ns; i++) {
        memcpy(save + hl->soff[i], hl_mem_at(hl, hl->slo[i]), hl->sn[i]);
        hl_host_written(hl->slo[i], hl->sn[i]);
    }
    for (i = 0; i < hl->nw; i++) hl_host_written(hl->wlo[i], hl->wn[i]);
    hl->declared = 1;
}

#define HL_TAG(name, id)
#define HL_FN(name) uint32_t name##_host(struct hl_ctx *hl_, hl_addr block)
#define HL_NEED_R(at, n) do { if (!hl_need(hl_, (uint64_t)(at), (uint64_t)(n), 0)) return 0; } while (0)
#define HL_NEED_W(at, n) do { if (!hl_need(hl_, (uint64_t)(at), (uint64_t)(n), 1)) return 0; } while (0)
#define HL_DECLARED() do { if (hl_->dry) return 0; hl_declared(hl_); } while (0)
#define HL_DECLINE() do { hl_fail(hl_, HL_WHY_INPUT); return 0; } while (0)
#define HL_RD8(a) hl_rd(hl_, (hl_addr)(a), 1)
#define HL_RD16(a) hl_rd(hl_, (hl_addr)(a), 2)
#define HL_RD32(a) hl_rd(hl_, (hl_addr)(a), 4)
#define HL_WR8(a, v) hl_wr8(hl_, (hl_addr)(a), (uint32_t)(v))
#define HL_PTR_R(var, a, n) do { if (!hl_span(hl_, (hl_addr)(a), (uint32_t)(n), 0)) return 0; (var) = hl_mem_at(hl_, (hl_addr)(a)); } while (0)
#define HL_PTR_W(var, a, n) do { if (!hl_span(hl_, (hl_addr)(a), (uint32_t)(n), 1)) return 0; (var) = hl_mem_at(hl_, (hl_addr)(a)); } while (0)
#define HL_STRUCT_R(var, a) do { if (!hl_span(hl_, (hl_addr)(a), (uint32_t)sizeof *(var), 0)) return 0; (var) = (__typeof__(var))(void *)hl_mem_at(hl_, (hl_addr)(a)); } while (0)
#define HL_STRUCT_W(var, a) do { if (!hl_span(hl_, (hl_addr)(a), (uint32_t)sizeof *(var), 1)) return 0; (var) = (__typeof__(var))(void *)hl_mem_at(hl_, (hl_addr)(a)); } while (0)

/* ---- ABI 2's host side ------------------------------------------------
 *   hl_need_s           HL_NEED_S: checks [at, at + n) as a write range is
 *                       checked, and that it overlaps no read range and keeps
 *                       the call's state at HL_STATE_MAX or less.
 *   hl_host_state_buf   HL_STATE_MAX bytes, the calling host thread's own.
 *   hl_host_parallel    HL_PARALLEL: fn(worker, block, part, parts) for every
 *                       part, on up to the cores the instance offers for host
 *                       work, each worker with its own copy of the context;
 *                       returns when all are done, with hl->why set if any
 *                       part's was. Deterministic by the parts' rule. */
#define HL_TAG_LOCKED(name, id)
#define HL_NEED_S(at, n) do { if (!hl_need_s(hl_, (uint64_t)(at), (uint64_t)(n))) return 0; } while (0)
#define HL_PART(fn) static void fn(struct hl_ctx *hl_, hl_addr block, uint32_t part, uint32_t parts)
#define HL_PARALLEL(n, fn) do { hl_host_parallel(hl_, (uint32_t)(n), fn, block); if (hl_->why) { hl_restore_state(hl_); return 0; } } while (0)
#endif

/* the block's long number n (at byte 4 n) */
#define HL_ARG(n) HL_RD32(block + 4u * (uint32_t)(n))

#endif
