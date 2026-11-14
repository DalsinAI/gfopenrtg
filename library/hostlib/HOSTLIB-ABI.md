# hostlib ABI, version 2

The contract between a library's 68k code and AmiCradle's runtime for running a call as host code. Both sides implement this file, not each other's builds. The plan behind it is `docs/design/host-run-libraries.md`.

Sections 1 to 7 are ABI 1 (pure calls), which still holds in full. Section 8 is ABI 2, agreed by Dale on 9 November 2026: locked calls, total functions, parallel host builds, the probe and thin shells, each optional per function. Section 9 is the verifier and runtime contract that stage C builds. Section 10 is the history.

## 1. How a call becomes host code

A host-run function starts with the ACMF tag (`common/amiga/ac_magic.h`):

    BRA.S *+12; DC.L 'ACMF'; DC.W $0001; DC.L id; JMP body

- **Any 68k without AmiCradle's JIT** takes the BRA.S into the body: a real Amiga, a PiStorm/Emu68, WinUAE, or AC090 with its JIT or magic off. The body is the library's own 68k code, always present.
- **AC090's JIT** recognises the tag when it translates the entry. If it knows the id, it runs the host version; if not, the body runs.

So:

- The ROM and every library carry the full 68k implementation, and are built with no reference to any runtime.
- No board probe is needed before a call, and there is no opcode foreign hardware could trap on.
- An old ROM on a new runtime, or a new ROM on an old runtime, works: an id one side lacks is simply 68k code.

## 2. Ids and versions

- **Ownership.** An id is 32 bits: the owner in the top half (the AmiCradle Team is `$0001`), the function below.
- **Permanence.** An id is never reused, and its contract (section 5) is frozen once merged. New behaviour, or a changed block layout, takes a new id. The old one stays, with its host version, for as long as old ROMs and disk libraries may call it.
- **Tag version word.** `$0001` marks a pure call, which may run on any core (12-byte tag, `BRA.S *+12`). `$0002` marks a locked call (section 8.1): a 14-byte tag, `BRA.S *+14; 'ACMF'; $0002; id; lock`, where the lock word is `$FFFF` for hostlib's locked calls. A runtime that knows only `$0001` does not recognise a `$0002` tag, and the call runs as 68k code. No other value is defined.
- **Registries.** `ac_magic.h` lists every id. `hostlib.toml` lists those built by hostlib, with each one's contract version. `gen.py --check` (in CI) fails on any disagreement between them, and on two ids with one value. `ids.lock` is the ledger of merged ids (section 8.5): their contracts are frozen there. `ranges.lock` permanently reserves inclusive id ranges to named owners; a manifest entry inside one must name that owner.
- **Manifest hash.** The sha256 of `hostlib.toml` (`HOSTLIB_MANIFEST` in `hl_ids.h`) is compiled into both sides for logs and reports. Nothing refuses to run on a mismatch.

## 3. One source, two builds

Each hostlib function is one portable C file under `hostlib/`, written with `hl.h`'s macros.

**Shape.** `HL_TAG(name, id)` then `HL_FN(name)`. The function takes one argument: a parameter block in guest memory, big-endian longs. It returns a 32-bit result in D0.

