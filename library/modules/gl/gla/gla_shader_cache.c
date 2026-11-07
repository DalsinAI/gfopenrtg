/* Copyright (c) 2026 AmiCradle Team. OpenGPU's GL module, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * GL.module's shader cache: Mesa's disk_cache calls (util/disk_cache.h),
 * done here, as Mesa is built without its own (-Dshader-cache=disabled,
 * mesa/build.sh; patch 0012 declares the calls anyway).
 *
 * Mesa's state tracker already knows what to keep: once a screen hands it a
 * cache (pipe_screen::get_disk_shader_cache; virgl makes its own with
 * disk_cache_create, gla_core.c gives softpipe one), glCompileShader of a
 * source it has seen is skipped, and glLinkProgram of a program it has seen
 * loads the linked program's metadata and NIR instead of preprocessing,
 * parsing, linking, translating to NIR and optimising on the 68k. What is
 * left on a hit is the driver's part: NIR to TGSI when a variant is made.
 *
 * Mesa's own cache doesn't fit GL.module: it compresses (zlib or zstd, both
 * left out), writes from a thread (a shared GL.module starts none:
 * posix_shim.c), maps its index (mmap), locks with flock, and names files
 * with 64 characters (FFS takes 30). This one is small and synchronous:
 *
 *  - Where: ENV:GFOpenGPU/ShaderCache, a directory, made if missing; "off"
 *    turns the cache off. Unset, SYS:Storage/GFOpenGPU/ShaderCache, kept
 *    across resets; where SYS: can't take it (write-protected),
 *    T:GFOpenGPU/ShaderCache, kept until the machine is reset. Not in
 *    ENVARC:, which the Startup-sequence copies into RAM at every boot. On
 *    the PC (tests), GLA_SHADER_CACHE, and unset is off.
 *  - How much: ENV:GFOpenGPU/ShaderCacheSize in KiB (GLA_SHADER_CACHE_SIZE
 *    on the PC), 4096 unless set, 64 at least. Past it, the oldest entries
 *    go until three quarters is left.
 *  - One file per entry, named by the first 12 bytes of its key in hex (24
 *    characters); the file holds the whole key, its size and a CRC-32, and a
 *    file that doesn't match is deleted and taken as a miss. Written to a
 *    "t" name first and renamed, so a reset mid-write leaves no half entry.
 *  - "keys": the shader sources compiled before (disk_cache_put_key), 32
 *    bytes each, read once per cache and appended to.
 *  - Read-only: a directory that can't be made is no cache; a write that
 *    fails (write-protected, full) stops writes for the rest of the run,
 *    and what is there is still read.
 *  - The key: Mesa's (disk_cache_compute_key) over this cache's version,
 *    GL.module's build (GLA_CACHE_BUILD: Mesa's version and the patches'
 *    sum, from mesa/build.sh; there is no ELF build id to take it from),
 *    the driver, the driver's id (virgl's includes the host's caps) and
 *    the pointer size. A new GL.module never reads an old one's entries. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/disk_cache.h"
#include "util/crc32.h"
#include "util/mesa-blake3.h"

#if __has_include("gla_cache_build.h")
#include "gla_cache_build.h"            /* mesa/build.sh writes it into the build directory */
#endif
#ifndef GLA_CACHE_BUILD
#define GLA_CACHE_BUILD "unknown"
#endif

#define SC_VERSION      1
#define SC_MAGIC        0x474C5343UL    /* "GLSC" */
#define SC_HEAD         (4 + 4 + CACHE_KEY_SIZE + 4 + 4)
#define SC_NAME_BYTES   12              /* of the key, in a file's name */
#define SC_MAX_KEYS     8192            /* "keys" starts again past this */
#define SC_DEFAULT_KIB  4096
#define SC_MIN_KIB      64

