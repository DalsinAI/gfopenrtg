/* SPDX-License-Identifier: MIT */
#ifndef AC_MAGIC_H
#define AC_MAGIC_H
/* Magic functions: a function of ours that runs as host code on AmiCradle
 * and as its own 68k code everywhere else (amicradle's
 * docs/architecture/MAGIC_FUNCTIONS_AND_BRIDGE.md, part 1, and
 * machines/cpus/m68k-ac090/native/jit_magic.c).
 *
 * The function's entry is a 12-byte tag, then a jump to its C body:
 *     BRA.S   *+12            ; $600A: a real Amiga branches over the tag
 *     DC.L    'ACMF'
 *     DC.W    $0001           ; the tag's version
 *     DC.L    id              ; who (AmiCradle Team is 1) in the top half, which below
 *     JMP     name_acbody
 * When AC090 translates the entry and knows the id, it runs its native
 * version instead, after checking the call (plain RAM only, no overlap it
 * cannot copy, nothing over the call's own stack frame, and so on); when it
 * declines, or does not know the id, the C body runs. A real Amiga, a
 * PiStorm or another emulator always runs the C body, for the cost of two
 * branches.
 *
 *   #include "ac_magic.h"
 *   AC_MAGIC(ac_memcpy, AC_MAGIC_MEMCPY)
 *   AC_MAGIC_KEEP void *AC_MAGIC_BODY(ac_memcpy)(void *dst, const void *src, size_t n) { ... }
 *
 * Callers call ac_memcpy as declared in their header. The rules:
 * - The body must do what the id's function does below, byte for byte, for
 *   every call: AC090_MAGIC=verify in the runtime runs both and compares.
 * - GCC's m68k-amigaos stack convention: the arguments as 32-bit values on
 *   the stack above the return address, the result in D0, D0/D1/A0/A1
 *   scratch. Not for a -mregparm build or a library's register entry (a
 *   LVO); tag a stack-convention function those call.
 * - Give the body external linkage (not static), so its name is the one
 *   the jump names. AC_MAGIC_KEEP keeps it out of the inliner's hands.
 * - GCC expands small memcpy, memset and strlen calls inline, and those
 *   are not magic. Name the tagged function something of its own (as
 *   ac_memcpy above), or build with -fno-builtin-memcpy and the like.
 * - The tag sits inside a small unused function (its name, name_actag), so
 *   that it goes into the code section whatever section GCC was in; the
 *   jump never returns into it.
 * - The tagged name is defined in assembly, which an LTO object's symbol
 *   table does not list. Linking such an object directly is fine, but in a
 *   static library its index would not offer the name, and a caller's link
 *   fails with an undefined reference. Build a file with tags in it with
 *   -fno-lto when it goes into a static library.
 *
 * The ids are a published list: a PiStorm's Emu68 or an FPGA accelerator
 * may honour the same tags. AmiCradle Team's (1 in the top half) are below; a new
 * one is added here and in jit_magic.c's magic_defs together. */

/* void *memcpy(void *dst, const void *src, size_t n): dst. AC090 declines
 * a call whose destination overlaps the source above it (dst > src and
 * dst - src < n), whose result would depend on the copy's direction. */
#define AC_MAGIC_MEMCPY  0x00010001
/* void *memmove(void *dst, const void *src, size_t n): dst, as if through
 * a buffer. */
#define AC_MAGIC_MEMMOVE 0x00010002
/* void *memset(void *dst, int c, size_t n): dst; the low byte of c. */
#define AC_MAGIC_MEMSET  0x00010003
/* int memcmp(const void *a, const void *b, size_t n): the first differing
 * bytes' difference, as unsigned bytes (a[i] - b[i], -255 to 255), else 0.
 * C only promises the sign; the native version gives this exact value. */
#define AC_MAGIC_MEMCMP  0x00010004
/* size_t strlen(const char *s) */
#define AC_MAGIC_STRLEN  0x00010005
/* unsigned long crc32(unsigned long crc, const unsigned char *buf,
 * unsigned len): zlib's (the reflected polynomial $EDB88320, inverted on
 * the way in and out); 0 when buf is NULL. */
