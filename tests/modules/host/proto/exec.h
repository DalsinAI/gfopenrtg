/* Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
 * SPDX-License-Identifier: MIT */
#ifndef HOST_PROTO_EXEC_H
#define HOST_PROTO_EXEC_H
#include <exec/memory.h>
#include <exec/semaphores.h>
void InitSemaphore(struct SignalSemaphore *s);
void ObtainSemaphore(struct SignalSemaphore *s);
void ReleaseSemaphore(struct SignalSemaphore *s);
APTR AllocMem(ULONG bytes, ULONG flags);
void FreeMem(APTR p, ULONG bytes);
APTR Allocate(struct MemHeader *mh, ULONG bytes);
void Deallocate(struct MemHeader *mh, APTR p, ULONG bytes);
void AddHead(struct List *l, struct Node *n);
void Remove(struct Node *n);
#endif
