/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libminigl.a (and libmgl.a): opening minigl.library from a program.
 * Clean-room, from docs/design/MiniGL-Compatible-Spec.md sections 3 and 4
 * (see library/minigl/PROVENANCE.md):
 *   - OpenLibrary("minigl.library", MINIGL_OPEN_VERSION);
 *   - the library's one function, at -30, called with A6 = the base and
 *     nothing else, answers the table in D0;
 *   - the table is refused unless its ABI version is exactly 5 and its size
 *     is at least the size these headers know (205 entries, 840 bytes).
 * The pointer is kept until MiniGLClose. */
#include <exec/types.h>
#include <exec/libraries.h>
#include <proto/exec.h>
#include <libraries/minigl.h>

struct Library *MiniGLBase;
const MGLDispatchTable *MiniGLDispatch;

static const MGLDispatchTable *fetch_table(struct Library *base)
{
    register struct Library *a6 __asm("a6") = base;
    register const MGLDispatchTable *d0 __asm("d0");
    __asm volatile ("jsr -30(%%a6)" : "=r"(d0) : "r"(a6) : "d1", "a0", "a1", "cc", "memory");
    return d0;
}

BOOL MiniGLOpen(void)
{
    const MGLDispatchTable *t;
    if (MiniGLDispatch) return TRUE;
    MiniGLBase = OpenLibrary((CONST_STRPTR)MINIGL_NAME, MINIGL_OPEN_VERSION);
    if (!MiniGLBase) return FALSE;
    t = fetch_table(MiniGLBase);
    if (!t || t->abi_version != MGL_DISPATCH_ABI || t->table_size < MGL_DISPATCH_SIZE || !t->current_context) {
        CloseLibrary(MiniGLBase);
        MiniGLBase = 0;
        return FALSE;
    }
    MiniGLDispatch = t;
    return TRUE;
}

void MiniGLClose(void)
{
    MiniGLDispatch = 0;
    if (MiniGLBase) {
        CloseLibrary(MiniGLBase);
        MiniGLBase = 0;
    }
}
