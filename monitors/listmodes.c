/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ListModes: every ModeID in the display database, with its name, monitor,
 * size, depth and properties, and whether it can be used.
 */
#include <stdio.h>
#include <exec/types.h>
#include <graphics/displayinfo.h>
#include <graphics/gfxbase.h>
#include <graphics/monitor.h>
#include <proto/exec.h>
#include <proto/graphics.h>

extern struct GfxBase *GfxBase;

int main(void)
{
    ULONG id = INVALID_ID;
    int n = 0;
    printf("chipset bits %02x\n", GfxBase->ChipRevBits0);
    while ((id = NextDisplayInfo(id)) != INVALID_ID) {
        struct NameInfo name;
        struct DimensionInfo dim;
        struct DisplayInfo di;
        struct MonitorInfo mon;
        DisplayInfoHandle h = FindDisplayInfo(id);
        int hn = GetDisplayInfoData(h, (UBYTE *)&name, sizeof name, DTAG_NAME, id);
        int hd = GetDisplayInfoData(h, (UBYTE *)&dim, sizeof dim, DTAG_DIMS, id);
        int hi = GetDisplayInfoData(h, (UBYTE *)&di, sizeof di, DTAG_DISP, id);
        int hm = GetDisplayInfoData(h, (UBYTE *)&mon, sizeof mon, DTAG_MNTR, id);
        printf("%08lx na=%lx prop=%08lx %dx%d d=%d mon=%s name=%s\n", (unsigned long)id,
               (unsigned long)ModeNotAvailable(id), hi ? (unsigned long)di.PropertyFlags : 0ul,
               hd ? dim.Nominal.MaxX - dim.Nominal.MinX + 1 : 0, hd ? dim.Nominal.MaxY - dim.Nominal.MinY + 1 : 0,
               hd ? dim.MaxDepth : 0, hm && mon.Mspc ? mon.Mspc->ms_Node.xln_Name : "?", hn ? (char *)name.Name : "");
        n++;
    }
    printf("%d modes\n", n);
    return 0;
}
