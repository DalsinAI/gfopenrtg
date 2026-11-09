# GLUT on GFOpenGPU

`libglut.a` and freeglut 3.6.0's headers (`GL/glut.h`, `GL/freeglut.h`),
so GLUT programs build for AmigaOS 3.2 as they are, with
`$(pkg-config --cflags --libs glut)` (`-lglut -lGLU -lGL`). It runs on GLA,
GFOpenGPU's own GL interface (GL.module: virgl on AmiCradle's GPU, softpipe
elsewhere), in an Intuition window.

    STOVE16=... KIT=... [NDK=...] library/modules/glut/build.sh [OUT_DIR]

after `library/modules/glu/build.sh` (freeglut's headers include
`GL/glu.h`). `tests/glut/build_tests.sh` builds both, the kit's examples
(`sdk/examples/glut`) and two of mesa-demos' programs (gears and the GLSL
brick) with them. `tools/make_sdk.py` puts `BUILD/glu` and `BUILD/glut`
in the kit when they are there.

## What is in it

- `glut_os3.c`, the Team's: the window, its events and GLUT's state.
- freeglut's own `fg_geometry.c`, `fg_teapot.c`, `fg_font.c`,
  `fg_font_data.c` and `fg_stroke_*.c`, unchanged, from the tarball that
  `UPSTREAM.json` pins. `fg_internal.h` and `fg_gl2.h` here stand in for
  freeglut's (which carry every platform's window system).
- freeglut's headers, with one line added to `freeglut_ext.h`:
  `GLUT_ES_PROFILE`.

## What it covers

| | |
|---|---|
| Starting | `glutInit` (takes `-geometry WxH+X+Y`, drops X11's other options), `glutInitDisplayMode` (`GLUT_DOUBLE`, `GLUT_DEPTH` 24 bits, `GLUT_STENCIL` 8, `GLUT_ALPHA`), `glutInitDisplayString` (the same words), `glutInitWindowSize`, `glutInitWindowPosition`, `glutInitContextVersion`, `glutInitContextProfile`, `glutInitContextFlags`, `glutInitErrorFunc`, `glutInitWarningFunc` |
| Contexts | compatibility profile at the highest version the display has (GL 4.3 on virgl, 3.3 on softpipe) by default and for anything up to 2.1; `GLUT_CORE_PROFILE`, or a version of 3.2 or more without a profile, gives core at that version; `GLUT_ES_PROFILE` GLES 2 or 3 |
| The window | `glutCreateWindow`, `glutDestroyWindow`, `glutSetWindowTitle`, `glutReshapeWindow`, `glutPositionWindow`, `glutPopWindow`, `glutPushWindow`, `glutFullScreen`, `glutLeaveFullScreen`, `glutFullScreenToggle`, `glutEnterGameMode` and `glutLeaveGameMode` (a 24-bit screen of its own, Workbench's size), `glutSetCursor` (`GLUT_CURSOR_NONE`, else Intuition's), `glutSwapBuffers`, `glutPostRedisplay` |
| Callbacks | Display, Reshape, Keyboard, KeyboardUp, Special, SpecialUp, Mouse, MouseWheel, Motion, PassiveMotion, Entry, Visibility, WindowStatus, Idle, Timer, Close, WMClose, Position, InitContext |
| Main loop | `glutMainLoop`, `glutMainLoopEvent`, `glutLeaveMainLoop`, `glutExit`, `GLUT_ACTION_ON_WINDOW_CLOSE` |
| Asking | `glutGet` (elapsed time from timer.device, window and screen sizes, the buffer's bits, init values, `GLUT_VERSION` 30600), `glutDeviceGet`, `glutGetModifiers`, `glutLayerGet`, `glutExtensionSupported` (core contexts too), `glutGetProcAddress`, `glutIgnoreKeyRepeat`, `glutSetKeyRepeat`, `glutSetOption` |
| Shapes | freeglut's: cube, sphere, cone, torus, cylinder, teapot, teacup, teaspoon, the Platonic solids, rhombic dodecahedron, Sierpinski sponge, solid and wire; through `glutSetVertexAttribCoord3`/`Normal`/`TexCoord2` in a core context |
| Fonts | freeglut's: `glutBitmapCharacter`/`String`/`Width`/`Length`/`Height` (8x13, 9x15, Times 10 and 24, Helvetica 10, 12, 18), `glutStrokeCharacter`/`String`/`Width`/`Length`/`Height` (Roman, Mono Roman) |

Events come from IDCMP: keys from `RAWKEY` through the keymap (dead keys
too), F1-F10 and the cursor keys as special keys, and F11, F12, Insert,
Page Up and Down, Home and End where the keyboard has them; the buttons from
`MOUSEBUTTONS` (the right one trapped, so it is the program's); the wheel
from NewMouse's `RAWKEY` codes (`glutMouseWheelFunc`, else buttons 3 and 4,
as freeglut does); motion from `MOUSEMOVE`, one call for a run of moves.
Without an idle callback the loop sleeps until a message, a timer or
Ctrl-C (which ends it, as the close gadget does). A one-buffer program's
frame is shown after its display callback, since it calls `glFlush`, not
`glutSwapBuffers`.

## Not covered

Menus are made and filled but never shown. No overlays, subwindows or
second window, no joystick, spaceball, tablet or dials callbacks (accepted,
never called), no `glutWarpPointer`, no colour index, and none of
freeglut's `...Ucall` calls (`freeglut_ucall.h`): a program calling one
won't link.
