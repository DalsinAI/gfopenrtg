/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * dos/dosextens.h's Process as far as stubs/stub_tell.h reads it, for the stubs' host tests. */
#ifndef DOS_DOSEXTENS_H
#define DOS_DOSEXTENS_H
#include <exec/tasks.h>
#include <dos/dos.h>
struct Process { struct Task pr_Task; BPTR pr_CLI; };
#endif
