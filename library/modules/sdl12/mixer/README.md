# SDL_mixer for SDL 1.2 programs, on AHI: libSDL_mixer.a

Dale, 24 November 2026 (LumaVAULT BL-142): one common mixer for the SDL game ports, "AHI direct". Before it each port carried its own `amiga/sdl_mixer_lite.c` (nine ports, six versions), mixing WAV only on the 68k into SDL's audio. This is the one they take instead, in the GFOpenGPU developer kit beside SDL 1.2's `libSDL.a`:

    m68k-amigaos-gcc game.c -o Game $(sdl-config --cflags) ... -lSDL_mixer $(sdl-config --libs) -lamiga

`SDL/SDL_mixer.h` keeps SDL_mixer 1.2's calls (`Mix_PlayChannel` and `Mix_FadeInChannel` are macros, as in SDL_mixer). It is the Team's own code, MIT.

## How it plays

| Part | What it does |
| --- | --- |
| Voices | `Mix_OpenAudio` takes AHI voices with `AHI_AllocAudio`: 17 (16 channels and the music), or 9, or 5 when fewer are free. On ACAHI's Host mix mode (`0x00AC0001`, acaudio.audio) when it is there, so the board mixes on the PC and the 68k mixes nothing; else on AHI's default mode, where AHI's mixer does. Nothing in the library adds samples up. |
| Sounds | Two "all of memory" sounds (address 0, length `0xFFFFFFFF`), 16-bit mono and stereo, as OpenAL.module's Voices: a chunk plays straight from where it was loaded, at its own rate and with its own channels. AHI resamples. Loading converts only to 16-bit in the 68k's byte order. |
| Loops, ends | Each voice always has its next sound queued: the same chunk again while loops are left, else `AHI_NOSOUND`. AHI's SoundFunc, when the queued one starts, counts the loops and frees the channel (`src/mix_core.c`). |
| Volume, panning, pause | `AHI_SetVol` (each side's gain to volume and pan, `mc_volpan`), `AHI_SetFreq` 0 to pause. |
| Fades, timed plays | The mixer's process ("SDL_mixer", priority 5), woken by AHI's PlayerFunc 50 times a second. It also calls `Mix_ChannelFinished` and `Mix_HookMusicFinished`. |
| Music | Streamed onto the music voice by the process: half a second a piece, four in flight, read ahead outside AHI's hooks. A WAV's PCM plays from memory; IMA ADPCM (what `ogg2wav.py` makes) is decoded a piece at a time; anything else is read from the sound datatypes with `OIA_Reader` and `OIM_READSOUND` (`datatypes/gfopenimage.h`). Loops and `Mix_SetMusicPosition` open the source again (a datatype reader has no seek). |
| Files | WAV (8 and 16-bit PCM, IMA ADPCM) is read here. Ogg, MP3, FLAC, AIFF and the rest go through gfopensound.datatype, which has them decoded by `media.decode/1` (the services card through Mitchy, AkikoPlus, or a paired Cradle) or on this CPU; tracker modules through gfopenmodule.datatype. |
| WAV fallback | For `name.ogg` (or `.mp3`, ...): when `OIA_DecodedBy` says this CPU ("this ...") and `name.wav` is beside it, the WAV plays instead. That is the WAV the installer makes on request (BL-143). A game asking for `name.wav` gets `name.ogg` when only that is there, so a port's code needn't change its names. `src/mix_wav.c`'s `mw_choose` holds the rule. |

`Mix_MusicDecodedBy()` (the Team's) gives the words for a status line.

## Not here

- `Mix_RegisterEffect`: effects on the mix need a mix, and AHI makes it. It returns 0 with an error, as the ports' copies did.
- `Mix_SetMusicPosition` on a tracker module (SDL_mixer's pattern number): -1. TecnoballZ, which drives libmikmod's player itself through `Mix_Music`, keeps its own mixer until its music is a datatype's.
- More than 16 channels, or more than the voices AHI gave: a play on one returns -1.

## Files

| Path | What it is |
| --- | --- |
| `include/SDL_mixer.h` | The calls. |
| `src/mix_core.c`, `.h` | The channels and the music voice, with no Amiga in them. |
| `src/mix_wav.c`, `.h` | WAV reading, IMA ADPCM, and the Ogg/WAV choice, with no Amiga in them. |
| `src/mix_amiga.c` | AHI, the process, files and datatypes, and the calls. |
| `build.sh` | `libSDL_mixer.a` and `include/SDL/SDL_mixer.h` into `OUT/sdl12/` (run by `../build.sh`). |
| `../../../../tests/sdl12/mixer/test_mixer_host.sh` | The core and readers on a PC, against a pretend AHI that keeps AHI's rules, with the sanitizers. |
| `../../../../tests/sdl12/mixtest.c` | `MixTest` on the Amiga (`../build-amiga.sh` builds it): plays a tone as a chunk and as music, then a file given, and says what decoded it. |
