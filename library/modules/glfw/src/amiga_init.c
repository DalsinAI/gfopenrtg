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

#include "internal.h"

#if defined(_GLFW_AMIGAOS)

#include "amiga_os.h"

#include <stdlib.h>
#include <string.h>

// Raw key codes to GLFW keys: the A500 to A4000 keyboards by position
// (US names), and the PC keys the Amiga's keyboard has none of (Insert,
// Home and the rest) at the codes PC keyboard adapters, AROS and AmigaOS 4
// give them
//
static void createKeyTables(void)
{
    static const struct { short scancode; short key; } keys[] =
    {
        { 0x00, GLFW_KEY_GRAVE_ACCENT },
        { 0x01, GLFW_KEY_1 }, { 0x02, GLFW_KEY_2 }, { 0x03, GLFW_KEY_3 },
        { 0x04, GLFW_KEY_4 }, { 0x05, GLFW_KEY_5 }, { 0x06, GLFW_KEY_6 },
        { 0x07, GLFW_KEY_7 }, { 0x08, GLFW_KEY_8 }, { 0x09, GLFW_KEY_9 },
        { 0x0a, GLFW_KEY_0 },
        { 0x0b, GLFW_KEY_MINUS },
        { 0x0c, GLFW_KEY_EQUAL },
        { 0x0d, GLFW_KEY_BACKSLASH },
        { 0x0f, GLFW_KEY_KP_0 },
        { 0x10, GLFW_KEY_Q }, { 0x11, GLFW_KEY_W }, { 0x12, GLFW_KEY_E },
        { 0x13, GLFW_KEY_R }, { 0x14, GLFW_KEY_T }, { 0x15, GLFW_KEY_Y },
        { 0x16, GLFW_KEY_U }, { 0x17, GLFW_KEY_I }, { 0x18, GLFW_KEY_O },
        { 0x19, GLFW_KEY_P },
        { 0x1a, GLFW_KEY_LEFT_BRACKET },
        { 0x1b, GLFW_KEY_RIGHT_BRACKET },
        { 0x1d, GLFW_KEY_KP_1 }, { 0x1e, GLFW_KEY_KP_2 }, { 0x1f, GLFW_KEY_KP_3 },
        { 0x20, GLFW_KEY_A }, { 0x21, GLFW_KEY_S }, { 0x22, GLFW_KEY_D },
        { 0x23, GLFW_KEY_F }, { 0x24, GLFW_KEY_G }, { 0x25, GLFW_KEY_H },
        { 0x26, GLFW_KEY_J }, { 0x27, GLFW_KEY_K }, { 0x28, GLFW_KEY_L },
        { 0x29, GLFW_KEY_SEMICOLON },
        { 0x2a, GLFW_KEY_APOSTROPHE },
        { 0x2b, GLFW_KEY_WORLD_1 },         // beside Return on international keyboards
        { 0x2d, GLFW_KEY_KP_4 }, { 0x2e, GLFW_KEY_KP_5 }, { 0x2f, GLFW_KEY_KP_6 },
        { 0x30, GLFW_KEY_WORLD_2 },         // beside left Shift on international keyboards
        { 0x31, GLFW_KEY_Z }, { 0x32, GLFW_KEY_X }, { 0x33, GLFW_KEY_C },
        { 0x34, GLFW_KEY_V }, { 0x35, GLFW_KEY_B }, { 0x36, GLFW_KEY_N },
        { 0x37, GLFW_KEY_M },
        { 0x38, GLFW_KEY_COMMA },
        { 0x39, GLFW_KEY_PERIOD },
        { 0x3a, GLFW_KEY_SLASH },
        { 0x3c, GLFW_KEY_KP_DECIMAL },
        { 0x3d, GLFW_KEY_KP_7 }, { 0x3e, GLFW_KEY_KP_8 }, { 0x3f, GLFW_KEY_KP_9 },
        { 0x40, GLFW_KEY_SPACE },
        { 0x41, GLFW_KEY_BACKSPACE },
        { 0x42, GLFW_KEY_TAB },
        { 0x43, GLFW_KEY_KP_ENTER },
        { 0x44, GLFW_KEY_ENTER },
        { 0x45, GLFW_KEY_ESCAPE },
        { 0x46, GLFW_KEY_DELETE },
        { 0x47, GLFW_KEY_INSERT },
        { 0x48, GLFW_KEY_PAGE_UP },
        { 0x49, GLFW_KEY_PAGE_DOWN },
        { 0x4a, GLFW_KEY_KP_SUBTRACT },
        { 0x4b, GLFW_KEY_F11 },
        { 0x4c, GLFW_KEY_UP },
        { 0x4d, GLFW_KEY_DOWN },
        { 0x4e, GLFW_KEY_RIGHT },
        { 0x4f, GLFW_KEY_LEFT },
        { 0x50, GLFW_KEY_F1 }, { 0x51, GLFW_KEY_F2 }, { 0x52, GLFW_KEY_F3 },
        { 0x53, GLFW_KEY_F4 }, { 0x54, GLFW_KEY_F5 }, { 0x55, GLFW_KEY_F6 },
        { 0x56, GLFW_KEY_F7 }, { 0x57, GLFW_KEY_F8 }, { 0x58, GLFW_KEY_F9 },
        { 0x59, GLFW_KEY_F10 },
        { 0x5c, GLFW_KEY_KP_DIVIDE },
        { 0x5d, GLFW_KEY_KP_MULTIPLY },
        { 0x5e, GLFW_KEY_KP_ADD },
        { 0x60, GLFW_KEY_LEFT_SHIFT },
        { 0x61, GLFW_KEY_RIGHT_SHIFT },
        { 0x62, GLFW_KEY_CAPS_LOCK },
        { 0x63, GLFW_KEY_LEFT_CONTROL },
        { 0x64, GLFW_KEY_LEFT_ALT },
        { 0x65, GLFW_KEY_RIGHT_ALT },
        { 0x66, GLFW_KEY_LEFT_SUPER },      // left Amiga
        { 0x67, GLFW_KEY_RIGHT_SUPER },     // right Amiga
        { 0x6b, GLFW_KEY_SCROLL_LOCK },
        { 0x6c, GLFW_KEY_PRINT_SCREEN },
        { 0x6d, GLFW_KEY_NUM_LOCK },
        { 0x6e, GLFW_KEY_PAUSE },
        { 0x6f, GLFW_KEY_F12 },
        { 0x70, GLFW_KEY_HOME },
        { 0x71, GLFW_KEY_END },
        { 0x5a, GLFW_KEY_NUM_LOCK },        // the A4000's keypad "[", NumLock on adapters
    };
    int i;

    for (i = 0;  i <= GLFW_OS3_SC_LAST;  i++)
        _glfw.os3.keycodes[i] = GLFW_KEY_UNKNOWN;
    for (i = 0;  i <= GLFW_KEY_LAST;  i++)
        _glfw.os3.scancodes[i] = -1;

    for (i = 0;  i < (int) (sizeof(keys) / sizeof(keys[0]));  i++)
    {
        _glfw.os3.keycodes[keys[i].scancode] = keys[i].key;
        _glfw.os3.scancodes[keys[i].key] = keys[i].scancode;
    }
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwConnectOS3(int platformID, _GLFWplatform* platform)
{
    const _GLFWplatform os3 =
    {
        .platformID = GLFW_PLATFORM_AMIGAOS,
        .init = _glfwInitOS3,
        .terminate = _glfwTerminateOS3,
        .getCursorPos = _glfwGetCursorPosOS3,
        .setCursorPos = _glfwSetCursorPosOS3,
        .setCursorMode = _glfwSetCursorModeOS3,
        .setRawMouseMotion = _glfwSetRawMouseMotionOS3,
        .rawMouseMotionSupported = _glfwRawMouseMotionSupportedOS3,
        .createCursor = _glfwCreateCursorOS3,
        .createStandardCursor = _glfwCreateStandardCursorOS3,
        .destroyCursor = _glfwDestroyCursorOS3,
        .setCursor = _glfwSetCursorOS3,
        .getScancodeName = _glfwGetScancodeNameOS3,
        .getKeyScancode = _glfwGetKeyScancodeOS3,
        .setClipboardString = _glfwSetClipboardStringOS3,
        .getClipboardString = _glfwGetClipboardStringOS3,
        .initJoysticks = _glfwInitJoysticksOS3,
        .terminateJoysticks = _glfwTerminateJoysticksOS3,
        .pollJoystick = _glfwPollJoystickOS3,
        .getMappingName = _glfwGetMappingNameOS3,
        .updateGamepadGUID = _glfwUpdateGamepadGUIDOS3,
        .freeMonitor = _glfwFreeMonitorOS3,
        .getMonitorPos = _glfwGetMonitorPosOS3,
        .getMonitorContentScale = _glfwGetMonitorContentScaleOS3,
        .getMonitorWorkarea = _glfwGetMonitorWorkareaOS3,
        .getVideoModes = _glfwGetVideoModesOS3,
        .getVideoMode = _glfwGetVideoModeOS3,
        .getGammaRamp = _glfwGetGammaRampOS3,
        .setGammaRamp = _glfwSetGammaRampOS3,
        .createWindow = _glfwCreateWindowOS3,
        .destroyWindow = _glfwDestroyWindowOS3,
        .setWindowTitle = _glfwSetWindowTitleOS3,
        .setWindowIcon = _glfwSetWindowIconOS3,
        .getWindowPos = _glfwGetWindowPosOS3,
        .setWindowPos = _glfwSetWindowPosOS3,
        .getWindowSize = _glfwGetWindowSizeOS3,
        .setWindowSize = _glfwSetWindowSizeOS3,
        .setWindowSizeLimits = _glfwSetWindowSizeLimitsOS3,
        .setWindowAspectRatio = _glfwSetWindowAspectRatioOS3,
        .getFramebufferSize = _glfwGetFramebufferSizeOS3,
        .getWindowFrameSize = _glfwGetWindowFrameSizeOS3,
        .getWindowContentScale = _glfwGetWindowContentScaleOS3,
        .iconifyWindow = _glfwIconifyWindowOS3,
        .restoreWindow = _glfwRestoreWindowOS3,
        .maximizeWindow = _glfwMaximizeWindowOS3,
        .showWindow = _glfwShowWindowOS3,
        .hideWindow = _glfwHideWindowOS3,
        .requestWindowAttention = _glfwRequestWindowAttentionOS3,
        .focusWindow = _glfwFocusWindowOS3,
        .setWindowMonitor = _glfwSetWindowMonitorOS3,
        .windowFocused = _glfwWindowFocusedOS3,
        .windowIconified = _glfwWindowIconifiedOS3,
        .windowVisible = _glfwWindowVisibleOS3,
        .windowMaximized = _glfwWindowMaximizedOS3,
        .windowHovered = _glfwWindowHoveredOS3,
        .framebufferTransparent = _glfwFramebufferTransparentOS3,
        .getWindowOpacity = _glfwGetWindowOpacityOS3,
        .setWindowResizable = _glfwSetWindowResizableOS3,
        .setWindowDecorated = _glfwSetWindowDecoratedOS3,
        .setWindowFloating = _glfwSetWindowFloatingOS3,
        .setWindowOpacity = _glfwSetWindowOpacityOS3,
        .setWindowMousePassthrough = _glfwSetWindowMousePassthroughOS3,
        .pollEvents = _glfwPollEventsOS3,
        .waitEvents = _glfwWaitEventsOS3,
        .waitEventsTimeout = _glfwWaitEventsTimeoutOS3,
        .postEmptyEvent = _glfwPostEmptyEventOS3,
        .getEGLPlatform = _glfwGetEGLPlatformOS3,
        .getEGLNativeDisplay = _glfwGetEGLNativeDisplayOS3,
        .getEGLNativeWindow = _glfwGetEGLNativeWindowOS3,
        .getRequiredInstanceExtensions = _glfwGetRequiredInstanceExtensionsOS3,
        .getPhysicalDevicePresentationSupport = _glfwGetPhysicalDevicePresentationSupportOS3,
        .createWindowSurface = _glfwCreateWindowSurfaceOS3
    };

    *platform = os3;
    return GLFW_TRUE;
}

int _glfwInitOS3(void)
{
    _glfw.os3.emptySignal = -1;
    _glfw.os3.IntuitionBase = OpenLibrary("intuition.library", 39);
    _glfw.os3.GfxBase = OpenLibrary("graphics.library", 39);
    _glfw.os3.KeymapBase = OpenLibrary("keymap.library", 37);
    if (!_glfw.os3.IntuitionBase || !_glfw.os3.GfxBase || !_glfw.os3.KeymapBase)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "AmigaOS: Failed to open intuition.library 39, graphics.library 39 or keymap.library");
        return GLFW_FALSE;
    }

    _glfw.os3.task = FindTask(NULL);
    _glfw.os3.emptySignal = AllocSignal(-1);
    if (_glfw.os3.emptySignal == -1)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR,
                        "AmigaOS: Failed to allocate a signal for empty events");
        return GLFW_FALSE;
    }

    // glfwWaitEventsTimeout's timer; without it the wait polls
    _glfw.os3.waitPort = CreateMsgPort();
    if (_glfw.os3.waitPort)
    {
        _glfw.os3.waitRequest = (struct timerequest*)
            CreateIORequest(_glfw.os3.waitPort, sizeof(struct timerequest));
    }
    if (_glfw.os3.waitRequest)
    {
        _glfw.os3.waitOpen =
            OpenDevice(TIMERNAME, UNIT_MICROHZ,
                       (struct IORequest*) _glfw.os3.waitRequest, 0) == 0;
    }

    // The hidden pointer: one empty line of sprite, in chip memory
    _glfw.os3.blankPointer = AllocVec(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);

    createKeyTables();
    _glfwPollMonitorsOS3();
    return GLFW_TRUE;
}

