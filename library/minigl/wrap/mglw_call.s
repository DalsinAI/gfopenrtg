| Copyright (c) 2026 AmiCradle Team. OpenRTG, MIT licence (LICENSE).
| SPDX-License-Identifier: MIT
|
| minigl.library over GL: its forwarded GL calls into GFOpenGPU's GL.module, as ogles3.library's are
| (library/ogles3/ogles3_call.s, the same routine).
|
| A program calls a GL entry of the dispatch table as a plain C function, the context first and the GL call's
| arguments after it, on the stack. Each forwarded entry (mglw_thunks.s, generated) puts its slot x 4 in D0 and
| the size of the GL arguments in longs in D1 and comes to mglw_call, which:
|   - answers 0 for a NULL context (spec 4.3: a program may call before it has one);
|   - makes the context current in its session's module copy first when it isn't (mglw_switch, in C);
|   - copies the GL arguments, without the context, and calls the session's function for the slot with A4 on
|     the session's copy of Mesa's data, on the session's GL stack when the program's own is small;
|   - gives back the program's A4 and the result in D0 (and A0, D1).
| mglw_call_bool is the same for a call that answers a GLboolean: the module's is a byte in D0, MiniGL's a long,
| so the answer is cut to 0 or 1.
| GL.module is shared (built -fbaserel32): every call into it needs A4 set (include/gfopengpu/module.h). This
| library never uses A4 itself (-ffixed-a4), and keeps the program's for the functions of the program it calls.
|
| struct mglw_session (mglw.h): 0 a4, 4 fn, 8 sss, 12 task, 16 depth, 20 current. The context's first long is
| its session.

	.text
	.even

	.globl	_mglw_call
_mglw_call:
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
	jsr	_mglw_switch
	addq.l	#4,%sp
	move.l	%d3,%d0
	move.l	12(%a5),%d1
	cmp.l	20(%a2),%d1		| not made current (the module refused): nothing is called
	bne.s	8f
1:	move.l	4(%a2),%a1
	move.l	0(%a1,%d0.l),%a1	| the function
	lea	16(%a5,%d2.l*4),%a0	| the end of the GL arguments
	bsr.s	mglw_core
	bra.s	9f
8:	moveq	#0,%d0
	moveq	#0,%d1
9:	move.l	%d0,%a0
	movem.l	(%sp)+,%d2-%d3/%a2/%a6
	unlk	%a5
	move.l	(%sp)+,%a4
	rts

| ULONG mglw_invoke(struct mglw_session *s, APTR fn, ULONG n, ...): fn with
| the n longs that follow, as a GL call would be (the GLA calls of
| ogles3_lib.c, and opening the module).
	.globl	_mglw_invoke
_mglw_invoke:
	move.l	%a4,-(%sp)
	link	%a5,#0
	movem.l	%d2-%d3/%a2/%a6,-(%sp)
	move.l	12(%a5),%a2
	move.l	16(%a5),%a1
	move.l	20(%a5),%d2
	lea	24(%a5,%d2.l*4),%a0
	bsr.s	mglw_core
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
mglw_core:
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
	.globl	_mglw_missing
_mglw_missing:
	moveq	#0,%d0
	moveq	#0,%d1
	sub.l	%a0,%a0
	rts

	.globl	_mglw_call_bool
_mglw_call_bool:
	link	%a5,#0
	move.l	%d2,-(%sp)
	move.l	%d1,%d2
	lea	8(%a5,%d2.l*4),%a0	| the end of the context and the GL arguments
	addq.l	#4,%a0
	move.l	%d2,%d1
	addq.l	#1,%d1			| the context too
	bra.s	2f
1:	move.l	-(%a0),-(%sp)
2:	dbra	%d1,1b
	move.l	%d2,%d1
	bsr	_mglw_call
	addq.l	#1,%d2
	lea	(%sp,%d2.l*4),%sp
	and.l	#0xFF,%d0
	sne	%d0
	and.l	#1,%d0
	move.l	%d0,%a0
	move.l	(%sp)+,%d2
	unlk	%a5
	rts
