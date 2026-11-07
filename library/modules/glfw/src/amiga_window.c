//========================================================================
// GLFW 3.4 AmigaOS 3 - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2026 AmiCradle Team
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would
//    be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such, and must not
//    be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source
//    distribution.
//
//========================================================================
//
// Windows are Intuition windows on the Workbench (the default public
// screen), opened when they are shown and closed when hidden; a full
// screen window is a borderless backdrop window on a screen of its own.
// Input is the windows' IDCMP: raw keys (MapRawKey for text), mouse
// buttons and moves, the wheel (V47's IDCMP_EXTENDEDMOUSE or NewMouse's
// raw keys), size, focus, refresh and the close gadget. Ctrl-C asks every
// window to close.

#include "internal.h"

#if defined(_GLFW_AMIGAOS)

#include "amiga_os.h"

#include <stdlib.h>
#include <string.h>

#define OS3_IDCMP (IDCMP_CLOSEWINDOW | IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | \
                   IDCMP_MOUSEMOVE | IDCMP_NEWSIZE | IDCMP_CHANGEWINDOW | \
                   IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW | \
                   IDCMP_REFRESHWINDOW)

// NewMouse's raw keys
#define NM_WHEEL_UP         0x7a
#define NM_WHEEL_DOWN       0x7b
#define NM_WHEEL_LEFT       0x7c
#define NM_WHEEL_RIGHT      0x7d
#define NM_BUTTON_FOURTH    0x7e

// An IDCMP message, copied so it can be replied to before GLFW's callbacks
// run (they may close the window)
typedef struct _GLFWos3event
{
    ULONG   class;
    UWORD   code;
    UWORD   qualifier;
    WORD    mouseX;
    WORD    mouseY;
    APTR    prevKeys;       // IECLASS_RAWKEY's dead key history
    WORD    wheelX;
    WORD    wheelY;
} _GLFWos3event;

// Set when IDCMP_EXTENDEDMOUSE has given a wheel: NewMouse's keys then
// stay unread, as some drivers send both
static GLFWbool wheelSeen;

static ULONG windowIDCMP(_GLFWwindow* window)
{
    ULONG idcmp = OS3_IDCMP;

    if (_glfw.os3.IntuitionBase->lib_Version >= 47)
        idcmp |= IDCMP_EXTENDEDMOUSE;
    if (window->os3.deltaMove)
        idcmp |= IDCMP_DELTAMOVE;

    return idcmp;
}

static int translateQualifier(UWORD qualifier)
{
    int mods = 0;

    if (qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT))
        mods |= GLFW_MOD_SHIFT;
    if (qualifier & IEQUALIFIER_CONTROL)
        mods |= GLFW_MOD_CONTROL;
    if (qualifier & (IEQUALIFIER_LALT | IEQUALIFIER_RALT))
        mods |= GLFW_MOD_ALT;
    if (qualifier & (IEQUALIFIER_LCOMMAND | IEQUALIFIER_RCOMMAND))
        mods |= GLFW_MOD_SUPER;
    if (qualifier & IEQUALIFIER_CAPSLOCK)
        mods |= GLFW_MOD_CAPS_LOCK;

    return mods;
}

// The pointer the window's cursor mode wants
//
static void updatePointer(_GLFWwindow* window)
{
    struct Window* handle = window->os3.handle;
    const GLFWbool hide = window->cursorMode == GLFW_CURSOR_HIDDEN ||
                          window->cursorMode == GLFW_CURSOR_DISABLED;
    const GLFWbool delta = window->cursorMode == GLFW_CURSOR_DISABLED;

    if (!handle)
        return;

    if (hide && _glfw.os3.blankPointer)
        SetPointer(handle, _glfw.os3.blankPointer, 1, 16, 0, 0);
    else
        ClearPointer(handle);
    window->os3.cursorHidden = hide;

    if (delta != window->os3.deltaMove)
    {
        window->os3.deltaMove = delta;
        ModifyIDCMP(handle, windowIDCMP(window));
    }
}

// The size limits Intuition knows are of the whole window, borders and all
//
static void updateLimits(_GLFWwindow* window)
{
    struct Window* handle = window->os3.handle;
    int bw, bh, minw, minh;
    ULONG maxw, maxh;

    if (!handle || window->monitor)
        return;

    bw = handle->BorderLeft + handle->BorderRight;
    bh = handle->BorderTop + handle->BorderBottom;
    minw = (window->minwidth != GLFW_DONT_CARE ? window->minwidth : 16) + bw;
    minh = (window->minheight != GLFW_DONT_CARE ? window->minheight : 16) + bh;
    maxw = window->maxwidth != GLFW_DONT_CARE ? (ULONG) (window->maxwidth + bw) : 0xffff;
    maxh = window->maxheight != GLFW_DONT_CARE ? (ULONG) (window->maxheight + bh) : 0xffff;

    if (window->resizable)
        WindowLimits(handle, minw, minh, maxw, maxh);
}

