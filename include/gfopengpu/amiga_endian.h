/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * amiga_endian.h: fast byte swaps for porting little-endian data to the
 * big-endian 68020-68060, and the same header in host tools.
 *
 *   #include <gfopengpu/amiga_endian.h>
 *
 * Single values (always inline):
 *   ae_swap16/32/64(x), ae_swapf(f)            swap, whatever the host
 *   ae_le_to_be16/32/64, ae_be_to_le16/32/64   the same swaps, by intent
 *   ae_le16toh/le32toh/le64toh, ae_htole16..   no-ops on a little-endian
 *   ae_be16toh/be32toh/be64toh, ae_htobe16..   host, swaps on the Amiga
 *   ae_load_le16/32/64(p), ae_store_le16/32/64(p, v), and _be: any
 *   alignment, value in host order
 *
 * Arrays (n is a number of elements, not bytes):
 *   ae_swap16_array(p, n)   ae_swap32_array(p, n)   ae_swap64_array(p, n)
 *   ae_swap_float_array(p, n)
 *   ae_swap16_copy(dst, src, n) ... ae_swap_float_copy: dst == src is fine,
 *   other overlaps are not
 *   ae_le16_array_to_host(p, n) ... ae_le_float_array_to_host: swap on the
 *   Amiga, nothing on a little-endian host (after fread of a LE file)
 *   ae_swap32_strided(base, count, stride, ncomp)  ncomp 32-bit words at
 *   ae_swap16_strided(base, count, stride, ncomp)  the start of each element
 *   ae_swap_interleaved(base, count, stride, "4442")  one digit per field,
 *   its size in bytes (1, 2, 4 or 8), from offset 0; bytes after the last
 *   field are left alone
 *
 * Without AMIGA_ENDIAN_NO_SHORT_NAMES the ae_ prefix may be left off
 * (swap16, le_to_be32, swap32_array, load_le32, ...) where nothing else
 * of that name is already defined as a macro.
 *
 * The array calls are static inline. To have one copy of them in a
 * program instead, define AMIGA_ENDIAN_USE_LIB and link -lendian
 * (library/endian, built for the kit's CPU).
 *
 * On the 68020-68060 the calls use GCC's __builtin_bswap16/32 (which GCC
 * 16 already makes ror.w #8 and ror.w/swap/ror.w) and, for the arrays and
 * unaligned loads, inline asm: 4 longs per loop, 2 words per long.
 * docs/endian.md has the timings.
 */
#ifndef GFOPENGPU_AMIGA_ENDIAN_H
#define GFOPENGPU_AMIGA_ENDIAN_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ---- The host's byte order, at compile time ------------------------------ */

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && defined(__ORDER_LITTLE_ENDIAN__)
#  if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#    define AMIGA_ENDIAN_BIG 1
#  elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#    define AMIGA_ENDIAN_BIG 0
#  endif
#endif
#ifndef AMIGA_ENDIAN_BIG
#  if defined(__m68k__) || defined(__mc68000__) || defined(__BIG_ENDIAN__) || defined(__ARMEB__) \
      || defined(__MIPSEB__) || defined(__sparc__) || defined(__AMIGA__) || defined(__amigaos__)
#    define AMIGA_ENDIAN_BIG 1
#  elif defined(_WIN32) || defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64) \
      || defined(_M_ARM64) || defined(__aarch64__) || defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) \
      || defined(__MIPSEL__) || (defined(__riscv) && !defined(__BIG_ENDIAN__))
#    define AMIGA_ENDIAN_BIG 0
#  else
#    error "amiga_endian.h: unknown byte order; define AMIGA_ENDIAN_BIG to 1 or 0"
#  endif
#endif
#define AMIGA_ENDIAN_LITTLE (!AMIGA_ENDIAN_BIG)

/* Inline asm for the 68020 and later (which read a long at any address);
 * the 68000 and other CPUs get plain C. */
#if defined(__GNUC__) && !defined(AMIGA_ENDIAN_NO_ASM) \
    && (defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__) || defined(__mc68060__))
#  define AE_M68K 1
#else
#  define AE_M68K 0
#endif

