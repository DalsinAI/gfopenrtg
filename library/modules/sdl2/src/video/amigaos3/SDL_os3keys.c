/*
  SDL 2 for AmigaOS 3.x: the standard keys every game has.
  Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).

  Every program on SDL2.module (and on SDL 1.2 through sdl12-compat) gets
  the same keys, on Right Amiga so that no game's own keys are taken:

    Right Amiga + F       window or full screen; the choice is kept in
                          ENVARC:GFGames/<program>/Screen for next time
    Right Amiga + Q       quit (as the window's close gadget)
    Right Amiga + P       pause: picture, sound and the game clock stop
    Right Amiga + M       free the mouse pointer from the game, or grab it
    Right Amiga + S       a PNG screenshot in PROGDIR:Screenshots/
    Help                  the controls card (PROGDIR:Controls), when the
                          game's drawer has one; otherwise the game's key
    Right Amiga + F1, F2  F11 and F12, which Amiga keyboards lack
    Right Amiga + cursor  Page Up, Page Down (up, down), Home, End (left,
                          right)

  The program's icon may say FULLSCREEN or WINDOW (tooltypes) for how it
  starts. The hint SDL_AMIGA_STANDARD_KEYS=0 (ENV:SDL_AMIGA_STANDARD_KEYS)
  turns all of this off, for a game that has keys of its own there.
*/

#include "../../SDL_internal.h"

#if SDL_VIDEO_DRIVER_AMIGAOS3

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>
#include <devices/inputevent.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <workbench/workbench.h>
#include <intuition/intuition.h>

#include "SDL_os3video.h"
#include "SDL_os3keys.h"
#include "../../events/SDL_keyboard_c.h"
#include "../../events/SDL_mouse_c.h"
#include "../../events/SDL_events_c.h"
#include "SDL_hints.h"
#include "SDL_timer.h"

#define OS3_HINT_STANDARD_KEYS "SDL_AMIGA_STANDARD_KEYS"

/* raw key codes (Amiga Hardware Reference Manual, appendix C) */
#define RAW_Q      0x10
#define RAW_P      0x19
#define RAW_S      0x21
#define RAW_F      0x23
#define RAW_M      0x37
#define RAW_UP     0x4C
#define RAW_DOWN   0x4D
#define RAW_RIGHT  0x4E
#define RAW_LEFT   0x4F
#define RAW_F1     0x50
#define RAW_F2     0x51
#define RAW_HELP   0x5F
#define RAW_UPFLAG 0x80

#define OS3_DO_FULLSCREEN 0x01
#define OS3_DO_PAUSE      0x02
#define OS3_DO_MOUSE      0x04
#define OS3_DO_SHOT       0x08
#define OS3_DO_HELP       0x10

static unsigned os3_todo;
static SDL_Window *os3_todo_window;
static int os3_mouse_freed;
/* a key Right Amiga turned into another (F11, Page Up ...), so that its
   release goes out as that key too: indexed by raw code */
static SDL_Scancode os3_standin[0x60];

static const char os3_standard_card[] =
    "Right Amiga + F   window or full screen\n"
    "Right Amiga + P   pause\n"
    "Right Amiga + Q   quit\n"
    "Right Amiga + M   free or grab the mouse\n"
    "Right Amiga + S   screenshot (Screenshots drawer)\n"
    "Right Amiga + F1, F2   F11, F12\n"
    "Right Amiga + cursor   Page Up/Down, Home, End";

static SDL_bool OS3_KeysOn(void)
{
    return SDL_GetHintBoolean(OS3_HINT_STANDARD_KEYS, SDL_TRUE);
}

/* The program's name, without its path: from the Shell's command name, or
   the process's name when started from the Workbench. */
static void OS3_ProgramName(char *buf, int len)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    STRPTR part;

    buf[0] = '\0';
    if (me->pr_CLI) {
        if (!GetProgramName((STRPTR)buf, len)) {
            buf[0] = '\0';
        }
    }
    if (!buf[0] && me->pr_Task.tc_Node.ln_Name) {
        SDL_strlcpy(buf, me->pr_Task.tc_Node.ln_Name, len);
    }
    part = FilePart((STRPTR)buf);
    if (part != (STRPTR)buf) {
        SDL_memmove(buf, part, SDL_strlen((const char *)part) + 1);
    }
    if (!buf[0]) {
        SDL_strlcpy(buf, "SDL", len);
    }
}

