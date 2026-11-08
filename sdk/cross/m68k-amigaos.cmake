# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# CMake toolchain file for AmigaOS 3.x on a 68040 with an FPU: bebbo's
# m68k-amigaos-gcc (GFOpenAmigaGCC 16.2, libnix) and the GFOpenGPU developer
# kit (SDL 2, GL, GLES, GLA).
#
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/m68k-amigaos.cmake
#
# Settings (-D on the first configure, or the environment variable of the
# same name):
#   AMIGAOS_KIT        the kit's prefix, the folder with include/ and lib/:
#                      the kit itself, or a stove's prefix/m68k-amigaos.
#                      Default: the kit this file is in (kit/cross/), else
#                      ~/CradleLab/stoves/os32-gcc16-canvas/prefix/m68k-amigaos
#   AMIGAOS_TOOLCHAIN  the compiler's folder (with bin/m68k-amigaos-gcc).
#                      Default: ~/CradleLab/toolchain/openamigagcc-16.2.0-open4-x86_64-linux,
#                      else the m68k-amigaos-gcc on PATH
#   AMIGAOS_CPU        default "-m68040 -m68881"; -m68060 -m68881 for a 68060.
#                      SDL, GL and GLES are hard float: keep -m68881.
#   AMIGAOS_RUNTIME    default -noixemul (libnix)
#   AMIGAOS_EXTRA      default -fno-tree-loop-distribute-patterns (GCC 16's
#                      loop miscompile; sdl2-config gives it too)
#   AMIGAOS_NDK        an NDK's include folder (proto/exec.h and the rest),
#                      searched after everything else. Default: the OpenNDK
#                      in ~/CradleLab/gfopensdk, when it is there
#
# What projects can test: AMIGAOS and AMIGA are TRUE, CMAKE_SYSTEM_NAME is
# AmigaOS (UNIX and WIN32 are not set), and the compiler defines __AMIGA__,
# __amigaos__, AMIGA and __mc68040__ (or __mc68060__) itself. There are no
# shared libraries: add_library builds static ones.

set(CMAKE_SYSTEM_NAME AmigaOS)
set(CMAKE_SYSTEM_VERSION 3)
set(CMAKE_SYSTEM_PROCESSOR m68k)
# Platform/AmigaOS.cmake, beside this file.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# The settings, from -D, the environment or the defaults.
macro(_amigaos_setting name default)
    if(NOT DEFINED ${name})
        if(DEFINED ENV{${name}})
            set(${name} "$ENV{${name}}")
        else()
            set(${name} "${default}")
        endif()
    endif()
endmacro()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../lib/pkgconfig/sdl2.pc")
    get_filename_component(_amigaos_kit "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
else()
    set(_amigaos_kit "$ENV{HOME}/CradleLab/stoves/os32-gcc16-canvas/prefix/m68k-amigaos")
endif()
set(_amigaos_tc "$ENV{HOME}/CradleLab/toolchain/openamigagcc-16.2.0-open4-x86_64-linux")
if(NOT EXISTS "${_amigaos_tc}/bin/m68k-amigaos-gcc")
    find_program(_amigaos_gcc m68k-amigaos-gcc NO_CACHE)
    if(_amigaos_gcc)
        get_filename_component(_amigaos_tc "${_amigaos_gcc}" DIRECTORY)
        get_filename_component(_amigaos_tc "${_amigaos_tc}" DIRECTORY)
    endif()
endif()
set(_amigaos_ndk "")
if(EXISTS "$ENV{HOME}/CradleLab/gfopensdk/OpenNDK/Include_H/proto/exec.h")
    set(_amigaos_ndk "$ENV{HOME}/CradleLab/gfopensdk/OpenNDK/Include_H")
endif()

_amigaos_setting(AMIGAOS_KIT "${_amigaos_kit}")
_amigaos_setting(AMIGAOS_TOOLCHAIN "${_amigaos_tc}")
_amigaos_setting(AMIGAOS_CPU "-m68040 -m68881")
_amigaos_setting(AMIGAOS_RUNTIME "-noixemul")
_amigaos_setting(AMIGAOS_EXTRA "-fno-tree-loop-distribute-patterns")
_amigaos_setting(AMIGAOS_NDK "${_amigaos_ndk}")
if(NOT EXISTS "${AMIGAOS_TOOLCHAIN}/bin/m68k-amigaos-gcc")
    message(FATAL_ERROR "m68k-amigaos.cmake: no bin/m68k-amigaos-gcc in AMIGAOS_TOOLCHAIN (${AMIGAOS_TOOLCHAIN})")
endif()
if(NOT EXISTS "${AMIGAOS_KIT}/lib/pkgconfig")
    message(FATAL_ERROR "m68k-amigaos.cmake: no lib/pkgconfig in AMIGAOS_KIT (${AMIGAOS_KIT})")
endif()
# try_compile's projects read this file again: hand them the same settings.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
     AMIGAOS_KIT AMIGAOS_TOOLCHAIN AMIGAOS_CPU AMIGAOS_RUNTIME AMIGAOS_EXTRA AMIGAOS_NDK)

