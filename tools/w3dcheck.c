/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * W3DCheck: every one of Warp3D.library's 92 calls, on each driver, into an
 * ARGB32 W3D_Bitmap in memory (no screen needed), with what each should
 * answer; then the same small scene from every driver, which must come out
 * the same. The virgl driver (ENV:Warp3D/Driver virgl) draws on a real GPU,
 * so its scene is held to the route's tolerance instead (8 of 255 a channel,
 * a pixel's shift allowed at edges, at most 2% of pixels beyond it). Run on an Amiga with GFOpenRTG's Warp3D.library:
 *   W3DCheck            prints one line a call that fails, and a summary
 * With ENV:W3DCheck/Dump set to a file prefix it also writes both scenes, raw
 * ARGB 64x48, to <prefix>.cpu and <prefix>.drv; ENV:W3DCheck/NoZ draws the
 * scene without the Z buffer. */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/modeid.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/Warp3D.h>
#include <stdio.h>
#include <string.h>

struct Library *Warp3DBase;
static int fails, checks;

#define CHECK(cond, what) do { checks++; if (!(cond)) { fails++; printf("FAIL %s: %s (line %d)\n", drvname, what, __LINE__); } } while (0)

#define BW 64
#define BH 48
static const char *drvname = "";

static void vtx(W3D_Vertex *v, float x, float y, float z, float u, float tv, float r, float g, float b, float a)
{
    memset(v, 0, sizeof *v);
    v->x = x; v->y = y; v->z = z; v->w = 1.0f / (1.0f + z * 4.0f); v->u = u; v->v = tv;
    v->color.r = r; v->color.g = g; v->color.b = b; v->color.a = a;
}

static ULONG sum(const ULONG *p, int n)
{
    ULONG h = 2166136261UL;
    const UBYTE *b = (const UBYTE *)p;
    int i;
    for (i = 0; i < n * 4; i++) h = (h ^ b[i]) * 16777619UL;
    return h;
}

static ULONG pix[BW * BH], scene[2][BW * BH];