void _glfwTerminateOS3(void)
{
    _glfwTerminateGLA();

    _glfw_free(_glfw.os3.clipboardString);
    _glfw.os3.clipboardString = NULL;

    if (_glfw.os3.blankPointer)
        FreeVec(_glfw.os3.blankPointer);
    if (_glfw.os3.waitOpen)
        CloseDevice((struct IORequest*) _glfw.os3.waitRequest);
    if (_glfw.os3.waitRequest)
        DeleteIORequest((struct IORequest*) _glfw.os3.waitRequest);
    if (_glfw.os3.waitPort)
        DeleteMsgPort(_glfw.os3.waitPort);
    if (_glfw.os3.emptySignal != -1)
        FreeSignal(_glfw.os3.emptySignal);

    if (_glfw.os3.KeymapBase)
        CloseLibrary(_glfw.os3.KeymapBase);
    if (_glfw.os3.GfxBase)
        CloseLibrary(_glfw.os3.GfxBase);
    if (_glfw.os3.IntuitionBase)
        CloseLibrary(_glfw.os3.IntuitionBase);

    _glfwTerminateTimerOS3();
    memset(&_glfw.os3, 0, sizeof(_glfw.os3));
}

// The clipboard stays inside the program: clipboard.device is not used
//
void _glfwSetClipboardStringOS3(const char* string)
{
    char* copy = _glfw_strdup(string);
    _glfw_free(_glfw.os3.clipboardString);
    _glfw.os3.clipboardString = copy;
}

