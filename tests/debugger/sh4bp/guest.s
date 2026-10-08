! Guest programs of the sh4bp gates. SH4, little endian, linked at 0x8C010000.
!
! libswirl/debugger/dbg_selftest_sh4bp_guest.inc holds this file assembled, as the bytes of the image and the
! addresses of the g_* labels; gen.sh makes it. The tests load the image into the RAM of their machine, point
! the pc at a program and say in registers what it is to do.
!
! The machine around it (machine.h): RAM at 0x0C000000 with its mirrors, and one device
!   0xA0600000  write: acknowledges the interrupt request (IRL9, taken at vbr + 0x600)
! The reset state of the machine has vbr = g_vbr, SR = MD, r15 = the top of the stack.

	.little
	.text
	.global	_start
_start:

! ---------------------------------------------------------------------------------------------------------
! boot: the entry point of the image, what a machine that boots its guest starts. It goes the way of a guest
! behind its entry point: it sets itself up with every interrupt masked, opens the interrupts and then works
! without end. No input. Needs the interrupt source of the machine from g_boot_open on.
!   g_boot_wait   the setup, a loop of two instructions that runs 0x3000 times
!   g_boot_open   the ldc that opens the interrupts
! Its main loop has a block of every kind, as the workload below has, and is busy: it waits for an interrupt in
! one pass of 32 only, so that between two waits it works through several interrupts and what a pass costs
! decides where the next wait begins.
!   g_boot_pass   a call with its slot
!   g_boot_poll   a loop block of three instructions that only reads: the decoder's idle class
!   g_boot_long   250 instructions in a row: the recompiler ends the block behind 224 of them
!   g_boot_sleep  the SLEEP of every 32nd pass
	.align	2
	.global	g_boot, g_boot_wait, g_boot_open, g_boot_pass, g_boot_poll, g_boot_long, g_boot_sleep
g_boot:
	mov.l	k_boot_count, r1
g_boot_wait:
	dt	r1
	bf	g_boot_wait
	mov.l	k_boot_sr, r0
g_boot_open:
	ldc	r0, sr
	mov.l	k_boot_data, r8
	mov	#0, r9
	mov	#0, r13
g_boot_pass:
	mov	r9, r4
	bsr	g_leaf
	add	#3, r4
	mov.l	r0, @r8
	mov	#3, r1
g_boot_poll:
	mov.l	@r8, r2
	dt	r1
	bf	g_boot_poll
g_boot_long:
	.rept	125
	add	#3, r3
	rotl	r3
	.endr
	add	r3, r13
	mov.l	r3, @(8, r8)
	fldi1	fr0
	lds	r9, fpul
	float	fpul, fr1
	fadd	fr0, fr1
	fmul	fr1, fr1
	ftrc	fr1, fpul
	sts	fpul, r5
	mov.l	r5, @(4, r8)
	mov.l	k_boot_outer, r0
	jsr	@r0
	mov	r9, r4
	add	r0, r13
	mov	r9, r0
	and	#31, r0
	cmp/eq	#0, r0
	bt/s	g_boot_sleep
	add	#1, r9
	bra	g_boot_pass
	nop
g_boot_sleep:
	sleep
	bra	g_boot_pass
	nop

	.align	2
k_boot_count:
	.long	0x3000
k_boot_sr:
	.long	0x40000000
k_boot_data:
	.long	g_data
k_boot_outer:
	.long	g_outer

! ---------------------------------------------------------------------------------------------------------
! line: straight code, three blocks. No input.
!   g_line           first instruction of a block
!   g_line_mid       in the middle of that block
!   g_line_m2        further on in the same block
!   g_line_slot      delay slot of the bra at g_line_br
!   g_line_bt_slot   delay slot of the bt/s at g_line_bt, which is not taken
	.align	2
	.global	g_line, g_line_mid, g_line_m2, g_line_br, g_line_slot, g_line_b2, g_line_bt, g_line_bt_slot, g_line_end
g_line:
	mov	#1, r0
	mov	#2, r1
	add	r0, r1
g_line_mid:
	mov	r1, r2
	shll2	r2
	cmp/eq	r0, r1
g_line_m2:
	mov	#5, r3
g_line_br:
	bra	g_line_b2
g_line_slot:
	add	#3, r3
	nop
	nop
g_line_b2:
	mov	#7, r4
	add	r3, r4
	tst	r4, r4
g_line_bt:
	bt/s	g_line_b2
g_line_bt_slot:
	add	#1, r4
g_line_end:
	bra	g_line_end
	nop