struct disk_cache {
    char *dir;                  /* the directory; NULL: keys are made, nothing is kept */
    int readonly;               /* a write failed: reads only from now on */
    unsigned long max_size;     /* bytes */
    long total;                 /* the entries' bytes; -1 until counted */
    uint8_t *keys;              /* the source keys (disk_cache_has_key) */
    unsigned nkeys, capkeys;
    int keys_loaded;
    uint8_t *blob;              /* what every key starts from */
    size_t blob_size;
};

/* ---- the system's part: settings, directories ---- */

#if defined(__amigaos__)
#include <exec/types.h>
#include <exec/nodes.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include "os3/gla_notice_os3.h"

#define SC_DIR_VAR      "GFOpenGPU/ShaderCache"
#define SC_SIZE_VAR     "GFOpenGPU/ShaderCacheSize"
#define SC_DEFAULT_DIR  "SYS:Storage/GFOpenGPU/ShaderCache"
#define SC_FALLBACK_DIR "T:GFOpenGPU/ShaderCache"

static int sc_getenv(const char *name, char *buf, int size)
{
    return gla_os3_getenv(name, buf, size);
}

/* No "Please insert volume" or "write-protected" requester for the cache:
 * a volume or assign that isn't there (no T:, a cache on a disk taken
 * out) is a miss, or no cache. */
static void *sc_quiet(void)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    void *was;
    if (me->pr_Task.tc_Node.ln_Type != NT_PROCESS)
        return NULL;
    was = me->pr_WindowPtr;
    me->pr_WindowPtr = (APTR)-1;
    return was;
}

static void sc_loud(void *was)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    if (me->pr_Task.tc_Node.ln_Type == NT_PROCESS)
        me->pr_WindowPtr = was;
}

/* The directory, and those above it, made where missing; 0 when it is there. */
static int sc_mkdirs(const char *path)
{
    char part[256];
    const char *colon = strchr(path, ':');
    size_t n = strlen(path), i;
    BPTR lock;
    if (n >= sizeof part)
        return -1;
    for (i = colon ? (size_t)(colon - path) + 1 : 0; i <= n; i++) {
        if (i < n && path[i] != '/')
            continue;
        memcpy(part, path, i);
        part[i] = 0;
        if (!part[0] || part[i - 1] == ':')
            continue;
        lock = Lock((CONST_STRPTR)part, SHARED_LOCK);
        if (!lock)
            lock = CreateDir((CONST_STRPTR)part);
        if (!lock)
            return -1;
        UnLock(lock);
    }
    return 0;
}

struct sc_file { char name[32]; unsigned long size, age; };

/* The directory's files (not directories): a malloc'd list, its length in *n. */
static struct sc_file *sc_list(const char *dir, unsigned *n)
{
    struct sc_file *list = NULL, *more;
    unsigned cap = 0;
    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    BPTR lock = fib ? Lock((CONST_STRPTR)dir, SHARED_LOCK) : 0;
    *n = 0;
    if (lock && Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType >= 0 || strlen((char *)fib->fib_FileName) >= sizeof list->name)
                continue;
            if (*n == cap) {
                cap = cap ? cap * 2 : 64;
                more = realloc(list, cap * sizeof *list);
                if (!more)
                    break;
                list = more;
            }
            strcpy(list[*n].name, (char *)fib->fib_FileName);
            list[*n].size = fib->fib_Size;
            list[*n].age = ((unsigned long)fib->fib_Date.ds_Days * 1440 + fib->fib_Date.ds_Minute) * 60
                           + fib->fib_Date.ds_Tick / TICKS_PER_SECOND;
            (*n)++;
        }
    }
    if (lock)
        UnLock(lock);
    if (fib)
        FreeDosObject(DOS_FIB, fib);
    return list;
}

#else   /* the PC: the tests */
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#define SC_DIR_VAR      "GLA_SHADER_CACHE"
#define SC_SIZE_VAR     "GLA_SHADER_CACHE_SIZE"
#define SC_DEFAULT_DIR  NULL
#define SC_FALLBACK_DIR NULL

static void *sc_quiet(void) { return NULL; }
static void sc_loud(void *was) { (void)was; }

