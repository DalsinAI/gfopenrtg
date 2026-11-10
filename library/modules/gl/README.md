# GFOpenGPU's GL module

Mesa's OpenGL and GLES inside GFOpenGPU (DESIGN.md section 5, "One library"):
the GLA core, which runs Mesa's GL state tracker on one of two Gallium
drivers:

- **virgl, on the PC's graphics chip.** On AmiCradle, gfopengpu.library
  carries OGPU_OP_VIRGL to the ACRTG board's ring, and the runtime hands it
  to virglrenderer (`host/virgl/acvirgl.c`). The GLA core's winsys
  (`gla/gla_virgl.c`) turns virgl's calls into ACVirgl request blocks
  (`include/gfopengpu/virgl.h`). It gives GL 4.3 and GLES 3.1.
- **softpipe, on the 68k.** Used on every other Amiga, and wherever
  `OGPU_Query(OGPU_OP_VIRGL)` doesn't answer "full". It gives GL 3.3 and
  GLES 3.1.

### Which one a program gets

A program gets virgl when gfopengpu.library carries it, and softpipe
otherwise. Softpipe is never silent (7 November 2026): before its screen is
made, GL.module puts one line on the program's output (`Output()`) and in
the runtime's log (the services card's `log/1`, through
gfopenservice.device), naming the program and why virgl wasn't used: no
ACRTG board, a runtime without the ring or without virglrenderer, no driver
in `LIBS:GFOpenGPU/` that carries virgl, virgl's context or screen refused,
or the switch below. For example:

```
GL: on softpipe, not virgl: the runtime has no virglrenderer (the ACRTG board doesn't offer virgl; the runtime's log says why). Shaders run on the 68k (ENV:GFOpenGPU/GLDriver gpu refuses softpipe).
```

`ENV:GFOpenGPU/GLDriver` chooses:

| Value | What a program gets |
|---|---|
| `gpu` (or `virgl`) | virgl or nothing: softpipe is refused (one line says so and why), and the program fails to start GL where it would have run on the 68k |
| `cpu` (or `softpipe`) | softpipe, without trying virgl |
| anything else, or unset | virgl when it's there, else softpipe |

`ENV:OGLES3/Driver` `CPU` still makes ogles3.library take softpipe, unless
`GLDriver` says `gpu`. GLQuery and OGLES3Check print the driver they got
(`driver: virgl` or `driver: softpipe`), and GLQuery the reason
(`gla_last_why`, a GLA call from this change: on an older GL.module it
answers NULL). The policy is `gla/os3/gla_virgl_os3.c`'s, tested on the PC
(`tests/test_gla_virgl_os3.c`); the Amiga's calls it makes are
`gla/os3/gla_notice_os3.c`'s.

GL.module and libGL.a are built hard float (`-m68040 -m68881`). The module
needs a 68040 or 68060 with its FPU; an LC040 or EC040 (no FPU) can't run it.

Both draw the same scenes: `tests/test_gla.c` checks softpipe against stored
pictures. It then checks virgl against softpipe's pictures, allowing a
little rounding: 99% of pixels within 8 on every channel.

## Big-endian

The 68k is big-endian and virglrenderer reads little-endian, so the module
deals with byte order in these places:

- **The command stream** goes little-endian, word by word, while byte
  strings such as shader text go as they are (patch 0008).
- **Constant buffers** are written as words (patch 0008).
- **Textures and buffers** move between Amiga memory and the host with
  their units turned round: a texture's by its format's channel or pixel
  size, a buffer's by 32 bits.
- **8- and 16-bit indices** are widened to 32 bits on the 68k (patch 0010).
- **The caps** arrive word by word in the Amiga's order, and the word of
  one-bit fields is reversed to match a big-endian compiler.

## Mesa

- **Version:** Mesa 26.2.4, pinned by SHA-256 in `mesa/UPSTREAM.json` and
  fetched at build time. It is MIT, and its notices are kept.
