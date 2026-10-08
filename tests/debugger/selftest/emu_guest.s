! The guest of the selftest "emu" (libswirl/debugger/dbg_selftest_emu.cpp).
! A raw image, loaded and entered at 8C010000. gen.sh makes emu_guest.inc and emu_guest.bin from it.
!
! The image is a set of routines; none of them ends. Each sets SR, VBR and the stack itself, so that a test
! can put the pc on any of them from any state of the machine. The boot enters the first one.
!
!   g_spin          counts in g_count, every interrupt masked: a long loop
!   g_sleep_masked  SLEEP with every interrupt masked: nothing ever ends the wait
!   g_sleep_tmu     SLEEP, woken by the underflow interrupt of TMU0; counts its wake-ups in g_wakes
!   g_busy_tmu      the loop of g_spin with that interrupt arriving in it
!   g_softreset     writes the reset register of the system bus
!   g_exit          returns to the BIOS
!   g_irq           the interrupt handler: takes the underflow back and counts in g_irqs

	.text
	.global	_start
_start:
g_spin:
	mov.l	sr_blocked, r0
	ldc	r0, sr
	mov.l	vbr_value, r0
	ldc	r0, vbr
	mov.l	stack_top, r15
	mov.l	count_addr, r2
	mov	#0, r1
g_spin_loop:
	add	#1, r1
	mov.l	r1, @r2
	bra	g_spin_loop
	 nop

g_sleep_masked:
	mov.l	sr_masked, r0
	ldc	r0, sr
	mov.l	vbr_value, r0
	ldc	r0, vbr
	mov.l	stack_top, r15
g_sleep_masked_at:
	sleep
	bra	g_sleep_masked_at
	 nop

g_sleep_tmu:
	mov.l	sr_blocked, r0
	ldc	r0, sr
	mov.l	vbr_value, r0
	ldc	r0, vbr
	mov.l	stack_top, r15
	bsr	tmu_start
	 nop
	mov.l	wakes_addr, r2
	mov	#0, r1
	mov.l	sr_open, r0
	ldc	r0, sr
g_sleep_tmu_at:
	sleep
g_sleep_tmu_woken:
	add	#1, r1
	mov.l	r1, @r2
	bra	g_sleep_tmu_at
	 nop

g_busy_tmu:
	mov.l	sr_blocked, r0
	ldc	r0, sr
	mov.l	vbr_value, r0
	ldc	r0, vbr
	mov.l	stack_top, r15
	bsr	tmu_start
	 nop
	mov.l	count_addr, r2
	mov	#0, r1
	mov.l	sr_open, r0
	ldc	r0, sr
g_busy_tmu_loop:
	add	#1, r1
	mov.l	r1, @r2
	bra	g_busy_tmu_loop
	 nop

g_softreset:
	mov.l	sr_blocked, r0
	ldc	r0, sr
	mov.l	sfres_addr, r1
	mov.l	sfres_magic, r0
	mov.l	r0, @r1
g_softreset_loop:
	bra	g_softreset_loop
	 nop

g_exit:
	mov.l	sr_blocked, r0
	ldc	r0, sr
	mov.l	exit_hook, r0
	jmp	@r0
	 nop

! TMU0 underflows every TMU_PERIOD counts of 16 cycles, with its interrupt on level 15
tmu_start:
	mov.l	tstr_addr, r1
	mov	#0, r0
	mov.b	r0, @r1
	mov.l	tmu_period, r0
	mov.l	tcor0_addr, r3
	mov.l	r0, @r3
	mov.l	tcnt0_addr, r3
	mov.l	r0, @r3
	mov.l	tcr0_addr, r3
	mov.w	tcr0_run, r0
	mov.w	r0, @r3
	mov.l	ipra_addr, r3
	mov.w	ipra_tmu0, r0
	mov.w	r0, @r3
	mov	#1, r0
	mov.b	r0, @r1
	rts
	 nop

! on the register bank of the handler: the loops keep their registers
g_irq:
	mov.l	tcr0_addr, r1
	mov.w	tcr0_run, r0
	mov.w	r0, @r1
	mov.l	irqs_addr, r1
	mov.l	@r1, r0
	add	#1, r0
	mov.l	r0, @r1
	rte
	 nop

	.align	2
tcr0_run:	.word	0x0020
ipra_tmu0:	.word	0xf000
	.align	2
sr_blocked:	.long	0x500000f0
sr_masked:	.long	0x400000f0
sr_open:	.long	0x40000000
vbr_value:	.long	g_irq - 0x600
stack_top:	.long	0x8c00f000
count_addr:	.long	g_count
wakes_addr:	.long	g_wakes
irqs_addr:	.long	g_irqs
sfres_addr:	.long	0xa05f6890
sfres_magic:	.long	0x00007611
exit_hook:	.long	0x8c00043c
tstr_addr:	.long	0xffd80004
tcor0_addr:	.long	0xffd80008
tcnt0_addr:	.long	0xffd8000c
tcr0_addr:	.long	0xffd80010
ipra_addr:	.long	0xffd00004
tmu_period:	.long	1750

! what the routines count in, behind the image
	.equ	g_count, 0x8c020000
	.equ	g_wakes, 0x8c020004
	.equ	g_irqs,  0x8c020008
