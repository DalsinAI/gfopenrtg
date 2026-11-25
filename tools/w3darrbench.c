/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * W3DArrBench: DrawElements and DrawArray triangles a second, for timing
 * Warp3D.library's array path (one build against another, or a runtime with
 * and without its host function). A 48x48 grid of small triangles (4608),
 * float positions and texture coordinates, UBYTE ARGB colours, drawn into an
 * ARGB32 W3D_Bitmap in memory (no screen), as indexed triangles (UWORD) and
 * as strips, for SECONDS n (default 10):
 *   W3DArrBench [CPU|GPU] [SECONDS n] [CULL] [SMALL]
 * CPU asks for W3D_DRIVER_CPU, GPU (the default) for W3D_DRIVER_3DHW, so the
 * drawing itself is the host's and the time is mostly the vertices. CULL
 * turns back-face culling on (half the grid faces away). SMALL is a 68k
 * game's kind of frame instead: textured, 576 draws of one quad each
 * (every fourth quad of the grid), with the 32x32 texture changed and
 * W3D_UpdateTexImage'd every 64 draws (9 a frame). Prints the triangles
 * drawn, the time and triangles a second (and, for SMALL, draws a second). */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/Warp3D.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Library *Warp3DBase;

#define BW 160
#define BH 120
#define G 48                                /* quads a side */
#define NV ((G + 1) * (G + 1))
#define NT (G * G * 2)

struct av { float x, y, z, u, v; ULONG argb; };     /* 24 bytes */

static long ticks(void)
{
    struct DateStamp d;
    DateStamp(&d);
    return (d.ds_Days % 1000) * 4320000L + d.ds_Minute * 3000L + d.ds_Tick;
}