#if defined(__GNUC__)
#  define AE_INLINE static __inline__ __attribute__((always_inline, unused))
#  define AE_HAVE_BUILTIN_BSWAP 1
#elif defined(_MSC_VER)
#  include <stdlib.h>
#  define AE_INLINE static __inline
#  define AE_HAVE_BUILTIN_BSWAP 0
#else
#  define AE_INLINE static inline
#  define AE_HAVE_BUILTIN_BSWAP 0
#endif

#if defined(AMIGA_ENDIAN_IMPLEMENTATION)
#  define AE_BULK                       /* library/endian: one real copy */
#elif defined(AMIGA_ENDIAN_USE_LIB)
#  define AE_BULK_PROTOTYPES_ONLY 1
#  define AE_BULK extern
#elif defined(__GNUC__)
#  define AE_BULK static __inline__ __attribute__((unused))
#else
#  define AE_BULK static
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Single values -------------------------------------------------------- */

/* GCC 16, -m68020/40/60 -O2: ror.w #8 / ror.w #8, swap, ror.w #8: optimal. */
AE_INLINE uint16_t ae_swap16(uint16_t x)
{
#if AE_HAVE_BUILTIN_BSWAP
    return __builtin_bswap16(x);
#elif defined(_MSC_VER)
    return _byteswap_ushort(x);
#else
    return (uint16_t)((x >> 8) | (x << 8));
#endif
}

AE_INLINE uint32_t ae_swap32(uint32_t x)
{
#if AE_HAVE_BUILTIN_BSWAP
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return (x >> 24) | ((x >> 8) & 0xff00u) | ((x << 8) & 0xff0000u) | (x << 24);
#endif
}

/* GCC's 64-bit swap is the two 32-bit swaps, plus a register shuffle when it
 * isn't inlined (a value returned in d0:d1); inlined it's 6 instructions. */
AE_INLINE uint64_t ae_swap64(uint64_t x)
{
#if AE_HAVE_BUILTIN_BSWAP
    return __builtin_bswap64(x);
#elif defined(_MSC_VER)
    return _byteswap_uint64(x);
#else
    return ((uint64_t)ae_swap32((uint32_t)x) << 32) | ae_swap32((uint32_t)(x >> 32));
#endif
}

AE_INLINE float ae_swapf(float f)
{
    union { float f; uint32_t u; } v;
    v.f = f;
    v.u = ae_swap32(v.u);
    return v.f;
}

#define ae_le_to_be16(x) ae_swap16(x)
#define ae_le_to_be32(x) ae_swap32(x)
#define ae_le_to_be64(x) ae_swap64(x)
#define ae_be_to_le16(x) ae_swap16(x)
#define ae_be_to_le32(x) ae_swap32(x)
#define ae_be_to_le64(x) ae_swap64(x)

#if AMIGA_ENDIAN_BIG
#  define ae_le16toh(x) ae_swap16(x)
#  define ae_le32toh(x) ae_swap32(x)
#  define ae_le64toh(x) ae_swap64(x)
#  define ae_be16toh(x) ((uint16_t)(x))
#  define ae_be32toh(x) ((uint32_t)(x))
#  define ae_be64toh(x) ((uint64_t)(x))
#else
#  define ae_le16toh(x) ((uint16_t)(x))
#  define ae_le32toh(x) ((uint32_t)(x))
#  define ae_le64toh(x) ((uint64_t)(x))
#  define ae_be16toh(x) ae_swap16(x)
#  define ae_be32toh(x) ae_swap32(x)
#  define ae_be64toh(x) ae_swap64(x)
#endif
#define ae_htole16(x) ae_le16toh(x)
#define ae_htole32(x) ae_le32toh(x)
#define ae_htole64(x) ae_le64toh(x)
#define ae_htobe16(x) ae_be16toh(x)
#define ae_htobe32(x) ae_be32toh(x)
#define ae_htobe64(x) ae_be64toh(x)

/* ---- Loads and stores at any address ------------------------------------- */

/* Native-order load/store of 2 or 4 bytes at any address. On the 68020+ a
 * move.w/move.l does it (the CPU splits a misaligned access itself); GCC
 * would go byte by byte, as m68k is a strict-alignment target to it. */
typedef struct { unsigned char b[2]; } ae_bytes2_;
typedef struct { unsigned char b[4]; } ae_bytes4_;

