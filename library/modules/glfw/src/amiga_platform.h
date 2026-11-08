//========================================================================
// GLFW 3.4 AmigaOS 3 (GFOpenGPU GLA) - www.glfw.org
//------------------------------------------------------------------------
// Copyright (c) 2016 Google Inc.
// Copyright (c) 2016-2017 Camilla Löwy <elmindreda@glfw.org>
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
// The AmigaOS 3 platform: Intuition windows (or a screen of their own for
// full screen), OpenGL and OpenGL ES through GFOpenGPU's GLA calls
// (GL.module: virgl on AmiCradle, softpipe elsewhere), IDCMP input,
// lowlevel.library pads and timer.device's E-clock. Altered from the Null
// platform. This header names no Amiga system header: every GLFW file
// includes it. The Amiga files include amiga_os.h for those.
//
// The compiler predefines amiga, AMIGA and amigaos as macros, so the
// platform's state is named os3 (window->os3, _glfw.os3).

#include <gla/gla_core.h>
#include <gla/os3/gla_present_os3.h>

struct Window;
struct Screen;
struct Library;
struct MsgPort;
struct Task;
struct timerequest;

#define GLFW_OS3_WINDOW_STATE           _GLFWwindowOS3 os3;
#define GLFW_OS3_LIBRARY_WINDOW_STATE   _GLFWlibraryOS3 os3;
#define GLFW_OS3_MONITOR_STATE          _GLFWmonitorOS3 os3;
#define GLFW_OS3_CURSOR_STATE

#define GLFW_GLA_CONTEXT_STATE          _GLFWcontextGLA gla;
#define GLFW_GLA_LIBRARY_CONTEXT_STATE  _GLFWlibraryGLA gla;

// Raw key codes are 7 bits; 0x7A to 0x7D are NewMouse's wheel.
#define GLFW_OS3_SC_LAST        0x7f

// AmigaOS-specific per-window data
//
typedef struct _GLFWwindowOS3
{
    struct Window*  handle;         // NULL while the window is hidden
    struct Screen*  screen;         // the window's own screen (full screen)
    char*           title;          // Intuition keeps the pointer
    int             xpos;           // content area's corner on the screen
    int             ypos;
    int             width;          // content area's size
    int             height;
    int             restoreXpos;    // the window box before maximizing
    int             restoreYpos;
    int             restoreWidth;
    int             restoreHeight;
    GLFWbool        visible;
    GLFWbool        iconified;
    GLFWbool        maximized;
    GLFWbool        resizable;
    GLFWbool        decorated;
    GLFWbool        floating;
    GLFWbool        focused;
    GLFWbool        hovered;
    GLFWbool        cursorHidden;
    GLFWbool        deltaMove;      // IDCMP_DELTAMOVE on (GLFW_CURSOR_DISABLED)
    int             lastCursorPosX;
    int             lastCursorPosY;
    // Where GLA shows the window's frames: its RastPort, inside the borders
    struct gla_os3_target target;
    struct gla_present present;
} _GLFWwindowOS3;

// AmigaOS-specific per-monitor data
//
typedef struct _GLFWmonitorOS3
{
    unsigned long   modeID;         // the Workbench screen's display mode
    int             depth;
} _GLFWmonitorOS3;

// AmigaOS-specific global data
//
typedef struct _GLFWlibraryOS3
{
    // The library bases GLFW's own calls use (amiga_os.h): the program's
    // IntuitionBase and the rest are its own business
    struct Library* IntuitionBase;
    struct Library* GfxBase;
    struct Library* KeymapBase;
    struct Library* LowLevelBase;
    struct Task*    task;           // the task that called glfwInit
    int             emptySignal;    // glfwPostEmptyEvent's signal bit
    struct MsgPort* waitPort;       // glfwWaitEventsTimeout's timer request
    struct timerequest* waitRequest;
    GLFWbool        waitOpen;
    unsigned short* blankPointer;   // a sprite with no pixels, in chip memory
    char*           clipboardString;
    _GLFWwindow*    focusedWindow;
    short           keycodes[GLFW_OS3_SC_LAST + 1];
    short           scancodes[GLFW_KEY_LAST + 1];
    char            keynames[GLFW_KEY_LAST + 1][5];
} _GLFWlibraryOS3;

// GLA-specific per-context data
//
typedef struct _GLFWcontextGLA
{
    struct gla_context* handle;
    struct gla_buffer*  buffer;
    struct gla_config   config;
    unsigned            width;
    unsigned            height;
} _GLFWcontextGLA;

// GLA-specific global data
//
typedef struct _GLFWlibraryGLA
{
    struct gla_display* display;    // one for every context, so they share
} _GLFWlibraryGLA;

GLFWbool _glfwConnectOS3(int platformID, _GLFWplatform* platform);
int _glfwInitOS3(void);
void _glfwTerminateOS3(void);

void _glfwPollMonitorsOS3(void);
void _glfwFreeMonitorOS3(_GLFWmonitor* monitor);
void _glfwGetMonitorPosOS3(_GLFWmonitor* monitor, int* xpos, int* ypos);
void _glfwGetMonitorContentScaleOS3(_GLFWmonitor* monitor, float* xscale, float* yscale);
void _glfwGetMonitorWorkareaOS3(_GLFWmonitor* monitor, int* xpos, int* ypos, int* width, int* height);
GLFWvidmode* _glfwGetVideoModesOS3(_GLFWmonitor* monitor, int* found);
GLFWbool _glfwGetVideoModeOS3(_GLFWmonitor* monitor, GLFWvidmode* mode);
GLFWbool _glfwGetGammaRampOS3(_GLFWmonitor* monitor, GLFWgammaramp* ramp);
void _glfwSetGammaRampOS3(_GLFWmonitor* monitor, const GLFWgammaramp* ramp);

