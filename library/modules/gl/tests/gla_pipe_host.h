/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * The shared pipe's files (library/pipe) in the GLA core's test on a PC or under qemu (mesa/build.sh includes this
 * ahead of each): the host there knows memory by an address of the test's arena, not by the pointer, which may be
 * 64 bits wide. On the Amiga an address is the pointer and this file isn't used. */
#ifndef GLA_PIPE_HOST_H
#define GLA_PIPE_HOST_H
unsigned int gla_pipe_host_addr(const void *p);
#define ADDR(p) gla_pipe_host_addr(p)
#endif
