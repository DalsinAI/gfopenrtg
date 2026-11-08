/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * libEGL.a: EGL 1.4 on GFOpenGPU's GL module, for the GLES and GL programs
 * that start GL through EGL (most of the Khronos world's samples). The one
 * display is a GLA display (gla/gla_core.h, in libGL.a): virgl on the GPU
 * where GFOpenGPU's back end has it, else Mesa's softpipe on the 68k
 * (ENV:GFOpenGPU/GLDriver picks, as for every GL program). A context is a
 * GLA context; a window surface is a GLA buffer whose frames go into an
 * Intuition window's RastPort, inside its borders, and a pbuffer one that
 * is never shown.
 *
 * Configs: RGB565, RGB888 and RGBA8888, each with depth 0, 16 or 24 and
 * stencil 0 or 8. GLA always draws 32-bit pixels, so the colour sizes are
 * what the config says, not what GL draws into: an RGB565 config draws
 * 8 bits a channel, and the window's screen shows what it can.
 *
 * One task uses EGL at a time: the current context is the program's, not a
 * thread's. Written by the Team from the EGL 1.4 specification. */
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/graphics.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gla/gla_core.h>
#include <gla/os3/gla_present_os3.h>
#include <gla/os3/gla_virgl_os3.h>

#define MAX_PBUFFER 4096

struct egl_config {
    EGLint id;
    EGLint red, green, blue, alpha;
    EGLint depth, stencil;
};

/* 3 colour formats x depth 0, 16, 24 x stencil 0, 8; filled at start. */
#define NCONFIGS 18
static struct egl_config configs[NCONFIGS];

enum { SURF_WINDOW = 1, SURF_PBUFFER = 2 };

struct egl_surface {
    struct egl_surface *next;
    int kind;                           /* SURF_ */
    int destroyed;                      /* eglDestroySurface while current: freed when released */
    const struct egl_config *cfg;
    EGLint render_buffer;               /* EGL_BACK_BUFFER or EGL_SINGLE_BUFFER */
    struct Window *window;
    unsigned w, h;
    EGLint largest;
    struct gla_os3_target target;
    struct gla_present hook;
    struct gla_buffer *b;
};

struct egl_context {
    struct egl_context *next;
    int destroyed;
    const struct egl_config *cfg;
    EGLenum api;
    EGLint major, minor;
    struct gla_context *c;
};

static struct {
    int initialized;
    struct gla_display *d;
    int es_version, core_version, compat_version;   /* major * 10 + minor, 0 when none */
    EGLint renderable;
    struct egl_surface *surfaces;
    struct egl_context *contexts;
} dpy;

static EGLint error = EGL_SUCCESS;
static EGLenum api = EGL_OPENGL_ES_API;
static EGLint swap_interval = 1;
static struct egl_context *cur_ctx;
static struct egl_surface *cur_draw;

#define DISPLAY ((EGLDisplay)&dpy)

static EGLBoolean fail(EGLint e)
{
    error = e;
    return EGL_FALSE;
}

static void *fail_null(EGLint e)
{
    error = e;
    return NULL;
}

static EGLBoolean ok(void)
{
    error = EGL_SUCCESS;
    return EGL_TRUE;
}

/* EGL_BAD_DISPLAY or EGL_NOT_INITIALIZED, or 0 when dpy_ is usable. */
static EGLint check_display(EGLDisplay d)
{
    if (d != DISPLAY)
        return EGL_BAD_DISPLAY;
    return dpy.initialized ? 0 : EGL_NOT_INITIALIZED;
}

static const struct egl_config *config_of(EGLConfig c)
{
    const struct egl_config *k = (const struct egl_config *)c;
    return k >= configs && k < configs + NCONFIGS ? k : NULL;
}

static struct egl_surface *surface_of(EGLSurface s)
{
    struct egl_surface *p;
    for (p = dpy.surfaces; p; p = p->next)
        if ((EGLSurface)p == s && !p->destroyed)
            return p;
    return NULL;
}

static struct egl_context *context_of(EGLContext c)
{
    struct egl_context *p;
    for (p = dpy.contexts; p; p = p->next)
        if ((EGLContext)p == c && !p->destroyed)
            return p;
    return NULL;
}

/* The GLA config for an EGL config: GLA's visual comes from it, for both
 * the context and the buffer, so the two always match. */
static void gla_cfg_of(struct gla_config *g, const struct egl_config *k, int doublebuf)
{
    memset(g, 0, sizeof *g);
    g->doublebuf = doublebuf;
    g->depth = k->depth;
    g->stencil = k->stencil;
    g->alpha = k->alpha > 0;
}

/* --- Displays ------------------------------------------------------------ */

EGLint eglGetError(void)
{
    EGLint e = error;
    error = EGL_SUCCESS;
    return e;
}

/* One display, the machine's, whatever the program names. */
EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    (void)display_id;
    return DISPLAY;
}

