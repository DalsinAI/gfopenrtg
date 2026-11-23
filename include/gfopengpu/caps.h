/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * gfopengpu.library's capabilities (0.30): OGPU_Query, the one place any library learns what the machine can do
 * (docs/design/OpenGPU-Shared-Pipe-Plan.md 2.8, stage MQ; every op and every PARTIAL in docs/design/OGPU-Query.md).
 *
 *   OGPU_Query(OGPU_OP_TEXTURE_2D, OGPU_FMT_RGB565)       FULL, PARTIAL or NONE, for the best back end there is
 *   OGPU_QueryDriver(OGPU_DRV_CPU, OGPU_OP_FOG, 0)        the same, for a named back end
 *   OGPU_QueryLimit(OGPU_OP_MAX_TEXTURE_SIZE)             a number (0: not known)
 *
 * The answer word is OGPU_Query's, as since 0.1: OGPU_ANSWER (FULL, PARTIAL, NONE), OGPU_BACKEND, OGPU_ONGPU.
 *
 * The capability ops are a range of their own, 0x8000 to 0x80FF, which no stream command may use, so they never
 * meet a stream opcode. Their answers come from one discovery per boot inside the library (virgl's HELLO,
 * ACV_CAPSET_INFO and ACV_CAPS, the CPU core's own table), kept in the library base. A library older than 0.30
 * answers NONE to every op here, as to any op it doesn't know; ask lib_Revision >= OGPU_CAPS_REVISION before calling
 * OGPU_QueryLimit or OGPU_QueryDriver, which it doesn't have.
 *
 * LVOs (gfopengpu_lib.sfd):
 *   198 OGPU_QueryLimit(op)(d0)
 *   204 OGPU_QueryDriver(drv, op, format)(d0,d1,d2) */
#ifndef OPENGPU_CAPS_H
#define OPENGPU_CAPS_H

#include "gfopengpu.h"

#define OGPU_CAPS_REVISION 30           /* the library's lib_Revision (version 0) that answers these */

#define OGPU_OP_CAP_FIRST          0x8000
#define OGPU_OP_CAP_LAST           0x80FF
#define OGPU_OP_IS_CAP(op)         ((unsigned long)(op) >= OGPU_OP_CAP_FIRST && (unsigned long)(op) <= OGPU_OP_CAP_LAST)

/* the proposal's names for the answers: the same values programs already compare with */
#define OGPU_CAP_FULL              OGPU_FULL
#define OGPU_CAP_PARTIAL           OGPU_PARTIAL
#define OGPU_CAP_NONE              OGPU_NONE

/* back ends (format 0) */
#define OGPU_OP_REAL_GPU           0x8001  /* virgl draws on a graphics chip */
#define OGPU_OP_LLVM_PIPE          0x8002  /* virgl, drawn by the host's software renderer */
#define OGPU_OP_CPU_CORE           0x8003  /* the 68k core (ogpu_3d.c): always there */
#define OGPU_OP_VIRGL_PRESENT      0x8004  /* the host writes a frame straight into a bitmap (ACV_PRESENT) */
#define OGPU_OP_VIRGL_GLES         0x8005  /* the host's virglrenderer runs on GLES */
/* OGPU_OP_VIRGL (0x0080, the stream command) keeps its own answer: virgl is carried */

/* resources (format: an OGPU_FMT_) */
#define OGPU_OP_TEXTURE_2D         0x8010
#define OGPU_OP_RENDER_TARGET      0x8011
#define OGPU_OP_DEPTH_STENCIL      0x8012
#define OGPU_OP_BUFFER             0x8013  /* format 0 */
#define OGPU_OP_MIPMAP             0x8014

/* fixed function (format 0) */
#define OGPU_OP_FF_TEXTURE_ENV     0x8020
#define OGPU_OP_FF_FOG             0x8021
#define OGPU_OP_FF_MULTITEXTURE    0x8022
#define OGPU_OP_FF_LIGHTING        0x8023
#define OGPU_OP_FF_TEXGEN          0x8024
#define OGPU_OP_FF_ALPHA_TEST      0x8025
#define OGPU_OP_FF_CHROMA          0x8026
#define OGPU_OP_FF_SPECULAR        0x8027

/* tests and blending (format 0) */
#define OGPU_OP_BLEND              0x8030
#define OGPU_OP_LOGIC_OP           0x8031
#define OGPU_OP_DEPTH_TEST         0x8032
#define OGPU_OP_STENCIL_TEST       0x8033
#define OGPU_OP_SCISSOR            0x8034

/* shaders (format 0) */
#define OGPU_OP_TGSI               0x8040

/* presentation (format 0): answered from what OpenRTG registers with the library; NONE until it does */
#define OGPU_OP_PRESENT_WINDOW     0x8050
#define OGPU_OP_PRESENT_SCREEN     0x8051
#define OGPU_OP_PRESENT_BITMAP     0x8052
#define OGPU_OP_VSYNC              0x8053

/* limits, for OGPU_QueryLimit */
#define OGPU_OP_MAX_TEXTURE_SIZE   0x8080
#define OGPU_OP_MAX_TEXTURE_UNITS  0x8081
#define OGPU_OP_MAX_VERTEX_ATTRIBS 0x8082
#define OGPU_OP_MAX_VIEWPORT_DIMS  0x8083

/* Formats. The stream's (stream.h, 1 to 12) and the 3D buffers' (stream3d.h: OGPU_FMT_Z16 16, OGPU_FMT_Z32 17,
 * OGPU_FMT_S8 18) are asked by their own numbers. These are for OGPU_Query only, from 32 so they never meet a
 * stream format; a stream command never takes them, and OGPU_FMT_COUNT stays the stream's. */
#define OGPU_FMT_CAP_FIRST         32
#define OGPU_FMT_RGBA4444          32      /* textures: Warp3D's W3D_A4R4G4B4, MiniGL's GL_RGBA4 */
#define OGPU_FMT_RGBA5551          33      /* W3D_A1R5G5B5, GL_RGB5_A1 */
#define OGPU_FMT_L8                34      /* W3D_L8, GL_LUMINANCE */
#define OGPU_FMT_LA88              35      /* W3D_L8A8, GL_LUMINANCE_ALPHA */
#define OGPU_FMT_Z24               36      /* depth: 24 bits (stored as 32) */
#define OGPU_FMT_Z24S8             37      /* depth 24 and stencil 8, packed */
#define OGPU_FMT_CAP_LAST          37

/* back ends, for OGPU_QueryDriver (and OGPU_Driver, stage M9) */
#define OGPU_DRV_BEST              0       /* the best there is: what OGPU_Query answers for */
#define OGPU_DRV_VIRGL             1       /* virgl, on a graphics chip */
#define OGPU_DRV_LLVMPIPE          2       /* virgl with no chip: the host's software renderer */
#define OGPU_DRV_CPU               3       /* the 68k core */
#define OGPU_DRV_ACRTG             4       /* the driver's stream route (ACRTG.gpu); deprecated with M11 */

#endif
