/* Copyright (c) 2026 AmiCradle Team. OpenGPU developer kit, MIT licence.
 * SPDX-License-Identifier: MIT
 *
 * GLTri: a GLES 2 triangle through SDL 2 (an ES context from SDL_GL), built
 * with CMake or meson and the kit's cross files (cross/). Esc or the close
 * gadget quits.
 *   GLTri [SECONDS n]
 * Build (in this folder):
 *   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=../../cross/m68k-amigaos.cmake && cmake --build build
 *   meson setup build-meson --cross-file ../../cross/m68k-amigaos.ini && meson compile -C build-meson
 */
#include "SDL.h"
#include <GLES2/gl2.h>
#include <stdio.h>
#include <stdlib.h>

#if !defined(__AMIGA__) || !defined(GLTRI_AMIGAOS)
#error "built without the AmigaOS cross file"
#endif

static const char *vs_src =
    "attribute vec2 pos; attribute vec3 col; varying vec3 v;\n"
    "void main() { v = col; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *fs_src =
    "precision mediump float; varying vec3 v;\n"
    "void main() { gl_FragColor = vec4(v, 1.0); }\n";

static GLuint shader(GLenum kind, const char *src)
{
    GLuint s = glCreateShader(kind);
    GLint ok = 0;
    char log[256];
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "GLTri: shader: %s\n", log);
    }
    return s;
}

int main(int argc, char *argv[])
{
    static const GLfloat tri[] = { 0, .8f, 1, .2f, .2f,  -.8f, -.7f, .2f, 1, .2f,  .8f, -.7f, .2f, .2f, 1 };
    Uint32 end = 0;
    int quit = 0;
    SDL_Window *win;
    SDL_GLContext ctx;
    GLuint prog;

    if (argc == 3 && SDL_strcasecmp(argv[1], "SECONDS") == 0)
        end = (Uint32)atoi(argv[2]) * 1000;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "GLTri: %s\n", SDL_GetError());
        return 20;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    win = SDL_CreateWindow("GLTri", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 320, 240, SDL_WINDOW_OPENGL);
    ctx = win ? SDL_GL_CreateContext(win) : NULL;
    if (!ctx) {
        fprintf(stderr, "GLTri: %s\n", SDL_GetError());
        SDL_Quit();
        return 20;
    }
    printf("GLTri: %s, %s\n", (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_RENDERER));
    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, vs_src));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, fs_src));
    glBindAttribLocation(prog, 0, "pos");
    glBindAttribLocation(prog, 1, "col");
    glLinkProgram(prog);
    glUseProgram(prog);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), tri + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    if (end)
        end += SDL_GetTicks();
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE))
                quit = 1;
        glClearColor(.1f, .1f, .15f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        SDL_GL_SwapWindow(win);
        if (end && SDL_TICKS_PASSED(SDL_GetTicks(), end))
            quit = 1;
    }
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
