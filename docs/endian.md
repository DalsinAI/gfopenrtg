# Little-endian data on the 68k: amiga_endian.h and preswap

Most GLES demos and games load little-endian binary assets: glTF `.bin`,
KTX, model files, raw floats. The 68020-68060 is big-endian, so every
value has to be byte-swapped before `glBufferData`, `glTexImage2D` or a
plain read. The kit has two tools for this:

1. **`include/gfopengpu/amiga_endian.h`** (plus `libendian.a`, which is
   optional): fast swaps at run time, for single values and for whole
   arrays.
2. **`tools/preswap.py`**: swaps the assets at build time, so the Amiga
   swaps nothing at run time. Use it wherever the port's assets are fixed.

## amiga_endian.h

```c
#include <gfopengpu/amiga_endian.h>

uint32_t n = load_le32(p);                /* any alignment, host order */
swap_float_array(verts, nverts * 8);      /* in place */
ae_swap16_copy(dst, src, nidx);           /* copy and swap; dst == src is fine */
ae_le32_array_to_host(buf, n);            /* swaps on the Amiga, no-op on a PC */
ae_swap_interleaved(vbo, nverts, 28, "4444422");   /* pos f32x3, uv f32x2, 2 x u16, padding */
```

- **Single values:** `swap16/32/64`, `ae_swapf`, and the aliases
  `le_to_be*` and `be_to_le*`. All of these always swap.
- **Host-relative:** `le16toh`, `le32toh`, `le64toh`, `htole*`, `be*toh`
  and `htobe*`. On the Amiga the `le` forms swap; on a little-endian host
  they do nothing.
- **Any alignment:** `load_le16/32/64`, `store_le*`, `load_be*` and
  `store_be*`. A misaligned `move.l` is legal on the 68020 and later, and
  the header uses one. GCC goes byte by byte instead, because it treats
  m68k as strict-alignment.
- **Arrays:** `swap16/32/64_array`, `swap_float_array` and the `_copy`
  forms. `n` counts elements, not bytes.
- **Interleaved data:** `ae_swap32_strided(base + offset, count, stride,
  ncomp)` swaps one attribute. `ae_swap_interleaved(base, count, stride,
  layout)` swaps every field: the layout has one digit per field, giving
  its size in bytes (1, 2, 4 or 8).

**Short names.** Every call has an `ae_` name. The short names (`swap16`,
`load_le32`, `le32toh`, ...) are macros. A port that has its own `swap16`
function will then fail to compile: define `AMIGA_ENDIAN_NO_SHORT_NAMES`
and use the `ae_` names. The `le32toh` family is left out when
`<endian.h>` is already included.

**Byte order.** The header detects it at compile time and sets
`AMIGA_ENDIAN_BIG` or `AMIGA_ENDIAN_LITTLE` to 1. It checks
`__BYTE_ORDER__` first, then known CPU macros, and stops with `#error` when
it can't tell. So the same header works in host tools: there the `le`
calls are no-ops and the rest use the host's builtins. GCC and Clang on x86
vectorise the array loops (8-9 GB/s measured on this PC).

**Inline or a library.** The array calls are `static inline`. To keep one
copy in a big program, build it with `-DAMIGA_ENDIAN_USE_LIB` and link
`-lendian`. `library/endian/build.sh [OUT]` builds `libendian.a`; it uses
`CC` and `CPU` from the environment and defaults to the kit's
`-m68040 -m68881`. The code is integer-only, so it runs on an LC040 too.
For the 68000, `-DAMIGA_ENDIAN_NO_ASM` (or plain `-m68000`) gives plain C.

### What GCC 16.2 already does, and what the asm adds

The toolchain is GFOpenAmigaGCC 16.2 (`openamigagcc-16.2.0-open4`). These
are the results at `-O2` with `-m68020`, `-m68040` and `-m68060`:

| | GCC's code | Verdict |
|---|---|---|
| `__builtin_bswap16` | `ror.w #8,d0` | optimal; the header uses it |
| `__builtin_bswap32` | `ror.w #8 / swap / ror.w #8` | optimal; the header uses it |
| `__builtin_bswap64` | two bswap32s; out of line, 4 extra `move.l` through a0/a1 to return in d0:d1 | fine inlined; the header uses it |
| byte loads (`p[0] \| p[1]<<8 ...`) or `memcpy` into a `uint32_t` | 4 byte moves, shifts and ors (17 instructions) | **asm**: one `move.l` plus the swap |
| `uint32_t` array loop | 1 long per turn, `dbra`; `-funroll-loops` unrolls it but keeps one register chain | **asm**: 4 longs per turn on 2 independent registers |
| `uint16_t` array loop | 1 word per turn: `move.w`, `ror.w`, `move.w` | **asm**: 2 words per long (`rol.w #8 / swap / rol.w #8 / swap`), 8 words per turn |

