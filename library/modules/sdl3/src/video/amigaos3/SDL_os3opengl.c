/*
  SDL 3 on AmigaOS 3.x: SDL_GL_* on GFOpenGPU's GL module.
  Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).

  Replaces SDL3_68k's MiniGL back end (same functions, SDL_os3opengl.h), the
  way SDL2.module's SDL_os3gl.c does it:

  - Every GL call goes through the program's GL (struct SDL3GLBridge, from
    libSDL3.a's SDL3_gl.o and the program's libGL.a), so SDL's contexts and
    the program's gl* calls are in one copy of Mesa (GL.module: virgl on the
    PC's graphics chip, or softpipe). The module calls the program's code
    with the program's A4 (ogpu_module_callout).
  - A context is GLA's, with SDL_GL_SetAttribute's profile, version, double
    buffer, depth (16 or 24), stencil (8) and alpha.
  - Each window has one GL buffer, the size of its inside, made the first
    time a context is current on it and resized on SDL_GL_SwapWindow. Its
    frames are drawn into the window's RastPort at its inner left/top by
    GL.module's present. The window's SDL_WindowData.gl_context holds that
    buffer's record (not a context), which is how SDL3_68k's window code
    knows a window is a GL window (no framebuffer, no resize copy); its
    OS3_GL_DestroyContext call when the window closes frees the buffer.
  - SDL_OPENGPU_GL=cpu (hint or env) draws with softpipe even where the PC's
    graphics chip is there. A swap interval other than 0 waits for the
    vertical blank (WaitTOF).
*/
#include "SDL_internal.h"

#if defined(SDL_VIDEO_DRIVER_AMIGAOS3) && defined(SDL_VIDEO_OPENGL)

#include <proto/graphics.h>
#include <proto/intuition.h>
#include "SDL_os3opengl.h"
#include "SDL_os3window.h"
#include "sdl3_module.h"

#if defined(SDL_OPEN_LIBRARY_BUILD)
#include "module_rt.h"
#define OS3_GLCALL(fn, a, b, c) \
    ogpu_module_callout((const void *)(fn), (long)(a), (long)(b), (long)(c), 0)
#else
#define OS3_GLCALL(fn, a, b, c) \
    ((long (*)(long, long, long))(fn))((long)(a), (long)(b), (long)(c))
#endif

#define OS3_GL_CONTEXT_MAGIC 0x474C4358UL   /* 'GLCX' */
#define OS3_GL_WINDOW_MAGIC  0x474C574EUL   /* 'GLWN' */

/* The program's GL (SDL3Module's set_gl); none in libSDL3_static.a. */
static const struct SDL3GLBridge *OS3_gl;
static APTR OS3_gl_display;
static int OS3_gl_swap_interval;

void SDL_OS3_SetGLBridge(const struct SDL3GLBridge *bridge)
{
    OS3_gl = (bridge && bridge->version >= 1) ? bridge : NULL;
}

typedef struct OS3_GLContext
{
    ULONG magic;
    APTR gla;
    struct SDL3GLConfig cfg;
} OS3_GLContext;

typedef struct OS3_GLWindow
{
    ULONG magic;
    APTR buffer;
    struct SDL3GLTarget target;
} OS3_GLWindow;

static void OS3_GL_Aim(SDL_Window *window, OS3_GLWindow *w)
{
    SDL_WindowData *d = window ? window->internal : NULL;
    struct Window *sw = d ? d->syswin : NULL;
    w->target.rp = sw ? sw->RPort : NULL;
    w->target.left = sw ? sw->BorderLeft : 0;
    w->target.top = sw ? sw->BorderTop : 0;
}

static void OS3_GL_InnerSize(SDL_Window *window, ULONG *width, ULONG *height)
{
    int w = 0, h = 0;
    OS3_GetWindowSizeInPixels(SDL_GetVideoDevice(), window, &w, &h);
    *width = w > 0 ? (ULONG)w : 1;
    *height = h > 0 ? (ULONG)h : 1;
}

void OS3_GL_DefaultProfileConfig(SDL_VideoDevice *_this, int *mask, int *major, int *minor)
{
    (void)_this;
    /* The compatibility profile, its highest version (GL.module: 2.1 and up). */
    if (mask) *mask = 0;
    if (major) *major = 2;
    if (minor) *minor = 1;
}

bool OS3_GL_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    const char *want;
    LONG cpu_only;
    (void)path;

    if (!OS3_gl) {
        return SDL_SetError("OpenGL: link the program with -lGL after -lSDL3, and use SDL3.module 3 or later");
    }
    if (OS3_gl_display) {
        return true;
    }
    want = SDL_GetHint("SDL_OPENGPU_GL");
    cpu_only = want && (!SDL_strcasecmp(want, "cpu") || !SDL_strcasecmp(want, "softpipe") || !SDL_strcmp(want, "0"));
    OS3_gl_display = (APTR)OS3_GLCALL(OS3_gl->display, cpu_only, 0, 0);
    if (!OS3_gl_display) {
        return SDL_SetError("OpenGL: GL.module couldn't start (memory?)");
    }
    SDL_strlcpy(_this->gl_config.driver_path, "LIBS:GFOpenGPU/GL.module", sizeof(_this->gl_config.driver_path));
    return true;
}