#define AC_MAGIC_CRC32   0x00010006
/* unsigned long adler32(unsigned long adler, const unsigned char *buf,
 * unsigned len): zlib's; 1 when buf is NULL. AC090 leaves a call with
 * either half 65521 or more to the body (zlib never makes one), and one
 * with a NULL buf and len 1, which zlib reads anyway. */
#define AC_MAGIC_ADLER32 0x00010007
/* GFOpenGfx's leaves (libraries/opengfx/ogfx_leaves.h, SPD-11): each takes
 * one parameter block and returns how much it drew. AC090 leaves a block
 * that reaches anything but plain memory, or whose arithmetic would wrap
 * past 4 GB, to the body. */
/* uint32_t ogfx_planar_rect(const struct ogfx_planar_rect *): RectFill on
 * planar bitmaps, set from the pen or inverted; the planes drawn. */
#define AC_MAGIC_OGFX_PLANAR_RECT 0x00010100
/* uint32_t ogfx_chunky_rect(const struct ogfx_chunky_rect *): RectFill on
 * 1 to 4 bytes a pixel, stored or XORed; the pixels drawn. */
#define AC_MAGIC_OGFX_CHUNKY_RECT 0x00010101
/* uint32_t ogfx_chunky_copy(const struct ogfx_chunky_copy *): a rectangle
 * of 1 to 4 bytes a pixel, each row as memmove, rows from the last up when
 * the destination starts above the source; the pixels copied. */
#define AC_MAGIC_OGFX_CHUNKY_COPY 0x00010102
/* uint32_t ogfx_planar_blit(const struct ogfx_planar_blit *): BltBitMap
 * on standard planar bitmaps, source/destination minterm and plane mask;
 * the destination planes involved. */
#define AC_MAGIC_OGFX_PLANAR_BLIT 0x00010103
/* uint32_t ogfx_scale_copy(const struct ogfx_scale_copy *): a rectangle
 * copied 1 to 4 times the size, nearest neighbour, chunky or planar, within
 * a destination clip; the destination pixels written. */
#define AC_MAGIC_OGFX_SCALE_COPY 0x00010104
/* uint32_t hostlib_text_strip(block): Text's glyphs gathered into one 1-bit
 * strip (gfopenrtg.library's Text); the block and the contract are in
 * hostlib/text/text_strip.h and hostlib/HOSTLIB-ABI.md. A hostlib function:
 * one C source for the 68k and the host (hostlib/hl.h). */
#define AC_MAGIC_OGFX_TEXT_STRIP 0x00010105
/* uint32_t hostlib_sdl2_blend(block): SDL 2's ARGB8888 SDL_BLENDMODE_BLEND.
 * The destination is read and written, so the call is locked (tag $0002)
 * and that rectangle is state: a decline puts it back before the 68k body
 * blends it. The block is hostlib/sdl/blend.h; the contract is
 * HOSTLIB-ABI.md. */
#define AC_MAGIC_SDL2_BLEND 0x00010106
/* uint32_t hostlib_mgl_draw(block): one MiniGL virgl draw appended to a
 * context. The cursors are locked state, so a decline does not append
 * twice and the next draw on that context follows this one. The vertices
 * are a read range. The block is hostlib/mgl/draw.h. */
#define AC_MAGIC_MGL_DRAW 0x00010107
/* uint32_t hostlib_mgl_vertices(block): an array draw's vertices, or a
 * batch of immediate ones, copied into the arena. The cursor is locked
 * state. The block is hostlib/mgl/vertices.h. */
#define AC_MAGIC_MGL_VERTICES 0x00010108
/* uint32_t hostlib_mgl_tex(block): one ACV_PUT for a texture upload. The
 * block cursor is locked state; the texels are only read. The block is
 * hostlib/mgl/tex.h. */
#define AC_MAGIC_MGL_TEX 0x00010109
/* uint32_t hostlib_mgl_consts(block): send_if_changed. The last-sent
 * constants are locked state, so a decline does not send them twice.
 * The block is hostlib/mgl/consts.h. */