// Reads the content area's place and size from the open window and tells
// GLFW of what changed
//
static void updateGeometry(_GLFWwindow* window, GLFWbool notify)
{
    struct Window* handle = window->os3.handle;
    int xpos, ypos, width, height;

    if (!handle)
        return;

    xpos = handle->LeftEdge + handle->BorderLeft;
    ypos = handle->TopEdge + handle->BorderTop;
    width = handle->Width - handle->BorderLeft - handle->BorderRight;
    height = handle->Height - handle->BorderTop - handle->BorderBottom;

    window->os3.target.rp = handle->RPort;
    window->os3.target.bitmap = NULL;
    window->os3.target.left = handle->BorderLeft;
    window->os3.target.top = handle->BorderTop;

    if (xpos != window->os3.xpos || ypos != window->os3.ypos)
    {
        window->os3.xpos = xpos;
        window->os3.ypos = ypos;
        if (notify)
            _glfwInputWindowPos(window, xpos, ypos);
    }

    if (width != window->os3.width || height != window->os3.height)
    {
        window->os3.width = width;
        window->os3.height = height;
        _glfwResizeContextGLA(window);
        if (notify)
        {
            _glfwInputFramebufferSize(window, width, height);
            _glfwInputWindowSize(window, width, height);
        }
    }
}

// A screen of the window's own in the display mode nearest the window's
// video mode, and a borderless backdrop window covering it
//
static GLFWbool openFullScreen(_GLFWwindow* window)
{
    const int bpp = window->videoMode.redBits + window->videoMode.greenBits +
                    window->videoMode.blueBits;
    const int width = window->videoMode.width;
    const int height = window->videoMode.height;
    static const int depths[] = { 24, 16, 15, 8 };
    struct DimensionInfo di;
    ULONG modeID = INVALID_ID;
    int depth = 8, i;

    for (i = 0;  i < 4 && modeID == INVALID_ID;  i++)
    {
        if (bpp > 0 && depths[i] > bpp && depths[i] != 8)
            continue;

        modeID = BestModeID(BIDTAG_NominalWidth, width,
                            BIDTAG_NominalHeight, height,
                            BIDTAG_DesiredWidth, width,
                            BIDTAG_DesiredHeight, height,
                            BIDTAG_Depth, depths[i],
                            TAG_END);
        depth = depths[i];
    }

    if (modeID == INVALID_ID)
        modeID = window->monitor->os3.modeID;

    // 32 bits where the mode has them: WritePixelArray's ARGB as it is
    if (GetDisplayInfoData(NULL, (UBYTE*) &di, sizeof(di), DTAG_DIMS, modeID))
        depth = di.MaxDepth >= 24 ? (di.MaxDepth > 32 ? 32 : di.MaxDepth) : di.MaxDepth;

    window->os3.screen = OpenScreenTags(NULL,
                                        SA_DisplayID, modeID,
                                        SA_Width, width,
                                        SA_Height, height,
                                        SA_Depth, depth,
                                        SA_Title, (ULONG) window->os3.title,
                                        SA_ShowTitle, FALSE,
                                        SA_Quiet, TRUE,
                                        SA_Draggable, FALSE,
                                        SA_Type, CUSTOMSCREEN,
                                        SA_FullPalette, TRUE,
                                        TAG_END);
    if (!window->os3.screen)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "AmigaOS: Failed to open a %ix%i screen of %i bits",
                        width, height, depth);
        return GLFW_FALSE;
    }

    window->os3.handle = OpenWindowTags(NULL,
                                        WA_CustomScreen, (ULONG) window->os3.screen,
                                        WA_Left, 0,
                                        WA_Top, 0,
                                        WA_Width, window->os3.screen->Width,
                                        WA_Height, window->os3.screen->Height,
                                        WA_Borderless, TRUE,
                                        WA_Backdrop, TRUE,
                                        WA_Activate, TRUE,
                                        WA_RMBTrap, TRUE,
                                        WA_ReportMouse, TRUE,
                                        WA_SimpleRefresh, TRUE,
                                        WA_IDCMP, windowIDCMP(window),
                                        TAG_END);
    if (!window->os3.handle)
    {
        CloseScreen(window->os3.screen);
        window->os3.screen = NULL;
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "AmigaOS: Failed to open a window on the full screen");
        return GLFW_FALSE;
    }

    return GLFW_TRUE;
}