static void make_configs(void)
{
    static const EGLint rgba[3][4] = { { 5, 6, 5, 0 }, { 8, 8, 8, 0 }, { 8, 8, 8, 8 } };
    static const EGLint depths[3] = { 0, 16, 24 };
    int i, j, s, n = 0;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            for (s = 0; s < 2; s++) {
                struct egl_config *k = &configs[n];
                k->id = ++n;
                k->red = rgba[i][0];
                k->green = rgba[i][1];
                k->blue = rgba[i][2];
                k->alpha = rgba[i][3];
                k->depth = depths[j];
                k->stencil = s ? 8 : 0;
            }
}

EGLBoolean eglInitialize(EGLDisplay d, EGLint *major, EGLint *minor)
{
    if (d != DISPLAY)
        return fail(EGL_BAD_DISPLAY);
    if (!dpy.initialized) {
        struct gla_virgl_transport t;
        dpy.d = NULL;
        if (gla_os3_virgl_transport(&t))
            dpy.d = gla_display_create_virgl(&t);
        if (!dpy.d)
            dpy.d = gla_display_create();
        if (!dpy.d)
            return fail(EGL_NOT_INITIALIZED);
        dpy.es_version = gla_display_version(dpy.d, GLA_ES2);
        dpy.core_version = gla_display_version(dpy.d, GLA_CORE);
        dpy.compat_version = gla_display_version(dpy.d, GLA_COMPAT);
        dpy.renderable = 0;
        if (dpy.es_version >= 20)
            dpy.renderable |= EGL_OPENGL_ES2_BIT;
        if (dpy.es_version >= 30)
            dpy.renderable |= EGL_OPENGL_ES3_BIT;
        if (dpy.core_version || dpy.compat_version)
            dpy.renderable |= EGL_OPENGL_BIT;
        make_configs();
        dpy.initialized = 1;
    }
    if (major)
        *major = 1;
    if (minor)
        *minor = 4;
    return ok();
}

static void free_surface(struct egl_surface *s)
{
    struct egl_surface **pp;
    for (pp = &dpy.surfaces; *pp; pp = &(*pp)->next)
        if (*pp == s) {
            *pp = s->next;
            break;
        }
    gla_buffer_destroy(s->b);
    free(s);
}

static void free_context(struct egl_context *c)
{
    struct egl_context **pp;
    for (pp = &dpy.contexts; *pp; pp = &(*pp)->next)
        if (*pp == c) {
            *pp = c->next;
            break;
        }
    gla_context_destroy(c->c);
    free(c);
}

/* Everything goes, the current context and surface too (EGL frees those
 * when they are released; here no other task can still be using them). */
EGLBoolean eglTerminate(EGLDisplay d)
{
    if (d != DISPLAY)
        return fail(EGL_BAD_DISPLAY);
    if (!dpy.initialized)
        return ok();
    if (cur_ctx)
        gla_make_current(NULL, NULL);
    cur_ctx = NULL;
    cur_draw = NULL;
    while (dpy.surfaces)
        free_surface(dpy.surfaces);
    while (dpy.contexts)
        free_context(dpy.contexts);
    gla_display_destroy(dpy.d);
    dpy.d = NULL;
    dpy.initialized = 0;
    return ok();
}

const char *eglQueryString(EGLDisplay d, EGLint name)
{
    EGLint e = check_display(d);
    if (e)
        return fail_null(e);
    error = EGL_SUCCESS;
    switch (name) {
    case EGL_VENDOR:
        return "AmiCradle Team (GFOpenGPU)";
    case EGL_VERSION:
        return gla_display_driver(dpy.d)[0] == 'v' ? "1.4 GFOpenGPU (GLA, virgl)" : "1.4 GFOpenGPU (GLA, softpipe)";
    case EGL_CLIENT_APIS:
        return "OpenGL OpenGL_ES";
    case EGL_EXTENSIONS:
        return "EGL_KHR_create_context EGL_KHR_get_all_proc_addresses EGL_KHR_surfaceless_context";
    }
    return fail_null(EGL_BAD_PARAMETER);
}

/* --- Configs ------------------------------------------------------------- */