static void OS3_ScreenVarName(char *var, int len)
{
    char name[64];

    OS3_ProgramName(name, sizeof(name));
    SDL_snprintf(var, len, "GFGames/%s/Screen", name);
}

/* 1 full screen, 0 window, -1 not said */
static int OS3_RememberedMode(void)
{
    char var[96], val[16];
    LONG n;

    OS3_ScreenVarName(var, sizeof(var));
    n = GetVar((STRPTR)var, (STRPTR)val, sizeof(val), GVF_GLOBAL_ONLY);
    if (n <= 0) {
        return -1;
    }
    if (SDL_strncasecmp(val, "FULLSCREEN", 10) == 0) {
        return 1;
    }
    if (SDL_strncasecmp(val, "WINDOW", 6) == 0) {
        return 0;
    }
    return -1;
}

static void OS3_RememberMode(int fullscreen)
{
    char var[96];
    const char *val = fullscreen ? "FULLSCREEN" : "WINDOW";

    OS3_ScreenVarName(var, sizeof(var));
    /* ENV: for now and ENVARC: for next time (OS 3.x makes the drawer) */
    SetVar((STRPTR)var, (STRPTR)val, (LONG)SDL_strlen(val), GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
}

/* The icon's FULLSCREEN or WINDOW tooltype: 1, 0, or -1 */
static int OS3_IconMode(void)
{
    struct Library *IconBase;
    struct DiskObject *dob;
    char name[64], path[80];
    int mode = -1;

    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 36UL);
    if (!IconBase) {
        return -1;
    }
    OS3_ProgramName(name, sizeof(name));
    SDL_snprintf(path, sizeof(path), "PROGDIR:%s", name);
    dob = GetDiskObject((STRPTR)path);
    if (dob) {
        if (dob->do_ToolTypes) {
            if (FindToolType((CONST_STRPTR *)dob->do_ToolTypes, (CONST_STRPTR)"FULLSCREEN")) {
                mode = 1;
            } else if (FindToolType((CONST_STRPTR *)dob->do_ToolTypes, (CONST_STRPTR)"WINDOW")) {
                mode = 0;
            }
        }
        FreeDiskObject(dob);
    }
    CloseLibrary(IconBase);
    return mode;
}

Uint32 OS3_StartModeFlags(Uint32 flags)
{
    int mode;

    if (!OS3_KeysOn()) {
        return flags;
    }
    if (flags & (SDL_WINDOW_UTILITY | SDL_WINDOW_TOOLTIP | SDL_WINDOW_POPUP_MENU)) {
        return flags;
    }
    mode = OS3_RememberedMode();
    if (mode < 0) {
        mode = OS3_IconMode();
    }
    if (mode == 1 && !(flags & SDL_WINDOW_FULLSCREEN)) {
        /* a screen of the window's own size, as the ports' -fullscreen */
        flags |= SDL_WINDOW_FULLSCREEN;
    } else if (mode == 0) {
        flags &= ~SDL_WINDOW_FULLSCREEN_DESKTOP;
    }
    return flags;
}

static void OS3_SendStandin(Uint8 state, UWORD raw, SDL_Scancode sc)
{
    if (state == SDL_PRESSED) {
        os3_standin[raw] = sc;
    } else {
        os3_standin[raw] = SDL_SCANCODE_UNKNOWN;
    }
    SDL_SendKeyboardKey(state, sc);
}