SDL_FunctionPointer OS3_GL_GetProcAddress(SDL_VideoDevice *_this, const char *proc)
{
    (void)_this;
    if (!OS3_gl || !proc) {
        return NULL;
    }
    return (SDL_FunctionPointer)OS3_GLCALL(OS3_gl->get_proc_address, proc, 0, 0);
}

void OS3_GL_UnloadLibrary(SDL_VideoDevice *_this)
{
    (void)_this;
    if (OS3_gl && OS3_gl_display) {
        OS3_GLCALL(OS3_gl->display_destroy, OS3_gl_display, 0, 0);
    }
    OS3_gl_display = NULL;
}

/* SDL_GL_SetAttribute's settings, as GLA takes them. */
static void OS3_GL_Config(SDL_VideoDevice *_this, struct SDL3GLConfig *cfg)
{
    const int mask = _this->gl_config.profile_mask;
    SDL_zerop(cfg);
    cfg->profile = (mask & SDL_GL_CONTEXT_PROFILE_ES) ? 2 : (mask & SDL_GL_CONTEXT_PROFILE_CORE) ? 1 : 0;
    cfg->major = _this->gl_config.major_version;
    cfg->minor = _this->gl_config.minor_version;
    /* The compatibility profile's default (2.1): the highest there is. */
    if (cfg->profile == 0 && cfg->major <= 2) {
        cfg->major = cfg->minor = 0;
    }
    cfg->doublebuf = _this->gl_config.double_buffer ? 1 : 0;
    cfg->depth = _this->gl_config.depth_size > 16 ? 24 : _this->gl_config.depth_size > 0 ? 16 : 0;
    cfg->stencil = _this->gl_config.stencil_size > 0 ? 8 : 0;
    cfg->alpha = _this->gl_config.alpha_size > 0 ? 1 : 0;
}

/* The window's GL buffer, made the first time a context is current on it. */
static OS3_GLWindow *OS3_GL_Window(SDL_Window *window, const struct SDL3GLConfig *cfg)
{
    SDL_WindowData *d = window ? window->internal : NULL;
    OS3_GLWindow *w;
    ULONG width, height;

    if (!d) {
        SDL_SetError("OpenGL: the window has no Amiga window");
        return NULL;
    }
    w = (OS3_GLWindow *)d->gl_context;
    if (w || !cfg) {
        return w;
    }
    w = (OS3_GLWindow *)SDL_calloc(1, sizeof(*w));
    if (!w) {
        SDL_OutOfMemory();
        return NULL;
    }
    w->magic = OS3_GL_WINDOW_MAGIC;
    OS3_GL_Aim(window, w);
    OS3_GL_InnerSize(window, &width, &height);
    w->target.width = width;
    w->target.height = height;
    w->buffer = (APTR)OS3_GLCALL(OS3_gl->buffer, OS3_gl_display, cfg, &w->target);
    if (!w->buffer) {
        SDL_free(w);
        SDL_SetError("OpenGL: no GL buffer for the window (memory?)");
        return NULL;
    }
    d->gl_context = (SDL_GLContext)w;
    d->gl_width = (int)width;
    d->gl_height = (int)height;
    return w;
}

SDL_GLContext OS3_GL_CreateContext(SDL_VideoDevice *_this, SDL_Window *window)
{
    OS3_GLContext *c;
    APTR share = NULL;

    if (!OS3_gl || (!OS3_gl_display && !OS3_GL_LoadLibrary(_this, NULL))) {
        return NULL;
    }
    c = (OS3_GLContext *)SDL_calloc(1, sizeof(*c));
    if (!c) {
        SDL_OutOfMemory();
        return NULL;
    }
    c->magic = OS3_GL_CONTEXT_MAGIC;
    OS3_GL_Config(_this, &c->cfg);
    if (_this->gl_config.share_with_current_context && SDL_GL_GetCurrentContext()) {
        share = ((OS3_GLContext *)SDL_GL_GetCurrentContext())->gla;
    }
    c->gla = (APTR)OS3_GLCALL(OS3_gl->context, OS3_gl_display, &c->cfg, share);
    if (!c->gla) {
        SDL_free(c);
        SDL_SetError("OpenGL: GL.module has no context for these attributes (profile %d, version %d.%d)",
                     _this->gl_config.profile_mask, _this->gl_config.major_version, _this->gl_config.minor_version);
        return NULL;
    }
    /* A new context is current (SDL_GL_CreateContext). */
    if (!OS3_GL_MakeCurrent(_this, window, (SDL_GLContext)c)) {
        OS3_GLCALL(OS3_gl->context_destroy, c->gla, 0, 0);
        SDL_free(c);
        return NULL;
    }
    return (SDL_GLContext)c;
}

