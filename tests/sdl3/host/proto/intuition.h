/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT
 * intuition.library's call that stubs/stub_tell.h makes, answered by the stubs' host tests. */
#ifndef PROTO_INTUITION_H
#define PROTO_INTUITION_H
#include <intuition/intuition.h>
LONG EasyRequestArgs(struct Window *w, struct EasyStruct *es, ULONG *idcmp, APTR args);
#endif