GLFWbool _glfwCreateWindowOS3(_GLFWwindow* window, const _GLFWwndconfig* wndconfig, const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig);
void _glfwDestroyWindowOS3(_GLFWwindow* window);
void _glfwSetWindowTitleOS3(_GLFWwindow* window, const char* title);
void _glfwSetWindowIconOS3(_GLFWwindow* window, int count, const GLFWimage* images);
void _glfwSetWindowMonitorOS3(_GLFWwindow* window, _GLFWmonitor* monitor, int xpos, int ypos, int width, int height, int refreshRate);
void _glfwGetWindowPosOS3(_GLFWwindow* window, int* xpos, int* ypos);
void _glfwSetWindowPosOS3(_GLFWwindow* window, int xpos, int ypos);
void _glfwGetWindowSizeOS3(_GLFWwindow* window, int* width, int* height);
void _glfwSetWindowSizeOS3(_GLFWwindow* window, int width, int height);
void _glfwSetWindowSizeLimitsOS3(_GLFWwindow* window, int minwidth, int minheight, int maxwidth, int maxheight);
void _glfwSetWindowAspectRatioOS3(_GLFWwindow* window, int n, int d);
void _glfwGetFramebufferSizeOS3(_GLFWwindow* window, int* width, int* height);
void _glfwGetWindowFrameSizeOS3(_GLFWwindow* window, int* left, int* top, int* right, int* bottom);
void _glfwGetWindowContentScaleOS3(_GLFWwindow* window, float* xscale, float* yscale);
void _glfwIconifyWindowOS3(_GLFWwindow* window);
void _glfwRestoreWindowOS3(_GLFWwindow* window);
void _glfwMaximizeWindowOS3(_GLFWwindow* window);
GLFWbool _glfwWindowMaximizedOS3(_GLFWwindow* window);
GLFWbool _glfwWindowHoveredOS3(_GLFWwindow* window);
GLFWbool _glfwFramebufferTransparentOS3(_GLFWwindow* window);
void _glfwSetWindowResizableOS3(_GLFWwindow* window, GLFWbool enabled);
void _glfwSetWindowDecoratedOS3(_GLFWwindow* window, GLFWbool enabled);
void _glfwSetWindowFloatingOS3(_GLFWwindow* window, GLFWbool enabled);
void _glfwSetWindowMousePassthroughOS3(_GLFWwindow* window, GLFWbool enabled);
float _glfwGetWindowOpacityOS3(_GLFWwindow* window);
void _glfwSetWindowOpacityOS3(_GLFWwindow* window, float opacity);
void _glfwSetRawMouseMotionOS3(_GLFWwindow *window, GLFWbool enabled);
GLFWbool _glfwRawMouseMotionSupportedOS3(void);
void _glfwShowWindowOS3(_GLFWwindow* window);
void _glfwRequestWindowAttentionOS3(_GLFWwindow* window);
void _glfwHideWindowOS3(_GLFWwindow* window);
void _glfwFocusWindowOS3(_GLFWwindow* window);
GLFWbool _glfwWindowFocusedOS3(_GLFWwindow* window);
GLFWbool _glfwWindowIconifiedOS3(_GLFWwindow* window);
GLFWbool _glfwWindowVisibleOS3(_GLFWwindow* window);
void _glfwPollEventsOS3(void);
void _glfwWaitEventsOS3(void);
void _glfwWaitEventsTimeoutOS3(double timeout);
void _glfwPostEmptyEventOS3(void);
void _glfwGetCursorPosOS3(_GLFWwindow* window, double* xpos, double* ypos);
void _glfwSetCursorPosOS3(_GLFWwindow* window, double x, double y);
void _glfwSetCursorModeOS3(_GLFWwindow* window, int mode);
GLFWbool _glfwCreateCursorOS3(_GLFWcursor* cursor, const GLFWimage* image, int xhot, int yhot);
GLFWbool _glfwCreateStandardCursorOS3(_GLFWcursor* cursor, int shape);
void _glfwDestroyCursorOS3(_GLFWcursor* cursor);
void _glfwSetCursorOS3(_GLFWwindow* window, _GLFWcursor* cursor);
void _glfwSetClipboardStringOS3(const char* string);
const char* _glfwGetClipboardStringOS3(void);
const char* _glfwGetScancodeNameOS3(int scancode);
int _glfwGetKeyScancodeOS3(int key);

EGLenum _glfwGetEGLPlatformOS3(EGLint** attribs);
EGLNativeDisplayType _glfwGetEGLNativeDisplayOS3(void);
EGLNativeWindowType _glfwGetEGLNativeWindowOS3(_GLFWwindow* window);

void _glfwGetRequiredInstanceExtensionsOS3(char** extensions);
GLFWbool _glfwGetPhysicalDevicePresentationSupportOS3(VkInstance instance, VkPhysicalDevice device, uint32_t queuefamily);
VkResult _glfwCreateWindowSurfaceOS3(VkInstance instance, _GLFWwindow* window, const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);

GLFWbool _glfwCreateContextGLA(_GLFWwindow* window, const _GLFWctxconfig* ctxconfig, const _GLFWfbconfig* fbconfig);
void _glfwTerminateGLA(void);
void _glfwResizeContextGLA(_GLFWwindow* window);