#define AC_MAGIC_MGL_CONSTS 0x0001010A
/* uint32_t hostlib_mgl_cached(block): cached(), the state objects' table
 * lookup. It only reads, so there is no state. The block and the result
 * are hostlib/mgl/cached.h. */
#define AC_MAGIC_MGL_CACHED 0x0001010B
/* uint32_t hostlib_zlib_inflate_codes(block): GFOpenZlib's inflate, the
 * codes of one block (literals, lengths, distances) until its end, the
 * input or the output runs out, or a bad code. Locked: the decoder's
 * registers and the window are state, so a decline puts them back before
 * the 68k body decodes the call. The block is hostlib/zlib/inflate_codes.h.
 * $00010180 up: compression. */
#define AC_MAGIC_ZLIB_INFLATE_CODES 0x00010180
/* uint32_t hostlib_zlib_deflate_all(block): raw deflate of a whole input
 * in one call, its working memory a write range it clears first (too big
 * for a locked call's 64 KB); the output's length, or 0 when it does not
 * fit. The block is hostlib/zlib/deflate_all.h. */
#define AC_MAGIC_ZLIB_DEFLATE_ALL 0x00010181
/* hostlib's own calls (hostlib/HOSTLIB-ABI.md), $00011000 up: above the
 * open libraries' LVO ranges ($00010300 to $00010FFF), which AC090 indexes
 * by bits 11-0. */
/* uint32_t hostlib_probe(block): what this runtime runs as host code, for
 * a thin shell's init (hostlib/probe/probe.h has the block): one answer
 * byte for each (id, version) asked about, HL_PROBE_HOST (1) and
 * HL_PROBE_TOTAL (2), and the host's feature level; returns the hostlib
 * ABI the runtime implements. The 68k body writes zeros and returns 0.
 * Its answer is about the host: AC_HOSTLIB=verify does not compare it. */
#define AC_MAGIC_HOSTLIB_PROBE 0x00011000
/* GFOpenCrypto's operations (DalsinAI/gfopentls, library/oc_magic.h, which
 * has each one's exact contract; AC090's side is jit_crypto.c). Byte
 * arrays throughout; each writes only its first argument's range. */
/* uint32_t ocm_probe(void): the OCF_* operations this machine runs as host
 * code; the 68k body answers 0. AC090_MAGIC=verify does not compare it. */
#define AC_MAGIC_OC_PROBE            0x00010200
/* int32_t ocm_x25519(out[32], scalar[32], point[32]): RFC 7748; -1 for an
 * all-zero result */
#define AC_MAGIC_OC_X25519           0x00010201
/* int32_t ocm_ed25519_verify(sig[64], pk[32], msg, len): 1 valid, 0 not */
#define AC_MAGIC_OC_ED25519_VERIFY   0x00010202
/* sntrup761: ocm_sntrup761_keypair(out[1158 + 1763], seed[32]),
 * ocm_sntrup761_enc(out[1039 + 32], pk[1158], seed[32]),
 * ocm_sntrup761_dec(out[32], ct[1039], sk[1763]) */
#define AC_MAGIC_OC_SNTRUP761_KEYPAIR 0x00010203
#define AC_MAGIC_OC_SNTRUP761_ENC    0x00010204
#define AC_MAGIC_OC_SNTRUP761_DEC    0x00010205
/* ocm_sha256_blocks(state[32], data, nblocks), ocm_sha512_blocks(state[64],
 * data, nblocks): the compression functions, the state big-endian */
#define AC_MAGIC_OC_SHA256_BLOCKS    0x00010206
#define AC_MAGIC_OC_SHA512_BLOCKS    0x00010207
/* ocm_chacha20(out[len], in, len, p[48] = key || nonce[8] || counter[8] BE);
 * ocm_poly1305(tag[16], data, len, key[32]) */
#define AC_MAGIC_OC_CHACHA20         0x00010208
#define AC_MAGIC_OC_POLY1305         0x00010209
/* int32_t ocm_aes(buf[len], len, key, keylen, iv[16], mode): CTR, CBC
 * encrypt, CBC decrypt, ECB encrypt (0 to 3) */