static EGLint config_attrib(const struct egl_config *k, EGLint attribute, int *known)
{
    *known = 1;
    switch (attribute) {
    case EGL_BUFFER_SIZE:           return k->red + k->green + k->blue + k->alpha;
    case EGL_RED_SIZE:              return k->red;
    case EGL_GREEN_SIZE:            return k->green;
    case EGL_BLUE_SIZE:             return k->blue;
    case EGL_ALPHA_SIZE:            return k->alpha;
    case EGL_LUMINANCE_SIZE:        return 0;
    case EGL_ALPHA_MASK_SIZE:       return 0;
    case EGL_DEPTH_SIZE:            return k->depth;
    case EGL_STENCIL_SIZE:          return k->stencil;
    case EGL_CONFIG_CAVEAT:         return EGL_NONE;
    case EGL_CONFIG_ID:             return k->id;
    case EGL_LEVEL:                 return 0;
    case EGL_MAX_PBUFFER_WIDTH:     return MAX_PBUFFER;
    case EGL_MAX_PBUFFER_HEIGHT:    return MAX_PBUFFER;
    case EGL_MAX_PBUFFER_PIXELS:    return MAX_PBUFFER * MAX_PBUFFER;
    case EGL_NATIVE_RENDERABLE:     return EGL_FALSE;
    case EGL_NATIVE_VISUAL_ID:      return 0;
    case EGL_NATIVE_VISUAL_TYPE:    return EGL_NONE;
    case EGL_SAMPLES:               return 0;
    case EGL_SAMPLE_BUFFERS:        return 0;
    case EGL_SURFACE_TYPE:          return EGL_WINDOW_BIT | EGL_PBUFFER_BIT;
    case EGL_TRANSPARENT_TYPE:      return EGL_NONE;
    case EGL_TRANSPARENT_RED_VALUE:
    case EGL_TRANSPARENT_GREEN_VALUE:
    case EGL_TRANSPARENT_BLUE_VALUE: return 0;
    case EGL_BIND_TO_TEXTURE_RGB:
    case EGL_BIND_TO_TEXTURE_RGBA:  return EGL_FALSE;
    case EGL_MIN_SWAP_INTERVAL:     return 0;
    case EGL_MAX_SWAP_INTERVAL:     return 4;
    case EGL_COLOR_BUFFER_TYPE:     return EGL_RGB_BUFFER;
    case EGL_RENDERABLE_TYPE:
    case EGL_CONFORMANT:            return dpy.renderable;
    }
    *known = 0;
    return 0;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay d, EGLConfig config, EGLint attribute, EGLint *value)
{
    const struct egl_config *k;
    EGLint e = check_display(d), v;
    int known;
    if (e)
        return fail(e);
    if (!(k = config_of(config)))
        return fail(EGL_BAD_CONFIG);
    v = config_attrib(k, attribute, &known);
    if (!known)
        return fail(EGL_BAD_ATTRIBUTE);
    if (value)
        *value = v;
    return ok();
}

EGLBoolean eglGetConfigs(EGLDisplay d, EGLConfig *out, EGLint config_size, EGLint *num_config)
{
    EGLint e = check_display(d), i, n = 0;
    if (e)
        return fail(e);
    if (!num_config)
        return fail(EGL_BAD_PARAMETER);
    for (i = 0; i < NCONFIGS; i++) {
        if (out && n >= config_size)
            break;
        if (out)
            out[n] = (EGLConfig)&configs[i];
        n++;
    }
    *num_config = n;
    return ok();
}

/* How an attribute is matched (EGL 1.4, table 3.4). */
enum { M_ATLEAST, M_EXACT, M_MASK, M_IGNORE };

static int match_of(EGLint attribute)
{
    switch (attribute) {
    case EGL_BUFFER_SIZE: case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE:
    case EGL_LUMINANCE_SIZE: case EGL_ALPHA_SIZE: case EGL_ALPHA_MASK_SIZE:
    case EGL_DEPTH_SIZE: case EGL_STENCIL_SIZE: case EGL_SAMPLE_BUFFERS: case EGL_SAMPLES:
        return M_ATLEAST;
    case EGL_SURFACE_TYPE: case EGL_RENDERABLE_TYPE: case EGL_CONFORMANT:
        return M_MASK;
    case EGL_MAX_PBUFFER_WIDTH: case EGL_MAX_PBUFFER_HEIGHT: case EGL_MAX_PBUFFER_PIXELS:
    case EGL_NATIVE_VISUAL_ID: case EGL_MATCH_NATIVE_PIXMAP:
        return M_IGNORE;
    }
    return M_EXACT;
}

/* What the program asked for: the attributes it named, in order. */
struct want {
    EGLint attr[48], value[48];
    int n, config_id, color[4];         /* config_id: -1 none; color: red, green, blue, alpha asked (> 0) */
};

static int want_of(struct want *w, const EGLint *list)
{
    int i;
    memset(w, 0, sizeof *w);
    w->config_id = -1;
    /* EGL's default for EGL_SURFACE_TYPE is EGL_WINDOW_BIT. EGL_RENDERABLE_TYPE's
     * is EGL_OPENGL_ES_BIT, which no config has (there is no ES 1 here): it
     * is left out, so a program that doesn't name it gets every config. */
    w->attr[w->n] = EGL_SURFACE_TYPE;
    w->value[w->n++] = EGL_WINDOW_BIT;
    for (; list && list[0] != EGL_NONE; list += 2) {
        EGLint a = list[0], v = list[1];
        int known;
        if (a == EGL_CONFIG_ID) {
            w->config_id = v;
            continue;
        }
        if (a == EGL_MATCH_NATIVE_PIXMAP)
            continue;                   /* no pixmaps */
        config_attrib(&configs[0], a, &known);
        if (!known)
            return EGL_BAD_ATTRIBUTE;
        if (a == EGL_RED_SIZE) w->color[0] = v > 0;
        if (a == EGL_GREEN_SIZE) w->color[1] = v > 0;
        if (a == EGL_BLUE_SIZE) w->color[2] = v > 0;
        if (a == EGL_ALPHA_SIZE) w->color[3] = v > 0;
        for (i = 0; i < w->n; i++)
            if (w->attr[i] == a)
                break;
        if (i == w->n) {
            if (w->n >= (int)(sizeof w->attr / sizeof w->attr[0]))
                return EGL_BAD_ATTRIBUTE;
            w->n++;
        }
        w->attr[i] = a;
        w->value[i] = v;
    }
    return 0;
}