AE_INLINE uint16_t ae_load_native16_(const void *p)
{
    uint16_t v;
#if AE_M68K
    __asm__("move.w %1,%0" : "=d"(v) : "m"(*(const ae_bytes2_ *)p));
#else
    memcpy(&v, p, 2);
#endif
    return v;
}

AE_INLINE uint32_t ae_load_native32_(const void *p)
{
    uint32_t v;
#if AE_M68K
    __asm__("move.l %1,%0" : "=d"(v) : "m"(*(const ae_bytes4_ *)p));
#else
    memcpy(&v, p, 4);
#endif
    return v;
}

AE_INLINE void ae_store_native16_(void *p, uint16_t v)
{
#if AE_M68K
    __asm__("move.w %1,%0" : "=m"(*(ae_bytes2_ *)p) : "d"(v));
#else
    memcpy(p, &v, 2);
#endif
}

AE_INLINE void ae_store_native32_(void *p, uint32_t v)
{
#if AE_M68K
    __asm__("move.l %1,%0" : "=m"(*(ae_bytes4_ *)p) : "d"(v));
#else
    memcpy(p, &v, 4);
#endif
}

#if AMIGA_ENDIAN_BIG
#  define AE_FROM_LE16_(v) ae_swap16(v)
#  define AE_FROM_LE32_(v) ae_swap32(v)
#  define AE_FROM_BE16_(v) (v)
#  define AE_FROM_BE32_(v) (v)
#else
#  define AE_FROM_LE16_(v) (v)
#  define AE_FROM_LE32_(v) (v)
#  define AE_FROM_BE16_(v) ae_swap16(v)
#  define AE_FROM_BE32_(v) ae_swap32(v)
#endif

AE_INLINE uint16_t ae_load_le16(const void *p) { return AE_FROM_LE16_(ae_load_native16_(p)); }
AE_INLINE uint32_t ae_load_le32(const void *p) { return AE_FROM_LE32_(ae_load_native32_(p)); }
AE_INLINE uint16_t ae_load_be16(const void *p) { return AE_FROM_BE16_(ae_load_native16_(p)); }
AE_INLINE uint32_t ae_load_be32(const void *p) { return AE_FROM_BE32_(ae_load_native32_(p)); }
AE_INLINE void ae_store_le16(void *p, uint16_t v) { ae_store_native16_(p, AE_FROM_LE16_(v)); }
AE_INLINE void ae_store_le32(void *p, uint32_t v) { ae_store_native32_(p, AE_FROM_LE32_(v)); }
AE_INLINE void ae_store_be16(void *p, uint16_t v) { ae_store_native16_(p, AE_FROM_BE16_(v)); }
AE_INLINE void ae_store_be32(void *p, uint32_t v) { ae_store_native32_(p, AE_FROM_BE32_(v)); }

AE_INLINE uint64_t ae_load_le64(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return (uint64_t)ae_load_le32(b) | ((uint64_t)ae_load_le32(b + 4) << 32);
}
AE_INLINE uint64_t ae_load_be64(const void *p)
{
    const unsigned char *b = (const unsigned char *)p;
    return ((uint64_t)ae_load_be32(b) << 32) | ae_load_be32(b + 4);
}
AE_INLINE void ae_store_le64(void *p, uint64_t v)
{
    unsigned char *b = (unsigned char *)p;
    ae_store_le32(b, (uint32_t)v);
    ae_store_le32(b + 4, (uint32_t)(v >> 32));
}
AE_INLINE void ae_store_be64(void *p, uint64_t v)
{
    unsigned char *b = (unsigned char *)p;
    ae_store_be32(b, (uint32_t)(v >> 32));
    ae_store_be32(b + 4, (uint32_t)v);
}

AE_INLINE float ae_load_le_float(const void *p)
{
    union { float f; uint32_t u; } v;
    v.u = ae_load_le32(p);
    return v.f;
}

/* ---- Arrays --------------------------------------------------------------- */

AE_BULK void ae_swap16_copy(void *dst, const void *src, size_t n);
AE_BULK void ae_swap32_copy(void *dst, const void *src, size_t n);
AE_BULK void ae_swap64_copy(void *dst, const void *src, size_t n);
AE_BULK void ae_swap32_strided(void *base, size_t count, size_t stride, size_t ncomp);
AE_BULK void ae_swap16_strided(void *base, size_t count, size_t stride, size_t ncomp);
AE_BULK int ae_swap_interleaved(void *base, size_t count, size_t stride, const char *layout);

