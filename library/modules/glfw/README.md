# GLFW 3.4 on GFOpenGPU

GLFW 3.4 (Zlib, <https://github.com/glfw/glfw>, pinned in `UPSTREAM.json`)
with an AmigaOS 3 platform of the Team's, `GLFW_PLATFORM_AMIGAOS`. It
opens Intuition windows and makes OpenGL and OpenGL ES contexts through
GFOpenGPU's GLA calls (libGL.a, GL.module): virgl on the PC's graphics chip
on AmiCradle (GL 4.3, GLES 3.1), softpipe on the 68k elsewhere (GL 3.3,
GLES 3.1). GL.module chooses, as for every GL program
(`ENV:GFOpenGPU/GLDriver`, see `library/modules/gl/README.md`).

Programs written for GLFW build unchanged:

```
m68k-amigaos-gcc -O2 prog.c -o Prog $(pkg-config --cflags --libs glfw3)
```

or, with CMake, `find_package(glfw3)` and `target_link_libraries(prog glfw)`.
`glfwGetProcAddress` is `gla_get_proc_address`, so glad, GLEW-style
loaders and sokol all work; libGL.a also has every GL call to link
directly.

## Files

| | |
|---|---|
| `build.sh` | builds `OUT/glfw/`: `include/GLFW/`, `lib/libglfw3.a`, `lib/pkgconfig/glfw3.pc`, `lib/cmake/glfw3/`, `LICENSE.md` |
| `src/amiga_*.c`, `src/amiga_*.h` | the platform (Zlib, as GLFW): copied into GLFW's `src/` |
| `patches/0001-amigaos-platform.patch` | GLFW's shared files: the platform ID in `glfw3.h`, the native calls in `glfw3native.h`, the platform's lists in `src/platform.h` and `src/platform.c` |
| `glfw3.pc`, `cmake/` | the kit's pkg-config and CMake package files |
| `tests/build-tests.sh` | builds GLFW's `examples/triangle-opengles.c` and `examples/gears.c` and sokol-samples' `glfw/triangle-glfw.c` against a kit |

`tools/make_sdk.py --glfw OUT/glfw` (or `BUILD/glfw`, found by itself)
puts it in the developer kit.

```
STOVE16=... NDK=.../OpenNDK/Include_H library/modules/glfw/build.sh build
STOVE16=... NDK=... library/modules/glfw/tests/build-tests.sh KIT build
```

`NDK` is only needed when the stove's compiler has no Amiga headers of its
own.

## What the platform does

- **Windows**: an Intuition window on the default public screen (the
  Workbench), opened by `glfwShowWindow` and closed by `glfwHideWindow`
  (Intuition can't hide a window). Title, position, size, size limits,
  resizable and decorated (both reopen the window), maximize (fills the
  Workbench below its title bar), focus, frame size. GLA's frames go into
  the window's RastPort inside its borders (`gla_os3_present_init`); a
  resize resizes the GLA buffer.
- **Full screen**: `glfwCreateWindow` with a monitor, or
  `glfwSetWindowMonitor`, opens a screen of its own in the display mode
  `BestModeID` finds for the video mode (24 bits first, then 16, 15, 8) and a
  borderless backdrop window on it. Changing mode opens a new screen.
- **Contexts**: `GLFW_OPENGL_ES_API` is `GLA_ES2` with the major and minor
  asked for (2.0 to 3.1); `GLFW_OPENGL_CORE_PROFILE` is `GLA_CORE`;
  compatibility or no profile is `GLA_COMPAT` (1.0, GLFW's default, is the
  highest there is), falling back to core for 3.2 or later with no
  profile. Depth 16 or 24, stencil 8, alpha and double buffering follow the
  hints. All contexts share one GLA display, so sharing works.
- **Input**: IDCMP. Raw keys go through a table to GLFW keys (positions,
  US names; Insert, Home, End, Page Up and Down, F11, F12, Print Screen,
  Scroll Lock, Num Lock and Pause at the codes PC keyboard adapters, AROS
  and AmigaOS 4 use), with repeat and modifiers (the Amiga keys are Super);
  text through `MapRawKey` with the user's keymap (Latin-1, dead keys
  included). Mouse buttons (left, right with `WA_RMBTrap`, middle,
  NewMouse's fourth), moves, enter and leave, the wheel (V47's
  `IDCMP_EXTENDEDMOUSE`, or NewMouse's raw keys), size, position, focus,
  refresh (damage) and the close gadget. Ctrl-C asks every window to close.
- **Cursor**: `GLFW_CURSOR_HIDDEN` blanks the pointer; `GLFW_CURSOR_DISABLED`
  blanks it and reads the mouse's own counts (`IDCMP_DELTAMOVE`), which is
  what raw mouse motion means here.
- **Events**: `glfwWaitEvents` waits on every window's IDCMP signal, Ctrl-C
  and `glfwPostEmptyEvent`'s own signal; `glfwWaitEventsTimeout` adds a
  timer.device request.
- **Time**: `ReadEClock` (timer.device), so `glfwGetTime` has the E-clock's
  resolution (about 1.4 µs).
- **Monitor**: one, the Workbench screen, with its mode (named from the
  display database). The video mode list is every display database mode
  with 15 bits or more, and the Workbench's own.
- **Joystick**: a CD32 pad or a joystick in the joystick port through
  lowlevel.library: 2 axes, 7 buttons, 1 hat, and a gamepad mapping, so
  `glfwGetGamepadState` works. Found once, when GLFW first asks.
- **Threads**: GLFW's thread-local slots are single values (GL and events
  stay on the task that called `glfwInit`); its mutexes are signal
  semaphores. `glfwPostEmptyEvent` works from any task.
- **Native**: `glfwGetAmigaWindow`, `glfwGetAmigaScreen`,
  `glfwGetGLAContext`, `glfwGetGLADriver` (`"virgl"` or `"softpipe"`) with
  `GLFW_EXPOSE_NATIVE_AMIGAOS` / `GLFW_EXPOSE_NATIVE_GLA`.

## What it doesn't do

- **Vulkan**: none (`glfwVulkanSupported` is false).
- **Clipboard**: kept inside the program; clipboard.device isn't used.
- **Cursor images and standard cursors**: made, but the pointer stays
  Intuition's. `glfwSetCursorPos` doesn't move the pointer.
- **Swap interval**: GLA has none, `glfwSwapInterval` does nothing.
- **Iconify**: Intuition has none; the window goes behind the others and is
  restored when it is active again.
- **Floating, opacity, transparency, mouse passthrough, window icons
  (a program's icon is its `.info`), aspect ratio, gamma ramps,
  content scale (always 1), attention (a `DisplayBeep`)**.
- **Hot-plugged pads, pads in the mouse port, more than one pad.**
- **EGL and OSMesa context APIs**: GLFW's code is there but finds no
  library (no shared objects to load); GLA is the native API.
- **Multisampling**: `GLFW_SAMPLES` is ignored (GLA's buffers have one
  sample); render to a multisampled FBO instead.
