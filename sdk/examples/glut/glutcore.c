/* Copyright (c) 2026 AmiCradle Team. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * GLUTCore: GLUT with a core profile context (OpenGL 3.3, as freeglut gives
 * one: glutInitContextVersion and glutInitContextProfile), drawing with a
 * GLSL program only. freeglut's shapes draw in a core context too, through
 * the attributes glutSetVertexAttribCoord3 and glutSetVertexAttribNormal
 * name. A spinning torus, then a teapot (Space); Esc quits.
 *   GLUTCore [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 glutcore.c -o GLUTCore $(pkg-config --cflags --libs glut)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/freeglut.h>

static const char *vs =
    "#version 330 core\n"
    "in vec3 pos;\n"
    "in vec3 nrm;\n"
    "uniform mat4 mvp;\n"
    "uniform mat3 nm;\n"
    "out vec3 n;\n"
    "void main() { n = nm * nrm; gl_Position = mvp * vec4(pos, 1.0); }\n";
static const char *fs =
    "#version 330 core\n"
    "in vec3 n;\n"
    "out vec4 colour;\n"
    "void main() {\n"
    "    float d = max(dot(normalize(n), normalize(vec3(0.4, 0.6, 0.7))), 0.0);\n"
    "    colour = vec4(vec3(0.15, 0.2, 0.3) + d * vec3(0.3, 0.7, 0.9), 1.0);\n"
    "}\n";

static GLuint prog, vao;
static GLint u_mvp, u_nm;
static int width = 320, height = 240, shape;

static GLuint shader(GLenum kind, const char *src)
{
    GLuint s = glCreateShader(kind);
    GLint ok;
    char log[512];
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, sizeof(log), NULL, log);
        printf("GLUTCore: shader: %s\n", log);
        exit(1);
    }
    return s;
}

/* m = a * b, column-major 4x4 */
static void mul(float *m, const float *a, const float *b)
{
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float s = 0;
            for (k = 0; k < 4; k++)
                s += a[k * 4 + j] * b[i * 4 + k];
            m[i * 4 + j] = s;
        }
}

static void display(void)
{
    float t = glutGet(GLUT_ELAPSED_TIME) * 0.001f, c = cosf(t), s = sinf(t), a = (float)width / height;
    float f = 1.0f / tanf(0.4f), zn = 1, zf = 20;
    /* turn about y then x a little, push back 4 */
    float model[16] = { c, s * 0.34f, -s * 0.94f, 0, 0, 0.94f, 0.34f, 0, s, -c * 0.34f, c * 0.94f, 0, 0, 0, -4, 1 };
    float proj[16] = { f / a, 0, 0, 0, 0, f, 0, 0, 0, 0, (zf + zn) / (zn - zf), -1, 0, 0, 2 * zf * zn / (zn - zf), 0 };
    float mvp[16], nm[9] = { model[0], model[1], model[2], model[4], model[5], model[6], model[8], model[9], model[10] };
    mul(mvp, proj, model);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(prog);
    glUniformMatrix4fv(u_mvp, 1, GL_FALSE, mvp);
    glUniformMatrix3fv(u_nm, 1, GL_FALSE, nm);
    glBindVertexArray(vao);
    if (shape)
        glutSolidTeapot(1.0);
    else
        glutSolidTorus(0.35, 0.9, 24, 36);
    glutSwapBuffers();
}

static void reshape(int w, int h)
{
    width = w;
    height = h;
    glViewport(0, 0, w, h);
}

static void keyboard(unsigned char k, int x, int y)
{
    if (k == 27)
        glutLeaveMainLoop();
    if (k == ' ')
        shape = !shape;
}

static void stop(int value) { glutLeaveMainLoop(); }

int main(int argc, char **argv)
{
    int i;
    glutInit(&argc, argv);
    glutInitContextVersion(3, 3);
    glutInitContextProfile(GLUT_CORE_PROFILE);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(width, height);
    glutCreateWindow("GLUTCore");
    printf("GLUTCore: OpenGL %s, GLSL %s\n", (const char *)glGetString(GL_VERSION),
           (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            glutTimerFunc(atoi(argv[++i]) * 1000, stop, 0);

    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    u_mvp = glGetUniformLocation(prog, "mvp");
    u_nm = glGetUniformLocation(prog, "nm");
    glutSetVertexAttribCoord3(glGetAttribLocation(prog, "pos"));
    glutSetVertexAttribNormal(glGetAttribLocation(prog, "nrm"));
    /* a core context draws only with a vertex array object bound */
    glGenVertexArrays(1, &vao);

    glClearColor(0.05f, 0.05f, 0.1f, 1);
    glEnable(GL_DEPTH_TEST);
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutIdleFunc(glutPostRedisplay);
    glutMainLoop();
    return 0;
}
