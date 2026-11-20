/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libSDL3.a's SDL_GL_ functions, and the program's GL for SDL3.module: the
 * same as libSDL2.a's (stubs/sdl2/SDL2_gl.c), with SDL 3's names.
 *
 * This member is linked only when the program calls an SDL_GL_ function,
 * and it then needs libGL.a (-lGL after -lSDL3). Its SDL_GL_ functions
 * jump through the table like every other SDL
 * function (SDL3_stub.h). Before main runs, it hands the stub the calls
 * below, which reach GL through the program's own libGL.a; the stub gives
 * them to the module (set_gl, sdl3_module.h), whose SDL_GL_CreateContext,
 * SDL_GL_SwapWindow and the rest call them. So SDL's context and the
 * program's gl* calls are in the same copy of GL. The module calls them
 * with the program's A4. */
#include "SDL3_stub.h"

#include <gla_core.h>
#include <os3/gla_present_os3.h>
#include <os3/gla_virgl_os3.h>

/* The window GL calls: SDL3_gl_procs.h has only them. */
#define SDL_DYNAPI_PROC SDL3STUB_PROC
#define SDL_DYNAPI_PROC_NO_VARARGS
#include "SDL3_gl_procs.h"
#undef SDL_DYNAPI_PROC
#undef SDL_DYNAPI_PROC_NO_VARARGS

/* SDL3GLConfig is GLA's struct gla_config; the target's priv holds GLA's
 * present hook and target. */
struct SDL3GL_priv {
    struct gla_os3_target os3;
    struct gla_present hook;
};
typedef char SDL3GL_config_matches[sizeof(struct SDL3GLConfig) == sizeof(struct gla_config) ? 1 : -1];
typedef char SDL3GL_priv_fits[sizeof(struct SDL3GL_priv) <= sizeof(((struct SDL3GLTarget *)0)->priv) ? 1 : -1];

static APTR SDL3GL_Display(LONG cpu_only)
{
    struct gla_display *d = NULL;
    if (!cpu_only) {
        struct gla_virgl_transport t;
        if (gla_os3_virgl_transport(&t)) {
            d = gla_display_create_virgl(&t);
        }
    }
    return d ? d : gla_display_create();
}

static void SDL3GL_DisplayDestroy(APTR d)
{
    gla_display_destroy((struct gla_display *)d);
}

static const char *SDL3GL_Driver(APTR d)
{
    return gla_display_driver((struct gla_display *)d);
}

static APTR SDL3GL_Context(APTR d, const struct SDL3GLConfig *cfg, APTR share)
{
    return gla_context_create((struct gla_display *)d, (const struct gla_config *)cfg, (struct gla_context *)share);
}

static void SDL3GL_ContextDestroy(APTR c)
{
    gla_context_destroy((struct gla_context *)c);
}

static void SDL3GL_Aim(struct SDL3GLTarget *t)
{
    struct SDL3GL_priv *p = (struct SDL3GL_priv *)t->priv;
    p->os3.rp = t->rp;
    p->os3.bitmap = NULL;
    p->os3.left = t->left;
    p->os3.top = t->top;
}

static APTR SDL3GL_Buffer(APTR d, const struct SDL3GLConfig *cfg, struct SDL3GLTarget *t)
{
    struct SDL3GL_priv *p = (struct SDL3GL_priv *)t->priv;
    SDL3GL_Aim(t);
    gla_os3_present_init(&p->hook, &p->os3);
    return gla_buffer_create((struct gla_display *)d, (const struct gla_config *)cfg, t->width, t->height, &p->hook);
}

static void SDL3GL_BufferResize(APTR b, ULONG w, ULONG h)
{
    gla_buffer_resize((struct gla_buffer *)b, w, h);
}

static void SDL3GL_BufferDestroy(APTR b)
{
    gla_buffer_destroy((struct gla_buffer *)b);
}

static LONG SDL3GL_MakeCurrent(APTR c, APTR b)
{
    return gla_make_current((struct gla_context *)c, (struct gla_buffer *)b);
}

static void SDL3GL_Swap(APTR c, APTR b, struct SDL3GLTarget *t)
{
    SDL3GL_Aim(t);
    gla_swap((struct gla_context *)c, (struct gla_buffer *)b);
}

static APTR SDL3GL_GetProcAddress(const char *name)
{
    return gla_get_proc_address(name);
}

static const struct SDL3GLBridge SDL3GL_bridge = {
    SDL3_GL_BRIDGE_VERSION,
    SDL3GL_Display,
    SDL3GL_DisplayDestroy,
    SDL3GL_Driver,
    SDL3GL_Context,
    SDL3GL_ContextDestroy,
    SDL3GL_Buffer,
    SDL3GL_BufferResize,
    SDL3GL_BufferDestroy,
    SDL3GL_MakeCurrent,
    SDL3GL_Swap,
    SDL3GL_GetProcAddress,
};

static void __attribute__((constructor)) SDL3GL_Register(void)
{
    SDL3Stub_gl = &SDL3GL_bridge;
}
