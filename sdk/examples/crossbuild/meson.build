# GLTri with meson: SDL 2 and GLES 2 from pkg-config (sdl2.pc, glesv2.pc).
project('GLTri', 'c')

deps = [dependency('sdl2'), dependency('glesv2')]
args = host_machine.system() == 'amigaos' ? ['-DGLTRI_AMIGAOS'] : []
executable('GLTri', 'gltri.c', dependencies: deps, c_args: args)