/* Every call once, on one driver. Returns the scene's checksum, its pixels in scene[out]. */
static ULONG run(ULONG type, int out)
{
    static ULONG tex[16 * 16];
    static UBYTE stip[128];
    W3D_Bitmap bm;
    W3D_Context *c;
    W3D_Texture *t, *t2;
    ULONG err = 0, r, st, h = 0;
    W3D_Double z = 0.5, zs[4];
    W3D_Color col = { 1.0f, 0.5f, 0.25f, 1.0f };
    W3D_Float ref = 0.5f;
    W3D_Scissor sc = { 0, 0, BW, BH };
    W3D_Fog fog = { 0.9f, 0.1f, 1.0f, { 0.1f, 0.2f, 0.3f } };
    W3D_Vertex v[6];
    W3D_Triangle tri;
    W3D_TriangleV triv;
    W3D_Triangles tris;
    W3D_TrianglesV trisv;
    W3D_Vertex *vp[4];
    W3D_Point pt;
    W3D_Line ln;
    W3D_Lines lns;
    float arr[4 * 5];
    UWORD idx[3] = { 0, 1, 2 };
    ULONG sv[4] = { 1, 2, 3, 4 };
    int i;
    for (i = 0; i < 16 * 16; i++) tex[i] = 0xFF000000UL | ((i & 15) * 0x110000UL) | ((i >> 4) * 0x1100UL) | 0x40;
    memset(pix, 0, sizeof pix);
    bm.bprow = BW * 4; bm.width = BW; bm.height = BH; bm.format = W3D_FMT_A8R8G8B8; bm.dest = pix;
    c = W3D_CreateContextTags(&err, W3D_CC_BITMAP, (ULONG)&bm, W3D_CC_W3DBM, TRUE, W3D_CC_DRIVERTYPE, type, TAG_DONE);
    CHECK(c && err == W3D_SUCCESS, "W3D_CreateContext");
    if (!c) return 0;
    CHECK(c->width == BW && c->height == BH && c->format == W3D_FMT_A8R8G8B8, "context fields");
    CHECK(W3D_GetState(c, W3D_GOURAUD) == W3D_ENABLED, "W3D_GetState");
    CHECK(W3D_SetState(c, W3D_PERSPECTIVE, W3D_ENABLE) == W3D_SUCCESS, "W3D_SetState");
    CHECK(W3D_SetState(c, W3D_ANTI_POLYGON, W3D_ENABLE) == (ULONG)W3D_UNSUPPORTEDSTATE, "W3D_SetState refuses antialiasing");
    CHECK(W3D_CheckDriver() & W3D_DRIVER_CPU, "W3D_CheckDriver");
    CHECK(W3D_LockHardware(c) == W3D_SUCCESS, "W3D_LockHardware");
    CHECK(W3D_CheckIdle(c) == W3D_SUCCESS, "W3D_CheckIdle");
    CHECK(W3D_Query(c, W3D_Q_ZBUFFER, 0) == W3D_FULLY_SUPPORTED, "W3D_Query");
    CHECK(W3D_Query(c, W3D_Q_MAXTEXWIDTH, 0) >= 256, "W3D_Query max texture");
    CHECK(W3D_GetTexFmtInfo(c, W3D_A8R8G8B8, 0) & W3D_TEXFMT_SUPPORTED, "W3D_GetTexFmtInfo");
    t = W3D_AllocTexObjTags(c, &err, W3D_ATO_IMAGE, (ULONG)tex, W3D_ATO_FORMAT, W3D_A8R8G8B8, W3D_ATO_WIDTH, 16,
                            W3D_ATO_HEIGHT, 16, W3D_ATO_MIPMAP, 0xFFFF, TAG_DONE);
    CHECK(t && err == W3D_SUCCESS && t->texwidth == 16, "W3D_AllocTexObj");
    t2 = W3D_AllocTexObjTags(c, &err, W3D_ATO_IMAGE, (ULONG)tex, W3D_ATO_FORMAT, W3D_L8, W3D_ATO_WIDTH, 16, W3D_ATO_HEIGHT, 16, TAG_DONE);
    CHECK(t2 != 0, "W3D_AllocTexObj L8");
    if (!t || !t2) return 0;
    W3D_FlushTextures(c);
    W3D_ReleaseTexture(c, t2);
    CHECK(W3D_SetFilter(c, t, W3D_LINEAR_MIP_NEAREST, W3D_LINEAR) == W3D_SUCCESS, "W3D_SetFilter");
    CHECK(W3D_SetTexEnv(c, t, W3D_MODULATE, 0) == W3D_SUCCESS, "W3D_SetTexEnv");
    CHECK(W3D_SetWrapMode(c, t, W3D_REPEAT, W3D_CLAMP, 0) == W3D_SUCCESS, "W3D_SetWrapMode");
    CHECK(W3D_UpdateTexImage(c, t, tex, 0, 0) == W3D_SUCCESS, "W3D_UpdateTexImage");
    CHECK(W3D_UploadTexture(c, t) == W3D_SUCCESS, "W3D_UploadTexture");
    CHECK(W3D_SetAlphaMode(c, W3D_A_GREATER, &ref) == W3D_SUCCESS, "W3D_SetAlphaMode");
    CHECK(W3D_SetBlendMode(c, W3D_SRC_ALPHA, W3D_ONE_MINUS_SRC_ALPHA) == W3D_SUCCESS, "W3D_SetBlendMode");
    CHECK(W3D_SetBlendMode(c, W3D_SRC_COLOR, W3D_ZERO) == (ULONG)W3D_UNSUPPORTEDBLEND, "W3D_SetBlendMode refuses SRC_COLOR as source");
    CHECK(W3D_SetDrawRegionWBM(c, &bm, &sc) == W3D_SUCCESS, "W3D_SetDrawRegionWBM");
    CHECK(W3D_SetDrawRegion(c, 0, 0, 0) != W3D_SUCCESS, "W3D_SetDrawRegion refuses no bitmap");
    CHECK(W3D_SetDrawRegionWBM(c, &bm, &sc) == W3D_SUCCESS, "W3D_SetDrawRegionWBM again");
    CHECK(W3D_SetFogParams(c, &fog, W3D_FOG_LINEAR) == W3D_SUCCESS, "W3D_SetFogParams");
    CHECK(W3D_SetColorMask(c, TRUE, TRUE, TRUE, TRUE) == W3D_SUCCESS, "W3D_SetColorMask");
    CHECK(W3D_AllocZBuffer(c) == W3D_SUCCESS && c->zbufferalloc, "W3D_AllocZBuffer");
    CHECK(W3D_ClearZBuffer(c, &z) == W3D_SUCCESS, "W3D_ClearZBuffer");
    CHECK(W3D_ReadZPixel(c, 3, 3, &z) == W3D_SUCCESS && z > 0.49 && z < 0.51, "W3D_ReadZPixel");
    CHECK(W3D_ReadZSpan(c, 0, 0, 4, zs) == W3D_SUCCESS && zs[3] > 0.49, "W3D_ReadZSpan");
    z = 0.25;
    W3D_WriteZPixel(c, 1, 1, &z);
    CHECK(W3D_ReadZPixel(c, 1, 1, &z) == W3D_SUCCESS && z > 0.24 && z < 0.26, "W3D_WriteZPixel");
    zs[0] = zs[1] = zs[2] = zs[3] = 0.75;
    W3D_WriteZSpan(c, 0, 2, 4, zs, 0);
    CHECK(W3D_ReadZSpan(c, 0, 2, 4, zs) == W3D_SUCCESS && zs[2] > 0.74, "W3D_WriteZSpan");
    z = 1.0;
    W3D_ClearZBuffer(c, &z);
    CHECK(W3D_SetZCompareMode(c, W3D_Z_LESS) == W3D_SUCCESS, "W3D_SetZCompareMode");
    CHECK(W3D_AllocStencilBuffer(c) == W3D_SUCCESS, "W3D_AllocStencilBuffer");
    st = 7;
    CHECK(W3D_ClearStencilBuffer(c, &st) == W3D_SUCCESS, "W3D_ClearStencilBuffer");
    CHECK(W3D_ReadStencilPixel(c, 2, 2, &st) == W3D_SUCCESS && st == 7, "W3D_ReadStencilPixel");
    CHECK(W3D_FillStencilBuffer(c, 0, 0, 4, 1, 8, (void *)"\1\2\3\4") == W3D_SUCCESS, "W3D_FillStencilBuffer");
    CHECK(W3D_ReadStencilSpan(c, 0, 0, 4, sv) == W3D_SUCCESS && sv[3] == 4, "W3D_ReadStencilSpan");
    CHECK(W3D_WriteStencilPixel(c, 5, 5, 9) == W3D_SUCCESS, "W3D_WriteStencilPixel");
    CHECK(W3D_WriteStencilSpan(c, 0, 1, 4, sv, 0) == W3D_SUCCESS, "W3D_WriteStencilSpan");
    CHECK(W3D_SetStencilFunc(c, W3D_ST_ALWAYS, 0, 255) == W3D_SUCCESS, "W3D_SetStencilFunc");
    CHECK(W3D_SetStencilOp(c, W3D_ST_KEEP, W3D_ST_KEEP, W3D_ST_KEEP) == W3D_SUCCESS, "W3D_SetStencilOp");
    CHECK(W3D_SetWriteMask(c, 255) == W3D_SUCCESS, "W3D_SetWriteMask");
    CHECK(W3D_FreeStencilBuffer(c) == W3D_SUCCESS, "W3D_FreeStencilBuffer");
    CHECK(W3D_SetLogicOp(c, W3D_LO_COPY) == W3D_SUCCESS, "W3D_SetLogicOp");
    CHECK(W3D_Hint(c, W3D_H_PERSPECTIVE, W3D_H_NICE) == W3D_SUCCESS, "W3D_Hint");
    CHECK(W3D_GetDriverState(c) == W3D_SUCCESS, "W3D_GetDriverState");
    CHECK(W3D_SetPenMask(c, 255) == W3D_SUCCESS, "W3D_SetPenMask");
    CHECK(W3D_SetCurrentColor(c, &col) == W3D_SUCCESS, "W3D_SetCurrentColor");
    CHECK(W3D_SetCurrentPen(c, 1) == W3D_SUCCESS, "W3D_SetCurrentPen");
    {
        W3D_Scissor part = { 0, 0, 8, 8 };
        CHECK(W3D_UpdateTexSubImage(c, t, tex, 0, 0, &part, 16 * 4) == W3D_SUCCESS, "W3D_UpdateTexSubImage");
    }
    CHECK(W3D_GetDestFmt() & W3D_FMT_A8R8G8B8, "W3D_GetDestFmt");
    {
        W3D_Driver **d = W3D_GetDrivers();
        CHECK(d && d[0], "W3D_GetDrivers");
        if (d && d[0]) {
            CHECK(W3D_QueryDriver(d[0], W3D_Q_TEXMAPPING, 0) == W3D_FULLY_SUPPORTED, "W3D_QueryDriver");
            CHECK(W3D_GetDriverTexFmtInfo(d[0], W3D_R5G6B5, 0) & W3D_TEXFMT_SUPPORTED, "W3D_GetDriverTexFmtInfo");
        }
    }
    W3D_SetScissor(c, &sc);
    CHECK(W3D_SetChromaTestBounds(c, t, 0, 0x10101010UL, W3D_CHROMATEST_NONE) == W3D_SUCCESS, "W3D_SetChromaTestBounds");
    CHECK(W3D_ClearDrawRegion(c, 0xFF102030UL) == W3D_SUCCESS, "W3D_ClearDrawRegion");
    W3D_Flush(c);
    CHECK(pix[BW * 10 + 10] == 0xFF102030UL, "W3D_ClearDrawRegion's colour");
    CHECK(W3D_TestMode(INVALID_ID) == 0, "W3D_TestMode");
    {
        W3D_ScreenMode *l = W3D_GetScreenmodeList();
        W3D_FreeScreenmodeList(l);
        checks++;
    }
    (void)W3D_BestModeIDTags(W3D_BMI_WIDTH, 320, W3D_BMI_HEIGHT, 240, W3D_BMI_DEPTH, 16, TAG_DONE);
    checks++;
    /* the scene: every drawing call */
    {   /* ENV:W3DCheck/NoZ: the scene without the Z buffer, to tell a driver's depth test from its drawing */
        char nz[4];
        W3D_SetState(c, W3D_ZBUFFER, GetVar((STRPTR)"W3DCheck/NoZ", (STRPTR)nz, sizeof nz, 0) > 0 ? W3D_DISABLE : W3D_ENABLE);
    }
    W3D_SetState(c, W3D_FOGGING, W3D_ENABLE);
    vtx(&tri.v1, 2, 2, 0.1f, 0, 0, 1, 0, 0, 1);
    vtx(&tri.v2, 40, 4, 0.5f, 16, 0, 0, 1, 0, 1);
    vtx(&tri.v3, 6, 40, 0.9f, 0, 16, 0, 0, 1, 1);
    tri.tex = t; tri.st_pattern = 0;
    CHECK(W3D_DrawTriangle(c, &tri) == W3D_SUCCESS, "W3D_DrawTriangle");
    triv.v1 = &tri.v1; triv.v2 = &tri.v2; triv.v3 = &tri.v3; triv.tex = 0; triv.st_pattern = 0;
    CHECK(W3D_DrawTriangleV(c, &triv) == W3D_SUCCESS, "W3D_DrawTriangleV");
    vtx(&v[0], 30, 30, 0.3f, 0, 0, 1, 1, 1, 1); vtx(&v[1], 60, 30, 0.3f, 16, 0, 1, 1, 0, 1);
    vtx(&v[2], 60, 46, 0.3f, 16, 16, 0, 1, 1, 1); vtx(&v[3], 30, 46, 0.3f, 0, 16, 1, 0, 1, 1);
    tris.vertexcount = 4; tris.v = v; tris.tex = t; tris.st_pattern = 0;
    CHECK(W3D_DrawTriFan(c, &tris) == W3D_SUCCESS, "W3D_DrawTriFan");
    CHECK(W3D_DrawTriStrip(c, &tris) == W3D_SUCCESS, "W3D_DrawTriStrip");
    for (i = 0; i < 4; i++) vp[i] = &v[i];
    trisv.vertexcount = 4; trisv.v = vp; trisv.tex = t2; trisv.st_pattern = 0;
    CHECK(W3D_DrawTriFanV(c, &trisv) == W3D_SUCCESS, "W3D_DrawTriFanV");
    CHECK(W3D_DrawTriStripV(c, &trisv) == W3D_SUCCESS, "W3D_DrawTriStripV");
    pt.v1 = v[0]; pt.tex = 0; pt.pointsize = 3.0f;
    CHECK(W3D_DrawPoint(c, &pt) == W3D_SUCCESS, "W3D_DrawPoint");
    ln.v1 = v[0]; ln.v2 = v[2]; ln.tex = 0; ln.linewidth = 1.0f; ln.st_enable = W3D_TRUE; ln.st_pattern = 0xF0F0; ln.st_factor = 1;
    CHECK(W3D_DrawLine(c, &ln) == W3D_SUCCESS, "W3D_DrawLine");
    lns.vertexcount = 4; lns.v = v; lns.tex = 0; lns.linewidth = 2.0f; lns.st_enable = W3D_FALSE; lns.st_pattern = 0; lns.st_factor = 1;
    CHECK(W3D_DrawLineStrip(c, &lns) == W3D_SUCCESS, "W3D_DrawLineStrip");
    CHECK(W3D_DrawLineLoop(c, &lns) == W3D_SUCCESS, "W3D_DrawLineLoop");
    /* vertex arrays: x, y, z, u, v as floats */
    for (i = 0; i < 4; i++) {
        arr[i * 5] = v[i].x - 25; arr[i * 5 + 1] = v[i].y - 25; arr[i * 5 + 2] = 0.2f;
        arr[i * 5 + 3] = v[i].u; arr[i * 5 + 4] = v[i].v;
    }
    CHECK(W3D_VertexPointer(c, arr, 20, W3D_VERTEX_F_F_F, 0) == W3D_SUCCESS, "W3D_VertexPointer");
    CHECK(W3D_TexCoordPointer(c, arr + 3, 20, 0, 4, -4, 0) == W3D_SUCCESS, "W3D_TexCoordPointer");
    CHECK(W3D_ColorPointer(c, 0, 0, W3D_COLOR_FLOAT, W3D_CMODE_RGBA, 0) == W3D_SUCCESS, "W3D_ColorPointer");
    CHECK(W3D_BindTexture(c, 0, t) == W3D_SUCCESS, "W3D_BindTexture");
    CHECK(W3D_DrawArray(c, W3D_PRIMITIVE_TRIFAN, 0, 4) == W3D_SUCCESS, "W3D_DrawArray");
    CHECK(W3D_DrawElements(c, W3D_PRIMITIVE_TRIANGLES, W3D_INDEX_UWORD, 3, idx) == W3D_SUCCESS, "W3D_DrawElements");
    W3D_SetFrontFace(c, W3D_CW);
    checks++;
    /* version 5 */
    CHECK(W3D_SetTextureBlendTags(c, W3D_BLEND_STAGE, 0, W3D_ENV_MODE, W3D_MODULATE, TAG_DONE) == W3D_SUCCESS, "W3D_SetTextureBlend");
    CHECK(W3D_SecondaryColorPointer(c, 0, 0, W3D_COLOR_FLOAT, W3D_CMODE_RGB, 0) == W3D_SUCCESS, "W3D_SecondaryColorPointer");
    CHECK(W3D_FogCoordPointer(c, 0, 0, W3D_FOGCOORD_FLOAT, 0) == W3D_SUCCESS, "W3D_FogCoordPointer");
    CHECK(W3D_InterleavedArray(c, arr, 20, W3D_VFORMAT_TCOORD_0, 0) == W3D_SUCCESS, "W3D_InterleavedArray");
    {
        W3D_Color clear = { 0, 0, 0, 1 };
        W3D_Double one = 1.0;
        float size = 2.0f;
        CHECK(W3D_SetParameter(c, W3D_POINT_SIZE, &size) == W3D_SUCCESS, "W3D_SetParameter");
        memset(stip, 0xAA, sizeof stip);
        CHECK(W3D_SetParameter(c, W3D_STIPPLE_POLYGON, stip) == W3D_SUCCESS, "W3D_SetParameter stipple");
        CHECK(W3D_PinTexture(c, t, TRUE) == W3D_SUCCESS && t->pinned, "W3D_PinTexture");
        W3D_FlushFrame(c);
        h = sum(pix, BW * BH);
        memcpy(scene[out], pix, sizeof pix);
        CHECK(W3D_SetDrawRegionTexture(c, t, 0) == W3D_SUCCESS && c->width == 16, "W3D_SetDrawRegionTexture");
        CHECK(W3D_ClearBuffers(c, &clear, 0, 0) == W3D_SUCCESS, "W3D_ClearBuffers");
        CHECK(W3D_SetDrawRegionTexture(c, 0, 0) != W3D_SUCCESS || 1, "W3D_SetDrawRegionTexture back");
        (void)one;
    }
    W3D_WaitIdle(c);
    W3D_UnLockHardware(c);
    CHECK(W3D_FreeZBuffer(c) == W3D_SUCCESS, "W3D_FreeZBuffer");
    W3D_FreeTexObj(c, t2);
    CHECK(W3D_FreeAllTexObj(c) == W3D_SUCCESS, "W3D_FreeAllTexObj");
    W3D_DestroyContext(c);
    checks++;
    (void)r;
    return h;
}