#ifndef AE_BULK_PROTOTYPES_ONLY

/* dst = byteswap(src), n longs; dst may be src. The 68k loop does 4 longs a
 * turn in two independent pairs, so the 68060 can issue the rotates of one
 * pair beside the other's. The loop starts on a long, which is what the
 * 68060's fetch wants; a 16-byte (cache line) start can't be had, as the
 * hunk linker puts objects on 4 bytes and LoadSeg hunks on 8, and doesn't
 * matter, as the 40-byte loop runs from the cache after its first turn. */
AE_BULK void ae_swap32_copy(void *dst, const void *src, size_t n)
{
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)dst;
#if AE_M68K
    size_t blocks = n >> 2;
    uint32_t t0, t1, t2, t3;
    __asm__ __volatile__(
        "\tbra.s 2f\n"
        "\t.balign 4\n"
        "1:\tmove.l (%0)+,%3\n"
        "\tmove.l (%0)+,%4\n"
        "\tror.w #8,%3\n"
        "\tror.w #8,%4\n"
        "\tswap %3\n"
        "\tswap %4\n"
        "\tror.w #8,%3\n"
        "\tror.w #8,%4\n"
        "\tmove.l (%0)+,%5\n"
        "\tmove.l (%0)+,%6\n"
        "\tmove.l %3,(%1)+\n"
        "\tmove.l %4,(%1)+\n"
        "\tror.w #8,%5\n"
        "\tror.w #8,%6\n"
        "\tswap %5\n"
        "\tswap %6\n"
        "\tror.w #8,%5\n"
        "\tror.w #8,%6\n"
        "\tmove.l %5,(%1)+\n"
        "\tmove.l %6,(%1)+\n"
        "2:\tsubq.l #1,%2\n"
        "\tbcc.s 1b\n"
        : "+a"(s), "+a"(d), "+d"(blocks), "=&d"(t0), "=&d"(t1), "=&d"(t2), "=&d"(t3)
        :
        : "memory", "cc");
    n &= 3;
#endif
    for (; n; n--, s++, d++)            /* any alignment, as the asm takes */
        ae_store_native32_(d, ae_swap32(ae_load_native32_(s)));
}

/* Two words in a long: rol.w #8, swap, rol.w #8, swap. */
AE_BULK void ae_swap16_copy(void *dst, const void *src, size_t n)
{
    const uint16_t *s = (const uint16_t *)src;
    uint16_t *d = (uint16_t *)dst;
#if AE_M68K
    size_t blocks;
    uint32_t t0, t1, t2, t3;
    if (n && ((uintptr_t)d & 2)) {      /* the longs on 4 bytes, for dst at least */
        ae_store_native16_(d++, ae_swap16(ae_load_native16_(s++)));
        n--;
    }
    blocks = n >> 3;
    __asm__ __volatile__(
        "\tbra.s 2f\n"
        "\t.balign 4\n"
        "1:\tmove.l (%0)+,%3\n"
        "\tmove.l (%0)+,%4\n"
        "\trol.w #8,%3\n"
        "\trol.w #8,%4\n"
        "\tswap %3\n"
        "\tswap %4\n"
        "\trol.w #8,%3\n"
        "\trol.w #8,%4\n"
        "\tswap %3\n"
        "\tswap %4\n"
        "\tmove.l (%0)+,%5\n"
        "\tmove.l (%0)+,%6\n"
        "\tmove.l %3,(%1)+\n"
        "\tmove.l %4,(%1)+\n"
        "\trol.w #8,%5\n"
        "\trol.w #8,%6\n"
        "\tswap %5\n"
        "\tswap %6\n"
        "\trol.w #8,%5\n"
        "\trol.w #8,%6\n"
        "\tswap %5\n"
        "\tswap %6\n"
        "\tmove.l %5,(%1)+\n"
        "\tmove.l %6,(%1)+\n"
        "2:\tsubq.l #1,%2\n"
        "\tbcc.s 1b\n"
        : "+a"(s), "+a"(d), "+d"(blocks), "=&d"(t0), "=&d"(t1), "=&d"(t2), "=&d"(t3)
        :
        : "memory", "cc");
    n &= 7;
#endif
    for (; n; n--, s++, d++)
        ae_store_native16_(d, ae_swap16(ae_load_native16_(s)));
}