// A window on the Workbench, its content area at xpos, ypos (or in the
// middle of the screen) and width by height
//
static GLFWbool openWorkbenchWindow(_GLFWwindow* window, GLFWbool activate)
{
    struct Screen* screen = LockPubScreen(NULL);
    const GLFWbool decorated = window->os3.decorated;
    int left, top, borderLeft = 0, borderTop = 0, outerWidth, outerHeight;

    if (!screen)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR, "AmigaOS: No Workbench screen");
        return GLFW_FALSE;
    }

    if (decorated)
    {
        borderLeft = screen->WBorLeft;
        borderTop = screen->WBorTop + screen->Font->ta_YSize + 1;
    }
    outerWidth = window->os3.width + (decorated ? screen->WBorLeft + screen->WBorRight : 0);
    outerHeight = window->os3.height + (decorated ? borderTop + screen->WBorBottom : 0);

    if (window->os3.xpos == GLFW_ANY_POSITION || window->os3.ypos == GLFW_ANY_POSITION)
    {
        left = (screen->Width - outerWidth) / 2;
        top = (screen->Height - outerHeight) / 2;
    }
    else
    {
        left = window->os3.xpos - borderLeft;
        top = window->os3.ypos - borderTop;
    }
    if (left < 0)
        left = 0;
    if (top < screen->BarHeight + 1 && outerHeight < screen->Height - screen->BarHeight - 1)
        top = screen->BarHeight + 1;
    if (top < 0)
        top = 0;

    window->os3.handle = OpenWindowTags(NULL,
                                        WA_PubScreen, (ULONG) screen,
                                        WA_Left, left,
                                        WA_Top, top,
                                        WA_InnerWidth, window->os3.width,
                                        WA_InnerHeight, window->os3.height,
                                        WA_Title, (ULONG) (decorated ? window->os3.title : NULL),
                                        WA_DragBar, decorated,
                                        WA_DepthGadget, decorated,
                                        WA_CloseGadget, decorated,
                                        WA_SizeGadget, decorated && window->os3.resizable,
                                        WA_SizeBBottom, decorated && window->os3.resizable,
                                        WA_Borderless, !decorated,
                                        WA_MinWidth, 16,
                                        WA_MinHeight, 16,
                                        WA_MaxWidth, (ULONG) ~0,
                                        WA_MaxHeight, (ULONG) ~0,
                                        WA_AutoAdjust, TRUE,
                                        WA_Activate, activate,
                                        WA_RMBTrap, TRUE,
                                        WA_ReportMouse, TRUE,
                                        WA_SimpleRefresh, TRUE,
                                        WA_NewLookMenus, TRUE,
                                        WA_IDCMP, windowIDCMP(window),
                                        TAG_END);
    UnlockPubScreen(NULL, screen);

    if (!window->os3.handle)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "AmigaOS: Failed to open a %ix%i window",
                        window->os3.width, window->os3.height);
        return GLFW_FALSE;
    }

    return GLFW_TRUE;
}

static GLFWbool openNativeWindow(_GLFWwindow* window, GLFWbool activate)
{
    GLFWbool ok;

    if (window->os3.handle)
        return GLFW_TRUE;

    if (window->monitor)
        ok = openFullScreen(window);
    else
        ok = openWorkbenchWindow(window, activate);
    if (!ok)
        return GLFW_FALSE;

    updateLimits(window);
    updatePointer(window);
    updateGeometry(window, GLFW_TRUE);
    return GLFW_TRUE;
}

// Intuition closes the window's IDCMP port and replies to what is left
//
static void closeNativeWindow(_GLFWwindow* window)
{
    if (window->os3.handle)
    {
        if (window->os3.cursorHidden)
            ClearPointer(window->os3.handle);
        CloseWindow(window->os3.handle);
        window->os3.handle = NULL;
    }

    if (window->os3.screen)
    {
        CloseScreen(window->os3.screen);
        window->os3.screen = NULL;
    }

    // Frames drawn while hidden go nowhere
    window->os3.target.rp = NULL;
    window->os3.target.bitmap = NULL;
    window->os3.deltaMove = GLFW_FALSE;
    window->os3.cursorHidden = GLFW_FALSE;

    if (window->os3.focused)
    {
        window->os3.focused = GLFW_FALSE;
        _glfwInputWindowFocus(window, GLFW_FALSE);
    }
    if (_glfw.os3.focusedWindow == window)
        _glfw.os3.focusedWindow = NULL;
    window->os3.hovered = GLFW_FALSE;
}

static void acquireMonitor(_GLFWwindow* window)
{
    _glfwInputMonitorWindow(window->monitor, window);
}

static void releaseMonitor(_GLFWwindow* window)
{
    if (window->monitor->window != window)
        return;

    _glfwInputMonitorWindow(window->monitor, NULL);
}

static void inputRawKey(_GLFWwindow* window, const _GLFWos3event* event)
{
    const int scancode = event->code & ~IECODE_UP_PREFIX;
    const int mods = translateQualifier(event->qualifier);
    const GLFWbool released = (event->code & IECODE_UP_PREFIX) != 0;
    int action, key;

    if (scancode >= NM_WHEEL_UP && scancode <= NM_WHEEL_RIGHT)
    {
        if (released || wheelSeen)
            return;

        if (scancode == NM_WHEEL_UP)
            _glfwInputScroll(window, 0.0, 1.0);
        else if (scancode == NM_WHEEL_DOWN)
            _glfwInputScroll(window, 0.0, -1.0);
        else if (scancode == NM_WHEEL_LEFT)
            _glfwInputScroll(window, 1.0, 0.0);
        else
            _glfwInputScroll(window, -1.0, 0.0);
        return;
    }

    if (scancode == NM_BUTTON_FOURTH)
    {
        _glfwInputMouseClick(window, GLFW_MOUSE_BUTTON_4,
                             released ? GLFW_RELEASE : GLFW_PRESS, mods);
        return;
    }

    key = _glfw.os3.keycodes[scancode];
    if (released)
        action = GLFW_RELEASE;
    else if (event->qualifier & IEQUALIFIER_REPEAT)
        action = GLFW_REPEAT;
    else
        action = GLFW_PRESS;

    _glfwInputKey(window, key, scancode, action, mods);

    if (!released)
    {
        struct InputEvent ie;
        UBYTE buffer[8];
        WORD count, i;

        memset(&ie, 0, sizeof(ie));
        ie.ie_Class = IECLASS_RAWKEY;
        ie.ie_Code = event->code;
        ie.ie_Qualifier = event->qualifier;
        ie.ie_EventAddress = event->prevKeys;

        // The keymap's Latin-1 is Unicode's first 256
        count = MapRawKey(&ie, (STRPTR) buffer, sizeof(buffer), NULL);
        for (i = 0;  i < count;  i++)
        {
            if (buffer[i] < 0x20 || (buffer[i] >= 0x7f && buffer[i] < 0xa0))
                continue;

            _glfwInputChar(window, buffer[i], mods,
                           !(mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER)));
        }
    }
}