set(AMIGAOS TRUE)
set(AMIGA TRUE)

# The compilers and binutils.
set(_amigaos_bin "${AMIGAOS_TOOLCHAIN}/bin/m68k-amigaos-")
set(CMAKE_C_COMPILER   "${_amigaos_bin}gcc")
set(CMAKE_CXX_COMPILER "${_amigaos_bin}g++")
set(CMAKE_ASM_COMPILER "${_amigaos_bin}gcc")
set(CMAKE_AR           "${_amigaos_bin}ar" CACHE FILEPATH "")
set(CMAKE_RANLIB       "${_amigaos_bin}ranlib" CACHE FILEPATH "")
set(CMAKE_STRIP        "${_amigaos_bin}strip" CACHE FILEPATH "")
set(CMAKE_NM           "${_amigaos_bin}nm" CACHE FILEPATH "")
set(CMAKE_OBJCOPY      "${_amigaos_bin}objcopy" CACHE FILEPATH "")
set(CMAKE_OBJDUMP      "${_amigaos_bin}objdump" CACHE FILEPATH "")
# Programs can't run here: no emulator, so try_run and tests are skipped.
unset(CMAKE_CROSSCOMPILING_EMULATOR)
unset(CMAKE_CROSSCOMPILING_EMULATOR CACHE)

# The CPU and runtime on every compile and link; the kit's headers and
# libraries as the system's (the toolchain's own gcc doesn't know the kit;
# a stove's gcc does, and then these are harmless). The NDK comes last.
set(_amigaos_flags "${AMIGAOS_CPU} ${AMIGAOS_RUNTIME}")
set(_amigaos_inc "-idirafter ${AMIGAOS_KIT}/include")
if(AMIGAOS_NDK)
    string(APPEND _amigaos_inc " -idirafter ${AMIGAOS_NDK}")
endif()
# A -DCMAKE_C_FLAGS=... on the first configure replaces the _INIT flags, and
# the build would quietly lose the CPU, the runtime and the kit: put them in
# front of such flags (once; the test keeps later configures from adding
# them again).
macro(_amigaos_flags_init var value)
    set(${var}_INIT "${value}")
    if(DEFINED CACHE{${var}})
        string(FIND " ${${var}} " " ${value} " _amigaos_at)
        if(_amigaos_at EQUAL -1)
            set(${var} "${value} ${${var}}" CACHE STRING "" FORCE)
        endif()
    endif()
endmacro()
foreach(lang C CXX ASM)
    _amigaos_flags_init(CMAKE_${lang}_FLAGS "${_amigaos_flags} ${AMIGAOS_EXTRA} ${_amigaos_inc}")
endforeach()
foreach(kind EXE SHARED MODULE)
    _amigaos_flags_init(CMAKE_${kind}_LINKER_FLAGS "${_amigaos_flags} -L${AMIGAOS_KIT}/lib")
endforeach()
# libgcc's unwinder (any g++ link) calls pthread_mutex_lock and the like.
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "-lpthread")

# Look for headers, libraries and packages only in the kit (and the
# compiler's own folder); programs (code generators) only on the host.
set(CMAKE_FIND_ROOT_PATH "${AMIGAOS_KIT}" "${AMIGAOS_TOOLCHAIN}/m68k-amigaos")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# No CMAKE_SYSROOT: --sysroot would hide libnix from gcc, and pkg-config
# would put it in front of the kit's paths, which are absolute already.

# FindOpenGL: GLES 2 and 3 are in libGL.a too (there is no libGLESv2.a), so
# find_package(OpenGL COMPONENTS GLES2) and OpenGL::GLES3 find them.
if(EXISTS "${AMIGAOS_KIT}/lib/libGL.a")
    set(OPENGL_gles2_LIBRARY "${AMIGAOS_KIT}/lib/libGL.a" CACHE FILEPATH "GLES 2 (GL.module)")
    set(OPENGL_gles3_LIBRARY "${AMIGAOS_KIT}/lib/libGL.a" CACHE FILEPATH "GLES 3 (GL.module)")
endif()

# pkg-config (FindPkgConfig, pkg_check_modules) sees only the kit's .pc files.
set(ENV{PKG_CONFIG_LIBDIR} "${AMIGAOS_KIT}/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")
unset(ENV{PKG_CONFIG_SYSROOT_DIR})