static int matches(const struct want *w, const struct egl_config *k)
{
    int i, known;
    if (w->config_id != -1 && w->config_id != EGL_DONT_CARE)
        return k->id == w->config_id;
    for (i = 0; i < w->n; i++) {
        EGLint v = w->value[i], have = config_attrib(k, w->attr[i], &known);
        if (v == EGL_DONT_CARE)
            continue;
        switch (match_of(w->attr[i])) {
        case M_ATLEAST: if (have < v) return 0; break;
        case M_EXACT:   if (have != v) return 0; break;
        case M_MASK:    if ((have & v) != v) return 0; break;
        }
    }
    return 1;
}

/* EGL 1.4's sort (3.4.1): the most colour bits of the channels asked for
 * first, then the smallest buffer, depth and stencil, then the config ID. */
static const struct want *sorting;

static int color_bits(const struct egl_config *k)
{
    const int *c = sorting->color;
    return (c[0] ? k->red : 0) + (c[1] ? k->green : 0) + (c[2] ? k->blue : 0) + (c[3] ? k->alpha : 0);
}

static int config_order(const void *pa, const void *pb)
{
    const struct egl_config *a = *(const struct egl_config *const *)pa, *b = *(const struct egl_config *const *)pb;
    int d;
    if ((d = color_bits(b) - color_bits(a)))
        return d;
    if ((d = (a->red + a->green + a->blue + a->alpha) - (b->red + b->green + b->blue + b->alpha)))
        return d;
    if ((d = a->depth - b->depth))
        return d;
    if ((d = a->stencil - b->stencil))
        return d;
    return a->id - b->id;
}

EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *attrib_list, EGLConfig *out, EGLint config_size,
                           EGLint *num_config)
{
    const struct egl_config *found[NCONFIGS];
    struct want w;
    EGLint e = check_display(d), i, n = 0;
    if (e)
        return fail(e);
    if (!num_config)
        return fail(EGL_BAD_PARAMETER);
    if ((e = want_of(&w, attrib_list)))
        return fail(e);
    for (i = 0; i < NCONFIGS; i++)
        if (matches(&w, &configs[i]))
            found[n++] = &configs[i];
    sorting = &w;
    qsort(found, n, sizeof found[0], config_order);
    if (out) {
        if (n > config_size)
            n = config_size < 0 ? 0 : config_size;
        for (i = 0; i < n; i++)
            out[i] = (EGLConfig)found[i];
    }
    *num_config = n;
    return ok();
}

/* --- Surfaces ------------------------------------------------------------ */

/* Where a window's frames go, and its size inside the borders. */
static void measure(struct egl_surface *s)
{
    struct Window *w = s->window;
    int iw, ih;
    s->target.rp = w->RPort;
    s->target.bitmap = NULL;            /* through the RastPort: layers respected */
    if (w->Flags & WFLG_GIMMEZEROZERO) {
        s->target.left = s->target.top = 0;
        iw = w->GZZWidth;
        ih = w->GZZHeight;
    } else {
        s->target.left = w->BorderLeft;
        s->target.top = w->BorderTop;
        iw = w->Width - w->BorderLeft - w->BorderRight;
        ih = w->Height - w->BorderTop - w->BorderBottom;
    }
    s->w = iw < 1 ? 1 : iw;
    s->h = ih < 1 ? 1 : ih;
}

/* A pbuffer's frames go nowhere. */
static void present_none(void *user, const struct gla_frame *frame)
{
    (void)user;
    (void)frame;
}

static struct egl_surface *new_surface(const struct egl_config *k, int kind)
{
    struct egl_surface *s = (struct egl_surface *)calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->kind = kind;
    s->cfg = k;
    s->render_buffer = EGL_BACK_BUFFER;
    return s;
}

static EGLSurface add_surface(struct egl_surface *s)
{
    struct gla_config g;
    gla_cfg_of(&g, s->cfg, s->render_buffer == EGL_BACK_BUFFER);
    s->b = gla_buffer_create(dpy.d, &g, s->w, s->h, &s->hook);
    if (!s->b) {
        free(s);
        return fail_null(EGL_BAD_ALLOC);
    }
    s->next = dpy.surfaces;
    dpy.surfaces = s;
    ok();
    return (EGLSurface)s;
}

EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list)
{
    const struct egl_config *k;
    struct egl_surface *s, *p;
    EGLint e = check_display(d);
    if (e)
        return fail_null(e);
    if (!(k = config_of(config)))
        return fail_null(EGL_BAD_CONFIG);
    if (!win || !win->RPort)
        return fail_null(EGL_BAD_NATIVE_WINDOW);
    for (p = dpy.surfaces; p; p = p->next)
        if (p->window == win && !p->destroyed)
            return fail_null(EGL_BAD_ALLOC);    /* one surface a window */
    if (!(s = new_surface(k, SURF_WINDOW)))
        return fail_null(EGL_BAD_ALLOC);
    for (; attrib_list && attrib_list[0] != EGL_NONE; attrib_list += 2) {
        if (attrib_list[0] == EGL_RENDER_BUFFER &&
            (attrib_list[1] == EGL_BACK_BUFFER || attrib_list[1] == EGL_SINGLE_BUFFER))
            s->render_buffer = attrib_list[1];
        else if (attrib_list[0] != EGL_VG_COLORSPACE && attrib_list[0] != EGL_VG_ALPHA_FORMAT) {
            free(s);
            return fail_null(EGL_BAD_ATTRIBUTE);
        }
    }
    s->window = win;
    measure(s);
    gla_os3_present_init(&s->hook, &s->target);
    return add_surface(s);
}

EGLSurface eglCreatePbufferSurface(EGLDisplay d, EGLConfig config, const EGLint *attrib_list)
{
    const struct egl_config *k;
    struct egl_surface *s;
    EGLint e = check_display(d), w = 0, h = 0;
    if (e)
        return fail_null(e);
    if (!(k = config_of(config)))
        return fail_null(EGL_BAD_CONFIG);
    if (!(s = new_surface(k, SURF_PBUFFER)))
        return fail_null(EGL_BAD_ALLOC);
    for (; attrib_list && attrib_list[0] != EGL_NONE; attrib_list += 2) {
        EGLint v = attrib_list[1];
        switch (attrib_list[0]) {
        case EGL_WIDTH:             w = v; break;
        case EGL_HEIGHT:            h = v; break;
        case EGL_LARGEST_PBUFFER:   s->largest = v; break;
        case EGL_TEXTURE_FORMAT:
        case EGL_TEXTURE_TARGET:
            if (v != EGL_NO_TEXTURE)
                goto bad_match;     /* no eglBindTexImage here */
            break;
        case EGL_MIPMAP_TEXTURE:
        case EGL_VG_COLORSPACE:
        case EGL_VG_ALPHA_FORMAT:
            break;
        default:
            free(s);
            return fail_null(EGL_BAD_ATTRIBUTE);
        }
    }
    if (w < 0 || h < 0) {
        free(s);
        return fail_null(EGL_BAD_PARAMETER);
    }
    if (w > MAX_PBUFFER || h > MAX_PBUFFER) {
        if (!s->largest) {
            free(s);
            return fail_null(EGL_BAD_ALLOC);
        }
        if (w > MAX_PBUFFER) w = MAX_PBUFFER;
        if (h > MAX_PBUFFER) h = MAX_PBUFFER;
    }
    s->w = w ? w : 1;               /* GLA's buffers have a pixel at least */
    s->h = h ? h : 1;
    s->hook.present = present_none;
    return add_surface(s);
bad_match:
    free(s);
    return fail_null(EGL_BAD_MATCH);
}

EGLSurface eglCreatePixmapSurface(EGLDisplay d, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list)
{
    EGLint e = check_display(d);
    (void)pixmap;
    (void)attrib_list;
    if (e)
        return fail_null(e);
    if (!config_of(config))
        return fail_null(EGL_BAD_CONFIG);
    return fail_null(EGL_BAD_MATCH);    /* no config has EGL_PIXMAP_BIT */
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay d, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config,
                                            const EGLint *attrib_list)
{
    EGLint e = check_display(d);
    (void)buffer;
    (void)config;
    (void)attrib_list;
    if (e)
        return fail_null(e);
    (void)buftype;
    return fail_null(EGL_BAD_PARAMETER);   /* only OpenVG's images are, and there is no OpenVG */
}

EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface surface)
{
    struct egl_surface *s;
    EGLint e = check_display(d);
    if (e)
        return fail(e);
    if (!(s = surface_of(surface)))
        return fail(EGL_BAD_SURFACE);
    if (s == cur_draw)
        s->destroyed = 1;           /* freed when it is no longer current */
    else
        free_surface(s);
    return ok();
}

