# Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
# SPDX-License-Identifier: MIT
#
# CMAKE_SYSTEM_NAME AmigaOS (m68k-amigaos.cmake): AmigaOS 3.x with libnix.
# Static libraries only (lib*.a), programs without a suffix, no rpath.

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)
set(CMAKE_EXECUTABLE_SUFFIX "")
set(CMAKE_C_OUTPUT_EXTENSION ".o")
set(CMAKE_CXX_OUTPUT_EXTENSION ".o")
set(CMAKE_ASM_OUTPUT_EXTENSION ".o")
set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_DL_LIBS "")

# find_* look in <root>/include, <root>/lib and <root>/lib/cmake, where
# <root> is each of CMAKE_FIND_ROOT_PATH (the kit and the compiler's folder).
set(CMAKE_SYSTEM_PREFIX_PATH /)
set(CMAKE_SYSTEM_INCLUDE_PATH /include)
set(CMAKE_SYSTEM_LIBRARY_PATH /lib)
set(CMAKE_SYSTEM_PROGRAM_PATH /bin)