! ---------------------------------------------------------------------------------------------------------
! loop: r4 passes of a five instruction loop that ends with bf/s. r5 = a buffer of r4 bytes.
!   g_loop_top    first instruction of the loop block
!   g_loop_mid    in the middle of it
!   g_loop_slot   delay slot of the bf/s at g_loop_br: taken r4 - 1 times, not taken once
	.align	2
	.global	g_loop, g_loop_top, g_loop_mid, g_loop_br, g_loop_slot, g_loop_exit, g_loop_end
g_loop:
	mov	#0, r0
g_loop_top:
	mov.b	r0, @r5
	add	#1, r5
g_loop_mid:
	add	#1, r0
	dt	r4
g_loop_br:
	bf/s	g_loop_top
g_loop_slot:
	extu.b	r0, r0
g_loop_exit:
	mov	r0, r6
	add	#1, r6
g_loop_end:
	bra	g_loop_end
	nop

! ---------------------------------------------------------------------------------------------------------
! calls: a call through a register to a function that keeps a frame and calls a leaf without one, a
! recursive call, a trap. No input.
	.align	2
	.global	g_calls, g_calls_jsr, g_calls_ret, g_calls_bsr, g_calls_ret2, g_calls_trapa, g_calls_ret3, g_calls_end
g_calls:
	mov	#3, r4
	mov.l	k_outer, r0
g_calls_jsr:
	jsr	@r0
	nop
g_calls_ret:
	mov	r0, r8
	mov	#4, r4
g_calls_bsr:
	bsr	g_fact
	nop
g_calls_ret2:
	mov	r0, r9
g_calls_trapa:
	trapa	#5
g_calls_ret3:
	mov	#1, r10
g_calls_end:
	bra	g_calls_end
	nop

	.align	2
k_outer:
	.long	g_outer

! outer(r4): keeps pr on the stack and calls leaf twice. r0 = leaf(leaf(r4) + 1)
	.global	g_outer, g_outer_body, g_outer_mid, g_outer_rts
g_outer:
	sts.l	pr, @-r15
g_outer_body:
	bsr	g_leaf
	nop
g_outer_mid:
	add	#1, r0
	bsr	g_leaf
	mov	r0, r4
	lds.l	@r15+, pr
g_outer_rts:
	rts
	nop

! leaf(r4): r0 = 2 * r4 + 1. Uses no stack
	.global	g_leaf, g_leaf_mid
g_leaf:
	mov	r4, r0
g_leaf_mid:
	shll	r0
	rts
	add	#1, r0

! fact(r4): r0 = r4!, recursive
	.global	g_fact, g_fact_call, g_fact_back
g_fact:
	sts.l	pr, @-r15
	mov.l	r4, @-r15
	mov	#1, r0
	cmp/gt	r0, r4
	bf	g_fact_base
	add	#-1, r4
g_fact_call:
	bsr	g_fact
	nop
g_fact_back:
	mov.l	@r15, r4
	mul.l	r4, r0
	sts	macl, r0
g_fact_base:
	add	#4, r15
	lds.l	@r15+, pr
	rts
	nop

! ---------------------------------------------------------------------------------------------------------
! wait: a call that only returns when r4 more interrupts have been served. Needs SR.IMASK below 6 and the
! interrupt source of the machine.
	.align	2
	.global	g_waitmain, g_waitmain_ret, g_waitmain_end, g_wait, g_wait_loop, g_wait_sleep
g_waitmain:
	mov	#3, r4
	bsr	g_wait
	nop
g_waitmain_ret:
	mov	#1, r10
g_waitmain_end:
	bra	g_waitmain_end
	nop

g_wait:
	sts.l	pr, @-r15
	mov.l	k_irqcnt, r1
	mov.l	@r1, r2
	add	r4, r2
g_wait_loop:
g_wait_sleep:
	sleep
	mov.l	@r1, r0
	cmp/hs	r2, r0
	bf	g_wait_loop
	lds.l	@r15+, pr
	rts
	nop

! ---------------------------------------------------------------------------------------------------------
! sleep: one SLEEP. No input.
	.align	2
	.global	g_sleep, g_sleep_after
g_sleep:
	sleep
g_sleep_after:
	bra	g_sleep_after
	nop

! ---------------------------------------------------------------------------------------------------------
! smc: calls a function, overwrites its first instruction and calls it again.
!   r5 = g_smc_target, r6 = the new opcode. r8, r9 = what the two calls returned
	.align	2
	.global	g_smc, g_smc_write, g_smc_end, g_smc_target, g_smc_target_rts
g_smc:
	bsr	g_smc_target
	nop
	mov	r0, r8
