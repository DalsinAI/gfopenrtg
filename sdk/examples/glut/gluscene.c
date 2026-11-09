/* Copyright (c) 2026 AmiCradle Team. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * GLUScene: the kit's GLU (SGI's libGLU) in a GLUT window. gluPerspective
 * and gluLookAt place the camera; a quadric sphere, cylinder and disk carry
 * a checked texture that gluBuild2DMipmaps made; the tessellator fills a
 * star with a hole in it; a NURBS surface rises behind them; gluProject
 * finds where the sphere is on the window, for its label. Space stops the
 * camera; Esc or the close gadget quits.
 *   GLUScene [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 gluscene.c -o GLUScene $(pkg-config --cflags --libs glut)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include <GL/glut.h>
#include <GL/glu.h>

#ifndef CALLBACK
#define CALLBACK
#endif

static GLUquadric *quad;
static GLUnurbs *nurbs;
static GLuint tex, star;
static int width = 320, height = 240, paused;
static float cam;

static void make_texture(void)
{
    static GLubyte pix[64 * 64 * 3];
    int x, y;
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) {
            GLubyte c = ((x / 8) ^ (y / 8)) & 1 ? 230 : 40;
            pix[(y * 64 + x) * 3 + 0] = c;
            pix[(y * 64 + x) * 3 + 1] = c / 2 + 60;
            pix[(y * 64 + x) * 3 + 2] = 255 - c;
        }
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, 64, 64, GL_RGB, GL_UNSIGNED_BYTE, pix))
        printf("GLUScene: gluBuild2DMipmaps: %s\n", (const char *)gluErrorString(glGetError()));
}

static void CALLBACK tess_error(GLenum e)
{
    printf("GLUScene: tessellator: %s\n", (const char *)gluErrorString(e));
}

static void CALLBACK tess_combine(GLdouble c[3], void *d[4], GLfloat w[4], void **out)
{
    static GLdouble extra[64][3];
    static int n;
    GLdouble *v = extra[n++ & 63];
    (void)d; (void)w;
    v[0] = c[0];
    v[1] = c[1];
    v[2] = c[2];
    *out = v;
}

/* A five-pointed star with a square hole, into a display list. */
static void make_star(void)
{
    static GLdouble outer[10][3], hole[4][3] = { { -0.2, -0.2, 0 }, { 0.2, -0.2, 0 }, { 0.2, 0.2, 0 }, { -0.2, 0.2, 0 } };
    GLUtesselator *t = gluNewTess();
    int i;
    for (i = 0; i < 10; i++) {
        double a = M_PI / 2 + i * M_PI / 5, r = i & 1 ? 0.4 : 1.0;
        outer[i][0] = r * cos(a);
        outer[i][1] = r * sin(a);
        outer[i][2] = 0;
    }
    gluTessCallback(t, GLU_TESS_BEGIN, (_GLUfuncptr)glBegin);
    gluTessCallback(t, GLU_TESS_VERTEX, (_GLUfuncptr)glVertex3dv);
    gluTessCallback(t, GLU_TESS_END, (_GLUfuncptr)glEnd);
    gluTessCallback(t, GLU_TESS_ERROR, (_GLUfuncptr)tess_error);
    gluTessCallback(t, GLU_TESS_COMBINE, (_GLUfuncptr)tess_combine);
    gluTessProperty(t, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
    star = glGenLists(1);
    glNewList(star, GL_COMPILE);
    glNormal3f(0, 0, 1);
    gluTessBeginPolygon(t, NULL);
    gluTessBeginContour(t);
    for (i = 0; i < 10; i++)
        gluTessVertex(t, outer[i], outer[i]);
    gluTessEndContour(t);
    gluTessBeginContour(t);
    for (i = 3; i >= 0; i--)
        gluTessVertex(t, hole[i], hole[i]);
    gluTessEndContour(t);
    gluTessEndPolygon(t);
    glEndList();
    gluDeleteTess(t);
}

static void draw_nurbs(void)
{
    static GLfloat knots[8] = { 0, 0, 0, 0, 1, 1, 1, 1 };
    GLfloat ctl[4][4][3];
    int u, v;
    for (u = 0; u < 4; u++)
        for (v = 0; v < 4; v++) {
            ctl[u][v][0] = 2.0f * (u - 1.5f);
            ctl[u][v][1] = 2.0f * (v - 1.5f);
            ctl[u][v][2] = ((u == 1 || u == 2) && (v == 1 || v == 2)) ? 2.5f : -1.0f;
        }
    gluBeginSurface(nurbs);
    gluNurbsSurface(nurbs, 8, knots, 8, knots, 4 * 3, 3, &ctl[0][0][0], 4, 4, GL_MAP2_VERTEX_3);
    gluEndSurface(nurbs);
}

static void label(const char *s, double x, double y, double z)
{
    GLdouble mv[16], pr[16], wx, wy, wz;
    GLint vp[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, mv);
    glGetDoublev(GL_PROJECTION_MATRIX, pr);
    glGetIntegerv(GL_VIEWPORT, vp);
    if (gluProject(x, y, z, mv, pr, vp, &wx, &wy, &wz) != GL_TRUE)
        return;
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, width, 0, height);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3f(1, 1, 1);
    glRasterPos2d(wx, wy);
    while (*s)
        glutBitmapCharacter(GLUT_BITMAP_8_BY_13, *s++);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void display(void)
{
    static const GLfloat red[] = { 0.9f, 0.3f, 0.2f, 1 }, green[] = { 0.3f, 0.8f, 0.3f, 1 }, white[] = { 1, 1, 1, 1 };
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    gluLookAt(7 * sin(cam), 3, 7 * cos(cam), 0, 0, 0, 0, 1, 0);

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, white);
    glEnable(GL_TEXTURE_2D);
    glPushMatrix();
    glTranslatef(-1.8f, 0, 0);
    gluSphere(quad, 0.9, 24, 16);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(1.8f, -0.9f, 0);
    glRotatef(-90, 1, 0, 0);
    gluCylinder(quad, 0.6, 0.3, 1.8, 20, 4);
    glTranslatef(0, 0, 1.8f);
    gluDisk(quad, 0, 0.3, 20, 2);
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, red);
    glPushMatrix();
    glTranslatef(0, 1.6f, 0);
    glCallList(star);
    glPopMatrix();

    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, green);
    glPushMatrix();
    glTranslatef(0, -1.2f, -3);
    glRotatef(-70, 1, 0, 0);
    glScalef(0.6f, 0.6f, 0.6f);
    draw_nurbs();
    glPopMatrix();

    label("gluSphere", -1.8, 1.0, 0);
    glutSwapBuffers();
}

