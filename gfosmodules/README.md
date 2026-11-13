# GFOpenGPU native module bodies (N2)

`build-native.sh OUT/gfosmodules` builds the existing portable GFOpenGPU CPU/3D core as PIC ELF bodies:

- `arm64/default/gfopengpu.library` (`e_machine` 183), for the CM4 host;
- `x86_64/default/gfopengpu.library` (`e_machine` 62), for the Cradle host.

The files deliberately export only GFOpenGPU's existing core and 3D entry points. They have no new loader descriptor or SONAME: the host-native loader contract is not designed yet. The 68k module remains the ABI and identity source.

No binary or Manifest is committed. After building the 68k folder first, run AmiCradle's `gfos_manifest.py` for each native folder; its Manifest supplies the 68k module's RomTag name/version and checks the platform's ELF machine.

Run `python3 gfosmodules/test_native.py` to build both bodies in a temporary directory and check their tree, ELF type/machine, exports, and absence of undefined symbols.