g_smc_write:
	mov.w	r6, @r5
	bsr	g_smc_target
	nop
	mov	r0, r9
g_smc_end:
	bra	g_smc_end
	nop
g_smc_target:
	mov	#1, r0
g_smc_target_rts:
	rts
	nop

! ---------------------------------------------------------------------------------------------------------
! work: the workload of the timing gates, a loop over every kind of block.
!   r10       the number of passes
!   r12 != 0  leaves the FPU instructions out
!   r14 != 0  SLEEPs every eighth pass: needs the interrupt source of the machine
! r9 counts the passes, r13 sums what the calls returned, the data area takes the stores.
!   g_work_poll      a loop block of three instructions that only reads: the decoder's idle class
!   g_work_long      250 instructions in a row: the recompiler ends the block behind 224 of them
!   g_work_long_mid  in the middle of that block
!   g_work_long_2nd  in the middle of the block that follows it
	.align	2
	.global	g_work, g_work_main, g_work_call, g_work_call_slot, g_work_after_call, g_work_poll, g_work_poll_mid
	.global	g_work_long, g_work_long_mid, g_work_long_2nd, g_work_fpu, g_work_nofpu, g_work_jsr, g_work_jsr_slot
	.global	g_work_ldc, g_work_bfs, g_work_bfs_slot, g_work_sleep, g_work_nosleep, g_work_done
g_work:
	mov.l	k_data, r8
	mov	#0, r9
	mov	#0, r13
g_work_main:
	mov	r9, r4
g_work_call:
	bsr	g_leaf
g_work_call_slot:
	add	#3, r4
g_work_after_call:
	mov.l	r0, @r8
	mov	#3, r1
g_work_poll:
	mov.l	@r8, r2
g_work_poll_mid:
	dt	r1
	bf	g_work_poll
g_work_long:
	.rept	20
	add	#1, r3
	xor	r9, r3
	.endr
g_work_long_mid:
	.rept	95
	add	#3, r3
	rotl	r3
	.endr
g_work_long_2nd:
	.rept	10
	add	r3, r5
	xor	r5, r3
	.endr
	mov	r3, r5
	shlr2	r5
	sub	r5, r3
	not	r3, r6
	and	r6, r5
	or	r5, r3
	mov	r6, r7
	shll8	r7
	xor	r7, r3
	neg	r3, r5
	swap.w	r6, r7
	add	r7, r3
	extu.w	r3, r5
	exts.b	r3, r6
	cmp/gt	r5, r6
	addc	r6, r3
	subc	r5, r3
	rotcl	r3
	rotr	r3
	add	r3, r13
	mov.l	r3, @(8, r8)
	tst	r12, r12
	bf	g_work_nofpu
g_work_fpu:
	fldi1	fr0
	lds	r9, fpul
	float	fpul, fr1
	fadd	fr0, fr1
	fmul	fr1, fr1
	fmov	fr1, fr2
	fsub	fr0, fr2
	ftrc	fr2, fpul
	sts	fpul, r5
	mov.l	r5, @(4, r8)
g_work_nofpu:
	mov.l	k_outer2, r0
g_work_jsr:
	jsr	@r0
g_work_jsr_slot:
	mov	r9, r4
	add	r0, r13
	stc	sr, r0
g_work_ldc:
	ldc	r0, sr
	mov	r9, r0
	and	#7, r0
	cmp/eq	#0, r0
g_work_bfs:
	bf/s	g_work_nosleep
g_work_bfs_slot:
	add	#1, r9
	tst	r14, r14
	bt	g_work_nosleep
g_work_sleep:
	sleep
g_work_nosleep:
	cmp/hs	r10, r9
	bt	g_work_done
	bra	g_work_main
	nop
g_work_done:
	bra	g_work_done
	nop

	.align	2
k_data:
	.long	g_data
k_outer2:
	.long	g_outer
k_irqcnt:
	.long	g_irqcnt
k_trapcnt:
	.long	g_trapcnt
k_mmio:
	.long	0xA0600000

! ---------------------------------------------------------------------------------------------------------
! data
	.align	4
	.global	g_data, g_irqcnt, g_trapcnt, g_buffer
g_data:
	.long	0, 0, 0, 0
g_irqcnt:
	.long	0
g_trapcnt:
	.long	0
g_buffer:
	.space	256

! ---------------------------------------------------------------------------------------------------------
! The vectors, vbr = g_vbr. Both handlers work on the registers of bank 1 and use no stack.
	.org	0x1000
	.global	g_vbr, g_trap, g_trap_rte, g_irq, g_irq_rte
g_vbr:

	.org	0x1100
