/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT */
#ifndef HOST_EXEC_MEMORY_H
#define HOST_EXEC_MEMORY_H
#include <exec/lists.h>
struct MemChunk { struct MemChunk *mc_Next; ULONG mc_Bytes; };
struct MemHeader {
    struct Node mh_Node;
    UWORD mh_Attributes;
    struct MemChunk *mh_First;
    APTR mh_Lower, mh_Upper;
    ULONG mh_Free;
};
#define MEMF_ANY 0UL
#endif