#define AC_MAGIC_OC_AES              0x0001020a
/* int32_t ocm_rsa_public(out[nlen], sig[nlen], n, nlen, e, elen);
 * int32_t ocm_ecdsa_verify(curve, qx, qy, r, s, hash, hlen) */
#define AC_MAGIC_OC_RSA_PUBLIC       0x0001020b
#define AC_MAGIC_OC_ECDSA_VERIFY     0x0001020c
/* int32_t ocm_ec_mul(point, curve, point_length, scalar, scalar_length):
 * point (04 || x || y) = scalar . point, in place. 1, or 0.
 * int32_t ocm_ec_mulgen(point, curve, scalar, scalar_length):
 * point = scalar . G. The point's length, or 0. */
#define AC_MAGIC_OC_EC_MUL           0x0001020d
#define AC_MAGIC_OC_EC_MULGEN        0x0001020e
/* The open libraries' LVO bodies (libraries/OPEN_LIBRARIES.md, section 3):
 * a library in bits 15-8, the LVO's slot (-LVO / 6) in bits 7-0. Each takes
 * its arguments in the registers its .lvo table names and answers in D0
 * (D0/D1 for a double or a 64-bit result), as the NDK's fd says; AC090's
 * side is jit_openlibs.c, which says what each one writes. Tagged with
 * AC_MAGIC_LVO below. */
#define AC_MAGIC_LIB_UTILITY   0x00010300   /* utility.library */
#define AC_MAGIC_LIB_SINGBAS   0x00010400   /* mathieeesingbas.library */
#define AC_MAGIC_LIB_DOUBBAS   0x00010500   /* mathieeedoubbas.library */
#define AC_MAGIC_LIB_SINGTRANS 0x00010600   /* mathieeesingtrans.library */
#define AC_MAGIC_LIB_DOUBTRANS 0x00010700   /* mathieeedoubtrans.library */
/* Motorola's FFP (libraries/openmathffp): the result in D0, and the
 * condition codes as MOVE to CCR leaves them (X and C clear, N, Z and V
 * as the autodocs say), their value in D1 too: each LVO's stub, which
 * starts with the tag itself (no jump), moves the core's CCR there. */
#define AC_MAGIC_LIB_FFP       0x00010800   /* mathffp.library */
#define AC_MAGIC_LIB_FFPTRANS  0x00010900   /* mathtrans.library */
/* The IEEE libraries' soft bodies (openmathieee's mathieee_soft.c and
 * mathtrans_soft.c), which a library takes in place of its FPU bodies on a
 * CPU with no FPU, or with the one softfpu.library emulates: the results
 * of softfpu.library's core (a 68040's FPU with 68040.library). Their own
 * ids, as they are their own functions: where the FPU bodies and the soft
 * core differ (an invalid operation's NaN, a result rounded twice), an id
 * names one of them. Only the calls whose results AC090 gives exactly are
 * tagged (jit_openlibs.c lists them). */
#define AC_MAGIC_LIB_SINGBAS_SOFT   0x00010A00   /* mathieeesingbas.library, soft */
#define AC_MAGIC_LIB_DOUBBAS_SOFT   0x00010B00   /* mathieeedoubbas.library, soft */
#define AC_MAGIC_LIB_SINGTRANS_SOFT 0x00010C00   /* mathieeesingtrans.library, soft */
#define AC_MAGIC_LIB_DOUBTRANS_SOFT 0x00010D00   /* mathieeedoubtrans.library, soft */
#define AC_MAGIC_LVO_ID(lib, lvo) ((lib) + (-(lvo)) / 6)

#define AC_MAGIC_STR_(x) #x
#define AC_MAGIC_STR(x) AC_MAGIC_STR_(x)
/* the assembler's name for a C name: "_memcpy" for m68k-amigaos */
#ifdef __USER_LABEL_PREFIX__
#define AC_MAGIC_SYM(name) AC_MAGIC_STR(__USER_LABEL_PREFIX__) #name
#else
#define AC_MAGIC_SYM(name) #name
#endif

#define AC_MAGIC_BODY(name) name##_acbody
#define AC_MAGIC_KEEP __attribute__((used, noinline))

