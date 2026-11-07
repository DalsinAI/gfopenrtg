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
// One monitor: the default public screen (the Workbench), with its display
// mode. A full screen window opens a screen of its own (amiga_window.c) in
// one of the modes the display database lists.

#include "internal.h"

#if defined(_GLFW_AMIGAOS)

#include "amiga_os.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// The refresh rate of a mode from its monitor's timings, or 60
//
static int modeRefreshRate(ULONG modeID)
{
    struct MonitorInfo mi;
    double hz;

    if (modeID == INVALID_ID ||
        !GetDisplayInfoData(NULL, (UBYTE*) &mi, sizeof(mi), DTAG_MNTR, modeID))
    {
        return 60;
    }

    // Colour clocks are 280 ns
    if (!mi.TotalRows || !mi.TotalColorClocks)
        return 60;
    hz = 1e9 / ((double) mi.TotalColorClocks * 280.0 * mi.TotalRows);
    if (hz < 20.0 || hz > 250.0)
        return 60;
    return (int) (hz + 0.5);
}

static void modeOfScreen(struct Screen* screen, ULONG modeID, int depth,
                         GLFWvidmode* mode)
{
    mode->width = screen->Width;
    mode->height = screen->Height;
    _glfwSplitBPP(depth > 24 ? 24 : depth, &mode->redBits, &mode->greenBits, &mode->blueBits);
    mode->refreshRate = modeRefreshRate(modeID);
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwPollMonitorsOS3(void)
{
    struct NameInfo ni;
    char name[48];
    int width = 640, height = 512, depth = 8;
    ULONG modeID = INVALID_ID;
    _GLFWmonitor* monitor;
    struct Screen* screen = LockPubScreen(NULL);

    if (screen)
    {
        width = screen->Width;
        height = screen->Height;
        depth = GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH);
        modeID = GetVPModeID(&screen->ViewPort);
        UnlockPubScreen(NULL, screen);
    }

    strcpy(name, "Workbench");
    if (modeID != INVALID_ID &&
        GetDisplayInfoData(NULL, (UBYTE*) &ni, sizeof(ni), DTAG_NAME, modeID))
    {
        snprintf(name, sizeof(name), "Workbench (%s)", (const char*) ni.Name);
    }

    // At 96 dots an inch, as there is no way to know
    monitor = _glfwAllocMonitor(name, (int) (width * 25.4f / 96.f),
                                (int) (height * 25.4f / 96.f));
    monitor->os3.modeID = modeID;
    monitor->os3.depth = depth;
    _glfwInputMonitor(monitor, GLFW_CONNECTED, _GLFW_INSERT_FIRST);
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

void _glfwFreeMonitorOS3(_GLFWmonitor* monitor)
{
}

void _glfwGetMonitorPosOS3(_GLFWmonitor* monitor, int* xpos, int* ypos)
{
    if (xpos)
        *xpos = 0;
    if (ypos)
        *ypos = 0;
}

void _glfwGetMonitorContentScaleOS3(_GLFWmonitor* monitor,
                                    float* xscale, float* yscale)
{
    if (xscale)
        *xscale = 1.f;
    if (yscale)
        *yscale = 1.f;
}

// The Workbench below its title bar
//
void _glfwGetMonitorWorkareaOS3(_GLFWmonitor* monitor,
                                int* xpos, int* ypos,
                                int* width, int* height)
{
    int x = 0, y = 0, w = 640, h = 512;
    struct Screen* screen = LockPubScreen(NULL);

    if (screen)
    {
        y = screen->BarHeight + 1;
        w = screen->Width;
        h = screen->Height - y;
        UnlockPubScreen(NULL, screen);
    }

    if (xpos)
        *xpos = x;
    if (ypos)
        *ypos = y;
    if (width)
        *width = w;
    if (height)
        *height = h;
}

// Every mode the display database has with 15 bits or more, and the
// Workbench's own
//
GLFWvidmode* _glfwGetVideoModesOS3(_GLFWmonitor* monitor, int* found)
{
    GLFWvidmode* modes;
    GLFWvidmode current;
    int count = 0, size = 16, i;
    ULONG id = INVALID_ID;

    modes = _glfw_calloc(size, sizeof(GLFWvidmode));
    if (!modes)
    {
        *found = 0;
        return NULL;
    }

    if (_glfwGetVideoModeOS3(monitor, &current))
        modes[count++] = current;

    while ((id = NextDisplayInfo(id)) != INVALID_ID)
    {
        struct DimensionInfo di;
        GLFWvidmode mode;

        if (ModeNotAvailable(id))
            continue;
        if (!GetDisplayInfoData(NULL, (UBYTE*) &di, sizeof(di), DTAG_DIMS, id))
            continue;
        if (di.MaxDepth < 15)
            continue;

        mode.width = di.Nominal.MaxX - di.Nominal.MinX + 1;
        mode.height = di.Nominal.MaxY - di.Nominal.MinY + 1;
        _glfwSplitBPP(di.MaxDepth > 24 ? 24 : di.MaxDepth,
                      &mode.redBits, &mode.greenBits, &mode.blueBits);
        mode.refreshRate = modeRefreshRate(id);

        for (i = 0;  i < count;  i++)
        {
            if (_glfwCompareVideoModes(modes + i, &mode) == 0)
                break;
        }
        if (i < count)
            continue;

        if (count == size)
        {
            GLFWvidmode* more = _glfw_realloc(modes, size * 2 * sizeof(GLFWvidmode));
            if (!more)
                break;
            modes = more;
            size *= 2;
        }

        modes[count++] = mode;
    }

    *found = count;
    return modes;
}

// The mode of the full screen window's own screen, or the Workbench's
//
GLFWbool _glfwGetVideoModeOS3(_GLFWmonitor* monitor, GLFWvidmode* mode)
{
    struct Screen* screen;

    if (monitor->window && monitor->window->os3.screen)
    {
        screen = monitor->window->os3.screen;
        modeOfScreen(screen, GetVPModeID(&screen->ViewPort),
                     GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH), mode);
        return GLFW_TRUE;
    }

    screen = LockPubScreen(NULL);
    if (!screen)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR, "AmigaOS: No Workbench screen");
        return GLFW_FALSE;
    }

    modeOfScreen(screen, monitor->os3.modeID, monitor->os3.depth, mode);
    UnlockPubScreen(NULL, screen);
    return GLFW_TRUE;
}

GLFWbool _glfwGetGammaRampOS3(_GLFWmonitor* monitor, GLFWgammaramp* ramp)
{
    _glfwInputError(GLFW_FEATURE_UNAVAILABLE,
                    "AmigaOS: Gamma ramps are not available");
    return GLFW_FALSE;
}

void _glfwSetGammaRampOS3(_GLFWmonitor* monitor, const GLFWgammaramp* ramp)
{
    _glfwInputError(GLFW_FEATURE_UNAVAILABLE,
                    "AmigaOS: Gamma ramps are not available");
}

#endif // _GLFW_AMIGAOS

