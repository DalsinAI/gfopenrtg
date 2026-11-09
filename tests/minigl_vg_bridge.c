/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * The host side of minigl.library's virgl route for its PC test. The
 * library's test is built 32-bit (its pointers are ULONGs, as on the
 * Amiga); virglrenderer is a 64-bit library. So this program, 64-bit, runs
 * the test as its child and carries out each request block the test sends
 * down a pipe with acvirgl_run (host/virgl/acvirgl.c, as the Cradle's
 * runtime does): the child's memory stands for the Amiga's, read and
 * written with process_vm_readv/process_vm_writev (a parent may, under
 * Yama's ptrace scope 1).
 *   minigl_vg_bridge PROGRAM [ARGS...]
 * ACVIRGL_LIB names libvirglrenderer.so.1. The child finds the pipe's
 * descriptors in MGL_VG_BRIDGE ("read,write"). Exits as the child does. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include "../host/virgl/acvirgl.h"

struct shadow { uint32_t addr, len; uint8_t *buf; };
static struct shadow sh[4096];
static int nsh, bad;
static pid_t child;

static uint8_t *map(void *user, uint32_t addr, uint32_t len)
{
    struct iovec l, r;
    int i;
    (void)user;
    for (i = 0; i < nsh; i++)
        if (addr >= sh[i].addr && (uint64_t)addr + len <= (uint64_t)sh[i].addr + sh[i].len) return sh[i].buf + (addr - sh[i].addr);
    if (nsh == (int)(sizeof sh / sizeof sh[0])) return NULL;
    sh[nsh].buf = malloc(len ? len : 4);
    if (!sh[nsh].buf) return NULL;
    l.iov_base = sh[nsh].buf; l.iov_len = len;
    r.iov_base = (void *)(uintptr_t)addr; r.iov_len = len;
    if (len && process_vm_readv(child, &l, 1, &r, 1, 0) != (ssize_t)len) {
        free(sh[nsh].buf);
        bad++;
        return NULL;
    }
    sh[nsh].addr = addr; sh[nsh].len = len;
    return sh[nsh++].buf;
}

static void write_back(void)
{
    int i;
    for (i = 0; i < nsh; i++) {
        struct iovec l, r;
        l.iov_base = sh[i].buf; l.iov_len = sh[i].len;
        r.iov_base = (void *)(uintptr_t)sh[i].addr; r.iov_len = sh[i].len;
        if (sh[i].len && process_vm_writev(child, &l, 1, &r, 1, 0) != (ssize_t)sh[i].len) bad++;
        free(sh[i].buf);
    }
    nsh = 0;
}

static int full_read(int fd, void *p, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)p + got, n - got);
        if (r <= 0) return 0;
        got += (size_t)r;
    }
    return 1;
}

int main(int argc, char **argv)
{
    char err[256], env[64];
    struct acvirgl *v;
    struct acvirgl_stats st;
    int down[2], up[2], status = 0;
    uint32_t req[2];
    if (argc < 2) { fprintf(stderr, "usage: minigl_vg_bridge PROGRAM [ARGS...]\n"); return 2; }
    if (!(v = acvirgl_create(NULL, err, sizeof err))) { printf("minigl_vg_bridge: skipped (%s)\n", err); return 0; }
    printf("minigl_vg_bridge: virglrenderer draws with %s on %s\n", acvirgl_api(v), acvirgl_renderer(v));
    fflush(stdout);
    if (pipe(down) || pipe(up)) { perror("pipe"); return 2; }
    child = fork();
    if (child < 0) { perror("fork"); return 2; }
    if (!child) {
        close(down[0]); close(up[1]);
        snprintf(env, sizeof env, "%d,%d", up[0], down[1]);
        setenv("MGL_VG_BRIDGE", env, 1);
        execvp(argv[1], argv + 1);
        perror("exec");
        _exit(127);
    }
    close(down[1]); close(up[0]);
    while (full_read(down[0], req, sizeof req)) {
        int32_t r = acvirgl_run(v, map, NULL, req[0], req[1]);
        write_back();
        if (bad) { r = -100; bad = 0; }
        if (write(up[1], &r, sizeof r) != sizeof r) break;
    }
    waitpid(child, &status, 0);
    acvirgl_stats(v, &st);
    printf("minigl_vg_bridge: %llu blocks, %llu submits (%llu words), %llu puts (%llu bytes), %llu gets, %llu presents, %llu errors\n",
           (unsigned long long)st.blocks, (unsigned long long)st.submits, (unsigned long long)st.submit_words,
           (unsigned long long)st.puts, (unsigned long long)st.put_bytes, (unsigned long long)st.gets,
           (unsigned long long)st.presents, (unsigned long long)st.errors);
    acvirgl_destroy(v);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 3;
}