#define AC_MAGIC(name, id)                                                   \
    __attribute__((used, noinline, noclone)) static void name##_actag(void) \
    {                                                                        \
        __asm__ volatile("\t.globl\t" AC_MAGIC_SYM(name) "\n"                \
                         AC_MAGIC_SYM(name) ":\n"                            \
                         "\t.short\t0x600a\n"                                \
                         "\t.long\t0x41434d46\n"                             \
                         "\t.short\t0x0001\n"                                \
                         "\t.long\t" AC_MAGIC_STR(id) "\n"                   \
                         "\tjmp\t" AC_MAGIC_SYM(name##_acbody) "\n");        \
    }

/* An LVO body's tag is also its call's core class (libraries/openexec/
 * DESIGN-NATIVE-BODIES.md, section 4): tag version 1 ($0001) marks a pure
 * call, which AmiCradle may run as its native body on CPU0 or on the
 * calling task core. Tag only pure calls with it; a locked call takes
 * version 2 (AC_MAGIC_LOCKED below), an anchored one no tag at all. */
/* A library's LVO body: the tag, then a jump to body, a C function that
 * takes its arguments in registers (register ... __asm("d0")), external and
 * kept (AC_MAGIC_KEEP). name is what the library's vector table holds:
 *   AC_MAGIC_LVO(M_GetTagData, AC_MAGIC_LVO_ID(AC_MAGIC_LIB_UTILITY, -36), L_GetTagData)
 *   static const APTR lib_vectors[] = { ..., (APTR)M_GetTagData, ... };
 * The id is an expression the assembler works out. */
#define AC_MAGIC_LVO(name, id, body)                                         \
    void name(void);                                                         \
    __attribute__((used, noinline, noclone)) static void name##_actag(void) \
    {                                                                        \
        __asm__ volatile("\t.globl\t" AC_MAGIC_SYM(name) "\n"                \
                         AC_MAGIC_SYM(name) ":\n"                            \
                         "\t.short\t0x600a\n"                                \
                         "\t.long\t0x41434d46\n"                             \
                         "\t.short\t0x0001\n"                                \
                         "\t.long\t" AC_MAGIC_STR(id) "\n"                   \
                         "\tjmp\t" AC_MAGIC_SYM(body) "\n");                 \
    }

/* A locked call's tag, version 2 (hostlib/HOSTLIB-ABI.md, section 8.1;
 * libraries/openexec/DESIGN-NATIVE-BODIES.md, section 4): 14 bytes, then
 * the jump to the body.
 *     BRA.S   *+14            ; $600C
 *     DC.L    'ACMF'
 *     DC.W    $0002
 *     DC.L    id
 *     DC.W    lock            ; reg<<12 | offset: the object's register and
 *                             ; its lock word's offset; $FFFF: no object
 *                             ; lock, the caller keeps two calls on one
 *                             ; state apart (hostlib's locked calls)
 *     JMP     name_acbody
 * A runtime that knows only version 1 does not recognise it, and the call
 * runs as 68k code: the form is safe to ship before the runtime takes it.
 * AC_MAGIC_LOCKED(name, id) is hostlib's (lock $FFFF). */
#define AC_MAGIC_NOLOCK 0xffff
#define AC_MAGIC_LOCKED_LK(name, id, lock)                                   \
    __attribute__((used, noinline, noclone)) static void name##_actag(void) \
    {                                                                        \
        __asm__ volatile("\t.globl\t" AC_MAGIC_SYM(name) "\n"                \
                         AC_MAGIC_SYM(name) ":\n"                            \
                         "\t.short\t0x600c\n"                                \
                         "\t.long\t0x41434d46\n"                             \
                         "\t.short\t0x0002\n"                                \
                         "\t.long\t" AC_MAGIC_STR(id) "\n"                   \
                         "\t.short\t" AC_MAGIC_STR(lock) "\n"                \
                         "\tjmp\t" AC_MAGIC_SYM(name##_acbody) "\n");        \
    }
#define AC_MAGIC_LOCKED(name, id) AC_MAGIC_LOCKED_LK(name, id, AC_MAGIC_NOLOCK)

#endif
