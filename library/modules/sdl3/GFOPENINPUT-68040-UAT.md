# SDL3 GFOpenInput native AmigaOS 3.2.3 / AC090 68040 qualification

**Date:** 18 November 2026. **Source:** `library/modules/sdl3/src/joystick/amigaos3/SDL_sysjoystick.c` in `DalsinAI/gfopenrtg`. This overlays the pinned SDL3_68k joystick source in `library/modules/sdl3/build.sh` and is compiled into `SDL3.module` using the normal build targets. No separate controller engine or application-specific fork.

## Reuse contract

- `gfopeninput.library` version 1.2 or later is preferred when available, opened dynamically with an ABI-level revision check. The `gfopeninput.h` and inline LVO headers already in `library/modules/sdl2/include/gfopeninput` were SHA-256 checked byte-for-byte identical to canonical `DalsinAI/gfopensdk` revision `4ac683894edf8c0e30e36d9318662fdde0ed39c9`.
- Controller listing is rescanned by SDL3 Detect; stable GFOpenInput IDs map to SDL-generated non-reused instance IDs, with added/removed notifications and no head-of-queue blocking. Original joystick handles are closed before controller resources are destroyed.
- Standard mapped gamepads have 21 buttons, six axes (the two triggers converted to SDL's signed axis convention) and a d-pad hat, plus SDL3 auto mapping for the canonical GFOpenInput button layout. Wheels and flight sticks use raw input instead.
- The bridge requests exclusive controller access to avoid duplicate legacy joystick feed, falling back to shared access if another user owns the pad. Rumble uses `OIN_Rumble`, and sends a stop on joystick close.
- If GFOpenInput cannot be opened or is older than v1.2, SDL3 falls back to `lowlevel.library`; only recognised joystick/CD32 types or UNKNOWN devices with actual nonzero input are registered. The scanner reconciles removals, avoiding stale/phantom joystick instances during startup.

## Reproducible build

From a GFOpenRTG checkout with its pinned SDL3 dependencies and an OpenAmigaGCC 16.2 target prefix:

```
C_INCLUDE_PATH=<prefix>/m68k-amigaos/ndk-include.vendor \
STOVE=<prefix> SDK_HEADERS=<prefix>/m68k-amigaos/include \
JOBS=4 sh library/modules/sdl3/build.sh build module stub
```

Builds on daletop returned zero for the module and link stub. `baserel_check` passed (module data written only through A4) and FPCR check found none. The final test module SHA-256 is `cc847b6d44e54080e3fa952cff828dfb1a44668d692478ebd8d74bc60843e452` (1,424,908 bytes).

Standalone probe binaries can be compiled using `m68k-amigaos-gcc -std=gnu11 -O2 -m68040 -m68881 -noixemul -I build/include tests/sdl3/gfopeninput_<name>_68040.c -L build -lSDL3 -lm -lamiga -o <guest-binary>`. The three checked-in sources are `gfopeninput_enumerate_68040.c`, `gfopeninput_motion_68040.c` and `gfopeninput_gamepad_68040.c`.

## Isolated guest evidence

The test was performed in an **independent cloned** AmigaOS 3.2.3 AC090 native 68040 instance at `/mnt/xs1000/oolite-sdl3-input-uat-20261118`, with distinct host/bridge ports 9241/9240, leaving the historical Oolite instance and Team 2 browser labs untouched. The transient clone is not part of the release package.

1. With the regular `gfopeninput.library`, the original Amiga joystick port appeared through SDL3's standard mapping, 6 axes/21 buttons, `SDL_IsGamepad=1`, native test RC0.
2. With `AC_PADS=python AC_TEST_PAD=1` on only the cloned native bridge, a 35-second `gfopeninput_motion_68040` test read axis min/max -32767/+32767, 814 analogue changes, 50 button samples, 411 trigger-active samples, three add and two remove events, all status PASS, RC0. Log `tests/evidence/sdl3-gfopeninput-motion-68040-guest-pass.log`.
3. `SDL_OpenGamepad`, `SDL_GetGamepadMapping`, real mapped analogue samples and `SDL_RumbleGamepad` succeeded, RC0. The isolated native bridge independently logged GFOpenInput rumble on synthetic pad 60, including stop. Log `tests/evidence/sdl3-gfopeninput-gamepad-68040-guest-pass.log`.
4. With `gfopeninput.library` **removed only from the cloned guest**, initial lowlevel fallback originally enumerated phantom devices; a direct `ReadJoyPort` guest diagnostic showed ports 0-2 returned type NOTAVAIL=0 and port 3 UNKNOWN=0x40000000 with no input. The fix filters idle unknown and reconciles devices. Final v5 source returned zero controllers and RC0, log `tests/evidence/sdl3-lowlevel-fallback-68040-guest-pass.log`. v6 changed only canonical button mappings/player assignment; its GFOpenInput/gamepad regression also returned RC0 after this change.

## Limits / remaining UAT gates

- No physical USB/Bluetooth/CD32 gamepad was attached for this session; synthetic-pad tests qualify the guest integration, not every HID mapping or hot-plug race on hardware.
- The full original Oolite title screen and gameplay remain separate UAT gates; these SDL3 component passes must not be represented as the game release.
- No new `gfopeninput.library` ABI vectors or SDK changes. The SDL3 driver is qualified on AC090 68040; AROS, physical 68040/68060 and repeated device exhaustion/stress remain to be tested.