g_trap:
	mov.l	k_trapcnt_v, r1
	mov.l	@r1, r2
	add	#1, r2
	mov.l	r2, @r1
g_trap_rte:
	rte
	nop
	.align	2
k_trapcnt_v:
	.long	g_trapcnt

	.org	0x1600
g_irq:
	mov.l	k_mmio_v, r0
	mov.l	r0, @r0
	mov.l	k_irqcnt_v, r1
	mov.l	@r1, r2
	add	#1, r2
	mov.l	r2, @r1
g_irq_rte:
	rte
	nop
	.align	2
k_mmio_v:
	.long	0xA0600000
k_irqcnt_v:
	.long	g_irqcnt

! ---------------------------------------------------------------------------------------------------------
! span: a block that starts on one page and ends on the next. No input.
!   g_span_p2     an instruction of the block on the second page
	.org	0x1FF4
	.global	g_span, g_span_p2, g_span_end
g_span:
	mov	#1, r0
	add	#1, r0
	add	#1, r0
	add	#1, r0
	add	#1, r0
	add	#1, r0
	add	#1, r0
g_span_p2:
	add	#1, r0
	add	#1, r0
g_span_end:
	bra	g_span_end
	nop

! span2: a delayed branch that is the last instruction of a page, its slot is the first of the next.
	.org	0x2FFA
	.global	g_span2, g_span2_br, g_span2_slot, g_span2_end
g_span2:
	mov	#1, r0
	add	#1, r0
g_span2_br:
	bra	g_span2_end
g_span2_slot:
	add	#1, r0
	nop
g_span2_end:
	bra	g_span2_end
	nop

! ---------------------------------------------------------------------------------------------------------
! istep: integer code in short blocks, for the comparison of stepping against running. r4 = passes.
	.org	0x3100
	.global	g_istep, g_istep_top, g_istep_end
g_istep:
	mov	#0, r0
	mov	#1, r1
g_istep_top:
	add	r1, r0
	rotl	r1
	xor	r0, r1
	tst	#1, r0
	bt	g_istep_even
	add	#3, r0
	bra	g_istep_next
	add	#1, r1
g_istep_even:
	add	#-1, r0
	shll	r1
g_istep_next:
	dt	r4
	bf	g_istep_top
g_istep_end:
	bra	g_istep_end
	nop

! fstep: the same with FPU instructions. r4 = passes.
	.align	2
	.global	g_fstep, g_fstep_top, g_fstep_end
g_fstep:
	fldi1	fr0
	fldi1	fr1
	mov	#3, r1
g_fstep_top:
	lds	r1, fpul
	float	fpul, fr2
	fadd	fr2, fr1
	fdiv	fr1, fr0
	fmul	fr2, fr0
	fsqrt	fr2
	fadd	fr2, fr0
	fmov	fr0, fr3
	fmac	fr0, fr2, fr3
	add	#1, r1
	dt	r4
	bf	g_fstep_top
g_fstep_end:
	bra	g_fstep_end
	nop

! ---------------------------------------------------------------------------------------------------------
! pair: delayed conditional branches that are not taken. r4 = passes. Takes interrupts when SR lets it.
! The interpreter runs a slot behind a branch that was not taken as an instruction of its own: a pass is nine
! of its instructions, a slice is 56, so the ends of the slices walk through every place of the loop, the
! ones between a branch and its slot included.
!   g_pair_bt, g_pair_bt_slot   a bt/s that is not taken until the last pass. r1 counts the runs of its slot
!   g_pair_bf, g_pair_bf_slot   a bf/s that is never taken. r2 counts the runs of its slot
!   g_pair_in                   an instruction that is only reached by a jump, behind a bf/s that never runs:
!                               the address of a slot, the instruction of no pair. r3 counts its runs
	.align	2
	.global	g_pair, g_pair_top, g_pair_bt, g_pair_bt_slot, g_pair_bf, g_pair_bf_slot, g_pair_jump, g_pair_in, g_pair_end
g_pair:
	mov	#0, r1
	mov	#0, r2
	mov	#0, r3
g_pair_top:
	dt	r4
g_pair_bt:
	bt/s	g_pair_end
g_pair_bt_slot:
	add	#1, r1
	cmp/eq	r0, r0
g_pair_bf:
	bf/s	g_pair_end
g_pair_bf_slot:
	add	#1, r2
g_pair_jump:
	bra	g_pair_in
	nop
	bf/s	g_pair_end
g_pair_in:
	add	#1, r3
	bra	g_pair_top
	nop
g_pair_end:
	bra	g_pair_end
	nop

	.global	g_image_end
g_image_end:
