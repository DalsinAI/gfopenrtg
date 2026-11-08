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
// OpenGL and OpenGL ES contexts through GFOpenGPU's GLA calls (libGL.a,
// GL.module): one GLA display for the program, virgl on the PC's graphics
// chip when gfopengpu.library carries it (AmiCradle) and softpipe on the
// 68k otherwise, as GL.module's own policy chooses (ENV:GFOpenGPU/GLDriver).
// Each window has a context and a buffer whose frames gla_swap writes into
// the window's RastPort, inside its borders.

#include "internal.h"

#if defined(_GLFW_AMIGAOS)

#include <gla/os3/gla_virgl_os3.h>

#include <string.h>

static void makeContextCurrentGLA(_GLFWwindow* window)
{
    if (window)
    {
        if (!gla_make_current(window->context.gla.handle,
                              window->context.gla.buffer))
        {
            _glfwInputError(GLFW_PLATFORM_ERROR,
                            "GLA: Failed to make context current");
            return;
        }
    }
    else
        gla_make_current(NULL, NULL);

    _glfwPlatformSetTls(&_glfw.contextSlot, window);
}

static void swapBuffersGLA(_GLFWwindow* window)
{
    gla_swap(window->context.gla.handle, window->context.gla.buffer);
}

static void swapIntervalGLA(int interval)
{
    // GLA has no swap interval: gla_swap shows the frame when it is done
}

static int extensionSupportedGLA(const char* extension)
{
    // GLA has no window-system extensions; GL's own are GLFW's to find
    return GLFW_FALSE;
}

static GLFWglproc getProcAddressGLA(const char* procname)
{
    return (GLFWglproc) gla_get_proc_address(procname);
}

static void destroyContextGLA(_GLFWwindow* window)
{
    if (_glfwPlatformGetTls(&_glfw.contextSlot) == window)
    {
        gla_make_current(NULL, NULL);
        _glfwPlatformSetTls(&_glfw.contextSlot, NULL);
    }

    if (window->context.gla.buffer)
    {
        gla_buffer_destroy(window->context.gla.buffer);
        window->context.gla.buffer = NULL;
    }

    if (window->context.gla.handle)
    {
        gla_context_destroy(window->context.gla.handle);
        window->context.gla.handle = NULL;
    }
}

static GLFWbool initDisplay(void)
{
    struct gla_virgl_transport transport;

    if (_glfw.gla.display)
        return GLFW_TRUE;

    if (gla_os3_virgl_transport(&transport))
        _glfw.gla.display = gla_display_create_virgl(&transport);
    if (!_glfw.gla.display)
        _glfw.gla.display = gla_display_create();

    if (!_glfw.gla.display)
    {
        _glfwInputError(GLFW_API_UNAVAILABLE,
                        "GLA: Failed to start GL (is LIBS:GFOpenGPU/GL.module there?)");
        return GLFW_FALSE;
    }

    return GLFW_TRUE;
}

static int bitsOrDefault(int bits, int fallback)
{
    return bits == GLFW_DONT_CARE ? fallback : bits;
}


//////////////////////////////////////////////////////////////////////////
//////                       GLFW internal API                      //////
//////////////////////////////////////////////////////////////////////////

