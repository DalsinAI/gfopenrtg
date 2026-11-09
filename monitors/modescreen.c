/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 *
 * ModeScreen ID SECONDS: opens a screen in one ModeID (hex), draws bars on
 * it for SECONDS and says whether it opened, to check a mode for real.
 */
#include <stdio.h>
#include <stdlib.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

int main(int argc, char **argv)
{
    ULONG id = argc > 1 ? strtoul(argv[1], NULL, 16) : 0;
    int secs = argc > 2 ? atoi(argv[2]) : 3, depth = argc > 3 ? atoi(argv[3]) : 3;
    struct Screen *s = OpenScreenTags(NULL, SA_DisplayID, id, SA_Depth, depth, SA_Title, (ULONG)"ModeScreen", SA_ShowTitle, TRUE, SA_Quiet, FALSE, TAG_DONE);
    if (!s) {
        printf("%08lx: did not open\n", (unsigned long)id);
        return RETURN_WARN;
    }
    printf("%08lx: open, %dx%d\n", (unsigned long)id, s->Width, s->Height);
    for (int i = 0; i < 8; i++) {
        SetAPen(&s->RastPort, i);
        RectFill(&s->RastPort, s->Width * i / 8, 20, s->Width * (i + 1) / 8 - 1, s->Height - 1);
    }
    Delay(50 * secs);
    CloseScreen(s);
    printf("closed\n");
    return RETURN_OK;
}
