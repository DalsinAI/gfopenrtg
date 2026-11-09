/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * MGLGears' scene: three turning gears, the shape of Brian Paul's gears
 * demo from Mesa's demos (MIT), drawn as that demo draws them: display
 * lists of quad strips and quads, flat-shaded faces and smooth-shaded
 * insides, one directional light, GL_NORMALIZE. For mgltest.c (built with
 * MGLTEST_SCENE naming this file): scene_init, scene_frame (t in seconds),
 * scene_done. */
#include MGLTEST_GL_HEADER

#ifdef MGLTEST_NO_LIBM
float mgltest_sin(float x);
float mgltest_cos(float x);
#define SIN mgltest_sin
#define COS mgltest_cos
#else
#include <math.h>
#define SIN sinf
#define COS cosf
#endif

static GLuint gear1, gear2, gear3;

static float root(float x)
{
    float r = x > 1.0f ? x : 1.0f;
    int i;
    if (x <= 0.0f) return 0.0f;
    for (i = 0; i < 20; i++) r = 0.5f * (r + x / r);
    return r;
}

static void gear(float inner, float outer, float width, int teeth, float depth)
{
    float r0 = inner, r1 = outer - depth / 2.0f, r2 = outer + depth / 2.0f, da = 2.0f * 3.14159265f / teeth / 4.0f;
    float u, v, len, a;
    int i;
    glShadeModel(GL_FLAT);
    glNormal3f(0.0f, 0.0f, 1.0f);
    /* the front face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        a = i * 2.0f * 3.14159265f / teeth;
        glVertex3f(r0 * COS(a), r0 * SIN(a), width * 0.5f);
        glVertex3f(r1 * COS(a), r1 * SIN(a), width * 0.5f);
        if (i < teeth) {
            glVertex3f(r0 * COS(a), r0 * SIN(a), width * 0.5f);
            glVertex3f(r1 * COS(a + 3 * da), r1 * SIN(a + 3 * da), width * 0.5f);
        }
    }
    glEnd();
    /* the front of the teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        a = i * 2.0f * 3.14159265f / teeth;
        glVertex3f(r1 * COS(a), r1 * SIN(a), width * 0.5f);
        glVertex3f(r2 * COS(a + da), r2 * SIN(a + da), width * 0.5f);
        glVertex3f(r2 * COS(a + 2 * da), r2 * SIN(a + 2 * da), width * 0.5f);
        glVertex3f(r1 * COS(a + 3 * da), r1 * SIN(a + 3 * da), width * 0.5f);
    }
    glEnd();
    glNormal3f(0.0f, 0.0f, -1.0f);
    /* the back face */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        a = i * 2.0f * 3.14159265f / teeth;
        glVertex3f(r1 * COS(a), r1 * SIN(a), -width * 0.5f);
        glVertex3f(r0 * COS(a), r0 * SIN(a), -width * 0.5f);
        if (i < teeth) {
            glVertex3f(r1 * COS(a + 3 * da), r1 * SIN(a + 3 * da), -width * 0.5f);
            glVertex3f(r0 * COS(a), r0 * SIN(a), -width * 0.5f);
        }
    }
    glEnd();
    /* the back of the teeth */
    glBegin(GL_QUADS);
    for (i = 0; i < teeth; i++) {
        a = i * 2.0f * 3.14159265f / teeth;
        glVertex3f(r1 * COS(a + 3 * da), r1 * SIN(a + 3 * da), -width * 0.5f);
        glVertex3f(r2 * COS(a + 2 * da), r2 * SIN(a + 2 * da), -width * 0.5f);
        glVertex3f(r2 * COS(a + da), r2 * SIN(a + da), -width * 0.5f);
        glVertex3f(r1 * COS(a), r1 * SIN(a), -width * 0.5f);
    }
    glEnd();
    /* the outward faces of the teeth */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i < teeth; i++) {
        a = i * 2.0f * 3.14159265f / teeth;
        glVertex3f(r1 * COS(a), r1 * SIN(a), width * 0.5f);
        glVertex3f(r1 * COS(a), r1 * SIN(a), -width * 0.5f);
        u = r2 * COS(a + da) - r1 * COS(a);
        v = r2 * SIN(a + da) - r1 * SIN(a);
        len = root(u * u + v * v);
        u /= len; v /= len;
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r2 * COS(a + da), r2 * SIN(a + da), width * 0.5f);
        glVertex3f(r2 * COS(a + da), r2 * SIN(a + da), -width * 0.5f);
        glNormal3f(COS(a), SIN(a), 0.0f);
        glVertex3f(r2 * COS(a + 2 * da), r2 * SIN(a + 2 * da), width * 0.5f);
        glVertex3f(r2 * COS(a + 2 * da), r2 * SIN(a + 2 * da), -width * 0.5f);
        u = r1 * COS(a + 3 * da) - r2 * COS(a + 2 * da);
        v = r1 * SIN(a + 3 * da) - r2 * SIN(a + 2 * da);
        glNormal3f(v, -u, 0.0f);
        glVertex3f(r1 * COS(a + 3 * da), r1 * SIN(a + 3 * da), width * 0.5f);
        glVertex3f(r1 * COS(a + 3 * da), r1 * SIN(a + 3 * da), -width * 0.5f);
        glNormal3f(COS(a), SIN(a), 0.0f);
    }
    glVertex3f(r1, 0.0f, width * 0.5f);
    glVertex3f(r1, 0.0f, -width * 0.5f);
    glEnd();
    glShadeModel(GL_SMOOTH);
    /* the inside */
    glBegin(GL_QUAD_STRIP);
    for (i = 0; i <= teeth; i++) {
        a = i * 2.0f * 3.14159265f / teeth;
        glNormal3f(-COS(a), -SIN(a), 0.0f);
        glVertex3f(r0 * COS(a), r0 * SIN(a), -width * 0.5f);
        glVertex3f(r0 * COS(a), r0 * SIN(a), width * 0.5f);
    }
    glEnd();
}

