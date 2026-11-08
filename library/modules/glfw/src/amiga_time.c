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

#if defined(GLFW_BUILD_OS3_TIMER)

#include "amiga_os.h"


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

// Closes timer.device (glfwTerminate)
//
void _glfwTerminateTimerOS3(void)
{
    if (_glfw.timer.os3.base)
    {
        CloseDevice((struct IORequest*) _glfw.timer.os3.request);
        _glfw.timer.os3.base = NULL;
    }
    _glfw_free(_glfw.timer.os3.request);
    _glfw.timer.os3.request = NULL;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

// The E-clock: 709379 Hz on a PAL machine, 715909 Hz on NTSC
//
void _glfwPlatformInitTimer(void)
{
    struct EClockVal ev;

    if (!_glfw.timer.os3.base)
    {
        _glfw.timer.os3.request = _glfw_calloc(1, sizeof(struct timerequest));
        if (!_glfw.timer.os3.request)
            return;

        // Only the base is used, for ReadEClock: the request is never sent
        if (OpenDevice(TIMERNAME, UNIT_ECLOCK,
                       (struct IORequest*) _glfw.timer.os3.request, 0) != 0)
        {
            _glfw_free(_glfw.timer.os3.request);
            _glfw.timer.os3.request = NULL;
            return;
        }

        _glfw.timer.os3.base =
            (struct Library*) _glfw.timer.os3.request->tr_node.io_Device;
    }

    _glfw.timer.os3.frequency = ReadEClock(&ev);
}

uint64_t _glfwPlatformGetTimerValue(void)
{
    struct EClockVal ev;

    if (!_glfw.timer.os3.base)
        return 0;

    ReadEClock(&ev);
    return ((uint64_t) ev.ev_hi << 32) | ev.ev_lo;
}

uint64_t _glfwPlatformGetTimerFrequency(void)
{
    // Never 0: glfwGetTime divides by it
    return _glfw.timer.os3.frequency ? _glfw.timer.os3.frequency : 1;
}

#endif // GLFW_BUILD_OS3_TIMER

