/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * SDL3.module's interface: what its first code takes and gives back. The
 * link stub (stubs/sdl3) and the module (library/modules/sdl3/module) share
 * it. It is the module ABI of include/gfopengpu/module.h, the same as
 * SDL2.module's (stubs/sdl2/sdl2_module.h): an entry taking SysBase, DOSBase,
 * OpenGPUBase and a version, returning the module's table, which starts with
 * struct OGPUModuleTable (its version and the A4 for calls into it).
 *
 * The table's first function is SDL's own SDL_DYNAPI_entry: it fills the
 * caller's SDL jump table (SDL_dynapi_procs.h, SDL's dynamic API version 2),
 * so every SDL_ call a program makes goes to the module's code, with the
 * table's A4 set by the stub.
 *
 * Version 2 (22 October 2026): the first; 2 because it starts with the A4
 * calls (OGPU_MODULE_A4_VERSION), as SDL2.module's version 2 does.
 * Version 3 (19 November 2026): set_gl, OpenGL through the program's GL
 * (below), as SDL2.module's version 3. A stub still opens version 2 and
 * hands its GL over only to a module of version 3 or later. */
#ifndef SDL3_MODULE_H
#define SDL3_MODULE_H

#include <exec/types.h>
#include <exec/execbase.h>
#include <gfopengpu/module.h>

#define SDL3_MODULE_NAME    "SDL3"
#define SDL3_MODULE_FILE    "LIBS:GFOpenGPU/SDL3.module"
#define SDL3_MODULE_VERSION 3
#define SDL3_MODULE_GL_VERSION 3        /* the first with set_gl */
#define SDL3_MODULE_OPEN_VERSION 2      /* what a stub asks for */

typedef struct OGPUModuleArgs SDL3ModuleArgs;

struct SDL3GLBridge;

struct SDL3ModuleTable {
    struct OGPUModuleTable head;    /* SDL3_MODULE_VERSION, a4, caller_a4 */
    LONG (*dynapi_entry)(ULONG apiver, void *table, ULONG tablesize);  /* SDL_DYNAPI_entry */
    void (*close)(void);            /* before OGPU_ModuleClose: SDL_Quit, libnix's exit list */
    /* Version 3: the program's GL (below), or NULL for none. */
    void (*set_gl)(const struct SDL3GLBridge *bridge);
};

/* SDL_GL_* on GFOpenGPU's GL module, exactly as SDL2.module does it
 * (stubs/sdl2/sdl2_module.h, the same layout): the program has one copy of
 * GL, the one its libGL.a opens, and libSDL3.a's SDL3_gl.o (linked when the
 * program calls an SDL_GL_ function, and then needing -lGL) hands the module
 * these calls into it. The module calls them with the program's A4, so each
 * takes at most four arguments. SDL3GLConfig is GLA's struct gla_config. */
#define SDL3_GL_BRIDGE_VERSION 1

struct SDL3GLConfig {
    LONG profile;               /* 0 compatibility, 1 core, 2 GLES */
    LONG major, minor;          /* 0, 0: the highest the profile has */
    LONG doublebuf;
    LONG depth, stencil;        /* bits */
    LONG alpha;
};

/* Where a buffer's frames are shown: a RastPort, its left and top, and the
 * size. priv is the stub's (GLA's present hook and target). */
struct RastPort;
struct SDL3GLTarget {
    struct RastPort *rp;
    LONG left, top;
    ULONG width, height;
    ULONG priv[16];
};

struct SDL3GLBridge {
    ULONG version;              /* SDL3_GL_BRIDGE_VERSION */
    APTR (*display)(LONG cpu_only);
    void (*display_destroy)(APTR display);
    const char *(*driver)(APTR display);            /* "virgl" or "softpipe" */
    APTR (*context)(APTR display, const struct SDL3GLConfig *cfg, APTR share);
    void (*context_destroy)(APTR context);
    APTR (*buffer)(APTR display, const struct SDL3GLConfig *cfg, struct SDL3GLTarget *target);
    void (*buffer_resize)(APTR buffer, ULONG width, ULONG height);
    void (*buffer_destroy)(APTR buffer);
    LONG (*make_current)(APTR context, APTR buffer);  /* context NULL: none current */
    void (*swap)(APTR context, APTR buffer, struct SDL3GLTarget *target);
    APTR (*get_proc_address)(const char *name);
};

/* The module's first code: NULL if it can't start (too old, no memory). */
typedef struct OGPUModuleTable *(*SDL3ModuleEntry)(SDL3ModuleArgs *args);

#endif