AE_BULK void ae_swap64_copy(void *dst, const void *src, size_t n)
{
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)dst;
    for (; n; n--, s += 2, d += 2) {
        uint32_t lo = ae_load_native32_(s), hi = ae_load_native32_(s + 1);
        ae_store_native32_(d, ae_swap32(hi));
        ae_store_native32_(d + 1, ae_swap32(lo));
    }
}

/* ncomp 32-bit words at base, base + stride, ...: one attribute of an
 * interleaved vertex buffer (pass base + the attribute's offset). */
AE_BULK void ae_swap32_strided(void *base, size_t count, size_t stride, size_t ncomp)
{
    unsigned char *p = (unsigned char *)base;
    for (; count; count--, p += stride) {
        size_t i;
        for (i = 0; i < ncomp; i++)
            ae_store_native32_(p + 4 * i, ae_swap32(ae_load_native32_(p + 4 * i)));
    }
}

AE_BULK void ae_swap16_strided(void *base, size_t count, size_t stride, size_t ncomp)
{
    unsigned char *p = (unsigned char *)base;
    for (; count; count--, p += stride) {
        size_t i;
        for (i = 0; i < ncomp; i++)
            ae_store_native16_(p + 2 * i, ae_swap16(ae_load_native16_(p + 2 * i)));
    }
}

/* Every element's fields, one layout digit each ("4442": three 32-bit fields
 * and a 16-bit one, then padding up to stride). Returns 0, or -1 for a bad
 * layout (a digit other than 1, 2, 4, 8, or fields longer than stride). */
AE_BULK int ae_swap_interleaved(void *base, size_t count, size_t stride, const char *layout)
{
    struct { unsigned short off, n; unsigned char size; } run[32];
    unsigned nrun = 0, off = 0, r;
    unsigned char *p = (unsigned char *)base;
    const char *c;

    for (c = layout; *c; c++) {         /* runs of fields of one size */
        unsigned size = (unsigned)(*c - '0');
        if (size != 1 && size != 2 && size != 4 && size != 8)
            return -1;
        if (size > 1) {
            if (nrun && run[nrun - 1].size == size && run[nrun - 1].off + run[nrun - 1].n * size == off)
                run[nrun - 1].n++;
            else if (nrun == 32)
                return -1;
            else {
                run[nrun].off = (unsigned short)off;
                run[nrun].n = 1;
                run[nrun].size = (unsigned char)size;
                nrun++;
            }
        }
        off += size;
    }
    if (off > stride)
        return -1;
    for (; count; count--, p += stride)
        for (r = 0; r < nrun; r++) {
            unsigned char *f = p + run[r].off;
            unsigned i, n = run[r].n;
            switch (run[r].size) {
            case 2:
                for (i = 0; i < n; i++, f += 2)
                    ae_store_native16_(f, ae_swap16(ae_load_native16_(f)));
                break;
            case 4:
                for (i = 0; i < n; i++, f += 4)
                    ae_store_native32_(f, ae_swap32(ae_load_native32_(f)));
                break;
            default:
                for (i = 0; i < n; i++, f += 8) {
                    uint32_t a = ae_load_native32_(f), b = ae_load_native32_(f + 4);
                    ae_store_native32_(f, ae_swap32(b));
                    ae_store_native32_(f + 4, ae_swap32(a));
                }
                break;
            }
        }
    return 0;
}

#endif /* !AE_BULK_PROTOTYPES_ONLY */

/* In place: the copies with dst == src. */
#define ae_swap16_array(p, n) ae_swap16_copy((p), (p), (n))
#define ae_swap32_array(p, n) ae_swap32_copy((p), (p), (n))
#define ae_swap64_array(p, n) ae_swap64_copy((p), (p), (n))
#define ae_swap_float_array(p, n) ae_swap32_copy((p), (p), (n))
#define ae_swap_float_copy(d, s, n) ae_swap32_copy((d), (s), (n))