const char* _glfwGetClipboardStringOS3(void)
{
    if (!_glfw.os3.clipboardString)
    {
        _glfwInputError(GLFW_FORMAT_UNAVAILABLE,
                        "AmigaOS: The clipboard is empty");
    }

    return _glfw.os3.clipboardString;
}

// A printable key's name is what the user's keymap types for it
//
const char* _glfwGetScancodeNameOS3(int scancode)
{
    struct InputEvent ie;
    char buffer[4];
    WORD count;
    int key;

    if (scancode < 0 || scancode > GLFW_OS3_SC_LAST)
    {
        _glfwInputError(GLFW_INVALID_VALUE, "Invalid scancode %i", scancode);
        return NULL;
    }

    key = _glfw.os3.keycodes[scancode];
    if (key == GLFW_KEY_UNKNOWN || key == GLFW_KEY_SPACE ||
        (key >= GLFW_KEY_ESCAPE && key < GLFW_KEY_KP_0) ||
        (key > GLFW_KEY_KP_EQUAL))
    {
        return NULL;
    }

    memset(&ie, 0, sizeof(ie));
    ie.ie_Class = IECLASS_RAWKEY;
    ie.ie_Code = scancode;
    count = MapRawKey(&ie, (STRPTR) buffer, sizeof(buffer), NULL);
    if (count != 1 || (UBYTE) buffer[0] < 0x21)
        return NULL;

    // Latin-1 to UTF-8
    if ((UBYTE) buffer[0] < 0x80)
    {
        _glfw.os3.keynames[key][0] = buffer[0];
        _glfw.os3.keynames[key][1] = '\0';
    }
    else
    {
        _glfw.os3.keynames[key][0] = (char) (0xc0 | ((UBYTE) buffer[0] >> 6));
        _glfw.os3.keynames[key][1] = (char) (0x80 | ((UBYTE) buffer[0] & 0x3f));
        _glfw.os3.keynames[key][2] = '\0';
    }

    return _glfw.os3.keynames[key];
}

int _glfwGetKeyScancodeOS3(int key)
{
    return _glfw.os3.scancodes[key];
}

#endif // _GLFW_AMIGAOS

