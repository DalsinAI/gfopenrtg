/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * Which GL driver a program gets on OS 3.2, and the notice when it is
 * softpipe (gla_virgl_os3.c). The policy is gla_virgl_os3.c's, tested on the
 * PC (tests/test_gla_virgl_os3.c); the three calls it makes on the Amiga are
 * gla_notice_os3.c's. Internal to GL.module: not in the SDK's headers. */
#ifndef GLA_NOTICE_OS3_H
#define GLA_NOTICE_OS3_H

/* ENV:GFOpenGPU/GLDriver */
enum { GLA_OS3_DRIVER_AUTO = 0, GLA_OS3_DRIVER_GPU = 1, GLA_OS3_DRIVER_CPU = 2 };
/* "gpu"/"virgl", "cpu"/"softpipe" (any case), else auto. */
int gla_os3_driver_of(const char *value);
/* The variable's, now. */
int gla_os3_gl_driver(void);
/* Registers the softpipe hook (gla_core.h); gla_os3_virgl_transport does
 * too. GL.module's start calls it, for programs that go straight to
 * softpipe. */
void gla_os3_driver_init(void);

/* gla_notice_os3.c: a variable's value into buf (GetVar), its length or -1. */
int gla_os3_getenv(const char *name, char *buf, int size);
/* One line on the program's output (Output()) and in the runtime's log
 * (gfopenservice.device's log/1, when the machine has it). */
void gla_os3_say(const char *line);
/* Why the machine's ACRTG board gives no virgl (no board, a runtime before
 * the ring, no virglrenderer), or NULL when the board does. */
const char *gla_os3_board_why(void);

#endif