EGLBoolean eglQuerySurface(EGLDisplay d, EGLSurface surface, EGLint attribute, EGLint *value)
{
    struct egl_surface *s;
    EGLint e = check_display(d), v;
    if (e)
        return fail(e);
    if (!(s = surface_of(surface)))
        return fail(EGL_BAD_SURFACE);
    if (s->kind == SURF_WINDOW && (attribute == EGL_WIDTH || attribute == EGL_HEIGHT))
        measure(s);         /* the window's size now; the buffer follows at the next swap */
    switch (attribute) {
    case EGL_WIDTH:                 v = s->w; break;
    case EGL_HEIGHT:                v = s->h; break;
    case EGL_CONFIG_ID:             v = s->cfg->id; break;
    case EGL_LARGEST_PBUFFER:       v = s->largest; break;
    case EGL_RENDER_BUFFER:         v = s->kind == SURF_PBUFFER ? EGL_BACK_BUFFER : s->render_buffer; break;
    case EGL_SWAP_BEHAVIOR:         v = EGL_BUFFER_DESTROYED; break;
    case EGL_MULTISAMPLE_RESOLVE:   v = EGL_MULTISAMPLE_RESOLVE_DEFAULT; break;
    case EGL_TEXTURE_FORMAT:
    case EGL_TEXTURE_TARGET:        v = EGL_NO_TEXTURE; break;
    case EGL_MIPMAP_TEXTURE:
    case EGL_MIPMAP_LEVEL:          v = 0; break;
    case EGL_HORIZONTAL_RESOLUTION:
    case EGL_VERTICAL_RESOLUTION:
    case EGL_PIXEL_ASPECT_RATIO:    v = EGL_UNKNOWN; break;
    case EGL_VG_COLORSPACE:         v = EGL_VG_COLORSPACE_sRGB; break;
    case EGL_VG_ALPHA_FORMAT:       v = EGL_VG_ALPHA_FORMAT_NONPRE; break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    if (value)
        *value = v;
    return ok();
}

EGLBoolean eglSurfaceAttrib(EGLDisplay d, EGLSurface surface, EGLint attribute, EGLint value)
{
    EGLint e = check_display(d);
    if (e)
        return fail(e);
    if (!surface_of(surface))
        return fail(EGL_BAD_SURFACE);
    switch (attribute) {
    case EGL_MIPMAP_LEVEL:
        return ok();
    case EGL_SWAP_BEHAVIOR:
        return value == EGL_BUFFER_DESTROYED ? ok() : fail(EGL_BAD_MATCH);
    case EGL_MULTISAMPLE_RESOLVE:
        return value == EGL_MULTISAMPLE_RESOLVE_DEFAULT ? ok() : fail(EGL_BAD_MATCH);
    }
    return fail(EGL_BAD_ATTRIBUTE);
}

EGLBoolean eglBindTexImage(EGLDisplay d, EGLSurface surface, EGLint buffer)
{
    EGLint e = check_display(d);
    (void)buffer;
    if (e)
        return fail(e);
    return fail(surface_of(surface) ? EGL_BAD_MATCH : EGL_BAD_SURFACE);
}

EGLBoolean eglReleaseTexImage(EGLDisplay d, EGLSurface surface, EGLint buffer)
{
    return eglBindTexImage(d, surface, buffer);
}

/* --- Contexts ------------------------------------------------------------ */

EGLBoolean eglBindAPI(EGLenum which)
{
    if (which != EGL_OPENGL_ES_API && which != EGL_OPENGL_API)
        return fail(EGL_BAD_PARAMETER);     /* no OpenVG */
    api = which;
    return ok();
}

EGLenum eglQueryAPI(void)
{
    return api;
}

EGLContext eglCreateContext(EGLDisplay d, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
    const struct egl_config *k;
    struct egl_context *c, *share = NULL;
    struct gla_config g;
    EGLint e = check_display(d), major = -1, minor = -1, profile = 0;
    int have;
    if (e)
        return fail_null(e);
    if (!(k = config_of(config)))
        return fail_null(EGL_BAD_CONFIG);
    if (share_context != EGL_NO_CONTEXT && !(share = context_of(share_context)))
        return fail_null(EGL_BAD_CONTEXT);
    for (; attrib_list && attrib_list[0] != EGL_NONE; attrib_list += 2) {
        EGLint v = attrib_list[1];
        switch (attrib_list[0]) {
        case EGL_CONTEXT_MAJOR_VERSION:         major = v; break;
        case EGL_CONTEXT_MINOR_VERSION:         minor = v; break;
        case EGL_CONTEXT_OPENGL_PROFILE_MASK:
            if (api != EGL_OPENGL_API)
                return fail_null(EGL_BAD_ATTRIBUTE);
            profile = v;
            break;
        /* Asked for, and nothing to do: GLA's contexts have no debug or
         * robust flags, and a lost context never comes back anyway. */
        case EGL_CONTEXT_FLAGS_KHR:
        case EGL_CONTEXT_OPENGL_DEBUG:
        case EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE:
        case EGL_CONTEXT_OPENGL_ROBUST_ACCESS:
        case EGL_CONTEXT_OPENGL_RESET_NOTIFICATION_STRATEGY:
            break;
        default:
            return fail_null(EGL_BAD_ATTRIBUTE);
        }
    }
    memset(&g, 0, sizeof g);
    gla_cfg_of(&g, k, 1);
    if (api == EGL_OPENGL_ES_API) {
        if (major < 0)
            major = 1;
        if (minor < 0)
            minor = 0;
        if (major < 2)
            return fail_null(EGL_BAD_MATCH);    /* no ES 1 */
        if (!(dpy.renderable & (major >= 3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT)))
            return fail_null(EGL_BAD_CONFIG);
        have = dpy.es_version;
        g.profile = GLA_ES2;
    } else {
        /* OpenGL: 1.0 and the core profile unless asked otherwise, as EGL
         * says; core is only from 3.2, and below that it is compatibility. */
        if (major < 0)
            major = 1;
        if (minor < 0)
            minor = 0;
        if (!profile)
            profile = EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT;
        if (!(dpy.renderable & EGL_OPENGL_BIT))
            return fail_null(EGL_BAD_CONFIG);
        if (major * 10 + minor >= 32 && (profile & EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT)) {
            g.profile = GLA_CORE;
            have = dpy.core_version;
        } else if (profile & (EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT | EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT)) {
            g.profile = GLA_COMPAT;
            have = dpy.compat_version;
        } else
            return fail_null(EGL_BAD_MATCH);
    }
    if (major * 10 + minor > have)
        return fail_null(EGL_BAD_MATCH);
    if (share && share->api != api)
        return fail_null(EGL_BAD_CONTEXT);
    g.major = major;
    g.minor = minor;
    if (!(c = (struct egl_context *)calloc(1, sizeof *c)))
        return fail_null(EGL_BAD_ALLOC);
    c->c = gla_context_create(dpy.d, &g, share ? share->c : NULL);
    if (!c->c) {
        free(c);
        return fail_null(EGL_BAD_MATCH);
    }
    c->cfg = k;
    c->api = api;
    c->major = major;
    c->minor = minor;
    c->next = dpy.contexts;
    dpy.contexts = c;
    ok();
    return (EGLContext)c;
}

EGLBoolean eglDestroyContext(EGLDisplay d, EGLContext ctx)
{
    struct egl_context *c;
    EGLint e = check_display(d);
    if (e)
        return fail(e);
    if (!(c = context_of(ctx)))
        return fail(EGL_BAD_CONTEXT);
    if (c == cur_ctx)
        c->destroyed = 1;           /* freed when it is no longer current */
    else
        free_context(c);
    return ok();
}

/* What was current and destroyed meanwhile goes now. */
static void release(struct egl_context *old_ctx, struct egl_surface *old_draw)
{
    if (old_draw && old_draw != cur_draw && old_draw->destroyed)
        free_surface(old_draw);
    if (old_ctx && old_ctx != cur_ctx && old_ctx->destroyed)
        free_context(old_ctx);
}

EGLBoolean eglMakeCurrent(EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
    struct egl_context *c = NULL, *old_ctx = cur_ctx;
    struct egl_surface *s = NULL, *old_draw = cur_draw;
    EGLint e;
    /* Releasing works on any display, even one terminated. */
    if (ctx == EGL_NO_CONTEXT && draw == EGL_NO_SURFACE && read == EGL_NO_SURFACE) {
        if (d != DISPLAY)
            return fail(EGL_BAD_DISPLAY);
        if (cur_ctx)
            gla_make_current(NULL, NULL);
        cur_ctx = NULL;
        cur_draw = NULL;
        release(old_ctx, old_draw);
        return ok();
    }
    if ((e = check_display(d)))
        return fail(e);
    if (!(c = context_of(ctx)))
        return fail(EGL_BAD_CONTEXT);
    if ((draw == EGL_NO_SURFACE) != (read == EGL_NO_SURFACE))
        return fail(EGL_BAD_MATCH);
    if (draw != EGL_NO_SURFACE) {
        if (!(s = surface_of(draw)) || !surface_of(read))
            return fail(EGL_BAD_SURFACE);
        if (draw != read)
            return fail(EGL_BAD_MATCH);         /* GLA reads where it draws */
        if (s->cfg->depth != c->cfg->depth || s->cfg->stencil != c->cfg->stencil ||
            (s->cfg->alpha > 0) != (c->cfg->alpha > 0))
            return fail(EGL_BAD_MATCH);         /* not the context's visual */
    }
    if (!gla_make_current(c->c, s ? s->b : NULL))
        return fail(EGL_BAD_MATCH);
    cur_ctx = c;
    cur_draw = s;
    release(old_ctx, old_draw);
    return ok();
}

EGLContext eglGetCurrentContext(void)
{
    return cur_ctx ? (EGLContext)cur_ctx : EGL_NO_CONTEXT;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw)
{
    if (readdraw != EGL_DRAW && readdraw != EGL_READ)
        return fail_null(EGL_BAD_PARAMETER);
    return cur_draw ? (EGLSurface)cur_draw : EGL_NO_SURFACE;
}

EGLDisplay eglGetCurrentDisplay(void)
{
    return cur_ctx ? DISPLAY : EGL_NO_DISPLAY;
}

EGLBoolean eglQueryContext(EGLDisplay d, EGLContext ctx, EGLint attribute, EGLint *value)
{
    struct egl_context *c;
    EGLint e = check_display(d), v;
    if (e)
        return fail(e);
    if (!(c = context_of(ctx)))
        return fail(EGL_BAD_CONTEXT);
    switch (attribute) {
    case EGL_CONFIG_ID:             v = c->cfg->id; break;
    case EGL_CONTEXT_CLIENT_TYPE:   v = c->api; break;
    case EGL_CONTEXT_CLIENT_VERSION: v = c->major; break;
    case EGL_RENDER_BUFFER:
        v = c != cur_ctx || !cur_draw ? EGL_NONE
            : cur_draw->kind == SURF_PBUFFER ? EGL_BACK_BUFFER : cur_draw->render_buffer;
        break;
    default:
        return fail(EGL_BAD_ATTRIBUTE);
    }
    if (value)
        *value = v;
    return ok();
}

/* --- Frames -------------------------------------------------------------- */

static void finish(void)
{
    static void (*gl_finish)(void);
    if (!gl_finish)
        gl_finish = (void (*)(void))gla_get_proc_address("glFinish");
    if (gl_finish)
        gl_finish();
}

EGLBoolean eglWaitClient(void)
{
    if (cur_ctx)
        finish();
    return ok();
}

EGLBoolean eglWaitGL(void)
{
    return eglWaitClient();
}

EGLBoolean eglWaitNative(EGLint engine)
{
    if (engine != EGL_CORE_NATIVE_ENGINE)
        return fail(EGL_BAD_PARAMETER);
    return ok();            /* Intuition's and graphics.library's drawing is done when its calls return */
}

EGLBoolean eglReleaseThread(void)
{
    if (cur_ctx)
        eglMakeCurrent(DISPLAY, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    api = EGL_OPENGL_ES_API;
    return ok();
}

/* The number of vertical blanks (WaitTOF) before each frame is shown:
 * 0 shows it at once. EGL's default is 1. */
EGLBoolean eglSwapInterval(EGLDisplay d, EGLint interval)
{
    EGLint e = check_display(d);
    if (e)
        return fail(e);
    if (!cur_ctx)
        return fail(EGL_BAD_CONTEXT);
    if (!cur_draw)
        return fail(EGL_BAD_SURFACE);
    swap_interval = interval < 0 ? 0 : interval > 4 ? 4 : interval;
    return ok();
}

EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface surface)
{
    struct egl_surface *s;
    EGLint e = check_display(d), i;
    if (e)
        return fail(e);
    if (!(s = surface_of(surface)))
        return fail(EGL_BAD_SURFACE);
    if (!cur_ctx || s != cur_draw)
        return fail(EGL_BAD_SURFACE);   /* the surface of this task's current context, as EGL 1.4 says */
    if (s->kind != SURF_WINDOW)
        return ok();                    /* a pbuffer: no effect */
    if (s->render_buffer == EGL_SINGLE_BUFFER) {
        finish();                       /* single-buffered: it is in the window at glFinish */
        return ok();
    }
    {
        unsigned w = s->w, h = s->h;
        measure(s);
        if (s->w != w || s->h != h)
            gla_buffer_resize(s->b, s->w, s->h);
    }
    for (i = 0; i < swap_interval; i++)
        WaitTOF();
    gla_swap(cur_ctx->c, s->b);
    return ok();
}

EGLBoolean eglCopyBuffers(EGLDisplay d, EGLSurface surface, EGLNativePixmapType target)
{
    EGLint e = check_display(d);
    (void)target;
    if (e)
        return fail(e);
    if (!surface_of(surface))
        return fail(EGL_BAD_SURFACE);
    return fail(EGL_BAD_NATIVE_PIXMAP);
}

/* --- Calls by name ------------------------------------------------------- */

#define EGL_CALL(name) { #name, (__eglMustCastToProperFunctionPointerType)name }
static const struct {
    const char *name;
    __eglMustCastToProperFunctionPointerType fn;
} egl_calls[] = {
    EGL_CALL(eglBindAPI), EGL_CALL(eglBindTexImage), EGL_CALL(eglChooseConfig), EGL_CALL(eglCopyBuffers),
    EGL_CALL(eglCreateContext), EGL_CALL(eglCreatePbufferFromClientBuffer), EGL_CALL(eglCreatePbufferSurface),
    EGL_CALL(eglCreatePixmapSurface), EGL_CALL(eglCreateWindowSurface), EGL_CALL(eglDestroyContext),
    EGL_CALL(eglDestroySurface), EGL_CALL(eglGetConfigAttrib), EGL_CALL(eglGetConfigs),
    EGL_CALL(eglGetCurrentContext), EGL_CALL(eglGetCurrentDisplay), EGL_CALL(eglGetCurrentSurface),
    EGL_CALL(eglGetDisplay), EGL_CALL(eglGetError), EGL_CALL(eglGetProcAddress), EGL_CALL(eglInitialize),
    EGL_CALL(eglMakeCurrent), EGL_CALL(eglQueryAPI), EGL_CALL(eglQueryContext), EGL_CALL(eglQueryString),
    EGL_CALL(eglQuerySurface), EGL_CALL(eglReleaseTexImage), EGL_CALL(eglReleaseThread),
    EGL_CALL(eglSurfaceAttrib), EGL_CALL(eglSwapBuffers), EGL_CALL(eglSwapInterval), EGL_CALL(eglTerminate),
    EGL_CALL(eglWaitClient), EGL_CALL(eglWaitGL), EGL_CALL(eglWaitNative),
};

/* EGL's own calls, and every GL and GLES call (EGL_KHR_get_all_proc_addresses). */
__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    unsigned i;
    if (!procname)
        return NULL;
    if (procname[0] == 'e' && procname[1] == 'g' && procname[2] == 'l') {
        for (i = 0; i < sizeof egl_calls / sizeof egl_calls[0]; i++)
            if (!strcmp(egl_calls[i].name, procname))
                return egl_calls[i].fn;
        return NULL;
    }
    return (__eglMustCastToProperFunctionPointerType)gla_get_proc_address(procname);
}
