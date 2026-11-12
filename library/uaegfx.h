/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The uaegfx backend (0.26): GFOpenRTG on the RTG board of the UAE family of
 * emulators (Amiberry, WinUAE, FS-UAE). docs/design/UAEGFX-20261111.md.
 *
 * screens.c and openrtg_lib.c talk to every board as they talk to ACRTG: a
 * register page. For a uaegfx monitor, reg() and rreg() come here instead;
 * the writes that mean something (a mode, where the picture starts, the
 * display, the switch, a pen, the pointer) become calls to the board's own
 * functions in the emulator's BoardInfo, and every read answers 0, so
 * nothing past ACRTG v0 (the ring, the card part, the watch, the Native
 * size) is ever switched on. Nothing of this reaches the emulator as
 * registers: the uaegfx board has none.
 */
#ifndef ORTG_UAEGFX_H
#define ORTG_UAEGFX_H

#include "ortg_state.h"

struct ConfigDev;

/* The largest picture GFOpenRTG offers on the board; its video RAM decides the rest (modes.c). */
#define UGX_MAX_W 3840
#define UGX_MAX_H 2160

/* What ortg_S->ugx points at: the board, monitor 1 when no ACRTG board is fitted. */
struct ugx_board;
int ugx_monitor_of(const struct ugx_board *u);

/* Finds the emulator's uaegfx board and readies it as monitor `monitor`: 1, with *cd (a ConfigDev of
 * the library's own whose cd_BoardAddr and cd_BoardSize are the board's video RAM) and *vram_bytes;
 * 0 when there is no such board, Picasso96 has it, or memory runs out. Once only. */
int ugx_attach(int monitor, struct ConfigDev **cd, ULONG *vram_bytes);

/* The register page's stand-in. */
void ugx_reg(ULONG off, ULONG v);
ULONG ugx_rreg(ULONG off);

/* 1 when monitor n is the uaegfx board. */
static inline int ugx_is(int n)
{
    return ortg_S->ugx && ugx_monitor_of((const struct ugx_board *)ortg_S->ugx) == n;
}

#endif
