/* SPDX-License-Identifier: MIT */
#ifndef HOSTLIB_OGFX_LEAVES_H
#define HOSTLIB_OGFX_LEAVES_H
/* GFOpenGfx's bitmap leaves: the existing $00010100-$00010103 contracts,
 * expressed as hostlib big-endian-long blocks. Copyright (c) 2026
 * AmiCradle Team. MIT. */
#include "../hl.h"

enum { HL_OPR_PLANES, HL_OPR_BPR, HL_OPR_DEPTH, HL_OPR_X0, HL_OPR_Y0,
       HL_OPR_X1, HL_OPR_Y1, HL_OPR_PEN, HL_OPR_MASK, HL_OPR_MODE,
       HL_OPR_LONGS };
#define HL_OPR_BLOCK (4u * HL_OPR_LONGS)

enum { HL_OCR_BASE, HL_OCR_BPR, HL_OCR_BPP, HL_OCR_X0, HL_OCR_Y0,
       HL_OCR_X1, HL_OCR_Y1, HL_OCR_COLOR, HL_OCR_MODE, HL_OCR_LONGS };
#define HL_OCR_BLOCK (4u * HL_OCR_LONGS)

enum { HL_OCC_SRC, HL_OCC_SBPR, HL_OCC_DST, HL_OCC_DBPR, HL_OCC_BPP,
       HL_OCC_SX, HL_OCC_SY, HL_OCC_DX, HL_OCC_DY, HL_OCC_WIDTH,
       HL_OCC_HEIGHT, HL_OCC_LONGS };
#define HL_OCC_BLOCK (4u * HL_OCC_LONGS)

enum { HL_OPB_SRC_PLANES, HL_OPB_SBPR, HL_OPB_DST_PLANES, HL_OPB_DBPR,
       HL_OPB_DEPTH, HL_OPB_SX, HL_OPB_SY, HL_OPB_DX, HL_OPB_DY,
       HL_OPB_WIDTH, HL_OPB_HEIGHT, HL_OPB_MINTERM, HL_OPB_MASK,
       HL_OPB_SAME, HL_OPB_LONGS };
#define HL_OPB_BLOCK (4u * HL_OPB_LONGS)

#ifndef HL_HOST
uint32_t hostlib_ogfx_planar_rect(hl_addr block);
uint32_t hostlib_ogfx_chunky_rect(hl_addr block);
uint32_t hostlib_ogfx_chunky_copy(hl_addr block);
uint32_t hostlib_ogfx_planar_blit(hl_addr block);
#else
uint32_t hostlib_ogfx_planar_rect_host(struct hl_ctx *, hl_addr block);
uint32_t hostlib_ogfx_chunky_rect_host(struct hl_ctx *, hl_addr block);
uint32_t hostlib_ogfx_chunky_copy_host(struct hl_ctx *, hl_addr block);
uint32_t hostlib_ogfx_planar_blit_host(struct hl_ctx *, hl_addr block);
#endif
#endif
