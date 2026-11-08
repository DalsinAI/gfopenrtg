# libEGL.a: EGL 1.4 on GLA

Most GLES samples and benchmarks (the OpenGL ES 3.0 Programming Guide's,
ARM's Mali SDK, glmark2, ANGLE's and the Raspberry Pi demos) start GL
through EGL. libEGL.a gives them EGL 1.4 on GFOpenGPU's GL module, mapped
onto GLA (`gla/gla_core.h`, in libGL.a), so they build with their own setup
code and only a window back end of their own.

    m68k-amigaos-gcc demo.c -o Demo $(pkg-config --cflags --libs egl glesv2)

`library/egl/build.sh [OUT_DIR]` builds it (`library/build.sh` does too);
the headers are `include/EGL/egl.h`, `eglplatform.h` and `eglext.h`, the
Team's own, with Khronos's names and values.

## How it maps

| EGL | GLA |
| --- | --- |
| `eglGetDisplay(anything)` | the one display there is |
| `eglInitialize` | `gla_os3_virgl_transport` + `gla_display_create_virgl`, else `gla_display_create` (softpipe); ENV:GFOpenGPU/GLDriver picks, as for every GL program |
| `eglCreateContext` | `gla_context_create`: ES (`GLA_ES2`, `EGL_CONTEXT_MAJOR_VERSION`/`CLIENT_VERSION` and `EGL_CONTEXT_MINOR_VERSION`) or, after `eglBindAPI(EGL_OPENGL_API)`, OpenGL (`GLA_CORE` from 3.2 with the core profile bit, the default; else `GLA_COMPAT`) |
| `eglCreateWindowSurface(win)` | `gla_os3_present_init` on the Intuition window's RastPort, inside its borders (or a GZZ window's inner one), and `gla_buffer_create` its size |
| `eglCreatePbufferSurface` | `gla_buffer_create` with a hook that shows nothing |
| `eglMakeCurrent` | `gla_make_current` (a context with no surface is allowed) |
| `eglSwapBuffers` | follows the window's size (`gla_buffer_resize`), waits `eglSwapInterval` vertical blanks (WaitTOF; 1 by default, as EGL says), then `gla_swap` |
| `eglGetProcAddress` | EGL's own calls, else `gla_get_proc_address` (every GL and GLES call) |

Configs: RGB565, RGB888 and RGBA8888, each with depth 0, 16 or 24 and
stencil 0 or 8 (18), window and pbuffer, ES 2, ES 3 and OpenGL. GLA draws
32-bit pixels whatever the config says, so an RGB565 config draws 8 bits a
channel. Extensions: EGL_KHR_create_context, EGL_KHR_get_all_proc_addresses,
EGL_KHR_surfaceless_context.

## What isn't there

- ES 1 and OpenVG (GL.module has neither): an ES 1 context is EGL_BAD_MATCH,
  `eglBindAPI(EGL_OPENVG_API)` EGL_BAD_PARAMETER.
- Pixmap surfaces, `eglCopyBuffers`, `eglBindTexImage`, client-buffer
  pbuffers, multisampled configs.
- A different read and draw surface (GLA reads where it draws).
- EGL 1.5's calls (`eglGetPlatformDisplay`, sync objects, images) and the
  `EGL_EXT_platform_*` extensions.
- `EGL_RENDERABLE_TYPE`'s default in `eglChooseConfig` is EGL_OPENGL_ES_BIT,
  which no config here has; it is left out, so a program that doesn't name
  it gets every config (as Mesa's EGL gives such programs their ES 2 configs).
- Threads: the current context is the program's, not a task's; one task uses
  EGL at a time.
- `eglTerminate` frees everything at once, the current context and surface too.

## Samples

`samples/opengles3-book/` builds two of the OpenGL ES 3.0 Programming
Guide's samples (opengles3-book, MIT) with an esUtil back end for Intuition
windows: `build.sh KIT [OUT_DIR]` fetches the book at a pinned commit.
`sdk/examples/egl/egltriangle.c` is the kit's own example.
