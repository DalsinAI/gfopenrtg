/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The OpenGL ES 3.0 Programming Guide's samples (opengles3-book, MIT, Dan
 * Ginsburg and Budirijanto Purnomo) on GFOpenGPU: esUtil's window back end
 * for AmigaOS, in place of its X11 one (Common/Source/LinuxX11/esUtil_X11.c).
 * WinCreate opens an Intuition window, esUtil.c gets its context and
 * surface from libEGL.a with the window as EGLNativeWindowType, and WinLoop
 * draws until the close gadget, Esc or Ctrl-C.
 *   Sample [SECONDS n]      SECONDS: stop after n seconds, and say the frame rate */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include "esUtil.h"

static struct Window *window;

GLboolean WinCreate(ESContext *esContext, const char *title)
{
    window = OpenWindowTags(NULL, WA_Title, (ULONG)title, WA_InnerWidth, esContext->width,
                            WA_InnerHeight, esContext->height, WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                            WA_CloseGadget, TRUE, WA_Activate, TRUE, WA_SimpleRefresh, TRUE,
                            WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW, TAG_END);
    if (!window)
        return GL_FALSE;
    esContext->eglNativeDisplay = EGL_DEFAULT_DISPLAY;
    esContext->eglNativeWindow = window;
    return GL_TRUE;
}

/* The window's messages: keys go to the sample's keyFunc; the close
 * gadget and Esc stop it. */
static GLboolean userInterrupt(ESContext *esContext)
{
    struct IntuiMessage *m;
    GLboolean stop = GL_FALSE;
    while ((m = (struct IntuiMessage *)GetMsg(window->UserPort))) {
        ULONG cls = m->Class;
        UWORD code = m->Code;
        if (cls == IDCMP_REFRESHWINDOW) {
            BeginRefresh(window);
            EndRefresh(window, TRUE);
        }
        ReplyMsg((struct Message *)m);
        if (cls == IDCMP_CLOSEWINDOW || (cls == IDCMP_VANILLAKEY && code == 27))
            stop = GL_TRUE;
        else if (cls == IDCMP_VANILLAKEY && esContext->keyFunc)
            esContext->keyFunc(esContext, (unsigned char)code, 0, 0);
    }
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
        stop = GL_TRUE;
    return stop;
}

static double now(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec + t.tv_usec * 1e-6;
}

static void WinLoop(ESContext *esContext, int seconds)
{
    double start = now(), t1 = start, t2;
    unsigned long frames = 0;
    while (!userInterrupt(esContext)) {
        t2 = now();
        if (esContext->updateFunc)
            esContext->updateFunc(esContext, (float)(t2 - t1));
        t1 = t2;
        if (esContext->drawFunc)
            esContext->drawFunc(esContext);
        eglSwapBuffers(esContext->eglDisplay, esContext->eglSurface);
        frames++;
        if (seconds && t2 - start >= seconds)
            break;
    }
    t2 = now() - start;
    printf("%lu frames in %.1f s, %.1f fps\n", frames, t2, frames / (t2 + 0.001));
}

/* esUtil.c's esLoadTGA reads TGA's little-endian header as the CPU's own
 * shorts; build.sh renames it, and this one reads the bytes. */
char *ESUTIL_API esLoadTGA(void *ioContext, const char *fileName, int *width, int *height)
{
    unsigned char h[18];
    char *buffer = NULL;
    size_t bytes;
    FILE *f = fopen(fileName, "rb");
    (void)ioContext;
    if (!f) {
        esLogMessage("esLoadTGA FAILED to load : { %s }\n", fileName);
        return NULL;
    }
    if (fread(h, sizeof h, 1, f) == 1 && (h[16] == 8 || h[16] == 24 || h[16] == 32) &&
        fseek(f, h[0], SEEK_CUR) == 0) {        /* past the image ID */
        *width = h[12] | h[13] << 8;
        *height = h[14] | h[15] << 8;
        bytes = (size_t)*width * *height * (h[16] / 8);
        if ((buffer = (char *)malloc(bytes)) && fread(buffer, bytes, 1, f) != 1) {
            free(buffer);
            buffer = NULL;
        }
    }
    fclose(f);
    return buffer;
}

/* libnix declares random() and srandom() and has neither (Noise3D.c uses
 * them): rand()'s. */
long random(void)
{
    return rand();
}

void srandom(unsigned seed)
{
    srand(seed);
}

extern int esMain(ESContext *esContext);

int main(int argc, char *argv[])
{
    ESContext esContext;
    int i, seconds = 0, rc = RETURN_OK;

    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);
    memset(&esContext, 0, sizeof esContext);
    if (esMain(&esContext) != GL_TRUE || esContext.eglContext == EGL_NO_CONTEXT) {
        printf("GL couldn't start (EGL error 0x%04x)\n", (unsigned)eglGetError());
        rc = RETURN_FAIL;
    } else {
        printf("%s, OpenGL ES %s, EGL %s\n", (const char *)glGetString(GL_RENDERER),
               (const char *)glGetString(GL_VERSION), eglQueryString(esContext.eglDisplay, EGL_VERSION));
        WinLoop(&esContext, seconds);
        if (esContext.shutdownFunc)
            esContext.shutdownFunc(&esContext);
    }
    free(esContext.userData);
    /* EGL lets go of the window before it closes. */
    if (esContext.eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(esContext.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglTerminate(esContext.eglDisplay);
    }
    if (window)
        CloseWindow(window);
    return rc;
}