/* Little-endian data (as read from a file) to the host's order. */
#if AMIGA_ENDIAN_BIG
#  define ae_le16_array_to_host(p, n) ae_swap16_copy((p), (p), (n))
#  define ae_le32_array_to_host(p, n) ae_swap32_copy((p), (p), (n))
#  define ae_le64_array_to_host(p, n) ae_swap64_copy((p), (p), (n))
#  define ae_le16_copy_to_host(d, s, n) ae_swap16_copy((d), (s), (n))
#  define ae_le32_copy_to_host(d, s, n) ae_swap32_copy((d), (s), (n))
#  define ae_le64_copy_to_host(d, s, n) ae_swap64_copy((d), (s), (n))
#else
#  define ae_le16_array_to_host(p, n) ((void)(p), (void)(n))
#  define ae_le32_array_to_host(p, n) ((void)(p), (void)(n))
#  define ae_le64_array_to_host(p, n) ((void)(p), (void)(n))
#  define ae_le16_copy_to_host(d, s, n) ((void)memmove((d), (s), (size_t)(n) * 2))
#  define ae_le32_copy_to_host(d, s, n) ((void)memmove((d), (s), (size_t)(n) * 4))
#  define ae_le64_copy_to_host(d, s, n) ((void)memmove((d), (s), (size_t)(n) * 8))
#endif
#define ae_le_float_array_to_host(p, n) ae_le32_array_to_host((p), (n))

/* ---- Names without the prefix -------------------------------------------- */

#ifndef AMIGA_ENDIAN_NO_SHORT_NAMES
#  ifndef swap16
#    define swap16(x) ae_swap16(x)
#  endif
#  ifndef swap32
#    define swap32(x) ae_swap32(x)
#  endif
#  ifndef swap64
#    define swap64(x) ae_swap64(x)
#  endif
#  define le_to_be16(x) ae_swap16(x)
#  define le_to_be32(x) ae_swap32(x)
#  define le_to_be64(x) ae_swap64(x)
#  define be_to_le16(x) ae_swap16(x)
#  define be_to_le32(x) ae_swap32(x)
#  define be_to_le64(x) ae_swap64(x)
#  define load_le16(p) ae_load_le16(p)
#  define load_le32(p) ae_load_le32(p)
#  define load_le64(p) ae_load_le64(p)
#  define load_be16(p) ae_load_be16(p)
#  define load_be32(p) ae_load_be32(p)
#  define load_be64(p) ae_load_be64(p)
#  define store_le16(p, v) ae_store_le16((p), (v))
#  define store_le32(p, v) ae_store_le32((p), (v))
#  define store_le64(p, v) ae_store_le64((p), (v))
#  define store_be16(p, v) ae_store_be16((p), (v))
#  define store_be32(p, v) ae_store_be32((p), (v))
#  define store_be64(p, v) ae_store_be64((p), (v))
#  define swap16_array(p, n) ae_swap16_array((p), (n))
#  define swap32_array(p, n) ae_swap32_array((p), (n))
#  define swap64_array(p, n) ae_swap64_array((p), (n))
#  define swap_float_array(p, n) ae_swap_float_array((p), (n))
#  define swap16_copy(d, s, n) ae_swap16_copy((d), (s), (n))
#  define swap32_copy(d, s, n) ae_swap32_copy((d), (s), (n))
#  define swap64_copy(d, s, n) ae_swap64_copy((d), (s), (n))
#  define swap_float_copy(d, s, n) ae_swap_float_copy((d), (s), (n))
#  define swap_interleaved(b, c, s, l) ae_swap_interleaved((b), (c), (s), (l))
#  if !defined(le16toh) && !defined(_ENDIAN_H) && !defined(_SYS_ENDIAN_H_)
#    define le16toh(x) ae_le16toh(x)
#    define le32toh(x) ae_le32toh(x)
#    define le64toh(x) ae_le64toh(x)
#    define be16toh(x) ae_be16toh(x)
#    define be32toh(x) ae_be32toh(x)
#    define be64toh(x) ae_be64toh(x)
#    define htole16(x) ae_htole16(x)
#    define htole32(x) ae_htole32(x)
#    define htole64(x) ae_htole64(x)
#    define htobe16(x) ae_htobe16(x)
#    define htobe32(x) ae_htobe32(x)
#    define htobe64(x) ae_htobe64(x)
#  endif
#endif

#ifdef __cplusplus
}
#endif

#endif /* GFOPENGPU_AMIGA_ENDIAN_H */
