/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * For tests/test_gla_virgl_os3.c on the PC: just what gla_virgl_os3.c uses. */
#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H
#include <exec/types.h>
#define TIMERNAME "timer.device"
#define UNIT_ECLOCK 2
struct Device { int id; };
struct IORequest { struct Device *io_Device; };
struct timerequest { struct IORequest tr_node; };
struct EClockVal { ULONG ev_hi, ev_lo; };
LONG OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags);
void CloseDevice(struct IORequest *io);
#endif