bool OS3_GL_MakeCurrent(SDL_VideoDevice *_this, SDL_Window *window, SDL_GLContext context)
{
    OS3_GLContext *c = (OS3_GLContext *)context;
    OS3_GLWindow *w;
    (void)_this;

    if (!OS3_gl) {
        return SDL_SetError("OpenGL: no GL library loaded");
    }
    if (!c || !window) {
        OS3_GLCALL(OS3_gl->make_current, 0, 0, 0);
        return true;
    }
    if (c->magic != OS3_GL_CONTEXT_MAGIC) {
        return SDL_SetError("OpenGL: not a context");
    }
    w = OS3_GL_Window(window, &c->cfg);
    if (!w) {
        return false;
    }
    if (!OS3_GLCALL(OS3_gl->make_current, c->gla, w->buffer, 0)) {
        return SDL_SetError("OpenGL: the context doesn't fit the window's GL buffer");
    }
    return true;
}

void OS3_GL_ResizeWindow(SDL_VideoDevice *_this, SDL_Window *window, int width, int height)
{
    /* The buffer follows the window at the next SDL_GL_SwapWindow. */
    (void)_this; (void)window; (void)width; (void)height;
}

bool OS3_GL_SetSwapInterval(SDL_VideoDevice *_this, int interval)
{
    (void)_this;
    OS3_gl_swap_interval = interval;
    return true;
}

bool OS3_GL_GetSwapInterval(SDL_VideoDevice *_this, int *interval)
{
    (void)_this;
    if (interval) *interval = OS3_gl_swap_interval;
    return true;
}

bool OS3_GL_SwapWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    OS3_GLContext *c = (OS3_GLContext *)SDL_GL_GetCurrentContext();
    SDL_WindowData *d = window ? window->internal : NULL;
    OS3_GLWindow *w = d ? (OS3_GLWindow *)d->gl_context : NULL;
    ULONG width, height;
    (void)_this;

    if (!OS3_gl || !c || !w) {
        return SDL_SetError("OpenGL: no context is current on this window");
    }
    OS3_GL_InnerSize(window, &width, &height);
    if (width != w->target.width || height != w->target.height) {
        OS3_GLCALL(OS3_gl->buffer_resize, w->buffer, width, height);
        w->target.width = width;
        w->target.height = height;
        d->gl_width = (int)width;
        d->gl_height = (int)height;
    }
    OS3_GL_Aim(window, w);
    if (!w->target.rp) {
        return true;   /* the window is between screens */
    }
    if (OS3_gl_swap_interval != 0) {
        WaitTOF();
    }
    OS3_GLCALL(OS3_gl->swap, c->gla, w->buffer, &w->target);
    return true;
}

/* A context, or (when the window closes, SDL_os3window.c) a window's GL
   buffer record. */
bool OS3_GL_DestroyContext(SDL_VideoDevice *_this, SDL_GLContext context)
{
    ULONG magic;
    (void)_this;
    if (!context || !OS3_gl) {
        return true;
    }
    magic = *(ULONG *)context;
    if (magic == OS3_GL_WINDOW_MAGIC) {
        OS3_GLWindow *w = (OS3_GLWindow *)context;
        SDL_Window *window;
        for (window = SDL_GetVideoDevice() ? SDL_GetVideoDevice()->windows : NULL; window; window = window->next) {
            SDL_WindowData *d = window->internal;
            if (d && d->gl_context == context) {
                if (SDL_GL_GetCurrentWindow() == window) {
                    OS3_GLCALL(OS3_gl->make_current, 0, 0, 0);
                }
                d->gl_context = NULL;
                d->gl_width = d->gl_height = 0;
            }
        }
        OS3_GLCALL(OS3_gl->buffer_destroy, w->buffer, 0, 0);
        w->magic = 0;
        SDL_free(w);
        return true;
    }
    if (magic == OS3_GL_CONTEXT_MAGIC) {
        OS3_GLContext *c = (OS3_GLContext *)context;
        if (SDL_GL_GetCurrentContext() == context) {
            OS3_GLCALL(OS3_gl->make_current, 0, 0, 0);
        }
        OS3_GLCALL(OS3_gl->context_destroy, c->gla, 0, 0);
        c->magic = 0;
        SDL_free(c);
        return true;
    }
    return SDL_SetError("OpenGL: not a context");
}

#else

/* No OpenGL in this build: the module's set_gl has nothing to set. */
struct SDL3GLBridge;
void SDL_OS3_SetGLBridge(const struct SDL3GLBridge *bridge)
{
    (void)bridge;
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 && SDL_VIDEO_OPENGL */
