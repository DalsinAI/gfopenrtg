/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * intuition/intuition.h's EasyStruct, for the stubs' host tests (stubs/stub_tell.h). */
#ifndef INTUITION_INTUITION_H
#define INTUITION_INTUITION_H
#include <exec/types.h>
struct Window;
struct EasyStruct {
    ULONG es_StructSize;
    ULONG es_Flags;
    CONST_STRPTR es_Title;
    CONST_STRPTR es_TextFormat;
    CONST_STRPTR es_GadgetFormat;
};
#endif