GLFWbool _glfwCreateContextGLA(_GLFWwindow* window,
                               const _GLFWctxconfig* ctxconfig,
                               const _GLFWfbconfig* fbconfig)
{
    struct gla_config* config = &window->context.gla.config;
    struct gla_context* share = NULL;
    const int depth = bitsOrDefault(fbconfig->depthBits, 24);
    const int stencil = bitsOrDefault(fbconfig->stencilBits, 8);
    int width, height;

    if (!initDisplay())
        return GLFW_FALSE;

    memset(config, 0, sizeof(*config));

    if (ctxconfig->client == GLFW_OPENGL_ES_API)
    {
        if (ctxconfig->major < 2)
        {
            _glfwInputError(GLFW_VERSION_UNAVAILABLE,
                            "GLA: OpenGL ES 1 is not available (ES 2.0 to %i.%i are)",
                            gla_display_version(_glfw.gla.display, GLA_ES2) / 10,
                            gla_display_version(_glfw.gla.display, GLA_ES2) % 10);
            return GLFW_FALSE;
        }

        config->profile = GLA_ES2;
        config->major = ctxconfig->major;
        config->minor = ctxconfig->minor;
    }
    else
    {
        config->profile =
            ctxconfig->profile == GLFW_OPENGL_CORE_PROFILE ? GLA_CORE : GLA_COMPAT;

        // 1.0 is GLFW's "any version": the highest the profile has
        if (ctxconfig->major > 1 || ctxconfig->minor > 0)
        {
            config->major = ctxconfig->major;
            config->minor = ctxconfig->minor;
        }
    }

    config->doublebuf = fbconfig->doublebuffer;
    config->depth = depth <= 0 ? 0 : depth <= 16 ? 16 : 24;
    config->stencil = stencil > 0 ? 8 : 0;
    config->alpha = bitsOrDefault(fbconfig->alphaBits, 8) > 0;

    if (ctxconfig->share)
        share = ctxconfig->share->context.gla.handle;

    window->context.gla.handle =
        gla_context_create(_glfw.gla.display, config, share);

    // GL 3.2 or later with no profile asked for: a core context when the
    // compatibility profile doesn't go that high (as on other systems)
    if (!window->context.gla.handle &&
        ctxconfig->client == GLFW_OPENGL_API &&
        ctxconfig->profile == GLFW_OPENGL_ANY_PROFILE &&
        (ctxconfig->major > 3 || (ctxconfig->major == 3 && ctxconfig->minor >= 2)))
    {
        config->profile = GLA_CORE;
        window->context.gla.handle =
            gla_context_create(_glfw.gla.display, config, share);
    }

    if (!window->context.gla.handle)
    {
        const int version = gla_display_version(_glfw.gla.display, config->profile);
        _glfwInputError(GLFW_VERSION_UNAVAILABLE,
                        "GLA: Failed to create an OpenGL%s %i.%i context (%s gives %i.%i)",
                        config->profile == GLA_ES2 ? " ES" : "",
                        ctxconfig->major, ctxconfig->minor,
                        gla_display_driver(_glfw.gla.display),
                        version / 10, version % 10);
        return GLFW_FALSE;
    }

    _glfw.platform.getFramebufferSize(window, &width, &height);
    width = width > 0 ? width : 1;
    height = height > 0 ? height : 1;

    window->context.gla.buffer =
        gla_buffer_create(_glfw.gla.display, config, width, height,
                          &window->os3.present);
    if (!window->context.gla.buffer)
    {
        gla_context_destroy(window->context.gla.handle);
        window->context.gla.handle = NULL;
        _glfwInputError(GLFW_OUT_OF_MEMORY,
                        "GLA: Failed to create a %ix%i buffer", width, height);
        return GLFW_FALSE;
    }

    window->context.gla.width = width;
    window->context.gla.height = height;

    window->context.makeCurrent = makeContextCurrentGLA;
    window->context.swapBuffers = swapBuffersGLA;
    window->context.swapInterval = swapIntervalGLA;
    window->context.extensionSupported = extensionSupportedGLA;
    window->context.getProcAddress = getProcAddressGLA;
    window->context.destroy = destroyContextGLA;

    return GLFW_TRUE;
}

// The window's content area changed size: so does its GL buffer
//
void _glfwResizeContextGLA(_GLFWwindow* window)
{
    int width, height;

    if (!window->context.gla.buffer)
        return;

    _glfw.platform.getFramebufferSize(window, &width, &height);
    if (width < 1)
        width = 1;
    if (height < 1)
        height = 1;

    if ((unsigned) width == window->context.gla.width &&
        (unsigned) height == window->context.gla.height)
    {
        return;
    }

    gla_buffer_resize(window->context.gla.buffer, width, height);
    window->context.gla.width = width;
    window->context.gla.height = height;
}

// What presenting and virgl's transport opened, GL.module closes itself
// when the program ends (libGL.a's destructor)
//
void _glfwTerminateGLA(void)
{
    if (_glfw.gla.display)
    {
        gla_display_destroy(_glfw.gla.display);
        _glfw.gla.display = NULL;
    }
}


//////////////////////////////////////////////////////////////////////////
//////                        GLFW native API                       //////
//////////////////////////////////////////////////////////////////////////

GLFWAPI struct gla_context* glfwGetGLAContext(GLFWwindow* handle)
{
    _GLFWwindow* window = (_GLFWwindow*) handle;
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (_glfw.platform.platformID != GLFW_PLATFORM_AMIGAOS ||
        window->context.source == GLFW_OSMESA_CONTEXT_API)
    {
        _glfwInputError(GLFW_PLATFORM_UNAVAILABLE,
                        "GLA: Platform not initialized");
        return NULL;
    }

    return window->context.gla.handle;
}

GLFWAPI const char* glfwGetGLADriver(void)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(NULL);

    if (!_glfw.gla.display)
        return NULL;

    return gla_display_driver(_glfw.gla.display);
}

#endif // _GLFW_AMIGAOS