**The asm loops.** Each loop is entered through a `bra.s` to a
`subq.l #1 / bcc.s` at its bottom; this handles counts of zero and has no
64K limit. The loop starts on a long (`.balign 4`), which is what the
68060's fetch wants. It can't start on a 16-byte cache line: the hunk
linker places objects on 4 bytes (objdump shows `2**2`) and LoadSeg places
hunks on 8 bytes. That doesn't cost anything, since the 40-byte loop runs
from the I-cache after its first turn. `ae_swap16_copy` first does one
word, if needed, to get dst onto 4 bytes.

### Timings: estimates

These are not measured: this PC has no qemu-m68k and no 68k, and the
Amiga lab wasn't used. Data is assumed to be in the cache. The clock
counts come from the 68040 and 68060 manuals' integer timing tables as
I remember them; I didn't re-check them against the manuals in this
session:

- **68040:** `move.l` to or from `(An)+` is 1 clock, `SWAP` 1 clock,
  `ROR.W`/`ROL.W #8` 2 clocks, a taken `Bcc` or `DBcc` about 2 clocks. If
  your copy of the manual gives the rotates 1 clock, take about a third
  off the 68040 numbers below.
- **68060:** `move`, `ror`, `rol`, `swap` and `subq` are 1 clock each and
  can issue in either pipe (pOEP or sOEP), two per clock when they don't
  depend on each other. A predicted `Bcc` costs about 0 clocks.

Clocks per byte swapped:

| | GCC loop, 68040 | asm, 68040 | GCC loop, 68060 | asm, 68060 |
|---|---|---|---|---|
| 32-bit | 9 per long, **2.25** | 31 per 16 B, **1.94** (-14%) | ~5.5 per long (one dependent chain), **~1.4** | ~12 per 16 B (2 chains, paired), **~0.75** (~1.9x) |
| 16-bit | 6 per word, **3.0** | 35 per 16 B, **2.19** (-27%) | ~3.5 per word, **~1.75** | ~14 per 16 B, **~0.85** (~2x) |

The same in MB/s, with the data in the cache:

| CPU | 32-bit asm | 16-bit asm |
|---|---|---|
| 68040 at 25 MHz | ~12.9 MB/s | ~11.4 MB/s |
| 68040 at 40 MHz | ~20.6 MB/s | ~18.3 MB/s |
| 68060 at 50 MHz | ~65 MB/s | ~59 MB/s |

**Large buffers.** A buffer larger than the 4 KB (68040) or 8 KB (68060)
data cache streams from Fast RAM. The 68060 then runs at memory speed,
about the speed of a CopyMem of the same buffer. The 68040 stays
compute-bound at 25 MHz.

**Example.** A 1 MB glTF vertex buffer takes ~80 ms on a 68040/25 and
~20 ms on a 68060/50 to swap at load. With preswap it takes 0 ms.

**Measuring.** `test_endian bench [KB]` times every array call (in KB/s,
integer arithmetic only). Run `build/tests/endian/test_endian-68040 bench
1024` on a real machine, or in the lab when Dale OKs it, to replace the
estimates above.

## preswap

```
tools/preswap.py raw  --layout f32x3,f32x2,u16 [--stride N] [--offset N] in.dat out.dat
tools/preswap.py raw  --all32 | --all16 | --all64 in.dat out.dat
tools/preswap.py gltf scene.glb scene-be.glb
tools/preswap.py gltf scene.gltf out/scene.gltf      # writes out/scene.be.bin
tools/preswap.py ktx  tex.ktx tex-be.ktx
```

The tool is plain Python 3 and needs nothing outside the standard
library. It never writes over its input, so the little-endian original
stays.

**raw** treats the file as a run of elements, each made of the layout's
fields:

- The field types are `f32`, `i32`, `u32`, `f16`, `i16`, `u16`, `f64`,
  `i64`, `u64`, `i8`, `u8` and `pad`. `xN` repeats a field.
- `--stride` is the size of one element. Bytes after the last field, up to
  the stride, are left alone.
- `--offset` skips a header: those bytes are copied unchanged.

**gltf** swaps every accessor's components according to its
`componentType` (sparse indices and values too). It follows each
bufferView's `byteStride` and glTF's rule that a 1- or 2-byte matrix
column starts on 4 bytes. On top of that:

- **Bytes no accessor reads** are left alone: images, Draco data, and
  meshopt-compressed bufferViews (their decoders write in the host's
  order).