static void inputCursor(_GLFWwindow* window, const _GLFWos3event* event)
{
    struct Window* handle = window->os3.handle;
    int x, y;
    GLFWbool inside;

    if (window->os3.deltaMove)
    {
        // IDCMP_DELTAMOVE: the mouse's own counts, as far as it goes
        if (event->mouseX || event->mouseY)
        {
            _glfwInputCursorPos(window,
                                window->virtualCursorPosX + event->mouseX,
                                window->virtualCursorPosY + event->mouseY);
        }
        return;
    }

    x = event->mouseX - handle->BorderLeft;
    y = event->mouseY - handle->BorderTop;
    inside = x >= 0 && y >= 0 && x < window->os3.width && y < window->os3.height;

    if (inside != window->os3.hovered)
    {
        window->os3.hovered = inside;
        _glfwInputCursorEnter(window, inside);
    }

    if (x != window->os3.lastCursorPosX || y != window->os3.lastCursorPosY)
    {
        window->os3.lastCursorPosX = x;
        window->os3.lastCursorPosY = y;
        _glfwInputCursorPos(window, x, y);
    }
}

static void inputMouseButton(_GLFWwindow* window, const _GLFWos3event* event)
{
    const int mods = translateQualifier(event->qualifier);

    switch (event->code)
    {
        case SELECTDOWN:
            _glfwInputMouseClick(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, mods);
            break;
        case SELECTUP:
            _glfwInputMouseClick(window, GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, mods);
            break;
        case MENUDOWN:
            _glfwInputMouseClick(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_PRESS, mods);
            break;
        case MENUUP:
            _glfwInputMouseClick(window, GLFW_MOUSE_BUTTON_RIGHT, GLFW_RELEASE, mods);
            break;
        case MIDDLEDOWN:
            _glfwInputMouseClick(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_PRESS, mods);
            break;
        case MIDDLEUP:
            _glfwInputMouseClick(window, GLFW_MOUSE_BUTTON_MIDDLE, GLFW_RELEASE, mods);
            break;
    }
}

static void processEvent(_GLFWwindow* window, const _GLFWos3event* event)
{
    switch (event->class)
    {
        case IDCMP_CLOSEWINDOW:
            _glfwInputWindowCloseRequest(window);
            break;

        case IDCMP_NEWSIZE:
        case IDCMP_CHANGEWINDOW:
            updateGeometry(window, GLFW_TRUE);
            break;

        case IDCMP_REFRESHWINDOW:
            BeginRefresh(window->os3.handle);
            EndRefresh(window->os3.handle, TRUE);
            _glfwInputWindowDamage(window);
            break;

        case IDCMP_ACTIVEWINDOW:
            if (window->os3.iconified)
            {
                window->os3.iconified = GLFW_FALSE;
                _glfwInputWindowIconify(window, GLFW_FALSE);
            }
            window->os3.focused = GLFW_TRUE;
            _glfw.os3.focusedWindow = window;
            _glfwInputWindowFocus(window, GLFW_TRUE);
            break;

        case IDCMP_INACTIVEWINDOW:
            window->os3.focused = GLFW_FALSE;
            if (_glfw.os3.focusedWindow == window)
                _glfw.os3.focusedWindow = NULL;
            if (window->os3.hovered)
            {
                window->os3.hovered = GLFW_FALSE;
                _glfwInputCursorEnter(window, GLFW_FALSE);
            }
            _glfwInputWindowFocus(window, GLFW_FALSE);
            break;

        case IDCMP_MOUSEMOVE:
            inputCursor(window, event);
            break;

        case IDCMP_MOUSEBUTTONS:
            inputCursor(window, event);
            inputMouseButton(window, event);
            break;

        case IDCMP_RAWKEY:
            inputRawKey(window, event);
            break;

        case IDCMP_EXTENDEDMOUSE:
            if (event->code == IMSGCODE_INTUIWHEELDATA)
            {
                wheelSeen = GLFW_TRUE;
                // Negative is up (and left)
                if (event->wheelX || event->wheelY)
                    _glfwInputScroll(window, -event->wheelX, -event->wheelY);
            }
            break;
    }
}

