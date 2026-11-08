# ogg2iff

Ogg Vorbis to AIFF, IFF 8SVX, WAV or raw PCM. Decoding Vorbis in real time
on a 68040 costs more CPU than a port can spare, so a port should ship its
music already decoded. ogg2iff is the build-time step that does it. It runs on
the PC, and also on the Amiga (slowly, offline).

    tools/ogg2iff/build.sh            # PC and Amiga builds, then the tests
    build/ogg2iff -r 22050 music/*.ogg -o data/music       # AIFF, 16-bit
    build/ogg2iff -f 8svx -m -r 11025 sfx/ -o data/sfx     # a folder, for Paula

## Formats

| `-f`   | what                                      | when |
|--------|-------------------------------------------|------|
| `aiff` | AIFF, 16-bit **big-endian** (default)     | **The one to use on the Amiga.** The 68k reads it with no byte swapping, so AHI or a port's own mixer can play it as it is. `-b 8` gives 8-bit signed. |
| `8svx` | IFF 8SVX, 8-bit signed, mono or stereo    | Paula and classic players. Stereo is a `CHAN` chunk (6) with the left channel and then the right one in `BODY`. `VHDR` holds the rate (65535 at most). |
| `wav`  | RIFF WAVE, 16-bit little-endian (or 8-bit unsigned) | Linux-side use. |
| `raw`  | headerless PCM, interleaved               | Ports that stream their own audio. `-b 8\|16`, `-e big\|little` (default big), `-u` for unsigned 8-bit. |

## Options

    -o, --output P        file, or a folder when there are several inputs or a folder in
    -r, --rate HZ         resample: 22050, 11025, 28867 (Paula), any rate from 1000 to 192000
    -m, --mono            downmix (stereo: (L+R)/2; 3 to 8 channels in Vorbis order, LFE left out)
    -b, --bits 8|16       sample size; 8-bit gets TPDF dither (--no-dither to turn it off)
    -n, --normalize[=DB]  peak to DB dBFS, default -0.1
    -g, --gain DB
    -s/-t/-l SEC          --start, --end, --length
    -T, --trim-silence[=DB]   cut silence quieter than DB (default -50 dBFS) from both ends
    -q, --quality 0|1|2   resampler length: 16, 32 or 64 zero crossings (default 1)
    -i, --info            describe the input and write nothing
    -v, --verbose         progress and levels

A folder given as input converts every `.ogg`/`.oga` file in it. Output names
are the input's with the format's extension (`.aiff`, `.8svx`, `.wav`, `.raw`).

## How it works

- **Decoder**: stb_vorbis 1.22 (public domain or MIT, by Sean Barrett).
  Third-party code is not committed. `build.sh` fetches `stb_vorbis.c` from
  nothings/stb at commit `4e9b368bfc071e97206ea5c6adba68dd0cbf8897` into
  `BUILD/stb/`, and checks its sha256
  (`4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`).
  `STB_VORBIS=path` uses a local copy instead; it must match the same hash. Its 16-bit output is within 1 LSB of libvorbis.
- **Streaming**: ogg2iff holds one 4096-frame block, the resampler's window
  and one output block in memory, never the whole file. stb_vorbis takes all
  its memory from one heap buffer, 256 KB to start and doubled if a stream's
  setup needs more, so it never uses `alloca` on the Amiga's small stack.
  Headers are written with zero sizes first and filled in at the end. A
  stereo 8SVX writes its right channel to `OUT.right` and then appends it.
  `-n` and `-T` decode the file twice: once to measure it, once to write it.
- **Order**: decode, cut (`-s/-t/-l`, `-T`), downmix, gain, resample,
  quantise (with dither on 8-bit), write.
- **Resampler**: a polyphase windowed sinc with a Kaiser window. The kernel
  is stored at 128 points per input sample and interpolated between them. The
  cut-off is set so the stopband (about 86 dB down at `-q 1`) starts at the
  lower of the two Nyquist frequencies. Taps are normalised to unity gain at
  DC. When there are few phases (44100 to 22050 or 11025 has one), each
  phase's taps are computed once and kept.
- **Clipping**: samples are clamped to range and counted; a warning gives
  the count.

## Building

`build.sh host` uses `cc`. `build.sh amiga` uses `m68k-amigaos-gcc -O2
-m68040 -m68881 -noixemul`. It finds the compiler as `AMIGA_CC`, else on
`PATH`, else the Cradle's GFOpenAmigaGCC 16.2. The Amiga build is hard-float
on purpose. The SDK's programs are soft float so they also run on an LC040,
but a soft-float Vorbis decoder would be many times slower, and this is an
offline tool. With 68040.library's FPU emulation it still runs on an LC040,
only slowly. The compiler emits none of the FPU instructions a 68040 traps.
The executable is about 95 KB and wants about 300 KB free, plus the
resampler's tables. Those grow as the output rate drops, and stay under
0.5 MB for rates of 8000 Hz and up.

## Tests

`build.sh test` (or `tests/check.py BUILD/ogg2iff`) converts `tests/tone.ogg`
in each format and checks the result. `tests/tone.ogg` is 2.5 s of 440 Hz
left and 1000 Hz right at -6 dBFS, with 0.5 s of silence at each end. We made
it ourselves (`tests/mkogg.py`, through the system's libsndfile), so it is
CC0. The checks:

- Each output is read with a strict parser written from its specification
  (EA IFF 85 / 8SVX with `CHAN`, AIFF 1.3, RIFF WAVE). The parser checks
  chunk and FORM sizes, padding, chunk order and the VHDR, COMM and fmt
  fields.
- The sound: frame counts, each tone's level and channel, mono mixing,
  dither in the silence, normalisation to -0.1 dBFS, trimming, sample-exact
  cutting, AIFF equal to WAV byte-swapped, raw byte orders and signedness,
  and a 1000 Hz tone removed when resampling to 1600 Hz.
- With libsndfile present, the 16-bit decode is compared with libvorbis's.
- `file` is shown for each output.

## Speed

On the PC (Ryzen 5 3500U), the test file decodes about 300 times faster
than real time. Resampling to 22050 costs about as much again.

On the Amiga, the estimate (not yet measured) is this. stb_vorbis on a
68040 at 25 MHz decodes 44.1 kHz stereo music about 3 to 5 times slower than
real time; at 40 MHz, 2 to 3 times slower. A 22 kHz mono stream is about 2
to 3 times cheaper. Resampling at `-q 1` adds roughly a second per second of
audio at 25 MHz; `-q 0` halves that. Converting is a job for the PC whenever
the port's build allows it.
