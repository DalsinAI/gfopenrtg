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

// A GLFW program's GL and its events stay on the task that called glfwInit:
// a slot of "thread-local" storage is one value. Mutexes are Exec's
// signal semaphores, so other tasks may still post empty events.

#define GLFW_OS3_TLS_STATE    _GLFWtlsOS3   os3;
#define GLFW_OS3_MUTEX_STATE  _GLFWmutexOS3 os3;

struct SignalSemaphore;

// AmigaOS-specific thread local storage data
//
typedef struct _GLFWtlsOS3
{
    GLFWbool        allocated;
    void*           value;
} _GLFWtlsOS3;

// AmigaOS-specific mutex data
//
typedef struct _GLFWmutexOS3
{
    GLFWbool        allocated;
    struct SignalSemaphore* handle;
} _GLFWmutexOS3;

