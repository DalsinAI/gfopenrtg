# GLFW 3.4 on GFOpenGPU (AmigaOS 3) for CMake: find_package(glfw3) gives the
# target glfw (as GLFW's own package does), linked with libGL.a (GLA).
# GFOpenGPU developer kit; the kit is three levels up (lib/cmake/glfw3).
get_filename_component(_ogpu_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(glfw3_VERSION "3.4.0")
set(glfw3_FOUND TRUE)
set(GLFW3_INCLUDE_DIR "${_ogpu_prefix}/include")
set(GLFW3_LIBRARY "${_ogpu_prefix}/lib/libglfw3.a")
if(NOT TARGET glfw)
    add_library(glfw STATIC IMPORTED)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${_ogpu_prefix}/lib/libglfw3.a"
        INTERFACE_INCLUDE_DIRECTORIES "${_ogpu_prefix}/include"
        INTERFACE_COMPILE_OPTIONS "-noixemul;-m68040;-m68881"
        INTERFACE_LINK_OPTIONS "-noixemul;-m68040;-m68881"
        INTERFACE_LINK_LIBRARIES "${_ogpu_prefix}/lib/libGL.a;m")
endif()
if(NOT TARGET glfw3::glfw)
    add_library(glfw3::glfw INTERFACE IMPORTED)
    set_target_properties(glfw3::glfw PROPERTIES INTERFACE_LINK_LIBRARIES glfw)
endif()