static int sc_getenv(const char *name, char *buf, int size)
{
    const char *v = getenv(name);
    if (!v || (int)strlen(v) >= size)
        return -1;
    strcpy(buf, v);
    return (int)strlen(buf);
}

static int sc_mkdirs(const char *path)
{
    char part[256];
    size_t n = strlen(path), i;
    if (n >= sizeof part)
        return -1;
    for (i = 1; i <= n; i++) {
        if (i < n && path[i] != '/')
            continue;
        memcpy(part, path, i);
        part[i] = 0;
        if (mkdir(part, 0777) != 0 && errno != EEXIST)
            return -1;
    }
    return 0;
}

struct sc_file { char name[32]; unsigned long size, age; };

static struct sc_file *sc_list(const char *dir, unsigned *n)
{
    struct sc_file *list = NULL, *more;
    unsigned cap = 0;
    char path[512];
    struct dirent *e;
    struct stat st;
    DIR *d = opendir(dir);
    *n = 0;
    while (d && (e = readdir(d))) {
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (strlen(e->d_name) >= sizeof list->name || stat(path, &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        if (*n == cap) {
            cap = cap ? cap * 2 : 64;
            more = realloc(list, cap * sizeof *list);
            if (!more)
                break;
            list = more;
        }
        strcpy(list[*n].name, e->d_name);
        list[*n].size = (unsigned long)st.st_size;
        list[*n].age = (unsigned long)st.st_mtime;
        (*n)++;
    }
    if (d)
        closedir(d);
    return list;
}
#endif

/* ---- names and words ---- */

static void sc_path(const struct disk_cache *c, const char *name, char *out, size_t size)
{
    size_t n = strlen(c->dir);
    int sep = n && c->dir[n - 1] != ':' && c->dir[n - 1] != '/';
    snprintf(out, size, "%s%s%s", c->dir, sep ? "/" : "", name);
}

/* An entry's name: "t" first for the file being written. */
static void sc_name(const cache_key key, int temp, char *out)
{
    static const char hex[] = "0123456789abcdef";
    int i;
    if (temp)
        *out++ = 't';
    for (i = 0; i < SC_NAME_BYTES; i++) {
        *out++ = hex[key[i] >> 4];
        *out++ = hex[key[i] & 15];
    }
    *out = 0;
}

static int sc_is_entry(const char *name, int temp)
{
    size_t i, n = strlen(name);
    if (temp) {
        if (name[0] != 't')
            return 0;
        name++, n--;
    }
    if (n != SC_NAME_BYTES * 2)
        return 0;
    for (i = 0; i < n; i++)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'a' && name[i] <= 'f')))
            return 0;
    return 1;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* "off" in any case, or "0". */
static int sc_is_off(const char *v)
{
    return ((v[0] | 32) == 'o' && (v[1] | 32) == 'f' && (v[2] | 32) == 'f' && !v[3]) || !strcmp(v, "0");
}

/* ---- the size cap ---- */

static int sc_older(const void *a, const void *b)
{
    const struct sc_file *x = a, *y = b;
    return x->age < y->age ? -1 : x->age > y->age;
}

/* Counts the entries, deletes what a reset left half written, and with
 * room > 0 deletes the oldest entries until that much more fits under
 * three quarters of the cap. */
static void sc_trim(struct disk_cache *c, unsigned long room)
{
    char path[512];
    unsigned n, i;
    struct sc_file *f = sc_list(c->dir, &n);
    unsigned long total = 0, keep = c->max_size / 4 * 3;
    for (i = 0; i < n; i++) {
        if (sc_is_entry(f[i].name, 1)) {
            sc_path(c, f[i].name, path, sizeof path);
            remove(path);
            f[i].size = 0;
        } else if (sc_is_entry(f[i].name, 0)) {
            total += f[i].size;
        } else {
            f[i].size = 0;              /* "keys", or not ours */
        }
    }
    if (room && total + room > c->max_size && n) {
        qsort(f, n, sizeof *f, sc_older);
        for (i = 0; i < n && total + room > keep; i++) {
            if (!f[i].size)
                continue;
            sc_path(c, f[i].name, path, sizeof path);
            if (remove(path) == 0)
                total -= f[i].size;
        }
    }
    free(f);
    c->total = (long)total;
}

