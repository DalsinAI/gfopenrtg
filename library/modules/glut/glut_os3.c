/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLUT on GFOpenGPU (AmigaOS 3.2): freeglut's interface, on GLA and an
 * Intuition window. This file is the window, its events and GLUT's state;
 * the shapes, the teapot and the fonts are freeglut's own files, built
 * unchanged (fg_internal.h says what they get from here).
 *
 * One window, on the Workbench screen or (glutFullScreen, glutEnterGameMode)
 * on a screen of its own. Its context is GLA's compatibility profile at the
 * highest version the display has, unless glutInitContextProfile asks for
 * GLUT_CORE_PROFILE (with glutInitContextVersion's version) or
 * GLUT_ES_PROFILE (GLES 2 or 3: this kit's addition to freeglut_ext.h).
 * virgl on the host's GPU when gfopengpu.library carries it, else softpipe,
 * as for every GLA program.
 *
 * The callbacks come from IDCMP: keys from RAWKEY through the keymap (so
 * KeyboardUp and SpecialUp too), the buttons from MOUSEBUTTONS (the right
 * one trapped, so it isn't Intuition's menu button), the wheel from
 * NewMouse's RAWKEY codes, motion from MOUSEMOVE (one call for a run of
 * moves), Reshape from NEWSIZE, and Entry from ACTIVEWINDOW and
 * INACTIVEWINDOW. Timers and glutGet(GLUT_ELAPSED_TIME) run on timer.device.
 * Ctrl-C leaves the main loop as closing the window does.
 *
 * Not here: menus (accepted and never shown: the right button reaches the
 * program), overlays, subwindows and more than one window, joysticks,
 * spaceballs, tablets and dials, warping the pointer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <graphics/modeid.h>
#include <graphics/displayinfo.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#define TIMER_BASE_NAME glut_timer_base
#define KEYMAP_BASE_NAME glut_keymap_base
#include <proto/timer.h>
#include <proto/keymap.h>

#include <GL/freeglut.h>
#include <gla/gla_core.h>
#include <gla/os3/gla_present_os3.h>
#include <gla/os3/gla_virgl_os3.h>

#include "fg_internal.h"

#ifndef GLUT_ES_PROFILE
#define GLUT_ES_PROFILE 0x0004
#endif

struct Device *glut_timer_base;
struct Library *glut_keymap_base;

SFG_State fgState;
SFG_Structure fgStructure;

/* The font handles: GLUT_BITMAP_8_BY_13 and the rest are their addresses. */
void *glutStrokeRoman = &glutStrokeRoman;
void *glutStrokeMonoRoman = &glutStrokeMonoRoman;
void *glutBitmap9By15 = &glutBitmap9By15;
void *glutBitmap8By13 = &glutBitmap8By13;
void *glutBitmapTimesRoman10 = &glutBitmapTimesRoman10;
void *glutBitmapTimesRoman24 = &glutBitmapTimesRoman24;
void *glutBitmapHelvetica10 = &glutBitmapHelvetica10;
void *glutBitmapHelvetica12 = &glutBitmapHelvetica12;
void *glutBitmapHelvetica18 = &glutBitmapHelvetica18;

/* NewMouse's wheel, as RAWKEY codes. */
#define NM_WHEEL_UP 0x7a
#define NM_WHEEL_DOWN 0x7b

struct glut_timer {
    struct glut_timer *next;
    unsigned long due;              /* ms since glutInit */
    void (*fn)(int);
    int value;
};

static struct {
    /* glutInit* */
    int argc_seen;
    const char *program;
    int init_x, init_y, init_w, init_h;
    unsigned mode;
    int ctx_major, ctx_minor, ctx_profile, ctx_flags;
    int action_on_close;
    int ignore_repeat;
    int key_repeat;
    void (*error_fn)(const char *fmt, va_list ap);
    void (*warning_fn)(const char *fmt, va_list ap);

    /* timer.device: the clock and the timer request the main loop waits on */
    struct MsgPort *tport;
    struct timerequest *treq;
    int treq_out;
    TimeVal_Type start;

    /* the window */
    int id;                         /* 1, or 0 when there is none */
    struct Window *win;
    struct Screen *screen;          /* our own (full screen), or NULL on Workbench */
    struct gla_os3_target target;
    struct gla_present hook;
    struct gla_display *d;
    struct gla_context *c;
    struct gla_buffer *b;
    struct gla_config cfg;
    SFG_Window fgwin;
    int w, h;                       /* the GL area */
    int fullscreen;
    int wb_left, wb_top, wb_w, wb_h; /* where it was on Workbench */
    char title[256];
    UWORD *blank_pointer;
    int cursor;
    void *user_data;
    int redisplay;
    int need_reshape;
    int visible_told;
    int buttons;                    /* held: bit per GLUT button */
    int modifiers;                  /* during a callback */
    int last_x, last_y;
    int leave;                      /* glutLeaveMainLoop, or the window closed */
    int in_loop;

    /* callbacks */
    void (*display)(void);
    void (*reshape)(int, int);
    void (*keyboard)(unsigned char, int, int);
    void (*keyboard_up)(unsigned char, int, int);
    void (*special)(int, int, int);
    void (*special_up)(int, int, int);
    void (*mouse)(int, int, int, int);
    void (*wheel)(int, int, int, int);
    void (*motion)(int, int);
    void (*passive)(int, int);
    void (*entry)(int);
    void (*visibility)(int);
    void (*status)(int);
    void (*idle)(void);
    void (*close)(void);
    void (*position)(int, int);
    void (*init_context)(void);

    struct glut_timer *timers;
} g = {
    .init_x = -1, .init_y = -1, .init_w = 300, .init_h = 300,
    .mode = GLUT_RGBA | GLUT_SINGLE,
    .ctx_major = 1, .ctx_minor = 0, .ctx_profile = 0,
    .action_on_close = GLUT_ACTION_EXIT,
    .key_repeat = GLUT_KEY_REPEAT_DEFAULT,
    .cursor = GLUT_CURSOR_INHERIT,
};

/* -- messages -------------------------------------------------------------- */

static void say(const char *kind, const char *fmt, va_list ap)
{
    fprintf(stderr, "freeglut (%s): ", g.program ? g.program : "GLUT");
    if (kind)
        fputs(kind, stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}

void fgError(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g.error_fn)
        g.error_fn(fmt, ap);
    else
        say(NULL, fmt, ap);
    va_end(ap);
    exit(1);
}

void fgWarning(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (g.warning_fn)
        g.warning_fn(fmt, ap);
    else
        say(NULL, fmt, ap);
    va_end(ap);
}

/* -- time ------------------------------------------------------------------ */

static unsigned long now_ms(void)
{
    TimeVal_Type t;
    if (!glut_timer_base)
        return 0;
    GetSysTime(&t);
    return (t.tv_secs - g.start.tv_secs) * 1000UL + t.tv_micro / 1000 - g.start.tv_micro / 1000;
}

static void timer_open(void)
{
    if (glut_timer_base)
        return;
    g.tport = CreateMsgPort();
    g.treq = g.tport ? (struct timerequest *)CreateIORequest(g.tport, sizeof(struct timerequest)) : NULL;
    if (!g.treq || OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ, (struct IORequest *)g.treq, 0))
        fgError("no timer.device");
    glut_timer_base = g.treq->tr_node.io_Device;
    GetSysTime(&g.start);
}

