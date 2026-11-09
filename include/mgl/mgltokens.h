/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * minigl.library-compatible interface: the tokens that are not OpenGL's
 * (clean-room; values from docs/design/MiniGL-Compatible-Spec.md section 7,
 * GLU 1.3 and GLUT 3.7; the names of MiniGL's own tokens are ours, from the
 * spec's descriptions). OpenGL's are in <mgl/gltokens.h>. */
#ifndef MGL_MGLTOKENS_H
#define MGL_MGLTOKENS_H

/* ---- MiniGL's range (spec 7.1, 7.2) ---- */
#define MGL_TOKEN_BASE                  0x7000
#define GL_POLYGON_OFFSET               0x7001  /* as GL_POLYGON_OFFSET_FILL in glEnable */
#define MGL_LOCK_AUTOMATIC              0x7010
#define MGL_LOCK_MANUAL                 0x7011  /* the default */
#define MGL_LOCK_SMART                  0x7012
#define MGL_WITHDRAWN_PRIM_1            0x7013  /* refused by glBegin */
#define MGL_WITHDRAWN_PRIM_2            0x7014  /* refused by glBegin */
#define MGL_PERSPECTIVE_MAPPING         0x7015  /* a capability: state only */
#define MGL_W_ONE_HINT                  0x7016  /* a glHint target */
#define MGL_Z_OFFSET                    0x7017  /* a capability: mglSetZOffset's offset */
#define MGL_UBYTE_BGRA                  0x7018  /* glColorPointer type: bytes B, G, R, A */
#define MGL_UBYTE_ARGB                  0x7019  /* glColorPointer type: bytes A, R, G, B */
#define MGL_UNSIGNED_SHORT_5_6_5        0x701A  /* texel type */
#define MGL_UNSIGNED_SHORT_4_4_4_4      0x701B  /* texel type */
#define MGL_FIXPOINTTRANS_HINT          0x701C  /* a glHint target */
#define MGL_ARRAY_TRANSFORMATIONS       0x701D  /* a capability, on: arrays are the 68k's (big-endian) */

/* Mouse buttons, in a mouse function's mask. */
#define MGL_MOUSE_LEFT                  1
#define MGL_MOUSE_RIGHT                 2
#define MGL_MOUSE_MIDDLE                4

/* Screen modes. */
#define MGL_SCREENMODE_BEST             0xFFFFFFFFUL
#define MGL_SCREENMODE_WINDOW           0UL

/* MGLLockInfo.format. */
#define MGL_LOCK_FORMAT_RGBA8           0

/* ---- GLU 1.3 ---- */
#define GLU_FALSE                       0
#define GLU_TRUE                        1
#define GLU_VERSION                     100800
#define GLU_EXTENSIONS                  100801
#define GLU_INVALID_ENUM                100900
#define GLU_INVALID_VALUE               100901
#define GLU_OUT_OF_MEMORY               100902
#define GLU_INCOMPATIBLE_GL_VERSION     100903
#define GLU_INVALID_OPERATION           100904
#define GLU_SMOOTH                      100000
#define GLU_FLAT                        100001
#define GLU_NONE                        100002
#define GLU_POINT                       100010
#define GLU_LINE                        100011
#define GLU_FILL                        100012
#define GLU_SILHOUETTE                  100013
#define GLU_OUTSIDE                     100020
#define GLU_INSIDE                      100021
#define GLU_ERROR                       100103

/* ---- GLUT 3.7 ---- */
#define GLUT_RGB                        0
#define GLUT_RGBA                       0
#define GLUT_INDEX                      1
#define GLUT_SINGLE                     0
#define GLUT_DOUBLE                     2
#define GLUT_ACCUM                      4
#define GLUT_ALPHA                      8
#define GLUT_DEPTH                      16
#define GLUT_STENCIL                    32
#define GLUT_MULTISAMPLE                128
#define GLUT_STEREO                     256
#define GLUT_LUMINANCE                  512

#define GLUT_WINDOW_X                   100
#define GLUT_WINDOW_Y                   101
#define GLUT_WINDOW_WIDTH               102
#define GLUT_WINDOW_HEIGHT              103
#define GLUT_WINDOW_BUFFER_SIZE         104
#define GLUT_WINDOW_STENCIL_SIZE        105
#define GLUT_WINDOW_DEPTH_SIZE          106
#define GLUT_WINDOW_RED_SIZE            107
#define GLUT_WINDOW_GREEN_SIZE          108
#define GLUT_WINDOW_BLUE_SIZE           109
#define GLUT_WINDOW_ALPHA_SIZE          110
#define GLUT_WINDOW_ACCUM_RED_SIZE      111
#define GLUT_WINDOW_ACCUM_GREEN_SIZE    112
#define GLUT_WINDOW_ACCUM_BLUE_SIZE     113
#define GLUT_WINDOW_ACCUM_ALPHA_SIZE    114
#define GLUT_WINDOW_DOUBLEBUFFER        115
#define GLUT_WINDOW_RGBA                116
#define GLUT_WINDOW_PARENT              117
#define GLUT_WINDOW_NUM_CHILDREN        118
#define GLUT_WINDOW_COLORMAP_SIZE       119
#define GLUT_WINDOW_NUM_SAMPLES         120
#define GLUT_WINDOW_STEREO              121
#define GLUT_WINDOW_CURSOR              122
#define GLUT_WINDOW_FORMAT_ID           123
#define GLUT_SCREEN_WIDTH               200
#define GLUT_SCREEN_HEIGHT              201
#define GLUT_SCREEN_WIDTH_MM            202
#define GLUT_SCREEN_HEIGHT_MM           203
#define GLUT_MENU_NUM_ITEMS             300
#define GLUT_DISPLAY_MODE_POSSIBLE      400
#define GLUT_INIT_WINDOW_X              500
#define GLUT_INIT_WINDOW_Y              501
#define GLUT_INIT_WINDOW_WIDTH          502
#define GLUT_INIT_WINDOW_HEIGHT         503
#define GLUT_INIT_DISPLAY_MODE          504
#define GLUT_ELAPSED_TIME               700

#define GLUT_CURSOR_INHERIT             100
#define GLUT_CURSOR_NONE                101

#define GLUT_GAME_MODE_ACTIVE           0
#define GLUT_GAME_MODE_POSSIBLE         1
#define GLUT_GAME_MODE_WIDTH            2
#define GLUT_GAME_MODE_HEIGHT           3
#define GLUT_GAME_MODE_PIXEL_DEPTH      4
#define GLUT_GAME_MODE_REFRESH_RATE     5
#define GLUT_GAME_MODE_DISPLAY_CHANGED  6

#define GLUT_KEY_F1                     1
#define GLUT_KEY_F2                     2
#define GLUT_KEY_F3                     3
#define GLUT_KEY_F4                     4
#define GLUT_KEY_F5                     5
#define GLUT_KEY_F6                     6
#define GLUT_KEY_F7                     7
#define GLUT_KEY_F8                     8
#define GLUT_KEY_F9                     9
#define GLUT_KEY_F10                    10
#define GLUT_KEY_F11                    11
#define GLUT_KEY_F12                    12
#define GLUT_KEY_LEFT                   100
#define GLUT_KEY_UP                     101
#define GLUT_KEY_RIGHT                  102
#define GLUT_KEY_DOWN                   103

#endif
