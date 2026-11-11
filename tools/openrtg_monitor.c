/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * DEVS:Monitors/GFOpenRTG: GFOpenRTG's monitor driver. LoadMonDrvs runs it with
 * the other monitors, before IPrefs opens Workbench's screen, so a Workbench
 * on an GFOpenRTG mode needs no line in S:Startup-Sequence. It does what
 * "C:GFOpenRTG SCREENS" does, quietly: GFOpenRTG's modes in the display database,
 * screens on them drawn by GFOpenRTG, and LIBS:GFOpenRTG/cybergraphics.library
 * opened once. With Picasso96 running, or no board (ACRTG, or from 0.9
 * uaegfx), it does nothing.
 *
 * 0.8: a ROM with GFOpenRTG in it (gfopenrtg library/build.sh module) has
 * done all that before DOS (library/openrtg_monitor_rom.c). Then
 * ORTG_Screens(0) answers 1, and this registers nothing a second time: it
 * only asks for the screens again (ORTG_Screens(1)), which opens
 * gfopengpu.library from the disk now that LIBS: is there, and ends.
 *
 * 0.9 (11 Nov 2026): the uaegfx board of Amiberry, WinUAE and FS-UAE as
 * well as ACRTG (gfopenrtg.library 0.26, docs/design/UAEGFX-20261111.md).
 * A ROM made before 0.26 holds a gfopenrtg.library that knows only ACRTG,
 * and OpenLibrary of the name alone would make that one from its RomTag. So
 * when the library isn't in memory yet and the ROM's is older than 0.26,
 * LIBS:gfopenrtg.library is opened by its path, and that one is used.
 */
#include <exec/types.h>
#include <exec/execbase.h>
#include <exec/resident.h>
#include <dos/dos.h>
#include <proto/exec.h>
#include <proto/gfopenrtg.h>

static const char version[] __attribute__((used)) = "$VER: GFOpenRTG-Monitor 0.9 (11.11.2026) AmiCradle Team";
struct Library *OpenRTGBase;

/* The revision in a RomTag's id string, "gfopenrtg.library 0.25 (...)": 25; 0 when there is none. */
static int rom_revision(const struct Resident *r)
{
    const char *p = r && r->rt_IdString ? r->rt_IdString : "";
    int rev = 0;
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    while (*p >= '0' && *p <= '9') p++;
    if (*p++ != '.') return 0;
    while (*p >= '0' && *p <= '9') rev = rev * 10 + (*p++ - '0');
    return rev;
}

/* gfopenrtg.library: the one in memory; else the disk's when the ROM's is older than 0.26; else by name. */
static struct Library *open_ortg(void)
{
    struct Library *lib;
    struct Resident *r;
    int in_memory;
    Forbid();
    in_memory = FindName(&SysBase->LibList, (STRPTR)OPENRTG_NAME) != NULL;
    Permit();
    if (!in_memory && (r = FindResident((STRPTR)OPENRTG_NAME)) != NULL && rom_revision(r) < 26 &&
        (lib = OpenLibrary((STRPTR)"LIBS:gfopenrtg.library", OPENRTG_VERSION)) != NULL)
        return lib;
    return OpenLibrary(OPENRTG_NAME, OPENRTG_VERSION);
}

int main(void)
{
    int p96, rc = RETURN_OK;
    (void)version;
    Forbid();
    p96 = FindName(&SysBase->LibList, (STRPTR)"rtg.library") != NULL;
    Permit();
    if (p96)
        return RETURN_OK;                   /* Picasso96 has the boards */
    if (!(OpenRTGBase = open_ortg()))
        return RETURN_WARN;
    if ((OpenRTGBase->lib_Version > 0 || OpenRTGBase->lib_Revision >= 18) && ORTG_Screens(0)) {
        (void)ORTG_Screens(1);              /* on already (the ROM's): gfopengpu.library, nothing else */
        CloseLibrary(OpenRTGBase);
        return RETURN_OK;
    }
    if (ORTG_MonitorCount() == 0 || !ORTG_DisplayDatabase(1) || !ORTG_Screens(1))
        rc = RETURN_WARN;
    else
        OpenLibrary((STRPTR)"LIBS:GFOpenRTG/cybergraphics.library", 41);   /* stays open, as with C:GFOpenRTG */
    /* gfopenrtg.library stays open too: its screens are in use from now on */
    return rc;
}