static void timer_stop(void)
{
    if (g.treq_out) {
        if (!CheckIO((struct IORequest *)g.treq))
            AbortIO((struct IORequest *)g.treq);
        WaitIO((struct IORequest *)g.treq);
        g.treq_out = 0;
    }
}

static void timer_close(void)
{
    if (g.treq) {
        timer_stop();
        if (glut_timer_base)
            CloseDevice((struct IORequest *)g.treq);
        DeleteIORequest((struct IORequest *)g.treq);
    }
    if (g.tport)
        DeleteMsgPort(g.tport);
    g.treq = NULL;
    g.tport = NULL;
    glut_timer_base = NULL;
}

/* -- the window ------------------------------------------------------------ */

static void measure(void)
{
    struct Window *w = g.win;
    g.target.rp = w->RPort;
    g.target.bitmap = NULL;             /* through the RastPort: layers respected */
    g.target.left = w->BorderLeft;
    g.target.top = w->BorderTop;
    g.w = w->Width - w->BorderLeft - w->BorderRight;
    g.h = w->Height - w->BorderTop - w->BorderBottom;
    if (g.w < 1)
        g.w = 1;
    if (g.h < 1)
        g.h = 1;
}

static const ULONG IDCMP = IDCMP_CLOSEWINDOW | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW | IDCMP_RAWKEY |
                           IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW |
                           IDCMP_CHANGEWINDOW;

static struct Window *open_on_workbench(int x, int y, int w, int h)
{
    struct Screen *wb = LockPubScreen(NULL);
    struct Window *win;
    if (!wb)
        return NULL;
    if (x < 0)
        x = (wb->Width - w) / 2 > 0 ? (wb->Width - w) / 2 : 0;
    if (y < 0)
        y = (wb->Height - h) / 2 > 0 ? (wb->Height - h) / 2 : 0;
    win = OpenWindowTags(NULL, WA_PubScreen, (ULONG)wb, WA_Left, x, WA_Top, y,
                         WA_InnerWidth, w, WA_InnerHeight, h, WA_Title, (ULONG)g.title,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_SizeGadget, TRUE,
                         WA_SizeBBottom, TRUE, WA_MinWidth, 64, WA_MinHeight, 48, WA_MaxWidth, ~0U, WA_MaxHeight, ~0U,
                         WA_Activate, TRUE, WA_SimpleRefresh, TRUE, WA_RMBTrap, TRUE, WA_ReportMouse, TRUE,
                         WA_IDCMP, IDCMP, TAG_END);
    UnlockPubScreen(NULL, wb);
    return win;
}

/* A screen of our own the size of Workbench's (as freeglut takes the
 * desktop's), in 24 or more bits, and a borderless window across it. */
static struct Window *open_full_screen(void)
{
    struct Screen *wb = LockPubScreen(NULL);
    int w = 640, h = 480;
    ULONG id;
    struct Window *win = NULL;
    if (wb) {
        w = wb->Width;
        h = wb->Height;
        UnlockPubScreen(NULL, wb);
    }
    id = BestModeID(BIDTAG_NominalWidth, w, BIDTAG_NominalHeight, h, BIDTAG_DesiredWidth, w,
                    BIDTAG_DesiredHeight, h, BIDTAG_Depth, 24, TAG_END);
    if (id == (ULONG)INVALID_ID)
        id = BestModeID(BIDTAG_NominalWidth, 640, BIDTAG_NominalHeight, 480, BIDTAG_Depth, 24, TAG_END);
    if (id == (ULONG)INVALID_ID)
        return NULL;
    g.screen = OpenScreenTags(NULL, SA_DisplayID, id, SA_Width, w, SA_Height, h, SA_Depth, 24,
                              SA_Quiet, TRUE, SA_ShowTitle, FALSE, SA_Type, CUSTOMSCREEN,
                              SA_Title, (ULONG)g.title, TAG_END);
    if (!g.screen)
        return NULL;
    win = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)g.screen, WA_Left, 0, WA_Top, 0,
                         WA_Width, g.screen->Width, WA_Height, g.screen->Height,
                         WA_Borderless, TRUE, WA_Backdrop, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                         WA_ReportMouse, TRUE, WA_SimpleRefresh, TRUE, WA_IDCMP, IDCMP, TAG_END);
    if (!win) {
        CloseScreen(g.screen);
        g.screen = NULL;
    }
    return win;
}

static void close_window(void)
{
    if (g.win) {
        if (g.blank_pointer)
            ClearPointer(g.win);
        CloseWindow(g.win);
    }
    g.win = NULL;
    if (g.screen)
        CloseScreen(g.screen);
    g.screen = NULL;
}