- **Two accessors on the same bytes** with the same component size (an
  interleaved view used twice) are swapped once. With different component
  sizes, the tool refuses the file.
- **The output is marked** in `asset.extras` with
  `"byteOrder": "big-endian", "preswap": 1`, keeping any extras already
  there. A file that already has the mark is refused, so nothing is
  swapped twice.
- **A `.glb`'s header and chunk headers** stay little-endian, as the spec
  requires; other tools can still open the file.
- **External `.bin` files** are written beside the output as
  `NAME.be.bin`, and the URI is updated to point at them. `data:` URIs are
  re-encoded in place.

A 200,000-vertex interleaved mesh takes about 0.2 s. The tool works on
whole views with extended slices; it only falls back to field-by-field
when two accessors overlap.

**ktx** (KTX 1) writes the file natively big-endian. That's legal, since
KTX 1 has an endianness field:

- The header, the key/value sizes and every `imageSize` are written
  big-endian.
- The endianness field is `04 03 02 01`.
- Texels are swapped by `glTypeSize` (2 or 4). A `glTypeSize` of 1, which
  every compressed format has, is left alone.
- The cube-face and mip padding is kept.

A loader that honours the endianness field (libktx, for example) reads
the result without swapping; so does one that ignores the field. KTX 2
has no endianness field (it is always little-endian), so the tool refuses
it: convert to KTX 1, or swap at load.

### What a loader needs: cgltf

This is from cgltf's source as I know it (1.13 and 1.14). Check it against
the copy in your port.

- **Accessor data reads natively.** `cgltf_accessor_read_float`,
  `cgltf_accessor_read_index`, `cgltf_accessor_unpack_floats` and the
  rest cast the buffer pointer and read in the host's order. So with a
  preswapped `.bin` they return the right values on the 68k with no
  patch. Without preswap they return garbage there, and this is the bug
  ports hit.
- **The `.glb` container needs a patch.** `cgltf_parse` copies the GLB
  magic, version and length, and each chunk's length and type, into
  `uint32_t`s with `memcpy`, in the host's order. On the 68k the magic
  then doesn't match, and the parse fails with
  `cgltf_result_unknown_format`. The patch is small: read those fields
  little-endian, about 7 reads near the top of `cgltf_parse`:

  ```c
  #include <gfopengpu/amiga_endian.h>
  /* in cgltf_parse, for each header word: */
  uint32_t tmp = ae_load_le32(ptr);       /* was: memcpy(&tmp, ptr, 4); */
  ```

  A `.gltf` with an external `.bin` or a `data:` URI needs no patch.
- **Check the mark.** On a big-endian host, the loader can look for the
  preswap mark in the asset's extras. cgltf keeps extras as a range of
  the JSON (`data->asset.extras.start_offset` and `end_offset`, or
  `cgltf_copy_extras_json`). If the mark isn't there, the loader can
  still swap at load: for each accessor, `ae_swap16_strided` or
  `ae_swap32_strided` over its bufferView, by component size, once per
  byte range.

  ```c
  #if AMIGA_ENDIAN_BIG
  char ex[256]; cgltf_size n = sizeof ex;
  int pre = cgltf_copy_extras_json(data, &data->asset.extras, ex, &n) == cgltf_result_success
            && strstr(ex, "\"big-endian\"") != NULL;   /* .glb extras are compact, .gltf indented */
  #endif
  ```

- **tinygltf.** It uses the same native reads. Its GLB header goes
  through its own swap helper under `TINYGLTF_LITTLE_ENDIAN`, so check
  that this macro is undefined on the 68k.

## Files

- `include/gfopengpu/amiga_endian.h`: the header. `tools/make_sdk.py`
  ships it with the rest of `include/gfopengpu/`.
- `library/endian/endian.c` and `build.sh`: `libendian.a`. It is not in
  the kit yet; `make_sdk.py` would need a line to copy it.
- `tools/preswap.py`: the build-time tool.
- `tests/endian/run.sh`: runs every check below.
  - `test_endian.c` checks every call against byte-by-byte reference
    swaps: array lengths 0-80, src and dst at each 2-byte offset, guard
    bytes, loads and stores at every byte offset, and the strided and
    interleaved calls.
  - The host build runs under ASan and UBSan, as C99 and as C++, both
    inline and with libendian.
  - With the cross compiler, it builds the checks for the 68000, 68020,
    68040 and 68060, and for libendian. With m68k-linux-gnu-gcc and
    qemu-m68k it also runs them.
  - `test_preswap.py` tests preswap on generated files.
