/* Copyright (c) 2026 AmiCradle Team. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * EGLTriangle: OpenGL ES 2 through EGL (libEGL.a, on GLA), in an Intuition
 * window: the start every EGL sample makes, with the window as
 * EGLNativeWindowType. A spinning triangle; Esc or the close gadget quits.
 *   EGLTriangle [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 egltriangle.c -o EGLTriangle $(pkg-config --cflags --libs egl glesv2)
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

static const char vs[] =
    "attribute vec2 pos; attribute vec3 col; uniform float angle; varying vec3 c;\n"
    "void main() { float s = sin(angle), k = cos(angle);\n"
    "  gl_Position = vec4(k * pos.x - s * pos.y, s * pos.x + k * pos.y, 0.0, 1.0); c = col; }\n";
static const char fs[] =
    "precision mediump float; varying vec3 c;\n"
    "void main() { gl_FragColor = vec4(c, 1.0); }\n";

static double now(void)
{
    struct DateStamp ds;
    DateStamp(&ds);
    return ds.ds_Days * 86400.0 + ds.ds_Minute * 60.0 + ds.ds_Tick / (double)TICKS_PER_SECOND;
}

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

int main(int argc, char **argv)
{
    static const GLfloat tri[] = { 0, 0.8f, 1, 0, 0, -0.7f, -0.6f, 0, 1, 0, 0.7f, -0.6f, 0, 0, 1 };
    static const EGLint want[] = { EGL_RED_SIZE, 5, EGL_GREEN_SIZE, 6, EGL_BLUE_SIZE, 5,
                                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    static const EGLint es2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    const int w = 320, h = 240;
    int i, seconds = 0, quit = 0, frames = 0;
    EGLDisplay dpy = EGL_NO_DISPLAY;
    EGLSurface surf = EGL_NO_SURFACE;
    EGLContext ctx = EGL_NO_CONTEXT;
    EGLConfig cfg;
    EGLint n = 0, major, minor;
    GLuint prog;
    GLint angle;
    struct Window *win;
    double start;

    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);

    win = OpenWindowTags(NULL, WA_Title, (ULONG) "EGLTriangle", WA_InnerWidth, w, WA_InnerHeight, h,
                         WA_DragBar, TRUE, WA_DepthGadget, TRUE, WA_CloseGadget, TRUE, WA_Activate, TRUE,
                         WA_SimpleRefresh, TRUE, WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_VANILLAKEY | IDCMP_REFRESHWINDOW,
                         TAG_END);
    if (!win) {
        printf("EGLTriangle: no window\n");
        return RETURN_FAIL;
    }
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!eglInitialize(dpy, &major, &minor) || !eglChooseConfig(dpy, want, &cfg, 1, &n) || n < 1 ||
        (surf = eglCreateWindowSurface(dpy, cfg, win, NULL)) == EGL_NO_SURFACE ||
        (ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, es2)) == EGL_NO_CONTEXT ||
        !eglMakeCurrent(dpy, surf, surf, ctx)) {
        printf("EGLTriangle: GL couldn't start (EGL error 0x%04x)\n", (unsigned)eglGetError());
        goto out;
    }
    printf("EGLTriangle: EGL %s, %s, %s\n", eglQueryString(dpy, EGL_VERSION),
           (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));

    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glBindAttribLocation(prog, 0, "pos");
    glBindAttribLocation(prog, 1, "col");
    glLinkProgram(prog);
    glUseProgram(prog);
    angle = glGetUniformLocation(prog, "angle");
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glViewport(0, 0, w, h);
    glClearColor(0.1f, 0.1f, 0.2f, 1);

    start = now();
    while (!quit) {
        struct IntuiMessage *m;
        double t = now() - start;
        glClear(GL_COLOR_BUFFER_BIT);
        glUniform1f(angle, (float)(t * 1.5708));
        glDrawArrays(GL_TRIANGLES, 0, 3);
        eglSwapBuffers(dpy, surf);
        frames++;
        while ((m = (struct IntuiMessage *)GetMsg(win->UserPort))) {
            if (m->Class == IDCMP_CLOSEWINDOW || (m->Class == IDCMP_VANILLAKEY && m->Code == 27))
                quit = 1;
            else if (m->Class == IDCMP_REFRESHWINDOW) {
                BeginRefresh(win);
                EndRefresh(win, TRUE);
            }
            ReplyMsg((struct Message *)m);
        }
        if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            quit = 1;
        if (seconds && t >= seconds)
            quit = 1;
    }
    printf("EGLTriangle: %d frames in %.1f s, %.1f fps\n", frames, now() - start, frames / (now() - start + 0.001));
out:
    /* EGL lets go of the window before it closes. */
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    CloseWindow(win);
    return RETURN_OK;
}
