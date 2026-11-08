/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GL.module's shader cache (gla/gla_shader_cache.c) on this machine, without
 * building Mesa: tests/shader_cache.sh compiles it with Mesa's BLAKE3 and
 * CRC-32 from the source tree. Entries come back as they went in, across a
 * new cache; keys depend on the driver; a damaged entry is a miss and is
 * deleted; the size cap holds; a cache that can't write still reads; "off"
 * and a directory that can't be made are no cache. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "util/disk_cache.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static char dir[256];

static struct disk_cache *open_cache(const char *where, const char *kib)
{
    if (where) setenv("GLA_SHADER_CACHE", where, 1); else unsetenv("GLA_SHADER_CACHE");
    if (kib) setenv("GLA_SHADER_CACHE_SIZE", kib, 1); else unsetenv("GLA_SHADER_CACHE_SIZE");
    return disk_cache_create("softpipe", "gla", 0);
}

/* The bytes of the entries (24 hex characters), and how many. */
static long entries(int *count)
{
    char path[512];
    struct dirent *e;
    struct stat st;
    long total = 0;
    DIR *d = opendir(dir);
    *count = 0;
    while (d && (e = readdir(d))) {
        if (strlen(e->d_name) != 24)
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (stat(path, &st) == 0) {
            total += (long)st.st_size;
            (*count)++;
        }
    }
    if (d) closedir(d);
    return total;
}

static void entry_path(const cache_key key, char *out, size_t size)
{
    int i, n = snprintf(out, size, "%s/", dir);
    for (i = 0; i < 12; i++)
        n += snprintf(out + n, size - n, "%02x", key[i]);
}

int main(void)
{
    struct disk_cache *c, *v;
    cache_key k1, k2, k3, kv, src;
    char data[3000], path[512];
    size_t size;
    void *got;
    int i, count;
    FILE *f;

    snprintf(dir, sizeof dir, "%s/gla-shader-cache-%d/a/b", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", (int)getpid());
    for (i = 0; i < (int)sizeof data; i++)
        data[i] = (char)(i * 7);

    /* Off: unset on the PC, "off", "OFF", "0". */
    CHECK(open_cache(NULL, NULL) == NULL);
    CHECK(open_cache("off", NULL) == NULL);
    CHECK(open_cache("OFF", NULL) == NULL);
    CHECK(open_cache("0", NULL) == NULL);
    /* A directory that can't be made. */
    CHECK(open_cache("/proc/gla-no-such/cache", NULL) == NULL);

    /* Made with its parents; an entry back as it went in. */
    c = open_cache(dir, NULL);
    CHECK(c != NULL);
    if (!c) return 1;
    disk_cache_compute_key(c, "program one", 11, k1);
    disk_cache_compute_key(c, "program two", 11, k2);
    CHECK(memcmp(k1, k2, sizeof k1) != 0);
    CHECK(disk_cache_get(c, k1, &size) == NULL && size == 0);
    disk_cache_put(c, k1, data, sizeof data, NULL);
    got = disk_cache_get(c, k1, &size);
    CHECK(got && size == sizeof data && !memcmp(got, data, size));
    free(got);
    CHECK(disk_cache_get(c, k2, &size) == NULL);

    /* Source keys. */
    disk_cache_compute_key(c, "void main(){}", 13, src);
    CHECK(!disk_cache_has_key(c, src));
    disk_cache_put_key(c, src);
    CHECK(disk_cache_has_key(c, src));
    disk_cache_destroy(c);

    /* A new cache (the next run) has both. */
    c = open_cache(dir, NULL);
    CHECK(disk_cache_has_key(c, src));
    got = disk_cache_get(c, k1, &size);
    CHECK(got && size == sizeof data && !memcmp(got, data, size));
    free(got);

    /* Keys are the driver's: virgl's for the same program differ. */
    v = disk_cache_create("virgl", "caps-of-this-host", 0);
    disk_cache_compute_key(v, "program one", 11, kv);
    CHECK(memcmp(k1, kv, sizeof k1) != 0);
    CHECK(disk_cache_get(v, kv, &size) == NULL);
    disk_cache_destroy(v);
    /* NULL (no cache) still makes a key: ARB_get_program_binary's. */
    disk_cache_compute_key(NULL, "x", 1, kv);

    /* A damaged entry is a miss, and is gone. */
    entry_path(k1, path, sizeof path);
    f = fopen(path, "r+b");
    CHECK(f != NULL);
    if (f) { fseek(f, 100, SEEK_SET); fputc(0x55 ^ data[100 - 48], f); fclose(f); }
    CHECK(disk_cache_get(c, k1, &size) == NULL);
    CHECK(access(path, F_OK) != 0);
    /* So is a short one. */
    disk_cache_put(c, k1, data, sizeof data, NULL);
    CHECK(truncate(path, 60) == 0);
    CHECK(disk_cache_get(c, k1, &size) == NULL);

    /* Removed. */
    disk_cache_put(c, k2, data, 100, NULL);
    disk_cache_remove(c, k2);
    CHECK(disk_cache_get(c, k2, &size) == NULL);

    /* put_nocopy takes the buffer. */
    got = malloc(50);
    memcpy(got, data, 50);
    disk_cache_put_nocopy(c, k2, got, 50, NULL);
    got = disk_cache_get(c, k2, &size);
    CHECK(got && size == 50 && !memcmp(got, data, 50));
    free(got);
    disk_cache_destroy(c);

    /* The cap: 64 KiB, 40 entries of 3 KB. What a reset left half written goes too. */
    snprintf(path, sizeof path, "%s/t0123456789abcdef01234567", dir);
    f = fopen(path, "wb");
    if (f) { fputs("half", f); fclose(f); }
    c = open_cache(dir, "64");
    for (i = 0; i < 40; i++) {
        char name[16];
        snprintf(name, sizeof name, "p%d", i);
        disk_cache_compute_key(c, name, strlen(name), k3);
        disk_cache_put(c, k3, data, sizeof data, NULL);
        CHECK(entries(&count) <= 64 * 1024);
    }
    got = disk_cache_get(c, k3, &size);         /* the newest is kept */
    CHECK(got && size == sizeof data);
    free(got);
    entries(&count);
    CHECK(count >= 10 && count < 40);
    CHECK(access(path, F_OK) != 0);
    /* Bigger than half the cap: not kept. */
    {
        char *big = calloc(1, 40 * 1024);
        disk_cache_compute_key(c, "big", 3, kv);
        disk_cache_put(c, kv, big, 40 * 1024, NULL);
        CHECK(disk_cache_get(c, kv, &size) == NULL);
        free(big);
    }
    disk_cache_destroy(c);

    /* Read-only: what is there is read, nothing is written, nothing fails. */
    if (geteuid() != 0) {
        c = open_cache(dir, "64");
        CHECK(chmod(dir, 0555) == 0);
        disk_cache_compute_key(c, "new", 3, kv);
        disk_cache_put(c, kv, data, 10, NULL);
        CHECK(disk_cache_get(c, kv, &size) == NULL);
        disk_cache_compute_key(c, "new source", 10, kv);
        disk_cache_put_key(c, kv);              /* "keys" can't be written */
        got = disk_cache_get(c, k3, &size);
        CHECK(got && size == sizeof data);
        free(got);
        disk_cache_destroy(c);
        chmod(dir, 0755);
    }

    printf(fails ? "shader cache: %d failed\n" : "shader cache: all passed\n", fails);
    return fails != 0;
}
