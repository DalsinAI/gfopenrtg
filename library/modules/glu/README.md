# GLU on GFOpenGPU

SGI's libGLU, from Mesa's glu repository (9.0.3, SGI Free Software License
B 2.0), built for AmigaOS 3.2 as `libGLU.a` with `GL/glu.h`, for programs
on GFOpenGPU's GL: `$(pkg-config --cflags --libs glu)` (`-lGLU -lGL`).

    STOVE16=... KIT=... library/modules/glu/build.sh [OUT_DIR]

makes `OUT_DIR/glu/`, laid out as the kit is (`include/`, `lib/`,
`lib/pkgconfig/glu.pc`, `LICENSE.txt`). The tarball is pinned by
`UPSTREAM.json` and built unchanged, from glu's own list of sources.

All of GLU is there: the matrix helpers (`gluPerspective`, `gluLookAt`,
`gluOrtho2D`, `gluPickMatrix`, `gluProject`, `gluUnProject`), the mipmaps
and image scaling (`gluBuild1D/2D/3DMipmaps`, `...MipmapLevels`,
`gluScaleImage`), the quadrics, the tessellator (and `gluBeginPolygon`'s
older interface), NURBS curves and surfaces, `gluErrorString`,
`gluGetString` and `gluCheckExtension`.

NURBS is C++. It is built without exceptions or RTTI, and `glu_cxx.cc` (the
Team's) gives it `operator new` and `delete` on `malloc`, weak, so a C
program links `-lGLU` with `gcc` and no libstdc++; the build checks that
nothing else of libstdc++'s is needed.

## TinyGL's five

`libtinygl.a` has five GLU calls of its own (`tgl/glu.h`: `gluPerspective`,
`gluLookAt`, `gluOrtho2D`, `gluBuild2DMipmaps`, `gluErrorString`). They are
weak in `libGLU.a`, so a TinyGL program can link `-lGLU` too, in either
order: the library that comes first gives those five, `libGLU.a` the rest.
The headers agree (`tgl/glu.h` and `GL/glu.h` declare them alike), so a
program can include both.
