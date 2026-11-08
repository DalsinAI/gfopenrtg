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

#define GLFW_OS3_LIBRARY_TIMER_STATE _GLFWtimerOS3 os3;

#include <stdint.h>

struct Library;

// AmigaOS-specific global timer data: timer.device's base, for ReadEClock
//
typedef struct _GLFWtimerOS3
{
    struct Library*     base;
    struct timerequest* request;
    uint64_t            frequency;
} _GLFWtimerOS3;

void _glfwTerminateTimerOS3(void);

