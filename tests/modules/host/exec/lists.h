/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT */
#ifndef HOST_EXEC_LISTS_H
#define HOST_EXEC_LISTS_H
#include <exec/types.h>
struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name; };
struct MinNode { struct MinNode *mln_Succ, *mln_Pred; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; UBYTE lh_Type, l_pad; };
struct MinList { struct MinNode *mlh_Head, *mlh_Tail, *mlh_TailPred; };
#define NT_MEMORY 10
#endif
