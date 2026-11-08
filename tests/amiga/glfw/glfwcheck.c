/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLFWCheck: GLFW's AmigaOS platform, checked from inside a program, so a
 * run says what happened without a picture of the screen. The monitor and
 * its mode; then, for each kind of context a GLFW program asks for (GL
 * compatibility, GLES 2.0, GLES 2.0 through the EGL API, GLES 3.1, GL 3.3
 * and 4.1 core), a window: the context's GL_VERSION and GL_RENDERER and
 * GLA's driver, a clear read back at the window's middle, frames a second
 * through glfwSwapBuffers, and the framebuffer before and after
 * glfwSetWindowSize. Last, a window with no context, and how long
 * glfwTerminate takes.
 *   GLFWCheck [SECONDS n]   (n: how long each window draws, default 1)
 * Returns 0 when every context but 4.1 core was made and read back right
 * (4.1 is virgl's only), 5 otherwise, 20 when glfwInit failed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_GLA
#define GLFW_NATIVE_INCLUDE_NONE
#include <GLFW/glfw3native.h>

static void on_error(int code, const char *text)
{
    printf("  GLFW error 0x%05x: %s\n", code, text);
    fflush(stdout);
}

struct kind {
    const char *name;
    int api, source, major, minor, profile;
    int required;                           /* counts against the result */
};

static const struct kind kinds[] = {
    { "GL compat",      GLFW_OPENGL_API,    GLFW_NATIVE_CONTEXT_API, 1, 0, GLFW_OPENGL_ANY_PROFILE,  1 },
    { "GLES 2.0",       GLFW_OPENGL_ES_API, GLFW_NATIVE_CONTEXT_API, 2, 0, GLFW_OPENGL_ANY_PROFILE,  1 },
    { "GLES 2.0 (EGL)", GLFW_OPENGL_ES_API, GLFW_EGL_CONTEXT_API,    2, 0, GLFW_OPENGL_ANY_PROFILE,  1 },
    { "GLES 3.1",       GLFW_OPENGL_ES_API, GLFW_NATIVE_CONTEXT_API, 3, 1, GLFW_OPENGL_ANY_PROFILE,  1 },
    { "GL 3.3 core",    GLFW_OPENGL_API,    GLFW_NATIVE_CONTEXT_API, 3, 3, GLFW_OPENGL_CORE_PROFILE, 1 },
    { "GL 4.1 core",    GLFW_OPENGL_API,    GLFW_NATIVE_CONTEXT_API, 4, 1, GLFW_OPENGL_CORE_PROFILE, 0 },
};

/* One clear colour a frame, read back at the middle before the swap */
static int draw(GLFWwindow *w, double seconds, int *frames)
{
    static const unsigned char want[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };
    double start = glfwGetTime();
    int width, height, bad = 0;
    *frames = 0;
    do {
        const unsigned char *c = want[*frames % 3];
        unsigned char got[4] = { 0, 0, 0, 0 };
        glfwGetFramebufferSize(w, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glReadPixels(width / 2, height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, got);
        if (got[0] != c[0] || got[1] != c[1] || got[2] != c[2]) {
            if (!bad)
                printf("  read back %u,%u,%u where %u,%u,%u was cleared\n",
                       got[0], got[1], got[2], c[0], c[1], c[2]);
            bad++;
        }
        glfwSwapBuffers(w);
        glfwPollEvents();
        (*frames)++;
    } while (glfwGetTime() - start < seconds && !glfwWindowShouldClose(w));
    return bad;
}

static int check(const struct kind *k, double seconds)
{
    GLFWwindow *w;
    int frames, bad, width, height, x, y;
    double start;

    printf("%s:\n", k->name);
    fflush(stdout);
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, k->api);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, k->source);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, k->major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, k->minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, k->profile);
    if (k->profile == GLFW_OPENGL_CORE_PROFILE)
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_DEPTH_BITS, 24);

    start = glfwGetTime();
    w = glfwCreateWindow(240, 160, k->name, NULL, NULL);
    if (!w) {
        printf("  no window (%.2f s)\n", glfwGetTime() - start);
        return 1;
    }
    glfwMakeContextCurrent(w);
    printf("  made in %.2f s; driver %s\n  GL_VERSION %s\n  GL_RENDERER %s\n",
           glfwGetTime() - start, glfwGetGLADriver() ? glfwGetGLADriver() : "(none)",
           (const char *) glGetString(GL_VERSION), (const char *) glGetString(GL_RENDERER));
    glfwGetWindowPos(w, &x, &y);
    glfwGetFramebufferSize(w, &width, &height);
    printf("  window at %d,%d, framebuffer %dx%d\n", x, y, width, height);
    fflush(stdout);

    bad = draw(w, seconds, &frames);
    printf("  %d frames in %.1f s, %d read back wrong\n", frames, seconds, bad);

    glfwSetWindowSize(w, 320, 200);
    glfwPollEvents();
    glfwGetFramebufferSize(w, &width, &height);
    bad += draw(w, seconds / 2, &frames);
    printf("  after glfwSetWindowSize(320, 200): framebuffer %dx%d, %d frames\n",
           width, height, frames);
    if (width != 320 || height != 200)
        bad++;
    fflush(stdout);

    glfwDestroyWindow(w);
    return bad != 0;
}

int main(int argc, char **argv)
{
    double seconds = 1.0;
    int i, failed = 0;
    GLFWmonitor *m;
    GLFWwindow *w;

    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atof(argv[++i]);

    glfwSetErrorCallback(on_error);
    if (!glfwInit()) {
        printf("glfwInit failed\n");
        return 20;
    }
    printf("%s\nplatform 0x%05x, glfwInit %.2f s\n", glfwGetVersionString(),
           glfwGetPlatform(), glfwGetTime());
    m = glfwGetPrimaryMonitor();
    if (m) {
        const GLFWvidmode *mode = glfwGetVideoMode(m);
        int count;
        glfwGetVideoModes(m, &count);
        printf("monitor %s: %dx%d, %d+%d+%d bits, %d modes\n", glfwGetMonitorName(m),
               mode->width, mode->height, mode->redBits, mode->greenBits, mode->blueBits,
               count);
    } else
        printf("no monitor\n");
    fflush(stdout);

    for (i = 0; i < (int) (sizeof kinds / sizeof kinds[0]); i++)
        if (check(&kinds[i], seconds) && kinds[i].required)
            failed++;

    printf("no context:\n");
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    w = glfwCreateWindow(160, 100, "no context", NULL, NULL);
    printf("  %s\n", w ? "window made" : "no window");
    if (w) {
        glfwPollEvents();
        glfwDestroyWindow(w);
    } else
        failed++;

    printf("glfwTerminate...\n");
    fflush(stdout);
    glfwTerminate();
    printf("done, %d failed\n", failed);
    return failed ? 5 : 0;
}