int main(int argc, char **argv)
{
    static ULONG pix[BW * BH];
    static struct av vs[NV];
    static UWORD ix[NT * 3];
    static UWORD strip[(G + 1) * 2];
    static ULONG tex[32 * 32];
    W3D_Texture *t = 0;
    W3D_Bitmap bm;
    W3D_Context *c;
    W3D_Scissor sc = { 0, 0, BW, BH };
    ULONG err = 0, type = W3D_DRIVER_3DHW;
    long secs = 10, t0, t1 = 0, frames = 0, tris = 0;
    long draws = 0;
    int cull = 0, small = 0, i, x, y, k = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "CPU")) type = W3D_DRIVER_CPU;
        else if (!strcmp(argv[i], "GPU")) type = W3D_DRIVER_3DHW;
        else if (!strcmp(argv[i], "CULL")) cull = 1;
        else if (!strcmp(argv[i], "SMALL")) small = 1;
        else if (!strcmp(argv[i], "SECONDS") && i + 1 < argc) secs = atol(argv[++i]);
    }
    if (secs < 1) secs = 1;
    if (!(Warp3DBase = OpenLibrary("Warp3D.library", 4))) { printf("no Warp3D.library 4\n"); return 20; }
    for (y = 0; y <= G; y++)
        for (x = 0; x <= G; x++) {
            struct av *v = &vs[y * (G + 1) + x];
            v->x = 2.0f + x * (BW - 4) / (float)G; v->y = 2.0f + y * (BH - 4) / (float)G;
            v->z = 0.25f + 0.5f * (float)(x + y) / (2 * G);
            v->u = x / (float)G; v->v = y / (float)G;
            v->argb = 0xFF000000UL | ((ULONG)(x * 5) << 16) | ((ULONG)(y * 5) << 8) | 0x40;
        }
    for (y = 0; y < G; y++)
        for (x = 0; x < G; x++) {
            UWORD a = (UWORD)(y * (G + 1) + x), b = (UWORD)(a + 1), d = (UWORD)(a + G + 1), e = (UWORD)(d + 1);
            if ((x + y) & 1) { ix[k++] = a; ix[k++] = b; ix[k++] = d; ix[k++] = b; ix[k++] = e; ix[k++] = d; }
            else { ix[k++] = a; ix[k++] = d; ix[k++] = b; ix[k++] = b; ix[k++] = d; ix[k++] = e; }  /* the other way round */
        }
    memset(pix, 0, sizeof pix);
    bm.bprow = BW * 4; bm.width = BW; bm.height = BH; bm.format = W3D_FMT_A8R8G8B8; bm.dest = pix;
    c = W3D_CreateContextTags(&err, W3D_CC_BITMAP, (ULONG)&bm, W3D_CC_W3DBM, TRUE, W3D_CC_DRIVERTYPE, type, TAG_DONE);
    if (!c || err != W3D_SUCCESS) { printf("W3D_CreateContext failed (%ld)\n", (long)err); CloseLibrary(Warp3DBase); return 20; }
    W3D_SetDrawRegionWBM(c, &bm, &sc);
    W3D_SetState(c, W3D_GOURAUD, W3D_ENABLE);
    W3D_SetState(c, W3D_CULLFACE, cull ? W3D_ENABLE : W3D_DISABLE);
    W3D_VertexPointer(c, &vs[0].x, sizeof vs[0], W3D_VERTEX_F_F_F, 0);
    W3D_TexCoordPointer(c, &vs[0].u, sizeof vs[0], 0, 4, 0, 0);
    W3D_ColorPointer(c, &vs[0].argb, sizeof vs[0], W3D_COLOR_UBYTE, W3D_CMODE_ARGB, 0);
    if (small) {
        for (i = 0; i < 32 * 32; i++) tex[i] = 0xFF000000UL | ((ULONG)(i & 31) << 19) | ((ULONG)(i >> 5) << 11) | 0x80;
        t = W3D_AllocTexObjTags(c, &err, W3D_ATO_IMAGE, (ULONG)tex, W3D_ATO_FORMAT, W3D_A8R8G8B8, W3D_ATO_WIDTH, 32,
                                W3D_ATO_HEIGHT, 32, TAG_DONE);
        if (!t || err != W3D_SUCCESS) { printf("W3D_AllocTexObj failed (%ld)\n", (long)err); W3D_DestroyContext(c); CloseLibrary(Warp3DBase); return 20; }
        W3D_SetTexEnv(c, t, W3D_MODULATE, 0);
        W3D_BindTexture(c, 0, t);
        W3D_SetState(c, W3D_TEXMAPPING, W3D_ENABLE);
        printf("W3DArrBench: %s, SMALL: 576 draws of 2 textured triangles a frame, 9 texture updates, %ld s%s\n",
               type == W3D_DRIVER_CPU ? "CPU" : "GPU", secs, cull ? ", culling" : "");
    } else
        printf("W3DArrBench: %s, %d triangles a frame (DrawElements) + %d (strips), %ld s%s\n",
               type == W3D_DRIVER_CPU ? "CPU" : "GPU", NT, NT, secs, cull ? ", culling" : "");
    t0 = ticks();
    do {
        if (W3D_LockHardware(c) != W3D_SUCCESS) { printf("W3D_LockHardware failed\n"); break; }
        if (small) {
            for (i = 0; i < 576; i++) {
                if (!(i & 63)) {
                    tex[(frames * 9 + (i >> 6)) & 1023] ^= 0x00FFFFFFUL;
                    W3D_UpdateTexImage(c, t, tex, 0, 0);
                }
                W3D_DrawElements(c, W3D_PRIMITIVE_TRIANGLES, W3D_INDEX_UWORD, 6, ix + (long)i * 4 * 6);
            }
            draws += 576;
            tris += 2L * 576;
        } else {
            W3D_DrawElements(c, W3D_PRIMITIVE_TRIANGLES, W3D_INDEX_UWORD, NT * 3, ix);
            for (y = 0; y < G; y++) {
                for (x = 0; x <= G; x++) { strip[2 * x] = (UWORD)(y * (G + 1) + x); strip[2 * x + 1] = (UWORD)((y + 1) * (G + 1) + x); }
                W3D_DrawElements(c, W3D_PRIMITIVE_TRISTRIP, W3D_INDEX_UWORD, (G + 1) * 2, strip);
            }
            tris += 2L * NT;
        }
        W3D_UnLockHardware(c);
        W3D_FlushFrame(c);
        W3D_WaitIdle(c);
        frames++;
        t1 = ticks();
    } while (t1 - t0 < secs * 50L);
    if (t1 > t0) {
        printf("  %ld frames, %ld triangles in %ld.%02ld s: %ld triangles a second\n", frames, tris,
               (t1 - t0) / 50, ((t1 - t0) % 50) * 2, (long)((double)tris * 50.0 / (double)(t1 - t0)));
        if (small) printf("  %ld draws a second, %ld frames a second\n", (long)((double)draws * 50.0 / (double)(t1 - t0)),
                          (long)((double)frames * 50.0 / (double)(t1 - t0)));
    }
    if (t) W3D_FreeTexObj(c, t);
    W3D_DestroyContext(c);
    CloseLibrary(Warp3DBase);
    return 0;
}
