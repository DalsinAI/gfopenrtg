/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Clean room: the part of the CyberGraphX interface gla_present_os3.c uses,
 * with the numbers of the Team's own cybergraphics.library
 * (compat/cybergraphics_lib.c), so GL.module builds from the open stack
 * (GFOpenAmigaGCC, libnix, OpenNDK) with no vendor CyberGraphX headers. */
#ifndef CYBERGRAPHX_CYBERGRAPHICS_H
#define CYBERGRAPHX_CYBERGRAPHICS_H

#include <exec/types.h>
#include <utility/tagitem.h>

/* GetCyberMapAttr */
#define CYBRMATTR_WIDTH         0x80000005UL
#define CYBRMATTR_HEIGHT        0x80000006UL

/* Pixel formats (LBMI_PIXFMT) and WritePixelArray's source format */
#define PIXFMT_RGB16            5UL
#define PIXFMT_ARGB32           11UL
#define RECTFMT_ARGB            2UL

/* LockBitMapTagList */
#define LBMI_PIXFMT             0x84001004UL
#define LBMI_BYTESPERROW        0x84001006UL
#define LBMI_BASEADDRESS        0x84001007UL

#endif
