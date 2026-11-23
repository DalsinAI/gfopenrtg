/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * exec/tasks.h and exec/nodes.h names that stubs/stub_tell.h uses, for the stubs' host tests. */
#ifndef EXEC_TASKS_H
#define EXEC_TASKS_H
#include <exec/types.h>
#define NT_TASK 1
#define NT_PROCESS 13
struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name; };
struct Task { struct Node tc_Node; };
#endif