int scene_init(int w, int h)
{
    static const GLfloat pos[4] = { 5.0f, 5.0f, 10.0f, 0.0f };
    static const GLfloat red[4] = { 0.8f, 0.1f, 0.0f, 1.0f }, green[4] = { 0.0f, 0.8f, 0.2f, 1.0f }, blue[4] = { 0.2f, 0.2f, 1.0f, 1.0f };
    float r = (float)h / (float)w;
    glViewport(0, 0, w, h);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, (GLfloat *)pos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    gear1 = glGenLists(1);
    glNewList(gear1, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, (GLfloat *)red);
    gear(1.0f, 4.0f, 1.0f, 20, 0.7f);
    glEndList();
    gear2 = glGenLists(1);
    glNewList(gear2, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, (GLfloat *)green);
    gear(0.5f, 2.0f, 2.0f, 10, 0.7f);
    glEndList();
    gear3 = glGenLists(1);
    glNewList(gear3, GL_COMPILE);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, (GLfloat *)blue);
    gear(1.3f, 2.0f, 0.5f, 10, 0.7f);
    glEndList();
    glEnable(GL_NORMALIZE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -r, r, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);
    return 0;
}

void scene_frame(float t)
{
    float angle = t * 70.0f;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPushMatrix();
    glRotatef(20.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(30.0f, 0.0f, 1.0f, 0.0f);
    glPushMatrix();
    glTranslatef(-3.0f, -2.0f, 0.0f);
    glRotatef(angle, 0.0f, 0.0f, 1.0f);
    glCallList(gear1);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(3.1f, -2.0f, 0.0f);
    glRotatef(-2.0f * angle - 9.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear2);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-3.1f, 4.2f, 0.0f);
    glRotatef(-2.0f * angle - 25.0f, 0.0f, 0.0f, 1.0f);
    glCallList(gear3);
    glPopMatrix();
    glPopMatrix();
}

void scene_done(void)
{
    glDeleteLists(gear1, 1);
    glDeleteLists(gear2, 1);
    glDeleteLists(gear3, 1);
}
