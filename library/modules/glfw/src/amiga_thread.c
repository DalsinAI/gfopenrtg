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

#if defined(GLFW_BUILD_OS3_THREAD)

#include "amiga_os.h"

#include <assert.h>
#include <string.h>


//////////////////////////////////////////////////////////////////////////
//////                       GLFW platform API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwPlatformCreateTls(_GLFWtls* tls)
{
    assert(tls->os3.allocated == GLFW_FALSE);

    tls->os3.value = NULL;
    tls->os3.allocated = GLFW_TRUE;
    return GLFW_TRUE;
}

void _glfwPlatformDestroyTls(_GLFWtls* tls)
{
    memset(tls, 0, sizeof(_GLFWtls));
}

void* _glfwPlatformGetTls(_GLFWtls* tls)
{
    assert(tls->os3.allocated == GLFW_TRUE);
    return tls->os3.value;
}

void _glfwPlatformSetTls(_GLFWtls* tls, void* value)
{
    assert(tls->os3.allocated == GLFW_TRUE);
    tls->os3.value = value;
}

GLFWbool _glfwPlatformCreateMutex(_GLFWmutex* mutex)
{
    assert(mutex->os3.allocated == GLFW_FALSE);

    mutex->os3.handle = AllocVec(sizeof(struct SignalSemaphore),
                                 MEMF_PUBLIC | MEMF_CLEAR);
    if (!mutex->os3.handle)
    {
        _glfwInputError(GLFW_PLATFORM_ERROR, "AmigaOS: Failed to create mutex");
        return GLFW_FALSE;
    }

    InitSemaphore(mutex->os3.handle);
    return mutex->os3.allocated = GLFW_TRUE;
}

void _glfwPlatformDestroyMutex(_GLFWmutex* mutex)
{
    if (mutex->os3.allocated)
        FreeVec(mutex->os3.handle);
    memset(mutex, 0, sizeof(_GLFWmutex));
}

void _glfwPlatformLockMutex(_GLFWmutex* mutex)
{
    assert(mutex->os3.allocated == GLFW_TRUE);
    ObtainSemaphore(mutex->os3.handle);
}

void _glfwPlatformUnlockMutex(_GLFWmutex* mutex)
{
    assert(mutex->os3.allocated == GLFW_TRUE);
    ReleaseSemaphore(mutex->os3.handle);
}

#endif // GLFW_BUILD_OS3_THREAD