/* ---- Mesa's calls ---- */

static struct disk_cache *
sc_create(const char *gpu_name, const char *driver_id,
                         uint64_t driver_flags, const char *cache_dir_name,
                         uint32_t max_size)
{
    static const char what[] = "GFOpenGPU GL.module shader cache";
    char dir[256], size[32];
    const char *where = cache_dir_name, *fallback = NULL;
    struct disk_cache *c;
    uint8_t version = SC_VERSION, ptr = sizeof(void *);
    uint8_t *p;
    size_t n;
    long kib = SC_DEFAULT_KIB;

    if (!where) {
        if (sc_getenv(SC_DIR_VAR, dir, sizeof dir) > 0)
            where = dir;
        else
            where = SC_DEFAULT_DIR, fallback = SC_FALLBACK_DIR;
        if (!where || sc_is_off(where))
            return NULL;
    }
    if (max_size)
        kib = (long)(max_size / 1024);
    else if (sc_getenv(SC_SIZE_VAR, size, sizeof size) > 0)
        kib = strtol(size, NULL, 10);
    if (kib < SC_MIN_KIB)
        kib = SC_MIN_KIB;
    /* A directory that isn't there and can't be made: nothing to read either. */
    if (sc_mkdirs(where) != 0) {
        if (!fallback || sc_mkdirs(fallback) != 0)
            return NULL;
        where = fallback;
    }

    c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->dir = strdup(where);
    c->max_size = (unsigned long)kib * 1024;
    c->total = -1;
    n = sizeof what + 1 + sizeof GLA_CACHE_BUILD + strlen(gpu_name) + 1 + strlen(driver_id) + 1 + 1 + sizeof driver_flags;
    c->blob = malloc(n);
    if (!c->dir || !c->blob) {
        disk_cache_destroy(c);
        return NULL;
    }
    p = c->blob;
    memcpy(p, what, sizeof what), p += sizeof what;
    *p++ = version;
    memcpy(p, GLA_CACHE_BUILD, sizeof GLA_CACHE_BUILD), p += sizeof GLA_CACHE_BUILD;
    memcpy(p, gpu_name, strlen(gpu_name) + 1), p += strlen(gpu_name) + 1;
    memcpy(p, driver_id, strlen(driver_id) + 1), p += strlen(driver_id) + 1;
    *p++ = ptr;
    memcpy(p, &driver_flags, sizeof driver_flags), p += sizeof driver_flags;
    c->blob_size = (size_t)(p - c->blob);
    return c;
}

struct disk_cache *
disk_cache_create(const char *gpu_name, const char *driver_id, uint64_t driver_flags)
{
    return disk_cache_create_custom(gpu_name, driver_id, driver_flags, NULL, 0);
}

void
disk_cache_destroy(struct disk_cache *cache)
{
    if (!cache)
        return;
    free(cache->dir);
    free(cache->keys);
    free(cache->blob);
    free(cache);
}

/* Writes happen in the call: nothing to wait for. */
void
disk_cache_wait_for_idle(struct disk_cache *cache)
{
    (void)cache;
}

static void
sc_remove(struct disk_cache *cache, const cache_key key)
{
    char name[SC_NAME_BYTES * 2 + 2], path[512];
    if (!cache || !cache->dir || cache->readonly)
        return;
    sc_name(key, 0, name);
    sc_path(cache, name, path, sizeof path);
    remove(path);
    cache->total = -1;
}

static void
sc_put(struct disk_cache *cache, const cache_key key,
               const void *data, size_t size,
               struct cache_item_metadata *cache_item_metadata)
{
    char name[SC_NAME_BYTES * 2 + 2], temp[SC_NAME_BYTES * 2 + 2];
    char path[512], tpath[512];
    uint8_t head[SC_HEAD];
    FILE *f;
    int ok;
    (void)cache_item_metadata;