int OS3_StandardKey(SDL_Window *window, UWORD code, UWORD qualifier)
{
    UWORD raw = code & ~RAW_UPFLAG;
    int up = (code & RAW_UPFLAG) != 0;
    int ramiga = (qualifier & IEQUALIFIER_RCOMMAND) != 0;
    int repeat = (qualifier & IEQUALIFIER_REPEAT) != 0;
    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;

    if (raw >= SDL_arraysize(os3_standin)) {
        return 0;
    }
    /* a stand-in key goes up as the key it went down as, Right Amiga or not */
    if (up && os3_standin[raw] != SDL_SCANCODE_UNKNOWN) {
        OS3_SendStandin(SDL_RELEASED, raw, os3_standin[raw]);
        return 1;
    }
    if (!OS3_KeysOn()) {
        return 0;
    }

    if (raw == RAW_HELP && !ramiga) {
        BPTR lock;

        if (up) {
            return 0;    /* SDL lets go of a key it never saw go down */
        }
        lock = Lock((CONST_STRPTR)"PROGDIR:Controls", ACCESS_READ);
        if (!lock) {
            return 0;    /* no card: Help is the game's */
        }
        UnLock(lock);
        os3_todo |= OS3_DO_HELP;
        os3_todo_window = window;
        return 1;
    }

    if (!ramiga) {
        return 0;
    }

    switch (raw) {
    case RAW_F1:    sc = SDL_SCANCODE_F11;      break;
    case RAW_F2:    sc = SDL_SCANCODE_F12;      break;
    case RAW_UP:    sc = SDL_SCANCODE_PAGEUP;   break;
    case RAW_DOWN:  sc = SDL_SCANCODE_PAGEDOWN; break;
    case RAW_LEFT:  sc = SDL_SCANCODE_HOME;     break;
    case RAW_RIGHT: sc = SDL_SCANCODE_END;      break;
    default: break;
    }
    if (sc != SDL_SCANCODE_UNKNOWN) {
        if (!up) {
            OS3_SendStandin(SDL_PRESSED, raw, sc);
        }
        return 1;
    }

    switch (raw) {
    case RAW_Q:
    case RAW_P:
    case RAW_F:
    case RAW_M:
    case RAW_S:
        break;
    default:
        return 0;    /* other Right Amiga keys are the game's */
    }
    if (up || repeat) {
        return 1;
    }
    os3_todo_window = window;
    switch (raw) {
    case RAW_Q: SDL_SendQuit();                break;
    case RAW_P: os3_todo |= OS3_DO_PAUSE;      break;
    case RAW_F: os3_todo |= OS3_DO_FULLSCREEN; break;
    case RAW_M: os3_todo |= OS3_DO_MOUSE;      break;
    case RAW_S: os3_todo |= OS3_DO_SHOT;       break;
    }
    return 1;
}

/* --- pause ------------------------------------------------------------- */

static struct Window *OS3_IntuiWindow(SDL_Window *window)
{
    OS3_WindowData *data;

    if (!window) {
        return NULL;
    }
    data = (OS3_WindowData *)window->driverdata;
    return data ? data->window : NULL;
}

static void OS3_StopEverything(int stop)
{
    SDL_OS3_PauseAllAudio(stop);
    SDL_OS3_TimerPause(stop);
}

/* Waits in the window for Right Amiga + P (go on), Right Amiga + Q or the
   close gadget (quit). Nothing reaches the game meanwhile. */
static void OS3_PauseWindow(SDL_Window *window)
{
    struct Window *iwin = OS3_IntuiWindow(window);
    struct IntuiMessage *imsg;
    UBYTE *old_title;
    static char title[96];
    int done = 0, quit = 0;

    if (!iwin) {
        return;
    }
    OS3_StopEverything(1);
    old_title = iwin->Title;
    SDL_snprintf(title, sizeof(title), "%s (paused: Right Amiga + P)",
                 old_title ? (const char *)old_title : "");
    SetWindowTitles(iwin, (UBYTE *)title, (UBYTE *)~0);

    while (!done) {
        ULONG port_sig = 1UL << iwin->UserPort->mp_SigBit;
        ULONG got = Wait(port_sig | SIGBREAKF_CTRL_C);

        if (got & SIGBREAKF_CTRL_C) {
            quit = 1;
            done = 1;
        }
        while ((imsg = (struct IntuiMessage *)GetMsg(iwin->UserPort)) != NULL) {
            ULONG class = imsg->Class;
            UWORD code = imsg->Code;
            UWORD qual = imsg->Qualifier;

            ReplyMsg((struct Message *)imsg);
            if (class == IDCMP_CLOSEWINDOW) {
                quit = 1;
                done = 1;
            } else if (class == IDCMP_RAWKEY && (qual & IEQUALIFIER_RCOMMAND) &&
                       !(code & RAW_UPFLAG)) {
                if (code == RAW_P) {
                    done = 1;
                } else if (code == RAW_Q) {
                    quit = 1;
                    done = 1;
                }
            }
        }
    }

    SetWindowTitles(iwin, old_title, (UBYTE *)~0);
    OS3_StopEverything(0);
    /* keys let go while paused would stay down otherwise */
    SDL_ResetKeyboard();
    if (quit) {
        SDL_SendQuit();
    }
}

/* --- the controls card --------------------------------------------------- */