- **The Team's changes:** the patches in `mesa/patches/`.
  - 0001-0006 are the AmigaOS port. It was gfopenmesa's until
    8 October 2026; it moved here with the one-library design, and
    gfopenmesa keeps GFOpenDemos.
  - 0007-0011 are for virgl.
  - 0012 declares Mesa's disk_cache calls for GL.module's shader cache
    (below).
  - 0013 lets virgl read a resource the host has never written (vertex and
    index buffers) without flushing first. The index widening of 0010 and
    u_vbuf's vertex widening (0011) read buffers at every draw, so before it
    a program drawing from buffer objects sent each draw as a request block
    of its own (Neverball: 29 submits and 29 puts a frame, now 1 and 1).
  - 0014 stores NIR's algebraic search automaton compactly (10 November
    2026): its filters paged, its biggest transition tables as tables of
    distinct rows, and each distinct table, filter and transform list
    once. `nir_opt_algebraic.c.o` was 7.8 MB of GL.module (bcsel's table
    alone 5.1 MB) and is 1.0 MB. Nothing is expanded at run time; the
    lookups take one more read. The automaton is the same, and the
    generator checks every lookup against the original dense tables as
    it emits them; `tests/nir_tables_check.py` checks the emitted C
    against the unpatched generator's.

## GL.module and libGL.a

On AmigaOS 3.2 programs don't carry Mesa. They link `libGL.a` (`stubs/gl`),
and their first GL call loads `LIBS:GFOpenGPU/GL.module` through
gfopengpu.library's `OGPU_ModuleOpen` (`include/gfopengpu/module.h`):

- **GL.module** (`module/`) is Mesa, the GLA core and its OS 3 parts, linked
  as a module on `library/modules/common`. Its table hands out every call by
  name: the 1,300 GL and GLES entry points Mesa's glapi generated for this
  build, and the 16 GLA calls (`stubs/gl/gla.names`). It is about 20 MB and
  opens in about a second on the AC090.
- **libGL.a** has one entry per call, generated with the module's table from
  the same list (`stubs/gl/gen_gl.py`). The first call loads the module and
  binds the whole table by name in one pass. A call the installed module
  doesn't have returns 0.
- **Shared** (residency step 2, DESIGN.md section 5): Mesa is built
  `-fbaserel32` (the cross file) and GL.module linked `-resident32`, so
  gfopengpu.library loads its code once for every program and each program's
  open gets its own copy of Mesa's data (about 200 KB with the BSS). An
  entry of libGL.a sets A4 to the program's copy: it copies its arguments
  (their size comes from Mesa's glapi XML, the GLA calls' from their
  headers), calls, and gives the program its A4 back.
  `gla_get_proc_address` answers with libGL.a's own entries, so calls
  through what it gives set A4 too.
- Each program has a virgl context of its own: the context id and the
  resource handles are the addresses of the program's own winsys and
  resources (`gla/gla_virgl.c`). They were 1 and a count from 1 for every
  program, so two GL programs on virgl at once shared one context, and
  their resources clashed.
- `tools/baserel_check.py` checks GL.module reaches its data only through
  A4; `module/baserel.allow` lists the reviewed exceptions. Mesa is built
  without C++ exceptions (nothing in it catches one), and it starts no
  threads here; a shared build refuses `pthread_create` (posix_shim.c), as
  the stove's libpthread would start a thread without the program's A4.
- **Programs.** One on libGL.a is an ordinary program: `LFLAGS` in
  `gla-link.env` has no `-fbaserel32`. One with Mesa inside (test_gla,
  GFOpenDemos.static) is `-fbaserel32` like Mesa and links with `MESA_LFLAGS`
  (`-resident32`) and `PROGOBJS` (`mesa/amigaos/initcpp.c`). A plain
  `-fbaserel32` program, started by the stove's nlbcrt0, never returns to
  the Shell when it ends, even `int main(void) { return 0; }`; `-resident32`
  ones (nlrcrt0) end as they should. From #40 until this was found,
  `LFLAGS` carried `-fbaserel32`, so GFOpenDemos hung as it exited.
- GL needs a big stack, and libGL.a gives it one. When the task that first
  calls GL has a stack under 256 KB (a Shell's default is 4 KB), each of its
  calls into GL runs on a 1 MB GL stack of libGL.a's own, through exec's
  StackSwap (`stubs/gl/gl_stub.c`). `unsigned long __stack` doesn't help:
  libnix honours it only when its stack swap is linked, which defining
  `__stack` doesn't do, and the GCC 16 stove's libnix stack swap crashes.

## Shader cache

A program's shaders are compiled on the 68k, on softpipe and on virgl
alike: Mesa's GLSL compiler, linker and NIR passes run in GL.module, and
only the result goes to the host. A demo with big shaders can take minutes
to start. GL.module keeps what it compiled, so the next run of the program
(or another that uses the same shaders) skips that work.

**What is kept.** Mesa's state tracker already caches when the screen
hands it a cache (`get_disk_shader_cache`): glCompileShader of a source it
has seen is skipped, and glLinkProgram loads the linked program's metadata
and NIR instead of preprocessing, parsing, linking, translating to NIR and
optimising. virgl's screen makes its cache itself (keyed on the host's
caps); `gla/gla_core.c` gives softpipe's screen one. What still runs on a
hit is the driver's part, NIR to TGSI as each variant is made: much
smaller, and not cached (yet). A cache also turns on Mesa's
ARB_get_program_binary / OES_get_program_binary (one binary format).

**Whose cache.** Mesa is built with `-Dshader-cache=disabled`. Its own cache
compresses (zlib or zstd, both left out), writes from a thread (a shared
GL.module starts none), maps its index with mmap, locks with flock and
names its files with 64 characters (FFS takes 30). Patch 0012 declares the
disk_cache calls anyway, and lets the linker's half of the GLSL cache
(`st_glsl_to_nir.cpp`, behind `ENABLE_SHADER_CACHE` in Mesa) run too: without
it glCompileShader skips a source it has seen and the link, which should
compile it again when the program isn't cached, doesn't (8 November 2026:
GLInfo's raymarcher, which shares the tiny pair's vertex shader, failed to
link). `gla/gla_shader_cache.c` does the calls: one file per entry, written
in the call, with the whole key, the size and a CRC-32 in its header.

| Variable | Meaning | Unset |
|---|---|---|
| `ENV:GFOpenGPU/ShaderCache` | the directory (made if missing), or `off` | `SYS:Storage/GFOpenGPU/ShaderCache`; `T:GFOpenGPU/ShaderCache` where SYS: can't take it |
| `ENV:GFOpenGPU/ShaderCacheSize` | the cap in KiB (64 at least); past it the oldest entries go until three quarters is left | 4096 |

The default lasts across resets. It is not in `ENVARC:`, which the
Startup-sequence copies into RAM (`ENV:`) at every boot: the cache would
cost its size in RAM and the copy's time on each boot. Only the variables
go there (`SetEnv SAVE`). On a write-protected SYS:, the cache falls back
to `T:` (usually RAM), which lasts until a reset.

- **No requesters.** The cache's disk calls run with the process's
  requesters off, so a missing volume or assign (no `T:`, a cache on a disk
  taken out) is a miss or no cache, not "Please insert volume".
- **Read-only.** A directory that can't be made is no cache. A write that
  fails (write-protected, full) stops writes for the rest of the run;
  what is there is still read.
- **Damage.** An entry whose header, size or CRC-32 doesn't match is
  deleted and compiled again. Entries are written under a `t` name and
  renamed, so a reset mid-write leaves no half entry, and the leftovers go
  at the next count.
- **Keys.** Mesa's: the program's sources and bindings, the GL version, and
  the cache's own part: the cache's version, the GL.module build
  (`GLA_CACHE_BUILD`: Mesa's version and the patches' sum, from
  `mesa/build.sh`; there is no ELF build id to take it from), the driver
  and its id. A GL.module built from other patches never reads an older
  one's entries; they age out under the cap.
- **On the PC** (tests) the variables are `GLA_SHADER_CACHE` and
  `GLA_SHADER_CACHE_SIZE`, and unset is off, so test_gla is unchanged.
  `tests/shader_cache.sh` tests the store without building Mesa.
- `MESA_GLSL=cache_info` should have Mesa say on standard error what it
  put and found (libnix's getenv reads `ENV:`; not yet tried on the Amiga).

### Measured, 8 November 2026

GLInfo (`tools/glinfo`, ES 3.1) on a copy of Instance-32 (AC090 68040; virgl
on the host's RX 460), GL.module from the Cradle's stove
(`build-amiga.sh`). Compile, link and first draw or dispatch, in ms:

| | virgl, no cache | virgl, cached | softpipe, no cache | softpipe, cached |
|---|---|---|---|---|
| raymarcher (154-line fragment shader) | 166, 187 | 57, 63 | 347, 310 | 205, 195 |
| compute shader (106 lines) | 230, 223 | 94, 124 | 435, 343 | 287, 284 |

The links go from 140 to 200 ms to 50 to 80; the compiles are skipped. What
is left is the driver's part at first use (on softpipe, the 68k drawing too).
The first cached run on virgl took longer once at first use (213 and 437 ms):
the host's GPU driver met the cached path's TGSI for the first time.
A full run (all profiles, both drivers) with the cap at 64 KiB kept the
cache under it; a volume that isn't there, and a write-protected one
(`Lock Work: ON`), gave no requester and no failure.

## GCC 16's FPCR clash

At `-m68040`, GCC 16 sometimes saves FPCR around a float-to-int store in
the register the store indexes with, so the value lands in the wrong place
(`tools/fpcr_check.py`). `mesa/fpcr_fix.py` scans Mesa's objects after
each build and builds any that have one again at `-m68020 -m68881` (which
converts with `fintrz`), linked ahead of the libraries. On 8 October 2026
that was one of 833: `feedback.c`'s `update_hit_record`. `module/build.sh`
checks GL.module itself before it ships.

## Build

```
sh mesa/build.sh                                        # this machine: test_gla
CROSS=mesa/cross/m68k-amigaos.ini sh mesa/build.sh      # AmigaOS 3.2 (the os32-gcc16 stove's bin/ first on PATH)
sh module/build.sh [OUT_DIR]                            # AmigaOS 3.2: GL.module, libGL.a, include/ (default build/gl)
ACVIRGL_LIB=/path/libvirglrenderer.so.1 sh tests/run.sh
```

A clean-room build from the open stack only (a `git archive` of a commit,
the pinned Mesa tarball and its patches, GFOpenAmigaGCC 16.2 with libnix,
OpenNDK's headers), packaged as the Amiga's part, with `BUILD-INFO.txt`
saying what it was built from:

```
JOBS=2 sh module/build-open.sh WORK [COMMIT]      # WORK/out (GL.module, libGL.a), WORK/package/GFOpenGPU-GL
sh module/package.sh OUT_DIR DEST [VERSION]       # the part alone, from module/build.sh's OUT_DIR
```

The part (`GFOpenGPU-GL/`) has OpenUp's layout: `Part`, `Libs/GFOpenGPU/GL.module`,
the licences in `Utilities/OpenUp/Licences/`, a readme and `SHA256SUMS`;
an `.lha` of it when `jlha` (or an `lha` that writes) is on PATH.
cybergraphics.library's headers are the Team's clean-room ones in
`gla/os3/include` (the numbers of `compat/cybergraphics_lib.c`), so no
vendor CyberGraphX headers are needed.

`GL_WORK` chooses the work directory, which defaults to `build/` here (about
1 GB). A program for OS 3.2 compiles with Mesa's `include/` and
`OUT_DIR/include` and links `-L OUT_DIR -lGL`; GFOpenDemos (gfopenmesa)
does.

## Measured, 8 October 2026

On a scratch copy of the AmiCradle Showcase instance (AC090 68040, ACRTG,
virglrenderer 1.3.0 on the PC's graphics chip):

- **test_gla:** softpipe gives its stored pictures. virgl gives GL 4.3, the
  same pictures as virgl on the x86 host (c3c62778, 34e0c2fd, 1a30012c),
  and 100% of softpipe's within the allowance.
- **GFOpenDemos at 320x240** (gfopenmesa), linked with libGL.a (100 KB
  rather than 20 MB), GL from GL.module, softpipe → virgl:
  - Boing: 4.0 → 357 fps
  - Chrome Gears: 1.5 → 427 fps
  - Copper Tunnel: 0.3 → 738 fps

  A second run gave 442, 556 and 1,205 fps on virgl (the PC's load
  varies) and the same on softpipe. Mesa linked into the program gave
  4.2 → 342, 1.5 → 421, 0.3 → 512: the module costs nothing per call.
- **ModuleCheck** (`tests/modules`) opens GL.module in 1,020 ms and finds
  its 1,315 calls.

## Speed on the 68k

Softpipe's rates above (4.0, 1.5 and 0.3 fps) were below the 5.8, 2.6 and
0.9 fps of 6 October. The Team measured why on a scratch copy of the
Showcase instance, at 320x240. Each figure is the steady rate after the
first 10 seconds (start-up, display lists and shaders are left out) over
30 to 70 seconds. GL.module was built with each set of flags, and the
6 October build of Mesa was relinked into a static GFOpenDemos.

| Build | Runtime of 8 Oct | JIT of amicradle #293 |
| --- | --- | --- |
| 6 October (`-m68040`, bit fields) | 6.5, 2.4, 0.24 | 16.1, 7.1, 0.41 |
| `-m68040 -mnobitfield` (until now) | 4.1, 1.6, 0.33 | 16.3 to 17.8, 7.2 to 7.6, 0.44 |
| `-m68040`, bit fields (now) | 6.3, 2.7, 0.24 | 17.9, 7.5, 0.45 |
| `-m68020 -m68881 -mnobitfield` | | 6.9, 3.5, 0.41 |
| `-m68020 -m68881`, bit fields | | 7.2, 3.6, 0.41 |
| `-m68020 -m68881 -mnobitfield`, `-O2` | | 8.6, 3.5, 0.36 |

Boing, Chrome Gears and Copper Tunnel, in fps.

- **`-mnobitfield`** cost Boing and Chrome Gears a third of their rate on
  the runtime of 8 October, so it is dropped. On the #293 JIT, which
  translates bit-field instructions, it makes no difference. The pictures
  are unchanged (test_gla's b6a1c876, a670987e and 209db179).
- **`-m68020 -m68881`** halves the rate, so `-m68040` stays.
- **`-O2`** is no faster than meson's release `-O3` overall.
- **The FPCR fix** rebuilds one object, `feedback.c`, which the demos don't
  use, so it plays no part.
- **So the drop came from `-mnobitfield`.** It arrived with the GL module.
  The steady rates of that build on that runtime (4.1 and 1.6) match the
  figures above. Copper Tunnel's 0.9 of 6 October was read from a
  one-second window title, and no build here reaches it: the steady rate is
  0.24 to 0.45, with the 6 October build too.

## Measured, residency step 2 (8 October 2026)

On a scratch copy of the Showcase instance (AC090 68040, ACRTG, virgl):

- **ModuleCheck:** the first open of GL.module takes 19,236 KB and about
  860 ms; a second and third open, with the first still open, take 345 KB
  and 1 ms each. Closing them all gives every byte back.
- **Three GFOpenDemos Gears at once**, memory in use over idle: on softpipe
  29.5, 39.6 and 49.8 MB for one, two and three (a copy each, before: 30.1,
  50.8 and 71.4 MB); on virgl 25.2, 30.9 and 36.6 MB. All of it comes back
  when they end.
- **Frame rates** at 320x240, softpipe → virgl, as before within the PC's
  variation: Boing 14.8 → 294 fps, Gears 5.9 → 340, Tunnel 0.4 → 581 (a
  copy each: 14.2 → 329, 5.9 → 330, 0.4 → 579). Two at once on virgl, Gears
  and Tunnel: 192 and 331 fps.
- **A call through libGL.a** (CallCost, `tests/modules/call_cost.c`,
  glGetError with no context): 1.3 to 1.6 µs, as before (1.5); Mesa's own
  work is most of it.

