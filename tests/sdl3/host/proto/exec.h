/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * exec.library's calls that stubs/sdl3 and stubs/openal make, answered by its host test
 * (tests/sdl3/stub_nomodule.c). */
#ifndef PROTO_EXEC_H
#define PROTO_EXEC_H
#include <exec/execbase.h>
#include <exec/tasks.h>
extern struct ExecBase *SysBase;
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *lib);
struct Task *FindTask(CONST_STRPTR name);
#endif
