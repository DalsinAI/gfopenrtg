/* Copyright (c) 2026 AmiCradle Team. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * GLUTSpin: a GLUT program as it is written on any other system, built
 * for GFOpenGPU with the kit's GLUT (freeglut's interface, on GLA in an
 * Intuition window). A lit, spinning teapot (or torus, sphere, cone, cube:
 * Space), with its frame rate in a bitmap font and its name in the stroke
 * font. Arrow keys or a drag with the left button turn it; F goes to a
 * screen of its own and back; Esc or the close gadget quits.
 *   GLUTSpin [SECONDS n]
 * Build:
 *   m68k-amigaos-gcc -O2 glutspin.c -o GLUTSpin $(pkg-config --cflags --libs glut)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <GL/freeglut.h>

static const char *names[] = { "teapot", "torus", "sphere", "cone", "cube" };
static int shape, frames, last_ms, drag, drag_x, drag_y;
static float angle, tilt = 20, turn, fps;
static int width = 320, height = 240;

static void draw_shape(void)
{
    switch (shape) {
    case 0: glutSolidTeapot(0.9); break;
    case 1: glutSolidTorus(0.35, 0.8, 24, 32); break;
    case 2: glutSolidSphere(1.0, 24, 16); break;
    case 3: glRotatef(-90, 1, 0, 0); glTranslatef(0, 0, -0.8f); glutSolidCone(0.8, 1.6, 24, 4); break;
    default: glutSolidCube(1.3); break;
    }
}

static void text(void)
{
    char s[64];
    const char *p;
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, width, 0, height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glColor3f(1, 1, 0.6f);
    snprintf(s, sizeof(s), "%.1f fps", fps);
    glRasterPos2i(8, height - 18);
    for (p = s; *p; p++)
        glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *p);
    glTranslatef(8, 8, 0);
    glScalef(0.15f, 0.15f, 1);
    glLineWidth(2);
    for (p = names[shape]; *p; p++)
        glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void display(void)
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    glTranslatef(0, 0, -4);
    glRotatef(tilt, 1, 0, 0);
    glRotatef(angle + turn, 0, 1, 0);
    draw_shape();
    text();
    glutSwapBuffers();
    frames++;
}

static void reshape(int w, int h)
{
    width = w;
    height = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0 * w / h, 1.0 * w / h, -1, 1, 1.5, 20);
    glMatrixMode(GL_MODELVIEW);
}

static void idle(void)
{
    int t = glutGet(GLUT_ELAPSED_TIME);
    angle = t * 0.05f;
    if (t - last_ms >= 1000) {
        fps = frames * 1000.0f / (t - last_ms);
        frames = 0;
        last_ms = t;
    }
    glutPostRedisplay();
}

static void keyboard(unsigned char k, int x, int y)
{
    switch (k) {
    case 27: glutLeaveMainLoop(); break;
    case ' ': shape = (shape + 1) % 5; break;
    case 'f': case 'F': glutFullScreenToggle(); break;
    }
}

static void special(int k, int x, int y)
{
    switch (k) {
    case GLUT_KEY_LEFT: turn -= 10; break;
    case GLUT_KEY_RIGHT: turn += 10; break;
    case GLUT_KEY_UP: tilt -= 10; break;
    case GLUT_KEY_DOWN: tilt += 10; break;
    }
}

static void mouse(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON) {
        drag = state == GLUT_DOWN;
        drag_x = x;
        drag_y = y;
    }
}

static void motion(int x, int y)
{
    if (!drag)
        return;
    turn += (x - drag_x) * 0.5f;
    tilt += (y - drag_y) * 0.5f;
    drag_x = x;
    drag_y = y;
}

static void stop(int value)
{
    printf("GLUTSpin: %.1f fps\n", fps);
    glutLeaveMainLoop();
}

int main(int argc, char **argv)
{
    static const GLfloat light[] = { 2, 3, 4, 0 }, gold[] = { 0.9f, 0.7f, 0.2f, 1 }, white[] = { 1, 1, 1, 1 };
    int i, seconds = 0;
    glutInit(&argc, argv);
    for (i = 1; i < argc; i++)
        if (!strcasecmp(argv[i], "SECONDS") && i + 1 < argc)
            seconds = atoi(argv[++i]);
    glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowSize(width, height);
    glutCreateWindow("GLUTSpin");
    printf("GLUTSpin: %s, OpenGL %s\n", (const char *)glGetString(GL_RENDERER), (const char *)glGetString(GL_VERSION));
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_GLUTMAINLOOP_RETURNS);
    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutIdleFunc(idle);
    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutMouseFunc(mouse);
    glutMotionFunc(motion);
    if (seconds)
        glutTimerFunc(seconds * 1000, stop, 0);

    glClearColor(0.1f, 0.1f, 0.25f, 1);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, gold);
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, white);
    glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 40);
    glutMainLoop();
    return 0;
}
