/* SPDX-License-Identifier: MIT */
/* Written by hostlib/gen.py from hostlib.toml: change that, then run it. */
#ifndef HOSTLIB_HL_IDS_H
#define HOSTLIB_HL_IDS_H

#define HOSTLIB_ABI 2
#define HOSTLIB_MANIFEST "258e312fa58c5abd"   /* sha256 of hostlib.toml, the first 16 hex digits */

#define HL_ID_OGFX_PLANAR_RECT 0x00010100   /* hostlib_ogfx_planar_rect, version 1 */
#define HL_ID_OGFX_CHUNKY_RECT 0x00010101   /* hostlib_ogfx_chunky_rect, version 1 */
#define HL_ID_OGFX_CHUNKY_COPY 0x00010102   /* hostlib_ogfx_chunky_copy, version 1 */
#define HL_ID_OGFX_PLANAR_BLIT 0x00010103   /* hostlib_ogfx_planar_blit, version 1 */
#define HL_ID_OGFX_TEXT_STRIP 0x00010105   /* hostlib_text_strip, version 1 */
#define HL_ID_OGFX_LAYOUT_BATCH 0x0001010d   /* hostlib_ogfx_layout_batch, version 1 */
#define HL_ID_SDL2_BLEND 0x00010106   /* hostlib_sdl2_blend, version 1 */
#define HL_ID_SDL2_BLEND_ARGB 0x0001010a   /* hostlib_sdl2_blend_argb, version 1 */
#define HL_ID_MGL_DRAW 0x00010107   /* hostlib_mgl_draw, version 1 */
#define HL_ID_MGL_VERTICES 0x00010108   /* hostlib_mgl_vertices, version 1 */
#define HL_ID_MGL_TEX 0x00010109   /* hostlib_mgl_tex, version 1 */
#define HL_ID_MGL_CONSTS 0x0001010b   /* hostlib_mgl_consts, version 1 */
#define HL_ID_MGL_CACHED 0x0001010c   /* hostlib_mgl_cached, version 1 */
#define HL_ID_W3D_VERTICES 0x00013000   /* hostlib_w3d_vertices, version 1 */
#define HL_ID_HOSTLIB_PROBE 0x00011000   /* hostlib_probe, version 1 */
#define HL_ID_ACM_CAPS 0x00012000   /* hostlib_acm_caps, version 1 */
#define HL_ID_ACM_PROBE 0x00012001   /* hostlib_acm_probe, version 1 */
#define HL_ID_ACM_OPEN 0x00012002   /* hostlib_acm_open, version 1 */
#define HL_ID_ACM_INFO 0x00012003   /* hostlib_acm_info, version 1 */
#define HL_ID_ACM_CONTROL 0x00012004   /* hostlib_acm_control, version 1 */
#define HL_ID_ACM_FRAME 0x00012005   /* hostlib_acm_frame, version 1 */
#define HL_ID_ACM_AUDIO 0x00012006   /* hostlib_acm_audio, version 1 */
#define HL_ID_ACM_FEED 0x00012007   /* hostlib_acm_feed, version 1 */
#define HL_ID_ACM_EVENTS 0x00012008   /* hostlib_acm_events, version 1 */
#define HL_ID_ACM_CLOSE 0x00012009   /* hostlib_acm_close, version 1 */
#define HL_ID_ACM_RESULT 0x0001200a   /* hostlib_acm_result, version 1 */
#define HL_ID_ACM_FAULT 0x0001200b   /* hostlib_acm_fault, version 1 */
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