**The 68k build** (the default):
- The accessors are plain loads and stores, and the tag goes in front.
- Callers (a library's LVO code, a patch) fill the block on their stack and call `name(block)`. GCC's stack convention applies, so not `-mregparm`.

**The host build** (`HL_HOST`):
- It runs inside the runtime on guest memory, through the accessors only.
- Guest memory is read and written big-endian.
- Every access must lie in a range declared beforehand.

**Rules for the source:**

1. Guest memory only through `HL_RD8/16/32`, `HL_WR8` and `HL_ARG`, by `hl_addr` (32-bit guest addresses). No host pointers, globals, static state or libc.
2. Arithmetic on guest values in 32 bits (`uint32_t`, `int32_t`), as the 68k does it.
3. Declare every range before touching it (`HL_NEED_R`, `HL_NEED_W`), then `HL_DECLARED()` before the first write.
   - Ranges come from values already read.
   - The host refuses a range that wraps past 4 GB, is over 16 MB, or is not plain RAM (ROM is allowed for reads).
   - The host also refuses a read, write or state range in a window that may
     alias other memory. A 3D texture or vertex upload must use its plain RTG
     or fast-RAM address, not an alternate GFOpenExtMem window.
4. Inputs the host should not take go back with `if (...) HL_DECLINE();`, before the first write. In the 68k build that line does nothing, and the code goes on as it always did.
5. A function rewrites every byte of each declared write range before it reads any of them back.
   - An access outside the declared ranges makes the host give the call up part way. From then on reads give 0 and writes are dropped.
   - The 68k body then runs the call from the start.
   - This rule makes the half-run harmless.
6. The result and every byte written must be the same in both builds for every input the host accepts.

## 4. The runtime's side

- **Vendoring.** gfosmotherboards vendors `hostlib/` by commit with `hostlib/sync.sh` (`amiga-native-bodies/native/hostlib/`, with its `FROM`; gfopenrtg's 68k build vendors it the same way, in `library/hostlib/`). `sync.sh --check` tells whether a copy is still its commit's.
- **Inclusion.** It includes the generated `hostlib_host.inc` after `jit_bodies.h`, and adds `HOSTLIB_DEFS` to the board's table.
- **Per-call wrapper (`hlw_<name>`).** Generated, it:
  1. runs the host build;
  2. hands the call back with the reason code when the build declined;
  3. otherwise sets D0, and charges a cost of the declared bytes / 4 + 8 instructions.
- **Write ranges.** Before the first write, the declared write ranges go to `jit_guest_written`, so translated code under them is dropped. Writes reach guest memory through the JIT's own mapping, so ACRTG video RAM written this way is seen by the display's guard as any CPU write is.
- **Verification.** `hlm_<name>` gives the declared write ranges as one, from a dry run that stops at `HL_DECLARED()`. With that, `AC090_MAGIC=verify` (`AC_HOSTLIB=verify`) runs the host version, undoes it, runs the 68k body in the interpreter and compares memory and registers. A difference is reported and turns that id off. ABI 2 replaces the one envelope with every range, each on its own (section 9); `hlm_` stays for a core without that.
- **Usual checks.** The runtime's other checks apply as to every magic call: the CPU state, the call's own stack frame, and 16 MB at most.

## 5. The functions

### GFOpenGfx leaves, `$00010100` to `$00010103`, version 1

The four existing GFOpenGfx contracts are now built from `hostlib/ogfx/`:

- `hostlib_ogfx_planar_rect`: planar RectFill, set or invert, returning the
  planes drawn;
- `hostlib_ogfx_chunky_rect`: one- to four-byte chunky RectFill, store or XOR,
  returning pixels drawn;
- `hostlib_ogfx_chunky_copy`: overlap-safe chunky rectangle copy, returning
  pixels copied;
- `hostlib_ogfx_planar_blit`: planar source/destination minterm BltBitMap,
  returning destination planes involved.

Their big-endian block layouts remain the structs documented by
GFOpenRTG's `library/ogfx/ogfx_leaves.h`; `hostlib/ogfx/leaves.h` names each
long. The host takes only complete ranges in plain guest memory, validates all
tables and spans before its first write, and declines blocks that wrap or whose
destinations overlap their block or plane tables. The IDs and results are the
same contracts formerly implemented by hand in the runtime.

### OGFX_TEXT_STRIP, `$00010105`, version 1: `hostlib_text_strip(block)`

Text's glyphs gathered into one 1-bit strip. Called by gfopenrtg.library's Text (`library/text_strip.h`).

**The block:** `hostlib/text/text_strip.h`.

**What it does:**
1. Clears `bpr * ysize` bytes at `strip`.
2. For characters `i` to `j - 1` of `s`, ORs each one's image into the strip, or XORs it when `xor` is non-zero. Bit 0 of a byte is its top bit, and row `r` is at `strip + r * bpr`.
3. Places each image at `pos + kern[k] - left` for its pen:
   - glyph `k`: `c - lo` for `lo <= c <= hi`, else `hi - lo + 1`;
   - width `loc[2k + 1]`, first bit `loc[2k]`, read from row `r` of `data` at `data + r * mod`.
4. Bold (`smear` > 0) draws the image again `smear` pixels right. Italic moves row `r` right by `floor((baseline - r) / 2)`.
5. The pen then moves by `space[k]` (or `xsize` without `space`) plus `kern[k]` plus `spacing`.

**Reads and writes:**
- reads the block, `s[i..j)`, `loc` (4 bytes a glyph), `space` and `kern` when non-zero (2 bytes a glyph), and `mod * ysize` bytes of `data`;
- writes only the strip.

**Returns** `j - i`, or 0 when `j <= i`.

**The host declines:**
- `hi - lo + 2` outside 1 to 257;
- a negative `ysize`, `bpr`, `smear` or `i`;
- `mod <= 0` with rows;
- any access outside those ranges.

### SDL2_BLEND, `$00010106`, version 1: `hostlib_sdl2_blend(block)`

SDL 2's software blit of ARGB8888 with `SDL_BLENDMODE_BLEND`, with SDL 2.32's `ALPHA_BLEND_RGBA` formula (`/255`). That is the formula of SDL's generic blitters, not of the blit SDL 2.32 picks for ARGB8888 to ARGB8888 on a 68k, which uses `>> 8` and differs by up to one in a channel: SDL2_BLEND_ARGB below is that one. This contract is frozen as merged. A locked call: the destination is read, then changed.

**The block:** `hostlib/sdl/blend.h`. Six longs: source, destination, source stride, destination stride, width, height. A pixel is four bytes, A, R, G, B, which is how the 68k stores SDL's ARGB8888. A stride is the bytes from one row to the next, and it is at least `width * 4`.

**What it does**, for each pixel, with SDL's formula (`s` the source channel, `d` the destination's, integer division toward zero):

- source alpha 0: the destination pixel stays;
- otherwise `dC = ((sC - dC) * sA) / 255 + dC` for R, G and B, and `dA = sA + dA - (sA * dA) / 255`.

**Returns** `width * height`, or 0 when there is nothing to do or the two rectangles overlap (the block included). Overlap returns 0 in both builds, with nothing written: the 68k body does not blend it either.

**Reads and writes:**
- reads the block and every byte of the source span (`(height - 1) * stride + width * 4`, the gaps between rows included);
- the destination span is state. A source alpha of 0 does not rewrite that pixel, so it is not a write range (rule 5).

**The host declines:**
- a destination span over 64 KB (`HL_STATE_MAX`): the 68k body still blends it;
- a range that is not plain RAM, or that wraps.

A decline after the blend has started puts the destination back, then the 68k body runs the call from the start. The blend therefore happens once. The rows are not `HL_PARALLEL`: a part may not write state, and these rows write the destination.


### SDL2_BLEND_ARGB, `$0001010A`, version 1: `hostlib_sdl2_blend_argb(block)`

SDL 2.32's own software blit of ARGB8888 to ARGB8888 with `SDL_BLENDMODE_BLEND`: the plain C per-pixel-alpha blit `SDL_CalculateBlitA` picks for that format on a 68k. Stock SDL 2 programs get these results; the SDL 2 module's software blit calls this one. A locked call: the destination is read, then changed.

**The block:** `hostlib/sdl/blend.h`, the same as SDL2_BLEND's.

**What it does**, for each pixel (`s` and `d` the source and destination pixels as 32-bit `ARGB` values, arithmetic in unsigned 32 bits):

- source alpha 0: the destination pixel stays;
- source alpha 255: the source pixel is copied;
- otherwise R and B together, `rb = ((d & 0xff00ff) + (((s & 0xff00ff) - (d & 0xff00ff)) * sA >> 8)) & 0xff00ff`; G alone the same way with `0xff00`; and `dA = sA + (dA * (255 - sA) >> 8)`.

For example, `0x80FF4020` over `0x40102030` gives `0x9F873028` (SDL2_BLEND gives `0xA0873028`).

**Returns, declines, state:** as SDL2_BLEND.
### MGL_DRAW, `$00010107`, version 1: `hostlib_mgl_draw(block)`

One non-indexed virgl `DRAW_VBO` appended to a MiniGL context, and the vertices it reads copied into that context's arena. The shared writers are `include/gfopengpu/virgl_cmd.h`'s (`vgc_draw`); this call does the same bytes. A locked call: the four cursor longs are the context's state.

**The block:** `hostlib/mgl/draw.h`. The state is four big-endian longs: the arena's byte cursor, the command stream's word cursor, how many draws have been appended, and a spare. The vertices are `count` tightly packed groups of `comps` floats (1 to 4), copied as they stand. The command is 13 little-endian words, `vgc_draw`'s non-indexed form, with the given primitive and start.

**Returns** `count`, or 0 when there is nothing to draw, `comps` is outside 1 to 4, the new bytes do not fit, or they overlap the source or the cursors. Those return 0 in both builds, with nothing written.

**Reads and writes:**
- reads the block, the cursors (the state range; it is not also a read range) and the vertices;
- writes only the new arena bytes and the 13 command words;
- then the cursors. A decline puts the cursors back. The next draw on that context therefore starts where this one would have, and a declined draw is not appended twice.

Not `HL_PARALLEL`: the cursors are state, and a part may not write state (8.3).

### MGL_VERTICES, `$00010108`, version 1: `hostlib_mgl_vertices(block)`

The arena pack shared by an array draw and by immediate vertex submission. `count` vertices are written at the arena cursor and the cursor advances by `count * stride`. The cursor (and how many vertices have been appended) is locked state.

**The block:** `hostlib/mgl/vertices.h`. Positions are `comps` floats, 2 to 4, copied as they stand (a 68k float array is already the big-endian words the arena keeps). A colour address of 0 means no colour; otherwise four raw bytes. A texture-coordinate address of 0 means none; otherwise `tc_comps` floats, 1 to 4. A source stride of 0 means that source is packed. Every byte of the vertex is written, the unused ones zero.

**Returns** `count`, or 0 when there is nothing to copy, a field does not fit the stride, the sources overlap each other or the new bytes or the cursor, or the new bytes do not fit. Those return 0 in both builds, with nothing written.

A decline puts the cursor back. The next batch on that context starts where this one would have.

Not `HL_PARALLEL`: the cursor is state (8.3).

### MGL_TEX, `$00010109`, version 1: `hostlib_mgl_tex(block)`

One texture upload: 16 big-endian words, an `ACV_PUT` as `vg_put` writes it (header, a zero result, then the handle, context, level, stride, x, y, width, height and the texel address). The block cursor and the upload count are locked state. The texels are read and not changed.

**The block:** `hostlib/mgl/tex.h`. The swap is `ACV_SWAP_NONE`, `_16`, `_32` or `_64`.

**Returns** 1, or 0 when the box is empty, the swap is not one of those four, the 16 words do not fit, or the texels overlap the command or the cursor. Those return 0 in both builds, with nothing written.

A decline puts the cursor back, so the upload is not counted twice.

Not `HL_PARALLEL`: the cursor is state (8.3).

### MGL_CONSTS, `$0001010B`, version 1: `hostlib_mgl_consts(block)`

`send_if_changed`. Compares `n` vec4s with the constants this context last sent and, when they differ, appends one `SET_CONSTANT_BUFFER` and remembers them. The last-sent values and the command cursor are locked state.

**The block:** `hostlib/mgl/consts.h`. `cap` is how many vec4s the state holds (1 to 4095). A `nlast` of `$FFFFFFFF` means nothing has been sent. The command is `vgc_constants`'s: three little-endian words (command 12, the shader, zero) and then the vec4s, each word swapped into little-endian. The remembered copy stays in the guest's byte order.

**Returns** 1 when a command was appended, or 0 when the constants match what was last sent, `n` exceeds `cap`, the shader is not 0 or 1, the command does not fit, or a range overlaps. A 0 from a match writes nothing. A 0 from a rejection writes nothing in both builds.

A decline puts the last-sent values and the cursor back, so the constants are not sent twice.

Not `HL_PARALLEL`: the last-sent values are state (8.3).

### ZLIB_INFLATE_CODES, `$00010180`, version 1: `hostlib_zlib_inflate_codes(block)`

The codes of one deflate block (RFC 1951 3.2.5): literals, lengths and distances, until the end-of-block code, the input or the output runs out, or a code is bad. Called by z.library's inflate (`libraries/openzlib/zlib_core.cpp`, `run_codes`), which keeps the headers, stored blocks, code tables and checks.

**The block:** `hostlib/zlib/inflate_codes.h`: the registers (`struct hl_zic_state`, 60 bytes), the window and its size (256 bytes to 32K, a power of two), the two code tables as the 68k lays out zlib_core.cpp's `Huff` (`struct hl_zic_huff`, 1120 bytes each), the input and its length, the output and its length.

**State (8.1):** the registers and the window, at most 32,828 bytes. The tables and the input are read ranges, the output a write range. A decline puts the registers and the window back, and the 68k body decodes the same bytes, so a stream may go on either way at every call.

**Returns** `HL_ZIC_MORE` (the input or the output ran out; the registers say where), `HL_ZIC_END` (the end-of-block code, taken), or `HL_ZIC_BAD_LIT`, `HL_ZIC_BAD_DIST`, `HL_ZIC_FAR` (z.library's three messages). Never 0. The registers carry `in_used` and `out_used` back. z.library gives the call at most 4 MB of input and of output at a time.

Not `HL_PARALLEL`: a block's codes are one sequence.

### ZLIB_DEFLATE_ALL, `$00010181`, version 1: `hostlib_zlib_deflate_all(block)`

Raw deflate (RFC 1951) of a whole input in one call, every level and strategy z.library has, the last block marked. A streaming deflate's state (window, hash chains and symbols, about 192 KB at windowBits 15 and memLevel 8) is over a locked call's 64 KB (A2), so this is 8.1's "larger state" call: the whole input in, the whole output out, and its working memory a write range that it clears first.

**The block:** `hostlib/zlib/deflate_all.h`: the input and its length, the output and its room, the working memory and its length (at least `hl_zda_work(len, windowBits, memLevel)`, sized down for small inputs), then the level, strategy, windowBits and memLevel. The working memory's numbers are big-endian in both builds, so the host and the 68k body leave the same bytes. After the call, its first long is how many input bytes the first block covers (z.library's data_type guess).

**Returns** the output's length, or 0 when it does not fit in the room given, the working memory is too small, or a parameter is out of range. Those return 0 in both builds.

**Its caller:** z.library's `deflate()` takes this path for a first `deflate(Z_FINISH)` with no dictionary, no primed bits and no gzip header fields (and so `compress2`), only when `HOSTLIB_PROBE` says the runtime runs this id as host code. A plain 68k streams as before. When the call answers 0, deflate streams from the start, with only the header written.

### LZMA_DECODE_ALL, `$00010182`, version 1: `hostlib_lzma_decode_all(block)`

LZMA (7z's method 030101: a raw stream, its size known, an end mark allowed) or LZMA2 (7z's 21, xz's: chunks, uncompressed or LZMA, with their resets), a whole stream in one call. Clean-room, from the formats' published description; the shared decoder that 7z and PhysicsFS use in place of each one's copy of the LZMA SDK. The output is the dictionary, so this is 8.1's "larger state" call: nothing is kept between calls.

**The block:** `hostlib/lzma/lzma_dec.h`: the mode and the props byte, the input and its length, the output and its length (LZMA: exactly; LZMA2 and LZMA_EOS, an LZMA stream of unknown size that its end mark closes: at most, one byte more than the output for LZMA_EOS), the working memory and its length (at least `hl_lzd_work(mode, props)`: the probabilities, big-endian words in both builds, set first), and a result range of two longs (bytes written, bytes read).

**Returns** `HL_LZD_OK`, `HL_LZD_DATA` (damaged: a bad code, a distance before the dictionary's start), `HL_LZD_SHORT` (the input ended first, inside a symbol), `HL_LZD_ROOM` (LZMA2 or LZMA_EOS wants more output than given) or `HL_LZD_PARAM`. Never 0. Both builds give the same answer and the same result longs for every input, damaged ones too.

**68k speed:** the 68k body is no slower than the LZMA SDK it replaces: on 1 MB at -O2 for the 68040, 90.24 M instructions against the SDK's 90.88 M for text (lc3 lp0 pb2), and 113.0-113.3 M against 116.8-117.1 M for 68k code (lc3/lp0/pb2, lc0/lp2/pb2, lc1/lp2/pb0).

### HOSTLIB_PROBE, `$00011000`, version 1: `hostlib_probe(block)`

What the runtime runs as host code, for a thin shell's init, and the host's feature level. The block and the answers are in `hostlib/probe/probe.h`, and the contract is section 8.4. The 68k body (`hostlib/probe/probe.c`) writes zeros and returns 0. The host side is the runtime's own (`runtime = true`).

### Earlier ids

Ids from before hostlib keep the contracts `ac_magic.h` gives them: memcpy to adler32, the OGFX leaves, GFOpenCrypto, the open libraries' LVO bodies. They move into hostlib's form one at a time, under their own ids when the block or the results stay exactly the same, else under new ones.

## 6. Tests

- **`tests/m68k/test_hostlib.sh` (CI, check-m68k.yml).**
  1. `gen.py --check`.
  2. Each function's 68k build, tags included, at -O0 and -O2 under qemu-m68k: the "no bridge" case, as on any 68k.
  3. Against its host build at -O0 and -O2 (with UBSan where the compiler has it), byte for byte, on generated inputs.
  4. Where a function replaced older code, against that code too. `HOST_CC`/`HOST_RUN` run the host side for aarch64 under qemu-aarch64.
  5. `test_hostlib_gen.py`: the manifest's rules (section 8.5), each broken once.
  6. `hostlib_abi2.c`: ABI 2's pieces of `hl.h` (section 8). An `HL_BE` struct and `HL_PARALLEL` parts, host against 68k byte for byte (the host runs the parts last first); the `$0002` tag's bytes and `HL_NEED_S` on the 68k; the probe's 68k body; and an `HL_THIN` build, whose tags reach `hl_thin_missing(id)` and which keeps none of the bodies.
- **The runtime's test** (gfosmotherboards, `amiga-native-bodies`): the generated wrappers through `magic_call` as the JIT makes it, with verification.
- **The matrix, to come:** ROM N on runtime N-1, N and N+1, with `AC_HOSTLIB=1`, `0` and `verify`.

## 7. Switches (the runtime's environment)

| | |
| --- | --- |
| `AC_HOSTLIB=0` | every magic call is 68k code (as `AC090_MAGIC=0`) |
| `AC_HOSTLIB=verify` | each call both ways, compared (as `AC090_MAGIC=verify`) |
| `AC_HOSTLIB_OFF=name,id,...` | those functions are 68k code (as `AC090_MAGIC_OFF`) |

`AC090_MAGIC` and `AC090_MAGIC_OFF` keep working, and win when both are set.

## 8. ABI 2

ABI 2 adds six things to ABI 1. Each is optional per function, and everything in sections 1 to 7 still holds unless this section says otherwise: the tag, ids and their permanence, one source and two builds, the accessors, declared ranges, verification and the switches. A runtime that knows only ABI 1 runs an ABI 2 library correctly: it does not recognise a `$0002` tag, so a locked call runs as 68k code; a total or parallel function is an ordinary pure call to it; and it does not know the probe or host services, whose 68k bodies report that the host facility is unavailable, so their caller uses its ordinary fallback (8.4 and 8.7).

The decisions behind it are Dale's of 9 November 2026 (A1 to A7, PR #655), section 10.

### 8.1 Locked calls (tag `$0002`)

A locked call works on a **state** owned by the caller: a `z_stream`'s internal state, a mixer, a running hash. Two calls on the same state never run at once; that is the caller's rule, as for zlib's streams. Calls on different states may run at once, on any core.

**The tag.** `HL_TAG_LOCKED(name, id)` in place of `HL_TAG`, and `locked = true` in `hostlib.toml` (gen.py checks they agree):

    BRA.S *+14; DC.L 'ACMF'; DC.W $0002; DC.L id; DC.W $FFFF; JMP body

The lock word is the one `libraries/openexec/DESIGN-NATIVE-BODIES.md` section 4 gives a locked LVO body (`reg<<12 | offset`, the object's lock). `$FFFF` means no object lock: the caller keeps two calls on one state apart. `ac_magic.h` has `AC_MAGIC_LOCKED(name, id)` (lock `$FFFF`) and `AC_MAGIC_LOCKED_LK(name, id, lock)`.

**State ranges.** Besides read ranges (`HL_NEED_R`) and write ranges (`HL_NEED_W`), a locked function declares state ranges with `HL_NEED_S(at, n)`. It may read them, then change them. For each one:

1. It is checked as a write range is: plain RAM, no wrap past 4 GB, 16 MB at most, not in a window that may alias, not over the call's own stack frame.
2. All of a call's state ranges together are 64 KB at most (`HL_STATE_MAX`, decision A2). Over that, the host declines the call (`HL_WHY_SIZE`).
3. It may not overlap a read range (decision A3). It may not overlap a write range or another state range either: a state byte is read before it is written, a write range's after (rule 5). The host declines the call when the second of two overlapping ranges is declared (`HL_WHY_INPUT`), before any write.
4. ABI 1's rule 5 does not apply to it.

**Copied aside, put back.**

- At `HL_DECLARED()`, before the first write, the host copies every state range aside (into `hl_host_state_buf()`, the host thread's own `HL_STATE_MAX` bytes), and the state ranges go to `jit_guest_written` with the write ranges.
- If the host gives the call up after that (an access outside the declared ranges), it puts the state ranges back from the copy before the 68k body runs the call from the start. Write ranges need no copy: the 68k body rewrites them, as in ABI 1.
- A call given up before `HL_DECLARED()` has written nothing.

**The state's layout is part of the contract.** It is the 68k build's layout: big-endian, the 68k's alignment (16- and 32-bit members at even offsets), 32-bit guest addresses for pointers. The host build reaches it through the accessors, or through a struct declared `HL_BE`:

- `HL_BE` is `scalar_storage_order("big-endian")`, `packed` and `aligned(2)` on GCC (both builds; on the 68k only the alignment matters). Clang has no `scalar_storage_order`: a host build for it uses the accessors.
- The struct has no padding of its own: where the 68k would pad, it has a pad member. `HL_LAYOUT(type, member, offset)` and `HL_SIZE(type, bytes)` are static assertions; the source states the layout with them, and both builds check it.
- Guest pointers in it are `hl_gptr` (`uint32_t`). A host build follows one only after declaring the range it points to.
- `HL_STRUCT_R(p, at)` and `HL_STRUCT_W(p, at)` point `p` at a struct in guest memory, all of it inside one declared range (a write or state range for `_W`), checked once, as `HL_PTR_R/W`.
- A nested struct needs `HL_BE` too. GCC does not let a program take the address of a scalar member of such a struct.

**State is interchangeable mid-stream.** A state made or last changed by the 68k body is valid for the host build and the other way round, at every call boundary. That is what makes giving up safe in the middle of a stream, and what verification checks (section 9).

**Larger state.** A state over 64 KB (LZMA with a large dictionary) is not a locked call's: the call takes its whole input and fills its whole output in one call, with the dictionary in a write range the call fills (rule 5 holds).

### 8.2 Total functions (`total = true`)

A **total** function's host version accepts every input its contract defines and never declines. Inputs the 68k body treats as errors become results the contract names (an error code in D0, nothing written). Total is what a thin shell needs (8.4).

- A function becomes total only under a new id. An existing id's contract is frozen, and gen.py fails on a merged id whose `total` changes (8.5).
- The 68k build is still built and tested. It is the specification for the host build, byte for byte, in CI, on generated inputs including the error cases.
- **An access outside the declared ranges** in a total function is a bug in its host build (decision A5). The runtime:
  1. logs a line: the function's name, its full id, the address and the calling task's name;
  2. turns the id off for the rest of the session;
  3. shows an Alert, recoverable, numbered `HL_ALERT(id)` (`$00AC0000 | (id & $FFFF)`, `hl.h`);
  4. stops the calling task: the call never returns, and the task Waits on no signals, as exec's Suspend leaves a task. Other tasks go on.

  How the 68k gets there is the runtime's (stage C, section 9.3). The same holds for a range the runtime refuses in a total function.
- Other reasons the runtime gives a call back (the CPU state, a trace bit, an MMU mapping, the switches) still send it to its body. In a full build that is the 68k body. In a thin shell it is `hl_thin_missing(id)`, which does the same as above from the 68k side. The probe answers so that a shell is thin only where that does not happen (8.4).

### 8.3 Parallel host builds (`parallel = true`)

A host build may split one call's work into parts, run on several host cores:

    HL_PART(fill_rows) { ... part, parts, block ... }     /* void fill_rows(block, part, parts) */
    HL_FN(name) { ...declare...; HL_DECLARED(); HL_PARALLEL(n, fill_rows); return ...; }

- **The 68k build** runs parts 0 to n - 1 in order, one after another: on the 68k `HL_PARALLEL` adds no parallelism (a no-op), only the loop.
- **Deterministic.** The result and every byte written are the same as with one thread, whatever the scheduling. Each part writes only its own disjoint bytes of the write ranges. Parts write no state: state ranges are written only by the function itself, before or after `HL_PARALLEL`.
- Parts declare nothing and do not decline. A part's access outside the declared ranges gives the whole call up, as in ABI 1 (and puts state back, 8.1).
- **One call to the 68k.** The 68k sees one call that returns when all the parts are done. Charging is unchanged (the declared bytes).
- **Bounded.** The runtime gives a call at most the cores it offers for host work (Settings › Cores), and none but the calling thread while the instance is set to one core. Under `AC_HOSTLIB=verify` it should run the parts in another order than 0 to n - 1 (the CI test runs them last first).
- `parallel` is not part of the contract (the result is the same either way), so it may change without a new id.
- **Later, not in ABI 2:** calls that return before the work is done (a ticket, the written pages guarded until it finishes). They need the JIT's page guards and are a separate design.

### 8.4 The probe and thin shells

**`HOSTLIB_PROBE`, `$00011000`, version 1** (`ac_magic.h`; decision A4), a pure call: `hostlib_probe(block)`, the block in `hostlib/probe/probe.h`.

- It reads `n` (id, version) pairs, and writes one answer byte for each: `HL_PROBE_HOST` (1) when this runtime runs that id at that version as host code now, plus `HL_PROBE_TOTAL` (2) when it runs it as a total function; 0 when not.
- With a features pointer, it also writes the host's feature level, as the runtime picked it at start for its host builds: the architecture (`HL_ARCH_X86_64`, `HL_ARCH_ARM64`, `HL_ARCH_PPC64`), the level (x86-64 baseline or AVX2; ARMv8.0 or ARMv8.2), and the host cores offered for parallel host work.
- It returns the ABI the runtime implements (2). Its 68k body writes zeros and returns 0, so on any 68k without AmiCradle, or on a runtime that does not know it, every answer is 0.
- The runtime answers 0 for every id under `AC_HOSTLIB=0` and `AC_HOSTLIB=verify`, and for an id in `AC_HOSTLIB_OFF` or turned off by a mismatch.
- Its answer is about the host: verification does not compare it (`noverify`). Its host side is the runtime's own, built from `HOSTLIB_CONTRACTS` (`hostlib_host.inc`), not from the source (`runtime = true`).
- n is 1024 at most; the runtime declines more, and the 68k body answers zeros.

**A thin shell** is a library's 68k build with `HL_THIN` defined:

- Its LVOs pack the arguments into blocks and call the tagged entries, as the full build's do.
- `HL_TAG` keeps each tag, and behind it there is no body, only a call to `hl_thin_missing(id)`. The shell defines that: it Alerts with `HL_ALERT(id)`, logs, and stops the task (8.2).
- `HL_FN`'s code stays in the source as the specification, and the build leaves it out.
- `hostlib_probe` keeps its real 68k body in a thin shell.

**Opening a thin shell** (decision A6):

1. Its init calls the probe for every id it uses, at the version its build has.
2. If every answer is `HL_PROBE_HOST | HL_PROBE_TOTAL`, it opens as itself.
3. Otherwise it **opens the full 68k build from disk in its place**, by the same "newer or available wins" rule as ROM and disk. GFOpenUp installs the full build with the shell, at `LIBS:68k/<name>`. The shell's init loads it and initialises it (`InitResident`), which adds it under the same name at a version no lower than the shell's. The shell then returns NULL and frees itself, and the open goes on with the full build. It logs one line saying why: the first id answered 0.
4. Only if the full build is not there either does the open fail, with that line on the console of the task that opened it.

GFOpenUp installs thin shells only where GFOpenUpTool finds AmiCradle, and the full 68k build everywhere.

### 8.5 The manifest (`hostlib.toml`, `abi = 2`)

Each function's keys, in addition to ABI 1's (`name`, `id`, `id_name`, `version`, `source`, `header`, `callers`), each false or absent when left out:

| key | type | meaning |
| --- | --- | --- |
| `locked` | bool | a locked call (8.1) |
| `state` | string | what its state is (for people); required with `locked` |
| `state_max` | int | the most state bytes a call declares, 1 to 65536; required with `locked` |
| `total` | bool | the host build never declines (8.2) |
| `parallel` | bool | the host build uses `HL_PARALLEL` (8.3) |
| `runtime` | bool | the host side is the runtime's own (the probe); gen.py writes no wrapper |
| `service` | bool | a host service (8.7); implies `runtime` and no verification comparison |
| `owner` | string | the owner from `ranges.lock`, required when the id lies in a reserved range |

`gen.py` (and `--check`, in CI) fails on:

- an unknown key, a wrong type, or an ABI 2 key with `abi = 1`;
- `locked` without `state` and `state_max`, `state_max` outside 1 to 65536, or either of them without `locked`;
- `runtime` (and therefore `service`) with `locked`, `total` or `parallel`;
- `service = true` with an explicit `runtime = false`, or an id in a reserved range whose `owner` differs;
- a source that disagrees with its flags: `locked` and `HL_TAG_LOCKED(name, ...)` go together (else `HL_TAG(name, ...)`); `HL_NEED_S` only with `locked`; `HL_PARALLEL` exactly with `parallel` (comments are not counted);
- two functions with one id, name or `id_name`, or an id not the AmiCradle Team's;
- **the contract ledger, `ids.lock`.** gen.py appends a line for each new id and never changes one. `--check` fails when a merged id is missing from the manifest, or when any of its frozen fields differ: `name`, `id_name`, `version`, `locked`, `total`, `state_max`, `runtime`, `service`. So making a function total, locked or a service, or changing its state's size, takes a new id; `parallel`, `owner` and the source may change.
- **the namespace ledger, `ranges.lock`.** Each line is `first last owner`, an inclusive permanent reservation. Ranges must be AmiCradle Team ids and may not overlap. A manifest id in a range must have the same `owner`; this keeps a later component from consuming another component's reserved namespace without filling `ids.lock` with fake functions.

`hl_ids.h` has the flags (`HL_C_LOCKED`, `HL_C_TOTAL`, `HL_C_PARALLEL`, `HL_C_RUNTIME`, `HL_C_SERVICE`), and `hostlib_host.inc` the table `HOSTLIB_CONTRACTS`, `{ id, version, flags, state_max }` for every id, for the probe and the Alert. Stage C's generator emits wrappers and separate span callbacks for locked and parallel functions; the board supplies the state buffer and worker pool.

### 8.6 Hosts

- Every host build is tested on x86-64 and ARM64 (the Pi 5 and the CM4), and on PPC when the runtime offers it. In CI that is `HOST_CC`/`HOST_RUN` under qemu-aarch64 (section 6); on the real boards it is a lab run before a module set ships.
- Host builds may be compiled more than once, for feature levels: x86-64 baseline and AVX2, ARMv8.0 and ARMv8.2. The runtime picks one at start and reports it through the probe. Each level is tested on its own, and all must give the same bytes.

### 8.7 Host services (`service = true`)

A host service is an ABI call whose answer comes from a facility owned by the runtime, rather than a second implementation of a deterministic 68k computation. `service = true` implies `runtime = true` and no verification comparison. It cannot be `locked`, `total` or `parallel`. The generator freezes the service bit in `ids.lock`, emits `HL_C_SERVICE` in `HOSTLIB_CONTRACTS`, and emits no host wrapper: the runtime supplies the service body, its spans callback and a `MAGIC_F_SERVICE`/`noverify` table entry.

The ordinary outer gates still apply. `AC_HOSTLIB=0`, `AC_HOSTLIB_OFF`, an unsupported CPU or MMU state, or an invalid declared span leaves the call to its 68k body. That body returns the service's typed unavailable result, so the caller can take its next implementation rung. A service is not total: an invalid span never takes 8.2's Alert-and-stop path.

Once those gates admit a call, the host body runs exactly once, including under `AC_HOSTLIB=verify`, and returns a typed service result. It never returns an `HL_DECLINE` reason. Semantic bad arguments are service errors, not requests to run the 68k body. The probe reports an admitted service in verify mode; it continues to hide ordinary compute ids there.

`$00012000`–`$00012FFF` is reserved to owner `ACMedia` in `ranges.lock`. ACMedia v1 uses `$00012000`–`$0001200B`; later versions have the remainder without claiming other components' ids.

## 9. The verifier and runtime contract (stage C)

Decision A1: verification of several ranges is a **multi-range callback in the CPU/runtime interface**, one mechanism for every board and library. It lives in gfoscpuemulators (`m68k-ac090/native/jit_bodies.h`, `jit_magic.c`). The board (gfosmotherboards, `amiga-native-bodies`) fills it from hostlib's generated wrappers. This section is what each side does.

### 9.1 The interface (gfoscpuemulators, `jit_bodies.h`)

```c
/* ABI 2: every range a call declares, each on its own */
enum { MAGIC_SPAN_W = 1, MAGIC_SPAN_S = 2 };            /* written; state (read, then changed) */
#define MAGIC_SPANS 24                                    /* hl.h's HL_RANGES write + HL_RANGES state */
typedef struct magic_span { uint32_t at, n; uint8_t kind; } magic_span;
typedef struct magic_spans { uint32_t n; magic_span s[MAGIC_SPANS]; } magic_spans;

/* magic_def gains, at its end (existing initialisers stand): */
int (*spans)(const uint32_t *arg, magic_spans *out);   /* the dry run: 0 with every range, or an MD_* */
uint8_t flags;                                          /* MAGIC_F_LOCKED 1, MAGIC_F_TOTAL 2 */
```

- `spans` runs the host build in dry-run mode (`hl_init(&hl, 1)`), which stops at `HL_DECLARED()`. It returns every write and state range, each with its kind, unmerged. A non-zero result is the decline `hl.why` gave (a range the host refuses at declare time, A3's overlap included).
- `spans` reads guest memory only. It writes nothing, takes no copy, and is safe to call more than once.
- A def with `spans` set uses it, and its `writes` may be NULL. A def without it is ABI 1's (one envelope from `writes`), unchanged.

### 9.2 What the core does (`jit_magic.c`)

**Recognition** (`magic_match`):

- A tag of version `$0001` (12 bytes) matches a def without `MAGIC_F_LOCKED`.
- A tag of version `$0002` (14 bytes, `BRA.S *+14`) matches a def with `MAGIC_F_LOCKED`. The lock word `$FFFF` means no object lock; other lock words are the open libraries' (DESIGN-NATIVE-BODIES.md section 4) and wait for that work.
- Any other combination does not match: the 68k code runs. The instruction's length is the tag's (12 or 14).

**A call, normally** (`magic_call`, for a def with `spans`):

1. The usual checks of section 4 (CPU state, the stack and its frame).
2. `spans(arg, &sp)`. Each range on its own: plain RAM, 16 MB at most, not over the call's frame, not aliased with it, as `writes`' envelope is checked today. The state ranges together are 64 KB at most. Any failure declines with its MD_* code.
3. Then `fn` (the board's `hlw_`). The core takes no copy: the state's copy and its restore on giving up belong to the runtime, inside `fn` (9.3). On a decline the core counts it and returns `MAGIC_DECLINED`, and the 68k body runs.
4. **A total function** (`MAGIC_F_TOTAL`) is never declined for a reason in steps 2 and 3. That is decision A5's path instead: log, the id off, the Alert, the task stopped (8.2).

**A call under `AC_HOSTLIB=verify`** (a new `magic_verify_spans`; the core owns every copy here):

1. `spans(arg, &sp)` and the checks of the normal call's step 2. If all the ranges together exceed 16 MB (`MAGIC_MAX`), the call is declined (`MD_SIZE`) and runs as 68k code.
2. **The core copies** every range (write and state) into `before`, and the registers. Each range goes to `jit_guest_written`.
3. It runs `fn`.
4. **If `fn` declined:** every state range must equal `before`, byte for byte. That checks the runtime's restore, and a difference is reported as "state not put back" and turns the id off. Write ranges are not compared (rule 5). The call then runs as 68k code, as normally.
5. **If `fn` finished:** the core copies every range into `native`, and the registers. It puts `before` back into every range, and the registers.
6. It runs the 68k body in the interpreter, as `magic_verify` does today: interrupts and the MMU off, writes logged.
7. **It compares:**
   - D0 and the registers the convention keeps;
   - every range against `native`, byte for byte;
   - every logged 68k write, which must lie in a declared range, in the stack below the frame (`MAGIC_STACK`), or in the call's own argument slots.
8. On a difference: the id is turned off, and one line goes to the log. It gives the name, entry, arguments, D0 both ways, and either the first differing range (its index, kind, `[at, at + n)` and the first differing address) or the 68k's write outside. The 68k body's results stand. Otherwise the call counts as handled.
9. A def with `noverify` (the probe) is not compared.

### 9.3 What the runtime does (gfosmotherboards, `amiga-native-bodies`)

- **`hl.h`'s host side** (vendored; the code goes into `hl.h` itself, in amicradle-guest, so that the CI test runs it too):
  - `struct hl_ctx` gains `ns, slo[HL_RANGES], sn[HL_RANGES]`, the state ranges;
  - `hl_need_s` with 8.1's checks and A3;
  - `hl_find`/`hl_span` treat a state range as readable and writable;
  - `HL_DECLARED()` copies the state into `hl_host_state_buf()` and hands the state ranges to `hl_host_written`;
  - every give-up after it (`hl->why` first set) puts the state back.
- **`hl_host_state_buf`**: `HL_STATE_MAX` bytes, the calling host thread's own (`_Thread_local`).
- **`hl_host_parallel`**: a pool of at most the cores offered for host work, each part with its own copy of `struct hl_ctx` (its own last-range caches), joined before it returns; `hl->why` set from any part's. At one core it runs the parts on the calling thread.
- **gen.py's output** (amicradle-guest, `hostlib/gen.py`):
  - an `hls_<name>` (the `spans` callback) for every function, ABI 1's too;
  - `flags` in `HOSTLIB_DEFS`;
  - wrappers for locked and parallel functions (it refuses them until then);
  - for a total function, `hlw_` reports an out-of-range access as A5's, not a decline.
- **The probe** (`HL_ID_HOSTLIB_PROBE`, a board def with `noverify`):
  - its answers from `HOSTLIB_CONTRACTS`, the defs registered, `magic_off` and the switches (8.4);
  - its feature level from the host build the runtime picked at start.
- **A5's path:** the log line, the id off, the Alert numbered `HL_ALERT(id)`, and the calling task stopped. The mechanism is the runtime's to choose; it must leave other tasks running and the system usable.
- **Feature levels:** the host builds compiled per level (x86-64 baseline/AVX2, ARMv8.0/8.2) and one picked at start (8.6).

### 9.4 Tests

- **amicradle-guest** `tests/m68k/test_hostlib.sh`: the locked and parallel host sides against their 68k builds, with the test's own stand-ins replaced by `hl.h`'s code, and a give-up in the middle of a locked call putting the state back.
- **gfoscpuemulators:** `magic_verify_spans` on a def with two small ranges far apart (Team 1's preflight case, beyond one envelope's 16 MB), a state range the host build changes wrongly (reported), and a decline that leaves state changed (reported).
- **gfosmotherboards** `hostlib_test`: every function through `magic_call` and verification, with ABI 2's functions as they come (zlib's inflate first).
- **3D-sized ranges:** `tests/m68k/hostlib_large_ranges.c` copies a 4 MB
  texture from fast RAM byte for byte, accepts an exact 16 MB range, refuses
  16 MB plus one byte, and refuses both aliased texture reads and writes.

## 10. History

- **ABI 1**, 7 November 2026: sections 1 to 7.
- **ABI 2**, 9 November 2026: drafted in `hostlib/HOSTLIB-ABI-2-DRAFT.md` (PR #655, Team 5), reviewed, and agreed by Dale with these decisions (PR #655, comment of 9 November, 21:25), answering Team 1's preflight. They are folded into section 8, and the draft file is removed (it is in the PR's history).
  - **A1:** verification of several ranges is a multi-range callback in the CPU/runtime interface (gfoscpuemulators). Section 9.
  - **A2:** 64 KB of state a call, all ranges together. A larger dictionary goes in a write range the call fills. Section 8.1.
  - **A3:** a state range may not overlap a read range; the host declines at declare time. Section 8.1.
  - **A4:** `HOSTLIB_PROBE` has its id now (`$00011000`), and it also reports the host's feature level. Section 8.4.
  - **A5:** a total function's access outside its ranges gives an Alert with the id, a log line, and the calling task stopped. Section 8.2.
  - **A6:** a thin shell whose ids are not all total opens the full 68k build from disk in its place. Section 8.4.
  - **A7:** yes to ABI 2.
- **ABI 2 host services**, 10 November 2026: Dale accepted `service = true`, its verify/fallback rules and ACMedia's namespace after Team 1's review. Section 8.7; `ranges.lock` records the permanent reservation.
