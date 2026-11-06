| Copyright (c) 2026 Dalsin Limited / the AmiCradle Team. OpenRTG, MIT licence (LICENSE).
| SPDX-License-Identifier: MIT
|
| ogles3.library's GL calls into GFOpenGPU's GL.module.
|
| A program calls a GL entry of the dispatch table as a plain C function,
| the context first and the GL call's arguments after it, on the stack. Each
| entry (ogles3_thunks.s, generated) puts its slot x 4 in D0 and the size of
| the GL arguments in longs in D1 and comes to ogl3_call, which:
|   - answers 0 for a NULL context;
|   - makes the context current in its session's module copy first when it
|     isn't (ogl3_switch, in C);
|   - copies the GL arguments, without the context, and calls the session's
|     function for the slot with A4 on the session's copy of Mesa's data,
|     on the session's GL stack when the program's own is small;
|   - gives back the program's A4 and the result in D0 (and A0, D1).
| GL.module is shared (built -fbaserel32): every call into it needs A4 set
| (include/gfopengpu/module.h). This library itself never uses A4
| (-ffixed-a4), so nothing of ours depends on it.
|
| struct ogl3_session (ogles3_face.h): 0 a4, 4 fn, 8 sss, 12 task,
| 16 depth, 20 current. The context's first long is its session.

	.text
	.even

	.globl	_ogl3_call
_ogl3_call:
	move.l	%a4,-(%sp)
	link	%a5,#0
	movem.l	%d2-%d3/%a2/%a6,-(%sp)
	move.l	%d1,%d2			| arguments, in longs
	move.l	12(%a5),%d1		| the context
	beq.s	8f
	move.l	%d1,%a0
	move.l	(%a0),%a2		| its session
	cmp.l	20(%a2),%d1
	beq.s	1f
	move.l	%d0,%d3
	move.l	%d1,-(%sp)
	jsr	_ogl3_switch
	addq.l	#4,%sp
	move.l	%d3,%d0
1:	move.l	4(%a2),%a1
	move.l	0(%a1,%d0.l),%a1	| the function
	lea	16(%a5,%d2.l*4),%a0	| the end of the GL arguments
	bsr.s	ogl3_core
	bra.s	9f
8:	moveq	#0,%d0
	moveq	#0,%d1
9:	move.l	%d0,%a0
	movem.l	(%sp)+,%d2-%d3/%a2/%a6
	unlk	%a5
	move.l	(%sp)+,%a4
	rts

| ULONG ogl3_invoke(struct ogl3_session *s, APTR fn, ULONG n, ...): fn with
| the n longs that follow, as a GL call would be (the GLA calls of
| ogles3_lib.c, and opening the module).
	.globl	_ogl3_invoke
_ogl3_invoke:
	move.l	%a4,-(%sp)
	link	%a5,#0
	movem.l	%d2-%d3/%a2/%a6,-(%sp)
	move.l	12(%a5),%a2
	move.l	16(%a5),%a1
	move.l	20(%a5),%d2
	lea	24(%a5,%d2.l*4),%a0
	bsr.s	ogl3_core
	move.l	%d0,%a0
	movem.l	(%sp)+,%d2-%d3/%a2/%a6
	unlk	%a5
	move.l	(%sp)+,%a4
	rts

| A2 the session, A1 the function, A0 the end of its arguments, D2 their
| number in longs. Onto the GL stack when the session has one, this is its
| task, and no call is on it already (exec's StackSwap, -732; ThisTask is at
| 276 in ExecBase). StackSwap moves SP, so the swap back happens here too,
| before this routine returns on the stack it was called on. The arguments
| are read from where they are, which stays valid on either stack.
ogl3_core:
	movem.l	%d3-%d4/%a3/%a6,-(%sp)
	move.l	%a1,%a3
	move.l	%a0,%d4
	moveq	#0,%d3
	move.l	8(%a2),%d0
	beq.s	5f
	tst.l	16(%a2)
	bne.s	5f
	move.l	_SysBase,%a6
	move.l	276(%a6),%d1
	cmp.l	12(%a2),%d1
	bne.s	5f
	move.l	%d0,%a0
	jsr	-732(%a6)
	moveq	#1,%d3
	addq.l	#1,16(%a2)
5:	move.l	%d4,%a0
	move.l	%d2,%d0
	bra.s	4f
3:	move.l	-(%a0),-(%sp)
4:	dbra	%d0,3b
	move.l	(%a2),%a4
	jsr	(%a3)
	lea	(%sp,%d2.l*4),%sp
	tst.l	%d3
	beq.s	6f
	subq.l	#1,16(%a2)
	move.l	%d0,%d4
	move.l	%d1,%a3
	move.l	8(%a2),%a0
	move.l	_SysBase,%a6
	jsr	-732(%a6)
	move.l	%d4,%d0
	move.l	%a3,%d1
6:	movem.l	(%sp)+,%d3-%d4/%a3/%a6
	rts

| A GL call the installed GL.module doesn't have.
	.globl	_ogl3_missing
_ogl3_missing:
	moveq	#0,%d0
	moveq	#0,%d1
	sub.l	%a0,%a0
	rts