// Takes one message from any open window's port and handles it: GLFW's
// callbacks may destroy windows, so the list is walked anew each time
//
static GLFWbool processOneEvent(void)
{
    _GLFWwindow* window;

    for (window = _glfw.windowListHead;  window;  window = window->next)
    {
        struct IntuiMessage* msg;
        _GLFWos3event event;

        if (!window->os3.handle)
            continue;

        msg = (struct IntuiMessage*) GetMsg(window->os3.handle->UserPort);
        if (!msg)
            continue;

        memset(&event, 0, sizeof(event));
        event.class = msg->Class;
        event.code = msg->Code;
        event.qualifier = msg->Qualifier;
        event.mouseX = msg->MouseX;
        event.mouseY = msg->MouseY;
        if (msg->Class == IDCMP_RAWKEY && msg->IAddress)
            event.prevKeys = *(APTR*) msg->IAddress;
        if (msg->Class == IDCMP_EXTENDEDMOUSE &&
            msg->Code == IMSGCODE_INTUIWHEELDATA && msg->IAddress)
        {
            const struct IntuiWheelData* wheel = msg->IAddress;
            event.wheelX = wheel->WheelX;
            event.wheelY = wheel->WheelY;
        }

        // Refreshing must happen before the reply (BeginRefresh)
        if (msg->Class == IDCMP_REFRESHWINDOW)
        {
            processEvent(window, &event);
            ReplyMsg((struct Message*) msg);
            return GLFW_TRUE;
        }

        ReplyMsg((struct Message*) msg);
        processEvent(window, &event);
        return GLFW_TRUE;
    }

    return GLFW_FALSE;
}

