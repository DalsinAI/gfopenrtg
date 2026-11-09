/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GLUT on GFOpenGPU: what freeglut's own geometry and font files
 * (fg_geometry.c, fg_teapot.c, fg_font.c, fg_font_data.c, fg_stroke_*.c,
 * built unchanged from freeglut 3.6.0) need from freeglut's internals. It
 * stands in for freeglut's fg_internal.h, which carries every platform's
 * window system; the state behind it is glut_os3.c's. The font structures
 * are freeglut's (MIT/X11, Pawel W. Olszta and the freeglut authors). */
#ifndef GLUT_FG_INTERNAL_H
#define GLUT_FG_INTERNAL_H

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <GL/freeglut.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The one window: what the geometry needs of it (glutSetVertexAttrib*). */
typedef struct tagSFG_WindowState {
    GLint attribute_v_coord;
    GLint attribute_v_normal;
    GLint attribute_v_texture;
} SFG_WindowState;

typedef struct tagSFG_WindowFlags {
    GLboolean VisualizeNormals;         /* GLUT_GEOMETRY_VISUALIZE_NORMALS */
} SFG_WindowFlags;

typedef struct tagSFG_Window {
    SFG_WindowState Window;
    SFG_WindowFlags State;
} SFG_Window;

typedef struct tagSFG_Structure {
    SFG_Window *CurrentWindow;
} SFG_Structure;

typedef struct tagSFG_State {
    GLboolean Initialised;
    GLboolean HasOpenGL20;              /* the context has GL 2.0's buffers and attributes */
    GLboolean StrokeFontDrawJoinDots;   /* GLUT_STROKE_FONT_DRAW_JOIN_DOTS */
} SFG_State;

extern SFG_State fgState;
extern SFG_Structure fgStructure;

/* The bitmap font structure */
typedef struct tagSFG_Font SFG_Font;
struct tagSFG_Font
{
    char*           Name;         /* The source font name             */
    int             Quantity;     /* Number of chars in font          */
    int             Height;       /* Height of the characters         */
    const GLubyte** Characters;   /* The characters mapping           */

    float           xorig, yorig; /* Relative origin of the character */
};

/* The stroke font structures */
typedef struct tagSFG_StrokeVertex SFG_StrokeVertex;
struct tagSFG_StrokeVertex
{
    GLfloat         X, Y;
};

typedef struct tagSFG_StrokeStrip SFG_StrokeStrip;
struct tagSFG_StrokeStrip
{
    int             Number;
    const SFG_StrokeVertex* Vertices;
};

typedef struct tagSFG_StrokeChar SFG_StrokeChar;
struct tagSFG_StrokeChar
{
    GLfloat         Right;
    int             Number;
    const SFG_StrokeStrip* Strips;
};

typedef struct tagSFG_StrokeFont SFG_StrokeFont;
struct tagSFG_StrokeFont
{
    char*           Name;                       /* The source font name      */
    int             Quantity;                   /* Number of chars in font   */
    GLfloat         Height;                     /* Height of the characters  */
    const SFG_StrokeChar** Characters;          /* The characters mapping    */
};

/* glut_os3.c: an error ends the program (freeglut's way), a warning is a line on stderr. */
void fgError(const char *fmt, ...);
void fgWarning(const char *fmt, ...);

#define FREEGLUT_EXIT_IF_NOT_INITIALISED(string) \
    if (!fgState.Initialised) { \
        fgError(" ERROR:  Function <%s> called" \
                " without first calling 'glutInit'.", (string)); \
    }

#define FREEGLUT_INTERNAL_ERROR_EXIT_IF_NOT_INITIALISED(string) \
    if (!fgState.Initialised) { \
        fgError(" ERROR:  Internal <%s> function called" \
                " without first calling 'glutInit'.", (string)); \
    }

#define freeglut_return_if_fail(expr) \
    if (!(expr)) \
        return;
#define freeglut_return_val_if_fail(expr, val) \
    if (!(expr)) \
        return val;

#define FREEGLUT_INTERNAL_ERROR_EXIT(cond, string, function) \
    if (!(cond)) { \
        fgError(" ERROR:  Internal error <%s> in function %s", (string), (function)); \
    }

#endif