    if (!cache || !cache->dir || cache->readonly || !size || size > cache->max_size / 2)
        return;
    if (cache->total < 0 || (unsigned long)cache->total + SC_HEAD + size > cache->max_size)
        sc_trim(cache, SC_HEAD + size);

    sc_name(key, 0, name);
    sc_name(key, 1, temp);
    sc_path(cache, name, path, sizeof path);
    sc_path(cache, temp, tpath, sizeof tpath);
    put32(head, SC_MAGIC);
    put32(head + 4, SC_VERSION);
    memcpy(head + 8, key, CACHE_KEY_SIZE);
    put32(head + 8 + CACHE_KEY_SIZE, (uint32_t)size);
    put32(head + 12 + CACHE_KEY_SIZE, util_hash_crc32(data, size));

    f = fopen(tpath, "wb");
    if (!f) {
        cache->readonly = 1;            /* write-protected, or no room for a file */
        return;
    }
    ok = fwrite(head, 1, sizeof head, f) == sizeof head && fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(path);                   /* AmigaDOS's Rename doesn't replace */
        ok = rename(tpath, path) == 0;
    }
    if (!ok) {
        remove(tpath);
        cache->readonly = 1;            /* full: what is there is still read */
        return;
    }
    cache->total += (long)(SC_HEAD + size);
}

void
disk_cache_put_nocopy(struct disk_cache *cache, const cache_key key,
                      void *data, size_t size,
                      struct cache_item_metadata *cache_item_metadata)
{
    disk_cache_put(cache, key, data, size, cache_item_metadata);
    free(data);
}

static void *
sc_get(struct disk_cache *cache, const cache_key key, size_t *size)
{
    char name[SC_NAME_BYTES * 2 + 2], path[512];
    uint8_t head[SC_HEAD];
    uint8_t *data = NULL;
    uint32_t n = 0;
    FILE *f;
    int ok;

    if (size)
        *size = 0;
    if (!cache || !cache->dir)
        return NULL;
    sc_name(key, 0, name);
    sc_path(cache, name, path, sizeof path);
    f = fopen(path, "rb");
    if (!f)
        return NULL;
    ok = fread(head, 1, sizeof head, f) == sizeof head;
    if (ok)
        n = get32(head + 8 + CACHE_KEY_SIZE);
    /* Another key with the same first 12 bytes is a miss, not a fault. */
    if (ok && get32(head) == SC_MAGIC && get32(head + 4) == SC_VERSION &&
        memcmp(head + 8, key, CACHE_KEY_SIZE) != 0) {
        fclose(f);
        return NULL;
    }
    ok = ok && get32(head) == SC_MAGIC && get32(head + 4) == SC_VERSION &&
         n && n <= cache->max_size && (data = malloc(n)) != NULL;
    ok = ok && fread(data, 1, n, f) == n && util_hash_crc32(data, n) == get32(head + 12 + CACHE_KEY_SIZE);
    fclose(f);
    if (!ok) {
        /* Short, damaged or of another version: gone, and compiled again. */
        free(data);
        if (!cache->readonly)
            remove(path);
        cache->total = -1;
        return NULL;
    }
    if (size)
        *size = n;
    return data;
}

