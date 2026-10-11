@ The ARM7 program of the sounding guest (README.md). Linked at 0; gen.sh makes arm7.bin from it, which
@ dsptest.s takes in and the SH4 copies to the start of the wave memory.
@
@ What tests of the debugger use it for is in README.md, label by label. In short: a loop whose state differs
@ behind every sample, a timer interrupt (FIQ) every 13 samples that is entered at the first instruction of a
@ sample or, when it was raised while the loop had F set, inside one, and a handler that changes what is heard.
@
@ a_main_loop and a_fiq load nothing from a literal: every address they use is in a register that a_reset set.
@ (The emulator prints a literal load as "ldr rN, [pc, #..]", and the contract runner of the devtools UI takes a
@ line that names pc for control flow.)
@
@ Registers. The loop runs in SVC mode, the handler in FIQ mode.
@
@   r0 r1       scratch of the loop
@   r2          what a_leaf leaves
@   r3          the passes of the loop
@   r4          the fold of the loop: the timer's count and SCIPD of every pass. Nothing else writes it
@   r5          0x00802800, the common registers of the AICA (the handler reads this register too)
@   r6          0x00000400, the variables of the loop in wave memory
@   r7          the calls of a_leaf
@   r8_fiq      the interrupts
@   r9_fiq      the fold of the handler: L, the timer's count, SCIPD, lr and the SPSR of every entry. Nothing else
@               writes it
@   r10_fiq r11_fiq     scratch of the handler
@   r12_fiq     0x00800000, the channel registers of the AICA
@
@ A fold is "rotate left by 5, add": it depends on the order of what goes into it.
@
@ The lengths are part of the program. Where the loop stands when the timer overflows decides whether the FIQ is
@ entered at the first instruction of a sample or inside one, and that is no chance: every sample starts at a
@ whole instruction, so the place is the orbit of a map on the 17 instructions of the loop, which can stay on a
@ few of them. As written the entries reach all 17 (README.md, "The FIQ entries", has the count). One instruction
@ more or less, in the loop, in the handler or in a_reset (whose length decides where in its sample the loop
@ starts), gives another orbit: such a change is followed by a new count.

	.text
	.arm

	.type	a_vectors, %function
a_vectors:
	b	a_reset			@ 0x00	reset
	b	a_hang			@ 0x04	undefined instruction
	b	a_hang			@ 0x08	software interrupt
	b	a_hang			@ 0x0C	prefetch abort
	b	a_hang			@ 0x10	data abort
	b	a_hang			@ 0x14
	b	a_hang			@ 0x18	IRQ
	b	a_fiq			@ 0x1C	FIQ
	.size	a_vectors, .-a_vectors

	.type	a_hang, %function
a_hang:
	b	a_hang
	.size	a_hang, .-a_hang

@ Entered in SVC mode with I and F set (the reset of the ARM7DI). It sets every register the program uses, and it
@ leaves the interrupt of the AICA as the start of the machine has it: nothing pending, no request latched, none
@ in service, the timer 13 samples in front of its overflow. So a release of the ARM7 starts the same program
@ whatever ran before it, and makes the FIQ entries of README.md again, counted from the release.
@
@ A release latches a request again that was pending when the ARM7 was held (the timer had overflowed with the
@ last sample in front of the hold, or the ARM7 was held in a_fiq in front of its clear of the pending bit). Such
@ a request waits for F and would be entered behind the msr that ends a_reset: an interrupt of the program that
@ is gone. Hence the end of a_reset, in this order:
@   - SCIEB first: nothing new is let in;
@   - the timer in front of SCIRE: from its write on it does not overflow for 13 samples, so no pending bit of
@     the timer is set again behind the clear, wherever a time step of the AICA falls (as the emulator charges
@     the ARM7, a_reset is over within the first sample behind a release, and none falls into it);
@   - then L and M, the handshake of a service as a_fiq makes it: reading L lets go of nFIQ, writing M ends the
@     service. Nothing is pending by then, so nothing is latched behind it;
@   - and SCIEB again, timer A, with F cleared last
	.type	a_reset, %function