static void reshape(int w, int h)
{
    width = w;
    height = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45, (double)w / h, 1, 50);
    glMatrixMode(GL_MODELVIEW);
}

static void idle(void)
{
    if (!paused)
        cam = glutGet(GLUT_ELAPSED_TIME) * 0.0005f;
    glutPostRedisplay();
}

static void keyboard(unsigned char k, int x, int y)
{
    if (k == 27)
        exit(0);
    if (k == ' ')
        paused = !paused;
}

static void stop(int value) { exit(0); }

int main(int argc, char **argv)
{
    static const GLfloat light[] = { 3, 5, 4, 0 };
    int i;
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(width, height);
    glutCreateWindow("GLUScene");
    printf("GLUScene: GLU %s, %s\n", (const char *)gluGetString(GLU_VERSION), (const char *)glGetString(GL_RENDERER));
    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            glutTimerFunc(atoi(argv[++i]) * 1000, stop, 0);
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyboard);

    glClearColor(0.15f, 0.15f, 0.2f, 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glEnable(GL_AUTO_NORMAL);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    make_texture();
    make_star();
    quad = gluNewQuadric();
    gluQuadricNormals(quad, GLU_SMOOTH);
    gluQuadricTexture(quad, GL_TRUE);
    nurbs = gluNewNurbsRenderer();
    gluNurbsProperty(nurbs, GLU_SAMPLING_TOLERANCE, 25.0f);
    gluNurbsProperty(nurbs, GLU_DISPLAY_MODE, GLU_FILL);
    glutMainLoop();
    return 0;
}