static void sc_load_keys(struct disk_cache *c)
{
    char path[512];
    long len;
    FILE *f;
    c->keys_loaded = 1;
    sc_path(c, "keys", path, sizeof path);
    f = fopen(path, "rb");
    if (!f)
        return;
    if (fseek(f, 0, SEEK_END) == 0 && (len = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0) {
        unsigned n = (unsigned)(len / CACHE_KEY_SIZE);
        if (n > SC_MAX_KEYS)
            n = SC_MAX_KEYS;
        c->keys = malloc((size_t)n * CACHE_KEY_SIZE + 1);
        if (c->keys) {
            c->capkeys = n;
            c->nkeys = (unsigned)(fread(c->keys, CACHE_KEY_SIZE, n, f));
        }
    }
    fclose(f);
}

static bool
sc_has_key(struct disk_cache *cache, const cache_key key)
{
    unsigned i;
    if (!cache || !cache->dir)
        return false;
    if (!cache->keys_loaded)
        sc_load_keys(cache);
    for (i = 0; i < cache->nkeys; i++)
        if (!memcmp(cache->keys + (size_t)i * CACHE_KEY_SIZE, key, CACHE_KEY_SIZE))
            return true;
    return false;
}

static void
sc_put_key(struct disk_cache *cache, const cache_key key)
{
    char path[512];
    const char *mode = "ab";
    FILE *f;
    if (!cache || !cache->dir || cache->readonly || sc_has_key(cache, key))
        return;
    if (cache->nkeys >= SC_MAX_KEYS) {
        cache->nkeys = 0;               /* start again: a key not found only means a compile */
        mode = "wb";
    }
    if (cache->nkeys == cache->capkeys) {
        unsigned cap = cache->capkeys ? cache->capkeys * 2 : 64;
        uint8_t *more;
        if (cap > SC_MAX_KEYS)
            cap = SC_MAX_KEYS;
        more = realloc(cache->keys, (size_t)cap * CACHE_KEY_SIZE);
        if (!more)
            return;
        cache->keys = more;
        cache->capkeys = cap;
    }
    memcpy(cache->keys + (size_t)cache->nkeys++ * CACHE_KEY_SIZE, key, CACHE_KEY_SIZE);
    sc_path(cache, "keys", path, sizeof path);
    f = fopen(path, mode);
    if (!f || fwrite(key, 1, CACHE_KEY_SIZE, f) != CACHE_KEY_SIZE)
        cache->readonly = 1;
    if (f && fclose(f) != 0)
        cache->readonly = 1;
}

void
disk_cache_compute_key(struct disk_cache *cache, const void *data, size_t size,
                       cache_key key)
{
    blake3_hasher ctx;
    _mesa_blake3_init(&ctx);
    if (cache)
        _mesa_blake3_update(&ctx, cache->blob, cache->blob_size);
    _mesa_blake3_update(&ctx, data, size);
    _mesa_blake3_final(&ctx, key);
}

/* Android's blob cache: there is none here. */
void
disk_cache_set_callbacks(struct disk_cache *cache, disk_cache_put_cb put,
                         disk_cache_get_cb get)
{
    (void)cache, (void)put, (void)get;
}

/* Mesa's calls that touch the disk, each with requesters off (sc_quiet). */
struct disk_cache *
disk_cache_create_custom(const char *gpu_name, const char *driver_id,
                         uint64_t driver_flags, const char *cache_dir_name,
                         uint32_t max_size)
{
    void *was = sc_quiet();
    struct disk_cache *c = sc_create(gpu_name, driver_id, driver_flags, cache_dir_name, max_size);
    sc_loud(was);
    return c;
}

void
disk_cache_remove(struct disk_cache *cache, const cache_key key)
{
    void *was = sc_quiet();
    sc_remove(cache, key);
    sc_loud(was);
}

void
disk_cache_put(struct disk_cache *cache, const cache_key key,
               const void *data, size_t size,
               struct cache_item_metadata *cache_item_metadata)
{
    void *was = sc_quiet();
    sc_put(cache, key, data, size, cache_item_metadata);
    sc_loud(was);
}

void *
disk_cache_get(struct disk_cache *cache, const cache_key key, size_t *size)
{
    void *was = sc_quiet();
    void *data = sc_get(cache, key, size);
    sc_loud(was);
    return data;
}

bool
disk_cache_has_key(struct disk_cache *cache, const cache_key key)
{
    void *was = sc_quiet();
    bool has = sc_has_key(cache, key);
    sc_loud(was);
    return has;
}

void
disk_cache_put_key(struct disk_cache *cache, const cache_key key)
{
    void *was = sc_quiet();
    sc_put_key(cache, key);
    sc_loud(was);
}