static void OS3_ShowCard(SDL_Window *window)
{
    struct Window *iwin = OS3_IntuiWindow(window);
    struct EasyStruct es;
    char *text;
    BPTR fh;
    LONG n = 0;
    const int max = 2048;

    text = (char *)SDL_malloc(max + sizeof(os3_standard_card) + 4);
    if (!text) {
        return;
    }
    fh = Open((CONST_STRPTR)"PROGDIR:Controls", MODE_OLDFILE);
    if (fh) {
        n = Read(fh, text, max);
        Close(fh);
        if (n < 0) {
            n = 0;
        }
    }
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ')) {
        n--;
    }
    if (n > 0) {
        text[n++] = '\n';
        text[n++] = '\n';
    }
    SDL_memcpy(text + n, os3_standard_card, sizeof(os3_standard_card));

    OS3_StopEverything(1);
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (UBYTE *)"Controls";
    es.es_TextFormat = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)"Back to the game";
    EasyRequest(iwin, &es, NULL, (ULONG)text);
    OS3_StopEverything(0);
    SDL_ResetKeyboard();
    SDL_free(text);
}

/* --- screenshots ----------------------------------------------------------- */

static void OS3_Put32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)(v >> 24);
    p[1] = (Uint8)(v >> 16);
    p[2] = (Uint8)(v >> 8);
    p[3] = (Uint8)v;
}

static int OS3_WriteChunk(BPTR fh, const char *type, const Uint8 *data, Uint32 len)
{
    Uint8 head[8], tail[4];
    Uint32 crc;

    OS3_Put32(head, len);
    SDL_memcpy(head + 4, type, 4);
    crc = SDL_crc32(0, head + 4, 4);
    if (len) {
        crc = SDL_crc32(crc, data, len);
    }
    OS3_Put32(tail, crc);
    if (Write(fh, head, 8) != 8) {
        return 0;
    }
    if (len && Write(fh, (APTR)data, len) != (LONG)len) {
        return 0;
    }
    return Write(fh, tail, 4) == 4;
}

/* A PNG of rows already filtered (a 0 before each row of RGB), stored in
   deflate's uncompressed blocks: no compressor needed in the module. */
