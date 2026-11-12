/* SPDX-License-Identifier: MIT */
/* Written by hostlib/gen.py from hostlib.toml: change that, then run it. */
#ifndef HOSTLIB_HL_IDS_H
#define HOSTLIB_HL_IDS_H

#define HOSTLIB_ABI 2
#define HOSTLIB_MANIFEST "f68f55ea2664350e"   /* sha256 of hostlib.toml, the first 16 hex digits */

#define HL_ID_OGFX_PLANAR_RECT 0x00010100   /* hostlib_ogfx_planar_rect, version 1 */
#define HL_ID_OGFX_CHUNKY_RECT 0x00010101   /* hostlib_ogfx_chunky_rect, version 1 */
#define HL_ID_OGFX_CHUNKY_COPY 0x00010102   /* hostlib_ogfx_chunky_copy, version 1 */
#define HL_ID_OGFX_PLANAR_BLIT 0x00010103   /* hostlib_ogfx_planar_blit, version 1 */
#define HL_ID_OGFX_TEXT_STRIP 0x00010105   /* hostlib_text_strip, version 1 */
#define HL_ID_SDL2_BLEND 0x00010106   /* hostlib_sdl2_blend, version 1 */
#define HL_ID_SDL2_BLEND_ARGB 0x0001010a   /* hostlib_sdl2_blend_argb, version 1 */
#define HL_ID_MGL_DRAW 0x00010107   /* hostlib_mgl_draw, version 1 */
#define HL_ID_MGL_VERTICES 0x00010108   /* hostlib_mgl_vertices, version 1 */
#define HL_ID_MGL_TEX 0x00010109   /* hostlib_mgl_tex, version 1 */
#define HL_ID_MGL_CONSTS 0x0001010b   /* hostlib_mgl_consts, version 1 */
#define HL_ID_HOSTLIB_PROBE 0x00011000   /* hostlib_probe, version 1 */
#define HL_ID_ZLIB_INFLATE_CODES 0x00010180   /* hostlib_zlib_inflate_codes, version 1 */
#define HL_ID_ZLIB_DEFLATE_ALL 0x00010181   /* hostlib_zlib_deflate_all, version 1 */
#define HL_ID_LZMA_DECODE_ALL 0x00010182   /* hostlib_lzma_decode_all, version 1 */

/* a contract's flags (HOSTLIB-ABI.md 8.5): hostlib_host.inc's HOSTLIB_CONTRACTS */
#define HL_C_LOCKED 1
#define HL_C_TOTAL 2
#define HL_C_PARALLEL 4
#define HL_C_RUNTIME 8
#define HL_C_SERVICE 16

#endif
