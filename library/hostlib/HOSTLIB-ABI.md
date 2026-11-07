# hostlib ABI, version 1

The contract between a library's 68k code and AmiCradle's runtime for running a call as host code. Both sides implement this file, not each other's builds. The plan behind it is `docs/design/host-run-libraries.md`.

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
- **Tag version word.** `$0001` marks a pure call, which may run on any core. `$0002` is reserved for locked calls (`ac_magic.h`).
- **Registries.** `ac_magic.h` lists every id. `hostlib.toml` lists those built by hostlib, with each one's contract version. `gen.py --check` (in CI) fails on any disagreement between them, and on two ids with one value.
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
   - The host also refuses a write range in a window that may alias other memory.
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
- **Verification.** `hlm_<name>` gives the declared write ranges as one, from a dry run that stops at `HL_DECLARED()`. With that, `AC090_MAGIC=verify` (`AC_HOSTLIB=verify`) runs the host version, undoes it, runs the 68k body in the interpreter and compares memory and registers. A difference is reported and turns that id off.
- **Usual checks.** The runtime's other checks apply as to every magic call: the CPU state, the call's own stack frame, and 16 MB at most.

## 5. The functions

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

### Earlier ids

Ids from before hostlib keep the contracts `ac_magic.h` gives them: memcpy to adler32, the OGFX leaves, GFOpenCrypto, the open libraries' LVO bodies. They move into hostlib's form one at a time, under their own ids when the block or the results stay exactly the same, else under new ones.

## 6. Tests

- **`tests/m68k/test_hostlib.sh` (CI, check-m68k.yml).**
  1. `gen.py --check`.
  2. Each function's 68k build, tags included, at -O0 and -O2 under qemu-m68k: the "no bridge" case, as on any 68k.
  3. Against its host build at -O0 and -O2 (with UBSan where the compiler has it), byte for byte, on generated inputs.
  4. Where a function replaced older code, against that code too. `HOST_CC`/`HOST_RUN` run the host side for aarch64 under qemu-aarch64.
- **The runtime's test** (gfosmotherboards, `amiga-native-bodies`): the generated wrappers through `magic_call` as the JIT makes it, with verification.
- **The matrix, to come:** ROM N on runtime N-1, N and N+1, with `AC_HOSTLIB=1`, `0` and `verify`.

## 7. Switches (the runtime's environment)

| | |
| --- | --- |
| `AC_HOSTLIB=0` | every magic call is 68k code (as `AC090_MAGIC=0`) |
| `AC_HOSTLIB=verify` | each call both ways, compared (as `AC090_MAGIC=verify`) |
| `AC_HOSTLIB_OFF=name,id,...` | those functions are 68k code (as `AC090_MAGIC_OFF`) |

`AC090_MAGIC` and `AC090_MAGIC_OFF` keep working, and win when both are set.