a_reset:
	msr	CPSR_c, #0xD1		@ FIQ mode: its registers
	mov	r8, #0
	mov	r9, #0
	mov	r12, #0x00800000
	mov	sp, #0x00000800		@ a stack for each of the two modes; the program pushes nothing
	msr	CPSR_c, #0xD3		@ SVC mode
	mov	sp, #0x00001000
	mov	r3, #0
	mov	r4, #0
	mov	r7, #0
	mov	r5, #0x00800000
	orr	r5, r5, #0x00002800
	mov	r6, #0x00000400
	mov	r1, #0
	mov	r0, #0x40		@ bit 6 of the interrupt registers: timer A
	str	r1, [r5, #0x9C]		@ SCIEB: nothing is let in while the rest is set
	str	r1, [r5, #0xA8]		@ SCILV0
	str	r0, [r5, #0xAC]		@ SCILV1: timer A has level 2
	str	r1, [r5, #0xB0]		@ SCILV2
	mov	r2, #0xF3
	str	r2, [r5, #0x90]		@ TIMA: the timer overflows with the 13th sample from here, and not before
	mvn	r2, #0
	str	r2, [r5, #0xA4]		@ SCIRE: every pending bit cleared
	ldr	r2, [r5, #0x500]	@ L: a request that is latched lets go of nFIQ
	mov	r2, #1
	str	r2, [r5, #0x504]	@ M: and its service ends
	str	r0, [r5, #0x9C]		@ SCIEB: timer A
	msr	CPSR_c, #0x93		@ F cleared
	.size	a_reset, .-a_reset

@ One pass: the count, what the pass sees of the timer and of the pending bits, the call.
@ Between a_fwin and a_fwin_end F is set (the F window): a request that a time step of the AICA raises while
@ the loop is in there waits, and is entered behind the msr of a_fwin_end, inside a sample
	.type	a_main_loop, %function
a_main_loop:
	add	r3, r3, #1		@ counted in a register
	str	r3, [r6, #0]		@ and in wave memory
a_fwin:
	msr	CPSR_c, #0xD3		@ F set
	ldr	r0, [r5, #0x90]		@ the timer's count
	ldr	r1, [r5, #0xA0]		@ SCIPD
	str	r0, [r6, #4]
	str	r1, [r6, #8]
	add	r4, r0, r4, ror #27	@ both into the fold
	add	r4, r1, r4, ror #27
a_fwin_end:
	msr	CPSR_c, #0x93		@ F cleared
	mov	r0, #0x400
	str	r0, [r5, #0xA4]		@ SCIRE: SAMPLE_DONE cleared, behind the fold: the fold says in which pass a time step fell
	bl	a_leaf
	b	a_main_loop
	.size	a_main_loop, .-a_main_loop

@ A leaf: something to step over and to step out of
	.type	a_leaf, %function
a_leaf:
	add	r7, r7, #1
	eor	r2, r7, r3
	mov	pc, lr
	.size	a_leaf, .-a_leaf

@ The timer interrupt. Every access of a register of the AICA is a word access
	.type	a_fiq, %function
a_fiq:
	ldr	r10, [r5, #0x500]	@ L: the read is the acknowledge
	ldr	r11, [r5, #0x90]	@ the timer's count as the handler finds it
	add	r9, r10, r9, ror #27	@ the fold: L,
	add	r9, r11, r9, ror #27	@ the count,
	ldr	r11, [r5, #0xA0]
	add	r9, r11, r9, ror #27	@ SCIPD as the handler finds it (has the loop cleared SAMPLE_DONE?),
	add	r9, lr, r9, ror #27	@ where the entry was made (the return address)
	mrs	r10, SPSR
	add	r9, r10, r9, ror #27	@ and under which flags (the saved status)
	mov	r10, #0x40
	str	r10, [r5, #0xA4]	@ SCIRE: the timer's pending bit cleared
	mov	r10, #0xF3
	str	r10, [r5, #0x90]	@ TIMA: armed again, 13 samples
	add	r8, r8, #1		@ counted
	ldr	r10, [r12, #0x18]	@ the pitch word of the first channel, between two values:
	eor	r10, r10, #0x200
	str	r10, [r12, #0x18]	@ heard
	tst	r8, #63
	bne	a_fiq_end
	mov	r10, r8, lsr #9		@ every 64th time: the pitch word of the second channel, which goes through
	and	r10, r10, #3		@ four values, one for 512 interrupts (0.15 s): what a person hears move
	mov	r10, r10, lsl #8
	orr	r10, r10, #0x7800
	str	r10, [r12, #0x98]
	mov	r10, #0x8200		@ the second channel keyed off (KYONEX without KYONB)
	orr	r10, r10, #1
	str	r10, [r12, #0x80]
	orr	r10, r10, #0x4000	@ and on (KYONEX with KYONB): it starts over
	str	r10, [r12, #0x80]
	mov	r10, #0x20
	str	r10, [r5, #0xB8]	@ MCIPD bit 5: the AICA's interrupt to the SH4, which answers in g_irq
a_fiq_end:
	mov	r10, #1
	str	r10, [r5, #0x504]	@ M: the service ends
	subs	pc, lr, #4
	.size	a_fiq, .-a_fiq
