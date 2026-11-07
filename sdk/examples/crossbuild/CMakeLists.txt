# GLTri with CMake: SDL 2 from find_package, GLES 2 from pkg-config (glesv2.pc).
cmake_minimum_required(VERSION 3.16)
project(GLTri C)

find_package(SDL2 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLES2 REQUIRED IMPORTED_TARGET glesv2)

add_executable(GLTri gltri.c)
target_link_libraries(GLTri PRIVATE SDL2::SDL2main SDL2::SDL2 PkgConfig::GLES2)
if(AMIGAOS)
    target_compile_definitions(GLTri PRIVATE GLTRI_AMIGAOS)
endif()
