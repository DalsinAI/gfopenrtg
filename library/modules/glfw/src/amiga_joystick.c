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
// Pads through lowlevel.library: a CD32 pad (or a two-button joystick) in
// the joystick port. The mouse port isn't read: a CD32 pad's probe there
// would upset a mouse. The pad is found when GLFW first asks for
// joysticks; there is no hot-plugging.

#include "internal.h"

#if defined(_GLFW_AMIGAOS)

#include "amiga_os.h"

#include <stdio.h>
#include <string.h>

// The GUID's last eight digits are the port
#define _GLFW_OS3_PAD_GUID "06000000416d696761506164%08x"

// A CD32 pad as an SDL game controller: red, blue, green and yellow are
// A, B, X and Y, play is start and the two shoulder buttons are reverse
// and forward
static const char* padMappings =
    "06000000416d69676150616400000001,Amiga pad,a:b0,b:b1,x:b2,y:b3,start:b4,rightshoulder:b5,leftshoulder:b6,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,leftx:a0,lefty:a1,platform:AmigaOS,\n";

static ULONG readPort(int port)
{
    return ReadJoyPort(port);
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwInitJoysticksOS3(void)
{
    static const int ports[] = { 1 };
    int i;

    // No lowlevel.library: no pads, which isn't an error
    _glfw.os3.LowLevelBase = OpenLibrary("lowlevel.library", 40);
    if (!_glfw.os3.LowLevelBase)
        return GLFW_TRUE;

    glfwUpdateGamepadMappings(padMappings);

    for (i = 0;  i < (int) (sizeof(ports) / sizeof(ports[0]));  i++)
    {
        // The first read of a port may only start lowlevel's detection
        ULONG type = readPort(ports[i]) & JP_TYPE_MASK;
        if (type == JP_TYPE_NOTAVAIL || type == JP_TYPE_UNKNOWN)
            type = readPort(ports[i]) & JP_TYPE_MASK;

        if (type == JP_TYPE_GAMECTLR || type == JP_TYPE_JOYSTK)
        {
            char guid[33];
            _GLFWjoystick* js;
            const char* name = type == JP_TYPE_GAMECTLR ? "Amiga CD32 pad" : "Amiga joystick";

            snprintf(guid, sizeof(guid), _GLFW_OS3_PAD_GUID, ports[i]);
            js = _glfwAllocJoystick(name, guid, 2, 7, 1);
            if (!js)
                continue;

            js->os3.port = ports[i];
            js->os3.type = type;
            _glfwInputJoystick(js, GLFW_CONNECTED);
        }
    }

    return GLFW_TRUE;
}

void _glfwTerminateJoysticksOS3(void)
{
    int jid;

    for (jid = 0;  jid <= GLFW_JOYSTICK_LAST;  jid++)
    {
        _GLFWjoystick* js = _glfw.joysticks + jid;
        if (js->allocated)
            _glfwFreeJoystick(js);
    }

    if (_glfw.os3.LowLevelBase)
    {
        CloseLibrary(_glfw.os3.LowLevelBase);
        _glfw.os3.LowLevelBase = NULL;
    }
}

GLFWbool _glfwPollJoystickOS3(_GLFWjoystick* js, int mode)
{
    static const ULONG buttons[] =
    {
        JPF_BUTTON_RED, JPF_BUTTON_BLUE, JPF_BUTTON_GREEN, JPF_BUTTON_YELLOW,
        JPF_BUTTON_PLAY, JPF_BUTTON_FORWARD, JPF_BUTTON_REVERSE
    };
    ULONG state;
    char hat = GLFW_HAT_CENTERED;
    int i;

    if (!_glfw.os3.LowLevelBase)
        return GLFW_FALSE;

    state = readPort(js->os3.port);
    if ((state & JP_TYPE_MASK) == JP_TYPE_NOTAVAIL ||
        (state & JP_TYPE_MASK) == JP_TYPE_MOUSE)
    {
        return GLFW_FALSE;
    }

    if (mode == _GLFW_POLL_PRESENCE)
        return GLFW_TRUE;

    if (mode & _GLFW_POLL_AXES)
    {
        _glfwInputJoystickAxis(js, 0, (state & JPF_JOY_LEFT) ? -1.f :
                                      (state & JPF_JOY_RIGHT) ? 1.f : 0.f);
        _glfwInputJoystickAxis(js, 1, (state & JPF_JOY_UP) ? -1.f :
                                      (state & JPF_JOY_DOWN) ? 1.f : 0.f);
    }

    if (mode & _GLFW_POLL_BUTTONS)
    {
        for (i = 0;  i < 7;  i++)
        {
            _glfwInputJoystickButton(js, i, (state & buttons[i]) ? GLFW_PRESS
                                                                 : GLFW_RELEASE);
        }

        if (state & JPF_JOY_UP)
            hat |= GLFW_HAT_UP;
        if (state & JPF_JOY_RIGHT)
            hat |= GLFW_HAT_RIGHT;
        if (state & JPF_JOY_DOWN)
            hat |= GLFW_HAT_DOWN;
        if (state & JPF_JOY_LEFT)
            hat |= GLFW_HAT_LEFT;

        _glfwInputJoystickHat(js, 0, hat);
    }

    return GLFW_TRUE;
}

const char* _glfwGetMappingNameOS3(void)
{
    return "AmigaOS";
}

void _glfwUpdateGamepadGUIDOS3(char* guid)
{
}

#endif // _GLFW_AMIGAOS