static int OS3_WritePNG(const char *path, const Uint8 *raw, Uint32 rawlen, int w, int h)
{
    static const Uint8 sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    Uint8 ihdr[13];
    Uint32 blocks = (rawlen + 65534) / 65535;
    Uint32 zlen = 2 + rawlen + blocks * 5 + 4;
    Uint8 *z, *p;
    Uint32 a = 1, b = 0, left = rawlen, i;
    const Uint8 *src = raw;
    BPTR fh;
    int ok;

    z = (Uint8 *)SDL_malloc(zlen);
    if (!z) {
        return 0;
    }
    p = z;
    *p++ = 0x78;
    *p++ = 0x01;
    while (left) {
        Uint32 n = left > 65535 ? 65535 : left;
        left -= n;
        *p++ = left ? 0 : 1;
        *p++ = (Uint8)n;
        *p++ = (Uint8)(n >> 8);
        *p++ = (Uint8)~n;
        *p++ = (Uint8)(~n >> 8);
        SDL_memcpy(p, src, n);
        p += n;
        src += n;
    }
    for (i = 0; i < rawlen; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    OS3_Put32(p, (b << 16) | a);

    OS3_Put32(ihdr, (Uint32)w);
    OS3_Put32(ihdr + 4, (Uint32)h);
    ihdr[8] = 8;    /* bits */
    ihdr[9] = 2;    /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    fh = Open((CONST_STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        SDL_free(z);
        return 0;
    }
    ok = Write(fh, (APTR)sig, 8) == 8 &&
         OS3_WriteChunk(fh, "IHDR", ihdr, 13) &&
         OS3_WriteChunk(fh, "IDAT", z, zlen) &&
         OS3_WriteChunk(fh, "IEND", NULL, 0);
    Close(fh);
    SDL_free(z);
    if (!ok) {
        DeleteFile((CONST_STRPTR)path);
    }
    return ok;
}

static void OS3_Screenshot(SDL_Window *window)
{
    struct Window *iwin = OS3_IntuiWindow(window);
    struct RastPort *rp;
    char name[64], path[128];
    Uint8 *raw;
    Uint32 rawlen;
    int w, h, x, y, n;
    BPTR lock;

    if (!iwin) {
        return;
    }
    rp = iwin->RPort;
    if (iwin->Flags & WFLG_GIMMEZEROZERO) {
        w = iwin->GZZWidth;
        h = iwin->GZZHeight;
    } else {
        w = iwin->Width;
        h = iwin->Height;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    rawlen = (Uint32)(w * 3 + 1) * (Uint32)h;
    raw = (Uint8 *)SDL_malloc(rawlen);
    if (!raw) {
        return;
    }

    if (CyberGfxBase && GetCyberMapAttr(rp->BitMap, CYBRMATTR_ISCYBERGFX)) {
        /* RTG: the window's pixels as RGB, one row at a time after its 0 */
        for (y = 0; y < h; y++) {
            Uint8 *row = raw + (Uint32)y * (w * 3 + 1);
            row[0] = 0;
            ReadPixelArray(row + 1, 0, 0, (UWORD)(w * 3), rp, 0, (UWORD)y,
                           (UWORD)w, 1, RECTFMT_RGB);
        }
    } else {
        /* AGA: each pen, through the screen's colours */
        struct ColorMap *cm = iwin->WScreen->ViewPort.ColorMap;
        ULONG rgb[3 * 256];
        int depth = iwin->WScreen->RastPort.BitMap->Depth;
        int colours = depth >= 8 ? 256 : (1 << depth);

        GetRGB32(cm, 0, (ULONG)colours, rgb);
        for (y = 0; y < h; y++) {
            Uint8 *row = raw + (Uint32)y * (w * 3 + 1);
            row[0] = 0;
            for (x = 0; x < w; x++) {
                LONG pen = ReadPixel(rp, x, y);
                if (pen < 0 || pen >= colours) {
                    pen = 0;
                }
                row[1 + x * 3] = (Uint8)(rgb[pen * 3] >> 24);
                row[2 + x * 3] = (Uint8)(rgb[pen * 3 + 1] >> 24);
                row[3 + x * 3] = (Uint8)(rgb[pen * 3 + 2] >> 24);
            }
        }
    }

    lock = Lock((CONST_STRPTR)"PROGDIR:Screenshots", ACCESS_READ);
    if (!lock) {
        lock = CreateDir((CONST_STRPTR)"PROGDIR:Screenshots");
    }
    if (lock) {
        UnLock(lock);
        OS3_ProgramName(name, sizeof(name));
        for (n = 1; n < 1000; n++) {
            BPTR there;
            SDL_snprintf(path, sizeof(path), "PROGDIR:Screenshots/%s_%03d.png", name, n);
            there = Lock((CONST_STRPTR)path, ACCESS_READ);
            if (!there) {
                break;
            }
            UnLock(there);
        }
        if (n < 1000) {
            OS3_WritePNG(path, raw, rawlen, w, h);
        }
    }
    SDL_free(raw);
}

/* --- after the messages ---------------------------------------------------- */

void OS3_RunStandardKeys(_THIS)
{
    unsigned todo = os3_todo;
    SDL_Window *window = os3_todo_window;
    SDL_Window *w;

    if (!todo) {
        return;
    }
    os3_todo = 0;
    os3_todo_window = NULL;

    /* the window must still be one of ours */
    for (w = _this->windows; w && w != window; w = w->next) {
    }
    if (!w) {
        return;
    }

    if (todo & OS3_DO_FULLSCREEN) {
        Uint32 flags = SDL_GetWindowFlags(window);
        int to_full = !(flags & SDL_WINDOW_FULLSCREEN);

        if (SDL_SetWindowFullscreen(window, to_full ? SDL_WINDOW_FULLSCREEN : 0) == 0) {
            OS3_RememberMode(to_full);
        }
    }
    if (todo & OS3_DO_MOUSE) {
        if (SDL_GetRelativeMouseMode()) {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_ShowCursor(SDL_ENABLE);
            os3_mouse_freed = 1;
        } else if (os3_mouse_freed) {
            SDL_SetRelativeMouseMode(SDL_TRUE);
            os3_mouse_freed = 0;
        } else {
            SDL_SetWindowGrab(window, SDL_GetWindowGrab(window) ? SDL_FALSE : SDL_TRUE);
        }
    }
    if (todo & OS3_DO_SHOT) {
        OS3_Screenshot(window);
    }
    if (todo & OS3_DO_HELP) {
        OS3_ShowCard(window);
    }
    if (todo & OS3_DO_PAUSE) {
        OS3_PauseWindow(window);
    }
}

#endif /* SDL_VIDEO_DRIVER_AMIGAOS3 */