static void apply_cursor(void)
{
    if (!g.win)
        return;
    if (g.cursor == GLUT_CURSOR_NONE) {
        if (!g.blank_pointer)
            g.blank_pointer = (UWORD *)AllocMem(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
        if (g.blank_pointer)
            SetPointer(g.win, g.blank_pointer, 1, 16, 0, 0);
    } else if (g.blank_pointer)
        ClearPointer(g.win);
}

/* Going to or from the full screen: GL's buffer is made again for the new
 * window (so nothing of the old window's is still being read); the context
 * stays, with everything the program made in it. */
static void drop_buffer(void)
{
    if (g.b) {
        gla_make_current(NULL, NULL);
        gla_buffer_destroy(g.b);
        g.b = NULL;
    }
}

static void use_window(struct Window *win)
{
    g.win = win;
    measure();
    g.b = gla_buffer_create(g.d, &g.cfg, g.w, g.h, &g.hook);
    if (!g.b || !gla_make_current(g.c, g.b))
        fgError("GL couldn't start in the new window");
    apply_cursor();
    g.need_reshape = 1;
    g.redisplay = 1;
}

static void destroy_all(void)
{
    struct glut_timer *t;
    if (g.c)
        gla_make_current(NULL, NULL);
    if (g.b)
        gla_buffer_destroy(g.b);
    if (g.c)
        gla_context_destroy(g.c);
    if (g.d)
        gla_display_destroy(g.d);
    g.b = NULL;
    g.c = NULL;
    g.d = NULL;
    close_window();
    if (g.blank_pointer)
        FreeMem(g.blank_pointer, 6 * sizeof(UWORD));
    g.blank_pointer = NULL;
    g.id = 0;
    fgStructure.CurrentWindow = NULL;
    while ((t = g.timers)) {
        g.timers = t->next;
        free(t);
    }
}

static void at_exit(void)
{
    destroy_all();
    timer_close();
    if (glut_keymap_base)
        CloseLibrary(glut_keymap_base);
    glut_keymap_base = NULL;
}

/* -- initialisation -------------------------------------------------------- */

static void drop_args(int *argc, char **argv, int i, int n)
{
    int j;
    for (j = i; j + n <= *argc; j++)
        argv[j] = argv[j + n];
    *argc -= n;
}

void FGAPIENTRY glutInit(int *pargc, char **argv)
{
    int i;
    if (fgState.Initialised)
        fgError("illegal glutInit() reinitialization attempt");
    if (pargc && *pargc > 0 && argv && argv[0]) {
        const char *p = argv[0], *s;
        for (s = p; *s; s++)
            if (*s == '/' || *s == ':')
                p = s + 1;
        g.program = p;
        /* X11's options freeglut takes: -geometry is used, the rest dropped. */
        for (i = 1; i < *pargc;) {
            if (!strcmp(argv[i], "-geometry") && i + 1 < *pargc) {
                int x, y;
                unsigned w, h;
                char c1, c2;
                if (sscanf(argv[i + 1], "%ux%u%c%d%c%d", &w, &h, &c1, &x, &c2, &y) == 6) {
                    g.init_w = w;
                    g.init_h = h;
                    g.init_x = c1 == '-' ? -1 : x;
                    g.init_y = c2 == '-' ? -1 : y;
                } else if (sscanf(argv[i + 1], "%ux%u", &w, &h) == 2) {
                    g.init_w = w;
                    g.init_h = h;
                }
                drop_args(pargc, argv, i, 2);
            } else if (!strcmp(argv[i], "-display") && i + 1 < *pargc) {
                drop_args(pargc, argv, i, 2);
            } else if (!strcmp(argv[i], "-direct") || !strcmp(argv[i], "-indirect") ||
                       !strcmp(argv[i], "-iconic") || !strcmp(argv[i], "-gldebug") ||
                       !strcmp(argv[i], "-sync")) {
                drop_args(pargc, argv, i, 1);
            } else
                i++;
        }
    }
    timer_open();
    glut_keymap_base = OpenLibrary((CONST_STRPTR)"keymap.library", 37);
    atexit(at_exit);
    fgState.Initialised = GL_TRUE;
}

void FGAPIENTRY glutInitWindowPosition(int x, int y)
{
    g.init_x = x;
    g.init_y = y;
}

void FGAPIENTRY glutInitWindowSize(int w, int h)
{
    if (w > 0 && h > 0) {
        g.init_w = w;
        g.init_h = h;
    }
}

void FGAPIENTRY glutInitDisplayMode(unsigned int mode) { g.mode = mode; }

void FGAPIENTRY glutInitDisplayString(const char *s)
{
    unsigned m = GLUT_RGBA;
    if (!s)
        return;
    if (strstr(s, "double"))
        m |= GLUT_DOUBLE;
    if (strstr(s, "depth"))
        m |= GLUT_DEPTH;
    if (strstr(s, "stencil"))
        m |= GLUT_STENCIL;
    if (strstr(s, "alpha"))
        m |= GLUT_ALPHA;
    g.mode = m;
}

void FGAPIENTRY glutInitContextVersion(int major, int minor)
{
    g.ctx_major = major;
    g.ctx_minor = minor;
}

void FGAPIENTRY glutInitContextFlags(int flags) { g.ctx_flags = flags; }
void FGAPIENTRY glutInitContextProfile(int profile) { g.ctx_profile = profile; }
void FGAPIENTRY glutInitErrorFunc(void (*fn)(const char *fmt, va_list ap)) { g.error_fn = fn; }
void FGAPIENTRY glutInitWarningFunc(void (*fn)(const char *fmt, va_list ap)) { g.warning_fn = fn; }

/* -- windows --------------------------------------------------------------- */

static void make_config(void)
{
    memset(&g.cfg, 0, sizeof(g.cfg));
    g.cfg.doublebuf = (g.mode & GLUT_DOUBLE) != 0;
    g.cfg.depth = (g.mode & GLUT_DEPTH) ? 24 : 0;
    g.cfg.stencil = (g.mode & GLUT_STENCIL) ? 8 : 0;
    g.cfg.alpha = (g.mode & GLUT_ALPHA) != 0;
    if (g.ctx_profile == GLUT_ES_PROFILE) {
        g.cfg.profile = GLA_ES2;
        g.cfg.major = g.ctx_major >= 2 ? g.ctx_major : 0;
        g.cfg.minor = g.ctx_major >= 2 ? g.ctx_minor : 0;
    } else if (g.ctx_profile == GLUT_CORE_PROFILE || g.ctx_major * 10 + g.ctx_minor >= 32) {
        /* freeglut: a 3.2 or later context asked for without a profile is core */
        g.cfg.profile = g.ctx_profile == GLUT_COMPATIBILITY_PROFILE ? GLA_COMPAT : GLA_CORE;
        g.cfg.major = g.ctx_major;
        g.cfg.minor = g.ctx_minor;
    } else {
        /* the compatibility profile, at the highest version it has: what
         * every GL 1.x and 2.x program gets elsewhere too */
        g.cfg.profile = GLA_COMPAT;
        g.cfg.major = g.ctx_major * 10 + g.ctx_minor > 21 ? g.ctx_major : 0;
        g.cfg.minor = g.ctx_major * 10 + g.ctx_minor > 21 ? g.ctx_minor : 0;
    }
}

static void gl_started(void)
{
    const char *v = (const char *)glGetString(GL_VERSION);
    int major = 0;
    if (v) {
        if (!strncmp(v, "OpenGL ES ", 10))
            v += 10;
        major = atoi(v);
    }
    fgState.HasOpenGL20 = major >= 2;
}

int FGAPIENTRY glutCreateWindow(const char *title)
{
    struct Window *win;
    FREEGLUT_EXIT_IF_NOT_INITIALISED("glutCreateWindow");
    if (g.id) {
        fgWarning("glutCreateWindow: only one window here; the second isn't made");
        return 0;
    }
    snprintf(g.title, sizeof(g.title), "%s", title ? title : (g.program ? g.program : "GLUT"));
    make_config();
    win = g.fullscreen ? open_full_screen() : open_on_workbench(g.init_x, g.init_y, g.init_w, g.init_h);
    if (!win && g.fullscreen) {
        g.fullscreen = 0;
        win = open_on_workbench(g.init_x, g.init_y, g.init_w, g.init_h);
    }
    if (!win)
        fgError("glutCreateWindow: no window");
    g.win = win;
    measure();
    gla_os3_present_init(&g.hook, &g.target);
    {
        struct gla_virgl_transport t;
        if (gla_os3_virgl_transport(&t))
            g.d = gla_display_create_virgl(&t);
    }
    if (!g.d)
        g.d = gla_display_create();
    if (!g.d)
        fgError("glutCreateWindow: no GL display");
    g.c = gla_context_create(g.d, &g.cfg, NULL);
    if (!g.c) {
        static const char *names[] = { "compatibility", "core", "ES" };
        if (g.cfg.major)
            fgError("glutCreateWindow: no OpenGL %s %d.%d context (%s)", names[g.cfg.profile], g.cfg.major,
                    g.cfg.minor, gla_display_driver(g.d));
        fgError("glutCreateWindow: no OpenGL %s context (%s)", names[g.cfg.profile], gla_display_driver(g.d));
    }
    g.b = gla_buffer_create(g.d, &g.cfg, g.w, g.h, &g.hook);
    if (!g.b || !gla_make_current(g.c, g.b))
        fgError("glutCreateWindow: GL couldn't start");
    g.id = 1;
    g.fgwin.Window.attribute_v_coord = -1;
    g.fgwin.Window.attribute_v_normal = -1;
    g.fgwin.Window.attribute_v_texture = -1;
    fgStructure.CurrentWindow = &g.fgwin;
    gl_started();
    apply_cursor();
    g.need_reshape = 1;
    g.redisplay = 1;
    g.visible_told = 0;
    if (g.init_context)
        g.init_context();
    return g.id;
}

int FGAPIENTRY glutCreateSubWindow(int window, int x, int y, int w, int h)
{
    fgWarning("glutCreateSubWindow: no subwindows here");
    return 0;
}

void FGAPIENTRY glutDestroyWindow(int window)
{
    if (!g.id || window != g.id)
        return;
    if (g.close)
        g.close();
    destroy_all();
    g.leave = 1;
}

void FGAPIENTRY glutSetWindow(int window) { (void)window; }
int FGAPIENTRY glutGetWindow(void) { return g.id; }

void FGAPIENTRY glutSetWindowTitle(const char *title)
{
    snprintf(g.title, sizeof(g.title), "%s", title ? title : "");
    if (g.win && !g.screen)
        SetWindowTitles(g.win, (CONST_STRPTR)g.title, (CONST_STRPTR)~0);
}

void FGAPIENTRY glutSetIconTitle(const char *title) { (void)title; }

void FGAPIENTRY glutReshapeWindow(int w, int h)
{
    if (!g.win || g.screen || w < 1 || h < 1)
        return;
    ChangeWindowBox(g.win, g.win->LeftEdge, g.win->TopEdge, w + g.win->BorderLeft + g.win->BorderRight,
                    h + g.win->BorderTop + g.win->BorderBottom);
}

void FGAPIENTRY glutPositionWindow(int x, int y)
{
    if (!g.win || g.screen)
        return;
    ChangeWindowBox(g.win, x, y, g.win->Width, g.win->Height);
}

void FGAPIENTRY glutPopWindow(void)
{
    if (g.win)
        WindowToFront(g.win);
}

void FGAPIENTRY glutPushWindow(void)
{
    if (g.win)
        WindowToBack(g.win);
}

void FGAPIENTRY glutShowWindow(void) { glutPopWindow(); }
void FGAPIENTRY glutHideWindow(void) {}
void FGAPIENTRY glutIconifyWindow(void) {}

void FGAPIENTRY glutFullScreen(void)
{
    struct Window *win;
    if (!g.id) {
        g.fullscreen = 1;           /* before the window: it opens on its own screen */
        return;
    }
    if (g.screen)
        return;
    g.wb_left = g.win->LeftEdge;
    g.wb_top = g.win->TopEdge;
    g.wb_w = g.w;
    g.wb_h = g.h;
    drop_buffer();
    close_window();
    win = open_full_screen();
    if (!win) {
        fgWarning("glutFullScreen: no screen of 24 bits or more");
        win = open_on_workbench(g.wb_left, g.wb_top, g.wb_w, g.wb_h);
        if (!win)
            fgError("glutFullScreen: no window");
    } else
        g.fullscreen = 1;
    use_window(win);
}

void FGAPIENTRY glutLeaveFullScreen(void)
{
    struct Window *win;
    if (!g.id || !g.screen)
        return;
    drop_buffer();
    close_window();
    win = open_on_workbench(g.wb_w ? g.wb_left : g.init_x, g.wb_w ? g.wb_top : g.init_y,
                            g.wb_w ? g.wb_w : g.init_w, g.wb_h ? g.wb_h : g.init_h);
    if (!win)
        fgError("glutLeaveFullScreen: no window");
    g.fullscreen = 0;
    use_window(win);
}

void FGAPIENTRY glutFullScreenToggle(void)
{
    if (g.screen)
        glutLeaveFullScreen();
    else
        glutFullScreen();
}

/* Game mode: the full screen, with the window made for it. */
void FGAPIENTRY glutGameModeString(const char *s) { (void)s; }

int FGAPIENTRY glutEnterGameMode(void)
{
    if (!g.id) {
        g.fullscreen = 1;
        return glutCreateWindow(g.program);
    }
    glutFullScreen();
    return g.id;
}

void FGAPIENTRY glutLeaveGameMode(void) { glutLeaveFullScreen(); }

int FGAPIENTRY glutGameModeGet(GLenum query)
{
    switch (query) {
    case GLUT_GAME_MODE_ACTIVE: return g.screen != NULL;
    case GLUT_GAME_MODE_POSSIBLE: return 1;
    case GLUT_GAME_MODE_WIDTH: return g.screen ? g.w : -1;
    case GLUT_GAME_MODE_HEIGHT: return g.screen ? g.h : -1;
    case GLUT_GAME_MODE_PIXEL_DEPTH: return g.screen ? 24 : -1;
    default: return -1;
    }
}

void FGAPIENTRY glutPostRedisplay(void) { g.redisplay = 1; }
void FGAPIENTRY glutPostWindowRedisplay(int window) { (void)window; g.redisplay = 1; }

void FGAPIENTRY glutSwapBuffers(void)
{
    if (g.c && g.b)
        gla_swap(g.c, g.b);
}

void FGAPIENTRY glutSetCursor(int cursor)
{
    g.cursor = cursor;
    apply_cursor();
}

void FGAPIENTRY glutWarpPointer(int x, int y) { (void)x; (void)y; }

void FGAPIENTRY glutSetWindowData(void *data) { g.user_data = data; }
void *FGAPIENTRY glutGetWindowData(void) { return g.user_data; }

/* -- callbacks ------------------------------------------------------------- */

void FGAPIENTRY glutDisplayFunc(void (*fn)(void))
{
    if (!fn)
        fgError("Fatal error in program.  NULL display callback not permitted in GLUT 3.0+ or freeglut 2.0.1+");
    g.display = fn;
}

void FGAPIENTRY glutReshapeFunc(void (*fn)(int, int)) { g.reshape = fn; }
void FGAPIENTRY glutKeyboardFunc(void (*fn)(unsigned char, int, int)) { g.keyboard = fn; }
void FGAPIENTRY glutKeyboardUpFunc(void (*fn)(unsigned char, int, int)) { g.keyboard_up = fn; }
void FGAPIENTRY glutSpecialFunc(void (*fn)(int, int, int)) { g.special = fn; }
void FGAPIENTRY glutSpecialUpFunc(void (*fn)(int, int, int)) { g.special_up = fn; }
void FGAPIENTRY glutMouseFunc(void (*fn)(int, int, int, int)) { g.mouse = fn; }
void FGAPIENTRY glutMouseWheelFunc(void (*fn)(int, int, int, int)) { g.wheel = fn; }
void FGAPIENTRY glutMotionFunc(void (*fn)(int, int)) { g.motion = fn; }
void FGAPIENTRY glutPassiveMotionFunc(void (*fn)(int, int)) { g.passive = fn; }
void FGAPIENTRY glutEntryFunc(void (*fn)(int)) { g.entry = fn; }
void FGAPIENTRY glutVisibilityFunc(void (*fn)(int))
{
    g.visibility = fn;
    if (fn && g.id && g.visible_told)
        fn(GLUT_VISIBLE);
}
void FGAPIENTRY glutWindowStatusFunc(void (*fn)(int)) { g.status = fn; }
void FGAPIENTRY glutIdleFunc(void (*fn)(void)) { g.idle = fn; }
void FGAPIENTRY glutCloseFunc(void (*fn)(void)) { g.close = fn; }
void FGAPIENTRY glutWMCloseFunc(void (*fn)(void)) { g.close = fn; }
void FGAPIENTRY glutPositionFunc(void (*fn)(int, int)) { g.position = fn; }
void FGAPIENTRY glutInitContextFunc(void (*fn)(void)) { g.init_context = fn; }
void FGAPIENTRY glutAppStatusFunc(void (*fn)(int)) { (void)fn; }

void FGAPIENTRY glutTimerFunc(unsigned int ms, void (*fn)(int), int value)
{
    struct glut_timer *t, **p;
    if (!fn)
        return;
    t = (struct glut_timer *)malloc(sizeof(*t));
    if (!t)
        fgError("glutTimerFunc: no memory");
    t->due = now_ms() + ms;
    t->fn = fn;
    t->value = value;
    for (p = &g.timers; *p && (*p)->due <= t->due; p = &(*p)->next)
        ;
    t->next = *p;
    *p = t;
}

/* Accepted and never called: the devices aren't here. */
void FGAPIENTRY glutJoystickFunc(void (*fn)(unsigned int, int, int, int), int ms) { (void)fn; (void)ms; }
void FGAPIENTRY glutForceJoystickFunc(void) {}
void FGAPIENTRY glutSpaceballMotionFunc(void (*fn)(int, int, int)) { (void)fn; }
void FGAPIENTRY glutSpaceballRotateFunc(void (*fn)(int, int, int)) { (void)fn; }
void FGAPIENTRY glutSpaceballButtonFunc(void (*fn)(int, int)) { (void)fn; }
void FGAPIENTRY glutButtonBoxFunc(void (*fn)(int, int)) { (void)fn; }
void FGAPIENTRY glutDialsFunc(void (*fn)(int, int)) { (void)fn; }
void FGAPIENTRY glutTabletMotionFunc(void (*fn)(int, int)) { (void)fn; }
void FGAPIENTRY glutTabletButtonFunc(void (*fn)(int, int, int, int)) { (void)fn; }
void FGAPIENTRY glutOverlayDisplayFunc(void (*fn)(void)) { (void)fn; }
void FGAPIENTRY glutMultiEntryFunc(void (*fn)(int, int)) { (void)fn; }
void FGAPIENTRY glutMultiButtonFunc(void (*fn)(int, int, int, int, int)) { (void)fn; }
void FGAPIENTRY glutMultiMotionFunc(void (*fn)(int, int, int)) { (void)fn; }
void FGAPIENTRY glutMultiPassiveFunc(void (*fn)(int, int, int)) { (void)fn; }
void FGAPIENTRY glutMenuStateFunc(void (*fn)(int)) { (void)fn; }
void FGAPIENTRY glutMenuStatusFunc(void (*fn)(int, int, int)) { (void)fn; }
void FGAPIENTRY glutMenuDestroyFunc(void (*fn)(void)) { (void)fn; }

/* Menus: made and filled, never shown (the right button stays the program's). */
static int menus, menu_current;
int FGAPIENTRY glutCreateMenu(void (*fn)(int)) { (void)fn; return menu_current = ++menus; }
void FGAPIENTRY glutDestroyMenu(int menu) { (void)menu; }
int FGAPIENTRY glutGetMenu(void) { return menu_current; }
void FGAPIENTRY glutSetMenu(int menu) { menu_current = menu; }
void FGAPIENTRY glutAddMenuEntry(const char *label, int value) { (void)label; (void)value; }
void FGAPIENTRY glutAddSubMenu(const char *label, int sub) { (void)label; (void)sub; }
void FGAPIENTRY glutChangeToMenuEntry(int item, const char *label, int value) { (void)item; (void)label; (void)value; }
void FGAPIENTRY glutChangeToSubMenu(int item, const char *label, int sub) { (void)item; (void)label; (void)sub; }
void FGAPIENTRY glutRemoveMenuItem(int item) { (void)item; }
void FGAPIENTRY glutAttachMenu(int button) { (void)button; }
void FGAPIENTRY glutDetachMenu(int button) { (void)button; }
void FGAPIENTRY glutSetMenuFont(int menu, void *font) { (void)menu; (void)font; }
void *FGAPIENTRY glutGetMenuData(void) { return NULL; }
void FGAPIENTRY glutSetMenuData(void *data) { (void)data; }

/* Overlays: there are none. */
void FGAPIENTRY glutEstablishOverlay(void) {}
void FGAPIENTRY glutRemoveOverlay(void) {}
void FGAPIENTRY glutUseLayer(GLenum layer) { (void)layer; }
void FGAPIENTRY glutPostOverlayRedisplay(void) {}
void FGAPIENTRY glutPostWindowOverlayRedisplay(int window) { (void)window; }
void FGAPIENTRY glutShowOverlay(void) {}
void FGAPIENTRY glutHideOverlay(void) {}

void FGAPIENTRY glutSetVertexAttribCoord3(GLint attrib) { g.fgwin.Window.attribute_v_coord = attrib; }
void FGAPIENTRY glutSetVertexAttribNormal(GLint attrib) { g.fgwin.Window.attribute_v_normal = attrib; }
void FGAPIENTRY glutSetVertexAttribTexCoord2(GLint attrib) { g.fgwin.Window.attribute_v_texture = attrib; }

/* -- state ----------------------------------------------------------------- */

void FGAPIENTRY glutSetOption(GLenum what, int value)
{
    switch (what) {
    case GLUT_ACTION_ON_WINDOW_CLOSE: g.action_on_close = value; break;
    case GLUT_INIT_WINDOW_X: g.init_x = value; break;
    case GLUT_INIT_WINDOW_Y: g.init_y = value; break;
    case GLUT_INIT_WINDOW_WIDTH: if (value > 0) g.init_w = value; break;
    case GLUT_INIT_WINDOW_HEIGHT: if (value > 0) g.init_h = value; break;
    case GLUT_INIT_DISPLAY_MODE: g.mode = value; break;
    case GLUT_INIT_MAJOR_VERSION: g.ctx_major = value; break;
    case GLUT_INIT_MINOR_VERSION: g.ctx_minor = value; break;
    case GLUT_INIT_FLAGS: g.ctx_flags = value; break;
    case GLUT_INIT_PROFILE: g.ctx_profile = value; break;
    case GLUT_STROKE_FONT_DRAW_JOIN_DOTS: fgState.StrokeFontDrawJoinDots = !!value; break;
    case GLUT_GEOMETRY_VISUALIZE_NORMALS: g.fgwin.State.VisualizeNormals = !!value; break;
    default: break;
    }
}

int FGAPIENTRY glutGet(GLenum what)
{
    switch (what) {
    case GLUT_ELAPSED_TIME: return (int)now_ms();
    case GLUT_INIT_STATE: return fgState.Initialised;
    case GLUT_VERSION: return 30600;
    case GLUT_SCREEN_WIDTH:
    case GLUT_SCREEN_HEIGHT: {
        struct Screen *s = g.screen;
        int v = 0;
        if (s)
            return what == GLUT_SCREEN_WIDTH ? s->Width : s->Height;
        if ((s = LockPubScreen(NULL))) {
            v = what == GLUT_SCREEN_WIDTH ? s->Width : s->Height;
            UnlockPubScreen(NULL, s);
        }
        return v;
    }
    case GLUT_SCREEN_WIDTH_MM: return glutGet(GLUT_SCREEN_WIDTH) / 4;
    case GLUT_SCREEN_HEIGHT_MM: return glutGet(GLUT_SCREEN_HEIGHT) / 4;
    case GLUT_INIT_WINDOW_X: return g.init_x;
    case GLUT_INIT_WINDOW_Y: return g.init_y;
    case GLUT_INIT_WINDOW_WIDTH: return g.init_w;
    case GLUT_INIT_WINDOW_HEIGHT: return g.init_h;
    case GLUT_INIT_DISPLAY_MODE: return g.mode;
    case GLUT_INIT_MAJOR_VERSION: return g.ctx_major;
    case GLUT_INIT_MINOR_VERSION: return g.ctx_minor;
    case GLUT_INIT_FLAGS: return g.ctx_flags;
    case GLUT_INIT_PROFILE: return g.ctx_profile;
    case GLUT_ACTION_ON_WINDOW_CLOSE: return g.action_on_close;
    case GLUT_DISPLAY_MODE_POSSIBLE: return 1;
    case GLUT_WINDOW_X: return g.win ? g.win->LeftEdge + g.win->BorderLeft : 0;
    case GLUT_WINDOW_Y: return g.win ? g.win->TopEdge + g.win->BorderTop : 0;
    case GLUT_WINDOW_WIDTH: return g.id ? g.w : 0;
    case GLUT_WINDOW_HEIGHT: return g.id ? g.h : 0;
    case GLUT_WINDOW_BORDER_WIDTH: return g.win ? g.win->BorderLeft : 0;
    case GLUT_WINDOW_HEADER_HEIGHT: return g.win ? g.win->BorderTop : 0;
    case GLUT_WINDOW_BUFFER_SIZE: return g.id ? (g.cfg.alpha ? 32 : 24) : 0;
    case GLUT_WINDOW_RED_SIZE:
    case GLUT_WINDOW_GREEN_SIZE:
    case GLUT_WINDOW_BLUE_SIZE: return g.id ? 8 : 0;
    case GLUT_WINDOW_ALPHA_SIZE: return g.id && g.cfg.alpha ? 8 : 0;
    case GLUT_WINDOW_DEPTH_SIZE: return g.id ? g.cfg.depth : 0;
    case GLUT_WINDOW_STENCIL_SIZE: return g.id ? g.cfg.stencil : 0;
    case GLUT_WINDOW_DOUBLEBUFFER: return g.id ? g.cfg.doublebuf : 0;
    case GLUT_WINDOW_RGBA: return 1;
    case GLUT_WINDOW_NUM_SAMPLES: return 0;
    case GLUT_WINDOW_STEREO: return 0;
    case GLUT_WINDOW_COLORMAP_SIZE: return 0;
    case GLUT_WINDOW_NUM_CHILDREN: return 0;
    case GLUT_WINDOW_PARENT: return 0;
    case GLUT_WINDOW_CURSOR: return g.cursor;
    case GLUT_WINDOW_FORMAT_ID: return 0;
    case GLUT_FULL_SCREEN: return g.screen != NULL;
    case GLUT_MENU_NUM_ITEMS: return 0;
    case GLUT_RENDERING_CONTEXT: return GLUT_CREATE_NEW_CONTEXT;
    case GLUT_DIRECT_RENDERING: return 1;
    case GLUT_SKIP_STALE_MOTION_EVENTS: return 1;
    case GLUT_GEOMETRY_VISUALIZE_NORMALS: return g.fgwin.State.VisualizeNormals;
    case GLUT_STROKE_FONT_DRAW_JOIN_DOTS: return fgState.StrokeFontDrawJoinDots;
    default: return -1;
    }
}

int FGAPIENTRY glutDeviceGet(GLenum what)
{
    switch (what) {
    case GLUT_HAS_KEYBOARD: return 1;
    case GLUT_HAS_MOUSE: return 1;
    case GLUT_NUM_MOUSE_BUTTONS: return 3;
    case GLUT_DEVICE_IGNORE_KEY_REPEAT: return g.ignore_repeat;
    case GLUT_DEVICE_KEY_REPEAT: return g.key_repeat;
    case GLUT_HAS_JOYSTICK: return 0;
    default: return 0;
    }
}

int FGAPIENTRY glutLayerGet(GLenum what)
{
    switch (what) {
    case GLUT_LAYER_IN_USE: return GLUT_NORMAL;
    case GLUT_TRANSPARENT_INDEX: return -1;
    default: return 0;
    }
}

int FGAPIENTRY glutGetModifiers(void) { return g.modifiers; }
void FGAPIENTRY glutIgnoreKeyRepeat(int ignore) { g.ignore_repeat = !!ignore; }
void FGAPIENTRY glutSetKeyRepeat(int mode) { g.key_repeat = mode; }

int FGAPIENTRY glutExtensionSupported(const char *name)
{
    size_t n;
    const char *e;
    if (!name || !*name || strchr(name, ' ') || !g.id)
        return 0;
    n = strlen(name);
    e = (const char *)glGetString(GL_EXTENSIONS);
    if (e) {
        const char *p = e;
        while ((p = strstr(p, name))) {
            if ((p == e || p[-1] == ' ') && (p[n] == ' ' || p[n] == 0))
                return 1;
            p += n;
        }
        return 0;
    }
    {   /* a core context: glGetStringi */
        GLint i, count = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &count);
        for (i = 0; i < count; i++) {
            const char *x = (const char *)glGetStringi(GL_EXTENSIONS, i);
            if (x && !strcmp(x, name))
                return 1;
        }
    }
    return 0;
}

GLUTproc FGAPIENTRY glutGetProcAddress(const char *name)
{
    return (GLUTproc)gla_get_proc_address(name);
}

void FGAPIENTRY glutReportErrors(void)
{
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR)
        fgWarning("GL error: 0x%04x", (unsigned)e);
}

void FGAPIENTRY glutSetColor(int i, GLfloat r, GLfloat gr, GLfloat b) { (void)i; (void)r; (void)gr; (void)b; }
GLfloat FGAPIENTRY glutGetColor(int i, int c) { (void)i; (void)c; return -1.0f; }
void FGAPIENTRY glutCopyColormap(int window) { (void)window; }
int *FGAPIENTRY glutGetModeValues(GLenum mode, int *size)
{
    (void)mode;
    if (size)
        *size = 0;
    return NULL;
}
int FGAPIENTRY glutVideoResizeGet(GLenum query) { (void)query; return 0; }
void FGAPIENTRY glutSetupVideoResizing(void) {}
void FGAPIENTRY glutStopVideoResizing(void) {}
void FGAPIENTRY glutVideoResize(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
void FGAPIENTRY glutVideoPan(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }

/* -- events ---------------------------------------------------------------- */

static int modifiers_of(UWORD q)
{
    int m = 0;
    if (q & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
        m |= GLUT_ACTIVE_SHIFT;
    if (q & IEQUALIFIER_CONTROL)
        m |= GLUT_ACTIVE_CTRL;
    if (q & (IEQUALIFIER_LALT | IEQUALIFIER_RALT))
        m |= GLUT_ACTIVE_ALT;
    return m;
}

/* GLUT's special keys, by raw key code (PC keyboards' extra keys as OS 3.2's keymaps give them). */
static int special_of(UWORD code)
{
    switch (code) {
    case 0x50: return GLUT_KEY_F1;
    case 0x51: return GLUT_KEY_F2;
    case 0x52: return GLUT_KEY_F3;
    case 0x53: return GLUT_KEY_F4;
    case 0x54: return GLUT_KEY_F5;
    case 0x55: return GLUT_KEY_F6;
    case 0x56: return GLUT_KEY_F7;
    case 0x57: return GLUT_KEY_F8;
    case 0x58: return GLUT_KEY_F9;
    case 0x59: return GLUT_KEY_F10;
    case 0x4b: return GLUT_KEY_F11;
    case 0x6f: return GLUT_KEY_F12;
    case 0x4c: return GLUT_KEY_UP;
    case 0x4d: return GLUT_KEY_DOWN;
    case 0x4e: return GLUT_KEY_RIGHT;
    case 0x4f: return GLUT_KEY_LEFT;
    case 0x47: return GLUT_KEY_INSERT;
    case 0x48: return GLUT_KEY_PAGE_UP;
    case 0x49: return GLUT_KEY_PAGE_DOWN;
    case 0x70: return GLUT_KEY_HOME;
    case 0x71: return GLUT_KEY_END;
    case 0x60: return GLUT_KEY_SHIFT_L;
    case 0x61: return GLUT_KEY_SHIFT_R;
    case 0x63: return GLUT_KEY_CTRL_L;
    case 0x64: return GLUT_KEY_ALT_L;
    case 0x65: return GLUT_KEY_ALT_R;
    default: return 0;
    }
}

/* deadkeys: the message's IAddress, read before it was replied to. */
static int ascii_of(UWORD code, UWORD qual, APTR deadkeys)
{
    struct InputEvent ie;
    UBYTE buf[4];
    LONG n;
    if (!glut_keymap_base)
        return -1;
    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = code & ~IECODE_UP_PREFIX;
    ie.ie_Qualifier = qual & ~IEQUALIFIER_REPEAT;
    ie.ie_EventAddress = deadkeys;
    n = MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL);
    return n == 1 ? buf[0] : -1;
}

static void inner_xy(const struct IntuiMessage *m, int *x, int *y)
{
    *x = m->MouseX - g.target.left;
    *y = m->MouseY - g.target.top;
}

static void do_reshape(void)
{
    g.need_reshape = 0;
    if (g.reshape)
        g.reshape(g.w, g.h);
    else
        glViewport(0, 0, g.w, g.h);
}

static void key(struct IntuiMessage *m)
{
    UWORD code = m->Code, qual = m->Qualifier;
    int up = (code & IECODE_UP_PREFIX) != 0, raw = code & ~IECODE_UP_PREFIX;
    int x, y, sp, c;
    if (raw == NM_WHEEL_UP || raw == NM_WHEEL_DOWN) {
        int dir = raw == NM_WHEEL_UP ? 1 : -1;
        if (up)
            return;
        inner_xy(m, &x, &y);
        g.modifiers = modifiers_of(qual);
        if (g.wheel)
            g.wheel(0, dir, x, y);
        else if (g.mouse) {
            /* freeglut's way without a wheel callback: buttons 3 and 4 */
            int b = dir > 0 ? 3 : 4;
            g.mouse(b, GLUT_DOWN, x, y);
            g.mouse(b, GLUT_UP, x, y);
        }
        g.modifiers = 0;
        return;
    }
    if ((qual & IEQUALIFIER_REPEAT) && (g.ignore_repeat || g.key_repeat == GLUT_KEY_REPEAT_OFF))
        return;
    inner_xy(m, &x, &y);
    g.modifiers = modifiers_of(qual);
    if ((sp = special_of(raw))) {
        if (up && g.special_up)
            g.special_up(sp, x, y);
        else if (!up && g.special)
            g.special(sp, x, y);
    } else if ((c = ascii_of(code, qual, m->IAddress)) >= 0) {
        if (up && g.keyboard_up)
            g.keyboard_up((unsigned char)c, x, y);
        else if (!up && g.keyboard)
            g.keyboard((unsigned char)c, x, y);
    }
    g.modifiers = 0;
}

static void button(struct IntuiMessage *m)
{
    int b, state, x, y;
    switch (m->Code & ~IECODE_UP_PREFIX) {
    case IECODE_LBUTTON: b = GLUT_LEFT_BUTTON; break;
    case IECODE_RBUTTON: b = GLUT_RIGHT_BUTTON; break;
    case IECODE_MBUTTON: b = GLUT_MIDDLE_BUTTON; break;
    default: return;
    }
    state = (m->Code & IECODE_UP_PREFIX) ? GLUT_UP : GLUT_DOWN;
    if (state == GLUT_DOWN)
        g.buttons |= 1 << b;
    else
        g.buttons &= ~(1 << b);
    inner_xy(m, &x, &y);
    g.modifiers = modifiers_of(m->Qualifier);
    if (g.mouse)
        g.mouse(b, state, x, y);
    g.modifiers = 0;
}

static void moved(int x, int y)
{
    if (g.buttons) {
        if (g.motion)
            g.motion(x, y);
    } else if (g.passive)
        g.passive(x, y);
}

/* The close gadget: the window goes, and with it the main loop (which then
 * ends the program, or returns: GLUT_ACTION_ON_WINDOW_CLOSE). */
static void closed(void)
{
    if (g.close)
        g.close();
    destroy_all();
    g.leave = 1;
}

/* Take every waiting message; 1 when the window went. */
static int events(void)
{
    struct IntuiMessage *m;
    int mx = 0, my = 0, move = 0;
    while (g.win && (m = (struct IntuiMessage *)GetMsg(g.win->UserPort))) {
        struct IntuiMessage copy = *m;
        /* RAWKEY's IAddress points at the dead keys before it, until the reply */
        if (copy.Class == IDCMP_RAWKEY)
            copy.IAddress = m->IAddress ? *(APTR *)m->IAddress : NULL;
        ReplyMsg((struct Message *)m);
        if (copy.Class != IDCMP_MOUSEMOVE && move) {
            moved(mx, my);
            move = 0;
        }
        switch (copy.Class) {
        case IDCMP_CLOSEWINDOW:
            closed();
            return 1;
        case IDCMP_NEWSIZE:
        case IDCMP_CHANGEWINDOW: {
            int w = g.w, h = g.h;
            measure();
            if (w != g.w || h != g.h) {
                gla_buffer_resize(g.b, g.w, g.h);
                g.need_reshape = 1;
                g.redisplay = 1;
            }
            if (copy.Class == IDCMP_CHANGEWINDOW && g.position)
                g.position(g.win->LeftEdge + g.win->BorderLeft, g.win->TopEdge + g.win->BorderTop);
            break;
        }
        case IDCMP_REFRESHWINDOW:
            BeginRefresh(g.win);
            EndRefresh(g.win, TRUE);
            g.redisplay = 1;
            break;
        case IDCMP_RAWKEY:
            key(&copy);
            break;
        case IDCMP_MOUSEBUTTONS:
            button(&copy);
            break;
        case IDCMP_MOUSEMOVE:
            inner_xy(&copy, &mx, &my);
            move = 1;
            break;
        case IDCMP_ACTIVEWINDOW:
            if (g.entry)
                g.entry(GLUT_ENTERED);
            break;
        case IDCMP_INACTIVEWINDOW:
            g.buttons = 0;
            if (g.entry)
                g.entry(GLUT_LEFT);
            break;
        }
        if (!g.id || g.leave)
            return 1;
    }
    if (move)
        moved(mx, my);
    return !g.id;
}

static void run_timers(void)
{
    unsigned long t = now_ms();
    while (g.timers && g.timers->due <= t) {
        struct glut_timer *x = g.timers;
        g.timers = x->next;
        x->fn(x->value);
        free(x);
    }
}

static void redraw(void)
{
    if (!g.id)
        return;
    if (!g.visible_told) {
        g.visible_told = 1;
        if (g.visibility)
            g.visibility(GLUT_VISIBLE);
        if (g.status)
            g.status(GLUT_FULLY_RETAINED);
    }
    if (g.need_reshape)
        do_reshape();
    if (g.redisplay && g.display) {
        g.redisplay = 0;
        g.display();
        /* one buffer: what was drawn is shown now (a program with one
         * buffer calls glFlush, not glutSwapBuffers) */
        if (g.id && !g.cfg.doublebuf)
            gla_swap(g.c, g.b);
    }
}

void FGAPIENTRY glutMainLoopEvent(void)
{
    if (!g.id && !g.timers)
        return;
    if (g.id)
        events();
    run_timers();
    if (g.id && !g.leave)
        redraw();
}

/* Sleep until a message, the next timer or Ctrl-C. */
static void wait_for_something(void)
{
    ULONG sigs = SIGBREAKF_CTRL_C;
    if (g.win)
        sigs |= 1UL << g.win->UserPort->mp_SigBit;
    if (g.timers) {
        unsigned long t = now_ms();
        unsigned long ms = g.timers->due > t ? g.timers->due - t : 0;
        if (!ms)
            return;
        g.treq->tr_node.io_Command = TR_ADDREQUEST;
        g.treq->tr_time.tv_secs = ms / 1000;
        g.treq->tr_time.tv_micro = (ms % 1000) * 1000;
        SetSignal(0, 1UL << g.tport->mp_SigBit);
        SendIO((struct IORequest *)g.treq);
        g.treq_out = 1;
        sigs |= 1UL << g.tport->mp_SigBit;
    }
    sigs = Wait(sigs);
    timer_stop();
    if (sigs & SIGBREAKF_CTRL_C)
        glutLeaveMainLoop();
}

void FGAPIENTRY glutMainLoop(void)
{
    FREEGLUT_EXIT_IF_NOT_INITIALISED("glutMainLoop");
    if (!g.id)
        fgError(" ERROR:  glutMainLoop called with no windows created.");
    g.in_loop = 1;
    g.leave = 0;
    while (!g.leave && g.id) {
        glutMainLoopEvent();
        if (g.leave)
            break;
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            break;
        if (g.idle)
            g.idle();
        else if (!g.redisplay && !g.need_reshape)
            wait_for_something();
    }
    g.in_loop = 0;
    if (g.action_on_close == GLUT_ACTION_EXIT)
        exit(0);
    destroy_all();
}

void FGAPIENTRY glutLeaveMainLoop(void) { g.leave = 1; }

void FGAPIENTRY glutExit(void)
{
    destroy_all();
    fgState.Initialised = GL_FALSE;
}