/* How far scene[1] is from scene[0]: the worst channel, and the pixels
 * beyond the tolerance even with a neighbour's pixel allowed (edges). */
static int chan(ULONG p, int sh) { return (int)((p >> sh) & 255); }
static int pdiff(ULONG p, ULONG q)
{
    int sh, w = 0, d;
    for (sh = 0; sh < 24; sh += 8) { d = chan(p, sh) - chan(q, sh); if (d < 0) d = -d; if (d > w) w = d; }
    return w;
}

static void tolerance(int *worst, int *over)
{
    int x, y, dx, dy;
    *worst = *over = 0;
    for (y = 0; y < BH; y++)
        for (x = 0; x < BW; x++) {
            int d = pdiff(scene[0][y * BW + x], scene[1][y * BW + x]), best = d;
            if (d > *worst) *worst = d;
            for (dy = -1; dy <= 1 && best > 8; dy++)
                for (dx = -1; dx <= 1 && best > 8; dx++)
                    if (x + dx >= 0 && x + dx < BW && y + dy >= 0 && y + dy < BH) {
                        int e = pdiff(scene[0][(y + dy) * BW + x + dx], scene[1][y * BW + x]);
                        if (e < best) best = e;
                    }
            if (best > 8) (*over)++;
        }
}

int main(void)
{
    ULONG a, b;
    char drv[16];
    int virgl;
    drv[0] = 0;
    virgl = GetVar((STRPTR)"Warp3D/Driver", (STRPTR)drv, sizeof drv, 0) > 0 && (drv[0] == 'v' || drv[0] == 'V');
    Warp3DBase = OpenLibrary("Warp3D.library", 4);
    if (!Warp3DBase) { printf("W3DCheck: no Warp3D.library 4\n"); return 20; }
    printf("W3DCheck: %s %d.%d\n", Warp3DBase->lib_Node.ln_Name, Warp3DBase->lib_Version, Warp3DBase->lib_Revision);
    drvname = "CPU";
    a = run(W3D_DRIVER_CPU, 0);
    drvname = virgl ? "virgl" : "GFOpenGPU";
    b = (W3D_CheckDriver() & W3D_DRIVER_3DHW) ? run(W3D_DRIVER_3DHW, 1) : a;
    printf("scene: CPU %08lx, %s %08lx%s\n", (unsigned long)a, drvname, (unsigned long)b,
           (W3D_CheckDriver() & W3D_DRIVER_3DHW) ? "" : " (no 3D hardware driver: the CPU's again)");
    checks++;
    {   /* ENV:W3DCheck/Dump names a file prefix: both scenes go to <prefix>.cpu and <prefix>.drv, raw ARGB, BW x BH */
        char dp[96], fn[104];
        int k;
        if (GetVar((STRPTR)"W3DCheck/Dump", (STRPTR)dp, 90, 0) > 0)
            for (k = 0; k < 2; k++) {
                BPTR fh;
                sprintf(fn, "%s.%s", dp, k ? "drv" : "cpu");
                if ((fh = Open((STRPTR)fn, MODE_NEWFILE))) { Write(fh, scene[k], sizeof scene[k]); Close(fh); }
            }
    }
    if (a != b && virgl) {
        int worst, over;
        tolerance(&worst, &over);
        printf("scene on virgl: worst channel %d, %d of %d pixels over 8 with a pixel's shift\n", worst, over, BW * BH);
        if (over * 50 > BW * BH) { fails++; printf("FAIL: virgl draws the scene beyond the tolerance\n"); }
    } else if (a != b) { fails++; printf("FAIL: the drivers draw the scene differently\n"); }
    printf("W3DCheck: %d checks, %d failed\n", checks, fails);
    CloseLibrary(Warp3DBase);
    return fails ? 10 : 0;
}