// Every open window's IDCMP signal, Ctrl-C and glfwPostEmptyEvent's
//
static ULONG waitMask(void)
{
    _GLFWwindow* window;
    ULONG mask = SIGBREAKF_CTRL_C;

    if (_glfw.os3.emptySignal != -1)
        mask |= 1UL << _glfw.os3.emptySignal;

    for (window = _glfw.windowListHead;  window;  window = window->next)
    {
        if (window->os3.handle)
            mask |= 1UL << window->os3.handle->UserPort->mp_SigBit;
    }

    return mask;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwCreateWindowOS3(_GLFWwindow* window,
                              const _GLFWwndconfig* wndconfig,
                              const _GLFWctxconfig* ctxconfig,
                              const _GLFWfbconfig* fbconfig)
{
    window->os3.title = _glfw_strdup(wndconfig->title);
    window->os3.xpos = wndconfig->xpos;
    window->os3.ypos = wndconfig->ypos;
    window->os3.width = wndconfig->width;
    window->os3.height = wndconfig->height;
    window->os3.resizable = wndconfig->resizable;
    window->os3.decorated = wndconfig->decorated;
    window->os3.floating = wndconfig->floating;
    window->os3.lastCursorPosX = -1;
    window->os3.lastCursorPosY = -1;

    gla_os3_present_init(&window->os3.present, &window->os3.target);

    // A full screen window shows now; the others when glfwShowWindow asks
    if (window->monitor)
    {
        if (!openNativeWindow(window, GLFW_TRUE))
            return GLFW_FALSE;
        window->os3.visible = GLFW_TRUE;
        acquireMonitor(window);
    }

    if (ctxconfig->client != GLFW_NO_API)
    {
        if (ctxconfig->source == GLFW_NATIVE_CONTEXT_API)
        {
            if (!_glfwCreateContextGLA(window, ctxconfig, fbconfig))
                return GLFW_FALSE;
        }
        else if (ctxconfig->source == GLFW_EGL_CONTEXT_API)
        {
            if (!_glfwInitEGL())
                return GLFW_FALSE;
            if (!_glfwCreateContextEGL(window, ctxconfig, fbconfig))
                return GLFW_FALSE;
        }
        else if (ctxconfig->source == GLFW_OSMESA_CONTEXT_API)
        {
            if (!_glfwInitOSMesa())
                return GLFW_FALSE;
            if (!_glfwCreateContextOSMesa(window, ctxconfig, fbconfig))
                return GLFW_FALSE;
        }

        if (!_glfwRefreshContextAttribs(window, ctxconfig))
            return GLFW_FALSE;
    }

    if (wndconfig->mousePassthrough)
        _glfwSetWindowMousePassthroughOS3(window, GLFW_TRUE);

    return GLFW_TRUE;
}

void _glfwDestroyWindowOS3(_GLFWwindow* window)
{
    if (window->monitor)
        releaseMonitor(window);

    if (window->context.destroy)
        window->context.destroy(window);

    closeNativeWindow(window);

    _glfw_free(window->os3.title);
    window->os3.title = NULL;
}

void _glfwSetWindowTitleOS3(_GLFWwindow* window, const char* title)
{
    char* copy = _glfw_strdup(title);

    if (window->os3.handle && !window->monitor && window->os3.decorated)
        SetWindowTitles(window->os3.handle, (UBYTE*) copy, (UBYTE*) ~0);

    _glfw_free(window->os3.title);
    window->os3.title = copy;
}

void _glfwSetWindowIconOS3(_GLFWwindow* window, int count, const GLFWimage* images)
{
    // A program's icon is its .info file
}

void _glfwSetWindowMonitorOS3(_GLFWwindow* window,
                              _GLFWmonitor* monitor,
                              int xpos, int ypos,
                              int width, int height,
                              int refreshRate)
{
    const GLFWbool wasOpen = window->os3.handle != NULL;

    if (window->monitor == monitor && !monitor)
    {
        _glfwSetWindowPosOS3(window, xpos, ypos);
        _glfwSetWindowSizeOS3(window, width, height);
        return;
    }

    // A screen's mode is set when it opens: a new mode is a new screen
    closeNativeWindow(window);

    if (window->monitor)
        releaseMonitor(window);

    _glfwInputWindowMonitor(window, monitor);

    window->os3.xpos = xpos;
    window->os3.ypos = ypos;
    window->os3.width = width;
    window->os3.height = height;

    if (window->monitor)
    {
        if (openNativeWindow(window, GLFW_TRUE))
        {
            window->os3.visible = GLFW_TRUE;
            acquireMonitor(window);
        }
    }
    else if (wasOpen || window->os3.visible)
        openNativeWindow(window, GLFW_TRUE);

    _glfwResizeContextGLA(window);
}

void _glfwGetWindowPosOS3(_GLFWwindow* window, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = window->os3.xpos == GLFW_ANY_POSITION ? 0 : window->os3.xpos;
    if (ypos)
        *ypos = window->os3.ypos == GLFW_ANY_POSITION ? 0 : window->os3.ypos;
}

void _glfwSetWindowPosOS3(_GLFWwindow* window, int xpos, int ypos)
{
    if (window->monitor)
        return;

    if (window->os3.handle)
    {
        // Intuition moves the window later and says so (IDCMP_CHANGEWINDOW)
        MoveWindow(window->os3.handle,
                   xpos - window->os3.xpos, ypos - window->os3.ypos);
    }
    else
    {
        window->os3.xpos = xpos;
        window->os3.ypos = ypos;
    }
}

void _glfwGetWindowSizeOS3(_GLFWwindow* window, int* width, int* height)
{
    if (width)
        *width = window->os3.width;
    if (height)
        *height = window->os3.height;
}

void _glfwSetWindowSizeOS3(_GLFWwindow* window, int width, int height)
{
    if (window->monitor)
    {
        if (window->monitor->window == window)
        {
            window->videoMode.width = width;
            window->videoMode.height = height;
            _glfwSetWindowMonitorOS3(window, window->monitor, 0, 0,
                                     width, height, window->videoMode.refreshRate);
        }
        return;
    }

    if (window->os3.handle)
    {
        // Intuition resizes the window later and says so (IDCMP_NEWSIZE)
        SizeWindow(window->os3.handle,
                   width - window->os3.width, height - window->os3.height);
    }
    else
    {
        window->os3.width = width;
        window->os3.height = height;
        _glfwResizeContextGLA(window);
    }
}

void _glfwSetWindowSizeLimitsOS3(_GLFWwindow* window,
                                 int minwidth, int minheight,
                                 int maxwidth, int maxheight)
{
    updateLimits(window);
}

void _glfwSetWindowAspectRatioOS3(_GLFWwindow* window, int n, int d)
{
    // Intuition can't keep an aspect ratio
}

void _glfwGetFramebufferSizeOS3(_GLFWwindow* window, int* width, int* height)
{
    if (width)
        *width = window->os3.width;
    if (height)
        *height = window->os3.height;
}

void _glfwGetWindowFrameSizeOS3(_GLFWwindow* window,
                                int* left, int* top,
                                int* right, int* bottom)
{
    int l = 0, t = 0, r = 0, b = 0;

    if (window->os3.handle)
    {
        l = window->os3.handle->BorderLeft;
        t = window->os3.handle->BorderTop;
        r = window->os3.handle->BorderRight;
        b = window->os3.handle->BorderBottom;
    }
    else if (window->os3.decorated && !window->monitor)
    {
        struct Screen* screen = LockPubScreen(NULL);
        if (screen)
        {
            l = screen->WBorLeft;
            t = screen->WBorTop + screen->Font->ta_YSize + 1;
            r = screen->WBorRight;
            b = screen->WBorBottom;
            UnlockPubScreen(NULL, screen);
        }
    }

    if (left)
        *left = l;
    if (top)
        *top = t;
    if (right)
        *right = r;
    if (bottom)
        *bottom = b;
}

void _glfwGetWindowContentScaleOS3(_GLFWwindow* window, float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 1.f;
    if (yscale)
        *yscale = 1.f;
}

// Intuition has no iconified windows: the window goes behind the others
// until it is active again
//
void _glfwIconifyWindowOS3(_GLFWwindow* window)
{
    if (window->os3.iconified)
        return;

    if (window->os3.handle)
        WindowToBack(window->os3.handle);

    window->os3.iconified = GLFW_TRUE;
    _glfwInputWindowIconify(window, GLFW_TRUE);
}

void _glfwRestoreWindowOS3(_GLFWwindow* window)
{
    if (window->os3.iconified)
    {
        if (window->os3.handle)
            WindowToFront(window->os3.handle);

        window->os3.iconified = GLFW_FALSE;
        _glfwInputWindowIconify(window, GLFW_FALSE);
    }
    else if (window->os3.maximized)
    {
        if (window->os3.handle)
        {
            ChangeWindowBox(window->os3.handle,
                            window->os3.restoreXpos, window->os3.restoreYpos,
                            window->os3.restoreWidth, window->os3.restoreHeight);
        }

        window->os3.maximized = GLFW_FALSE;
        _glfwInputWindowMaximize(window, GLFW_FALSE);
    }
}

// The window fills the Workbench below its title bar
//
void _glfwMaximizeWindowOS3(_GLFWwindow* window)
{
    struct Window* handle = window->os3.handle;
    struct Screen* screen;

    if (window->os3.maximized || window->monitor || !handle)
        return;

    screen = handle->WScreen;
    window->os3.restoreXpos = handle->LeftEdge;
    window->os3.restoreYpos = handle->TopEdge;
    window->os3.restoreWidth = handle->Width;
    window->os3.restoreHeight = handle->Height;

    ChangeWindowBox(handle, 0, screen->BarHeight + 1,
                    screen->Width, screen->Height - screen->BarHeight - 1);

    window->os3.maximized = GLFW_TRUE;
    _glfwInputWindowMaximize(window, GLFW_TRUE);
}

GLFWbool _glfwWindowMaximizedOS3(_GLFWwindow* window)
{
    return window->os3.maximized;
}

GLFWbool _glfwWindowHoveredOS3(_GLFWwindow* window)
{
    struct Window* handle = window->os3.handle;
    int x, y;

    if (!handle)
        return GLFW_FALSE;

    x = handle->MouseX - handle->BorderLeft;
    y = handle->MouseY - handle->BorderTop;
    return x >= 0 && y >= 0 && x < window->os3.width && y < window->os3.height;
}

GLFWbool _glfwFramebufferTransparentOS3(_GLFWwindow* window)
{
    return GLFW_FALSE;
}

// Intuition sets a window's gadgets when it opens: an open window opens
// again with the new ones
//
void _glfwSetWindowResizableOS3(_GLFWwindow* window, GLFWbool enabled)
{
    if (window->os3.resizable == enabled)
        return;

    window->os3.resizable = enabled;
    if (window->os3.handle && !window->monitor)
    {
        closeNativeWindow(window);
        openNativeWindow(window, GLFW_TRUE);
    }
}

void _glfwSetWindowDecoratedOS3(_GLFWwindow* window, GLFWbool enabled)
{
    if (window->os3.decorated == enabled)
        return;

    window->os3.decorated = enabled;
    if (window->os3.handle && !window->monitor)
    {
        closeNativeWindow(window);
        openNativeWindow(window, GLFW_TRUE);
    }
}

void _glfwSetWindowFloatingOS3(_GLFWwindow* window, GLFWbool enabled)
{
    // Intuition has no windows above the others
    window->os3.floating = enabled;
}

void _glfwSetWindowMousePassthroughOS3(_GLFWwindow* window, GLFWbool enabled)
{
}

float _glfwGetWindowOpacityOS3(_GLFWwindow* window)
{
    return 1.f;
}

void _glfwSetWindowOpacityOS3(_GLFWwindow* window, float opacity)
{
}

// GLFW_CURSOR_DISABLED reads the mouse's counts (IDCMP_DELTAMOVE) anyway
//
void _glfwSetRawMouseMotionOS3(_GLFWwindow *window, GLFWbool enabled)
{
}

GLFWbool _glfwRawMouseMotionSupportedOS3(void)
{
    return GLFW_TRUE;
}

void _glfwShowWindowOS3(_GLFWwindow* window)
{
    if (!window->os3.handle)
    {
        // GLFW focuses a new window itself when it should (GLFW_FOCUSED)
        if (!openNativeWindow(window, GLFW_FALSE))
            return;
    }

    window->os3.visible = GLFW_TRUE;
}

void _glfwRequestWindowAttentionOS3(_GLFWwindow* window)
{
    if (window->os3.handle)
        DisplayBeep(window->os3.handle->WScreen);
}

void _glfwHideWindowOS3(_GLFWwindow* window)
{
    if (window->monitor)
        return;

    closeNativeWindow(window);
    window->os3.visible = GLFW_FALSE;
}

void _glfwFocusWindowOS3(_GLFWwindow* window)
{
    if (!window->os3.handle)
        return;

    WindowToFront(window->os3.handle);
    ActivateWindow(window->os3.handle);
}

GLFWbool _glfwWindowFocusedOS3(_GLFWwindow* window)
{
    return window->os3.focused;
}

GLFWbool _glfwWindowIconifiedOS3(_GLFWwindow* window)
{
    return window->os3.iconified;
}

GLFWbool _glfwWindowVisibleOS3(_GLFWwindow* window)
{
    return window->os3.visible;
}

void _glfwPollEventsOS3(void)
{
    ULONG signals;

    while (processOneEvent())
        ;

    // Ctrl-C from the Shell: every window is asked to close
    signals = SetSignal(0, SIGBREAKF_CTRL_C);
    if (signals & SIGBREAKF_CTRL_C)
    {
        _GLFWwindow* window;
        for (window = _glfw.windowListHead;  window;  window = window->next)
            _glfwInputWindowCloseRequest(window);
    }

    if (_glfw.os3.emptySignal != -1)
        SetSignal(0, 1UL << _glfw.os3.emptySignal);
}

// Wait clears the signals it returns: Ctrl-C is set again for
// _glfwPollEventsOS3 to find
//
static void waitSignals(ULONG mask)
{
    const ULONG got = Wait(mask);

    if (got & SIGBREAKF_CTRL_C)
        SetSignal(SIGBREAKF_CTRL_C, SIGBREAKF_CTRL_C);
}

void _glfwWaitEventsOS3(void)
{
    // A message waiting already leaves its port's signal set
    waitSignals(waitMask());
    _glfwPollEventsOS3();
}

void _glfwWaitEventsTimeoutOS3(double timeout)
{
    struct timerequest* request = _glfw.os3.waitRequest;
    ULONG mask = waitMask();

    if (!_glfw.os3.waitOpen || timeout <= 0.0)
    {
        _glfwPollEventsOS3();
        return;
    }

    if (timeout > 86400.0)
        timeout = 86400.0;

    request->tr_node.io_Command = TR_ADDREQUEST;
    request->tr_time.tv_secs = (ULONG) timeout;
    request->tr_time.tv_micro = (ULONG) ((timeout - (ULONG) timeout) * 1e6);
    SetSignal(0, 1UL << _glfw.os3.waitPort->mp_SigBit);
    SendIO((struct IORequest*) request);

    waitSignals(mask | (1UL << _glfw.os3.waitPort->mp_SigBit));

    if (!CheckIO((struct IORequest*) request))
        AbortIO((struct IORequest*) request);
    WaitIO((struct IORequest*) request);

    _glfwPollEventsOS3();
}

void _glfwPostEmptyEventOS3(void)
{
    if (_glfw.os3.task && _glfw.os3.emptySignal != -1)
        Signal(_glfw.os3.task, 1UL << _glfw.os3.emptySignal);
}

void _glfwGetCursorPosOS3(_GLFWwindow* window, double* xpos, double* ypos)
{
    int x = window->os3.lastCursorPosX, y = window->os3.lastCursorPosY;

    if (window->os3.handle)
    {
        x = window->os3.handle->MouseX - window->os3.handle->BorderLeft;
        y = window->os3.handle->MouseY - window->os3.handle->BorderTop;
    }

    if (xpos)
        *xpos = x;
    if (ypos)
        *ypos = y;
}

// Intuition's pointer goes where the mouse takes it: GLFW only keeps the
// place it was told
//
void _glfwSetCursorPosOS3(_GLFWwindow* window, double x, double y)
{
    window->os3.lastCursorPosX = (int) x;
    window->os3.lastCursorPosY = (int) y;
}

void _glfwSetCursorModeOS3(_GLFWwindow* window, int mode)
{
    updatePointer(window);
}

// Cursor images are not made: a window's cursor is Intuition's pointer
//
GLFWbool _glfwCreateCursorOS3(_GLFWcursor* cursor,
                              const GLFWimage* image,
                              int xhot, int yhot)
{
    return GLFW_TRUE;
}

GLFWbool _glfwCreateStandardCursorOS3(_GLFWcursor* cursor, int shape)
{
    return GLFW_TRUE;
}

void _glfwDestroyCursorOS3(_GLFWcursor* cursor)
{
}

void _glfwSetCursorOS3(_GLFWwindow* window, _GLFWcursor* cursor)
{
}

EGLenum _glfwGetEGLPlatformOS3(EGLint** attribs)
{
    return 0;
}

EGLNativeDisplayType _glfwGetEGLNativeDisplayOS3(void)
{
    return 0;
}

EGLNativeWindowType _glfwGetEGLNativeWindowOS3(_GLFWwindow* window)
{
    return (EGLNativeWindowType) window->os3.handle;
}

void _glfwGetRequiredInstanceExtensionsOS3(char** extensions)
{
}

GLFWbool _glfwGetPhysicalDevicePresentationSupportOS3(VkInstance instance,
                                                      VkPhysicalDevice device,
                                                      uint32_t queuefamily)
{
    return GLFW_FALSE;
}

VkResult _glfwCreateWindowSurfaceOS3(VkInstance instance,
                                     _GLFWwindow* window,
                                     const VkAllocationCallbacks* allocator,
                                     VkSurfaceKHR* surface)
{
    // There is no Vulkan on AmigaOS 3
    return VK_ERROR_EXTENSION_NOT_PRESENT;
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW native API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI struct Window* glfwGetAmigaWindow(GLFWwindow* handle)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (_glfw.platform.platformID != GLFW_PLATFORM_AMIGAOS)
    {
        _glfwInputError(GLFW_PLATFORM_UNAVAILABLE,
                        "AmigaOS: Platform not initialized");
        return NULL;
    }

    return window->os3.handle;
}

GLFWAPI struct Screen* glfwGetAmigaScreen(GLFWwindow* handle)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (_glfw.platform.platformID != GLFW_PLATFORM_AMIGAOS)
    {
        _glfwInputError(GLFW_PLATFORM_UNAVAILABLE,
                        "AmigaOS: Platform not initialized");
        return NULL;
    }

    return window->os3.handle ? window->os3.handle->WScreen : NULL;
}

#endif // _GLFW_AMIGAOS

