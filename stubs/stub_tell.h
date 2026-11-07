/* stub_tell.h: how a GFOpenGPU link stub says why a program can't start
 * (a module missing or too old). It always goes on the program's Output.
 * A Workbench start (no CLI) or an output that isn't a console, such as
 * Run >NIL:, would show it nowhere, so then it is also an Intuition
 * requester on the Workbench screen, wrapped at 60 columns.
 * Only exec, dos and intuition: no C library, so any stub may include it.
 * The AmiCradle Team. */
#ifndef GFOPENGPU_STUB_TELL_H
#define GFOPENGPU_STUB_TELL_H

#include <exec/types.h>
#include <exec/tasks.h>
#include <dos/dosextens.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

static void ogpu_stub_tell(const char *prefix, const char *why)
{
    struct Process *me = (struct Process *)FindTask(NULL);
    BPTR out = Output();
    int console = out && IsInteractive(out)
               && me->pr_Task.tc_Node.ln_Type == NT_PROCESS && me->pr_CLI;
    char text[512];
    const char *src[2];
    ULONG n = 0, col = 0, wstart, s, i;

    if (out) {
        FPuts(out, (CONST_STRPTR)prefix);
        FPuts(out, (CONST_STRPTR)why);
        FPuts(out, (CONST_STRPTR) "\n");
    }
    if (console)
        return;

    /* prefix, then why, word-wrapped at 60 columns into text[] */
    src[0] = prefix;
    src[1] = why;
    for (s = 0; s < 2; s++) {
        const char *p = src[s];
        while (*p && n < sizeof(text) - 2) {
            while (*p == ' ')
                p++;
            if (!*p)
                break;
            wstart = 0;
            while (p[wstart] && p[wstart] != ' ')
                wstart++;
            if (col && col + 1 + wstart > 60) {
                text[n++] = '\n';
                col = 0;
            } else if (col) {
                text[n++] = ' ';
                col++;
            }
            for (i = 0; i < wstart && n < sizeof(text) - 2; i++)
                text[n++] = p[i];
            col += wstart;
            p += wstart;
        }
    }
    text[n] = 0;

    {
        struct Library *IntuitionBase = OpenLibrary((CONST_STRPTR) "intuition.library", 37);
        if (IntuitionBase) {
            struct EasyStruct es;
            APTR args[1];
            es.es_StructSize = sizeof(es);
            es.es_Flags = 0;
            es.es_Title = (CONST_STRPTR) "GFOpenGPU";
            es.es_TextFormat = (CONST_STRPTR) "%s";
            es.es_GadgetFormat = (CONST_STRPTR) "OK";
            args[0] = (APTR)text;
            EasyRequestArgs(NULL, &es, NULL, args);
            CloseLibrary(IntuitionBase);
        }
    }
}

#endif
