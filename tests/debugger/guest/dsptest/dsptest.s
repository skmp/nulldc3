! The sounding guest of the debugger's tests (README.md): the SH4 program. Bare metal, for the emulator only:
! reios loads the ELF and enters it at 8C010000, where _start is. gen.sh makes dsptest.elf from this file and
! arm7.bin.
!
! Everything the SH4 does to the sound it does in _start, between the first tick of the AICA and the second, on
! every engine. The first tick is not a tick's length away: it comes at the end of the first slice of the SH4, 448
! cycles behind the start of the machine. That is 56 instructions of the interpreter, while the recompiler, which
! charges this code about a thirtieth of that, would be through all of _start: nothing that is seen or heard can
! be in front of the first tick on both. _start therefore lets it pass. It releases the ARM7 onto one instruction
! that branches to itself, which gives the AICA its clock (no timer steps and no sample is made while the ARM7 is
! held), and waits until SAMPLE_DONE says that a tick has run. Then it holds the ARM7, copies the ARM7 program
! (arm7.s) and one period of a wave into the wave memory, sets two channels up, keys them on and releases the ARM7
! again: 6500 cycles on the interpreter, the slowest engine, of the 145124 between two ticks. From then on the ARM7
! and the timer of the AICA drive the sound, and the SH4 counts in g_main_loop; every 64th timer interrupt the ARM7
! raises the AICA's interrupt to the SH4, and g_irq answers it with one register write that is heard, in front of
! the next tick.
!
! What tests of the debugger use it for is in README.md, label by label. The properties they lean on:
!   - the program stores into no page that holds its code: its counters are registers, its stores go to the wave
!     memory, to registers of the AICA and to one register of the system bus. It does not write CCR
!   - no FP instruction, no MACH / MACL, and FPSCR stays what reios leaves (0x00040001); FD is clear in SR
!   - g_main_loop goes back by a PC-relative branch, and nothing in it takes the PC from a register or from
!     memory; g_irq returns by rte
!   - g_irq uses the registers of its bank and no stack
!   - no byte access to SCIPD, SCIRE, MCIPD or MCIRE
!
! Registers: r0 to r3 are scratch of _start and of the routines a test enters; g_main_loop has r8 to r11; g_irq
! has r0 to r3 of bank 1.

	.text

	.global	_start
	.type	_start, @function
_start:
	mov.l	.L_sr_setup, r0
	ldc	r0, sr			! privileged, bank 0, interrupts blocked for the set-up, FD clear
	mov.l	.L_vbr, r0
	ldc	r0, vbr			! g_irq is at VBR + 0x600
	mov.l	.L_stack, r15		! the top of main memory

	mov.l	.L_wave0, r2		! the first tick of the AICA passes: the ARM7 gets a program of one word
	mov.l	.L_arm7_spin, r0
	mov.l	r0, @r2
	mov.l	.L_armrst, r1
	mov	#0, r0
	mov.b	r0, @r1			! and is released: the AICA has a clock from here on
	mov.l	.L_scipd, r1
	mov.w	.L_sample_done, r3
.L_wait_tick:
	mov.l	@r1, r0
	tst	r3, r0
	bt	.L_wait_tick		! until SAMPLE_DONE is set: a tick has run

	mov.l	.L_armrst, r1
	mov	#1, r0
	mov.b	r0, @r1			! the ARM7 is held
	mov	#0, r0
	ldc	r0, r2_bank		! what g_irq counts in
	mov	#0, r8
	mov	#0, r10			! what g_main_loop counts in

	mov.l	.L_image, r1		! the ARM7 program, to wave memory 0
	mov.l	.L_image_end, r3
	mov.l	.L_wave0, r2
.L_copy_image:
	mov.l	@r1+, r0
	mov.l	r0, @r2
	cmp/hs	r3, r1
	bf/s	.L_copy_image
	 add	#4, r2

	mov.l	.L_wave, r1		! one period of a 16-bit wave, to wave memory 0x10000
	mov.l	.L_wave_end, r3
	mov.l	.L_wave1, r2
.L_copy_wave:
	mov.l	@r1+, r0
	mov.l	r0, @r2
	cmp/hs	r3, r1
	bf/s	.L_copy_wave
	 add	#4, r2

	mov.l	.L_table, r1		! the table: (address, value), a long write each; address 0 is an empty entry
	mov.l	.L_table_end, r3
.L_table_next:
	mov.l	@r1+, r2
	mov.l	@r1+, r0
	tst	r2, r2
	bt	.L_table_skip
	mov.l	r0, @r2
.L_table_skip:
	cmp/hs	r3, r1
	bf	.L_table_next

	mov.l	.L_armrst, r1
	mov	#0, r0
	mov.b	r0, @r1			! the ARM7 is released: it starts at a_reset
	.size	_start, .-_start

! The way into the loop: interrupts open
	.type	g_main, @function
g_main:
	mov.l	.L_sr_open, r0
	ldc	r0, sr
	.size	g_main, .-g_main

! Counts. Eight instructions that are neither control flow nor a delay slot, one PC-relative branch back
	.type	g_main_loop, @function
g_main_loop:
	add	#1, r8			! the passes
	mov	r8, r9
	shlr8	r9
	xor	r8, r9
	add	r9, r10			! a sum of them
	rotl	r10
	mov	r10, r11
	and	r8, r11
	bra	g_main_loop
	 nop
	.size	g_main_loop, .-g_main_loop

! SLEEP in a loop; the AICA's interrupt wakes it. Not reached by itself: a test sets the PC
	.type	g_sleep_loop, @function
g_sleep_loop:
	mov.l	.L_sr_open, r0
	ldc	r0, sr
	sleep
	bra	g_sleep_loop
	 nop
	.size	g_sleep_loop, .-g_sleep_loop

! Holds the ARM7 and spins. Not reached by itself
	.type	g_hold_arm7, @function
g_hold_arm7:
	mov.l	.L_sr_open, r0
	ldc	r0, sr
	mov.l	.L_armrst, r1
	mov	#1, r0
	mov.b	r0, @r1
g_hold_arm7_spin:
	bra	g_hold_arm7_spin
	 nop
	.size	g_hold_arm7, .-g_hold_arm7

! Releases the ARM7 and goes to g_main_loop. Not reached by itself
	.type	g_release_arm7, @function
g_release_arm7:
	mov.l	.L_armrst, r1
	mov	#0, r0
	mov.b	r0, @r1
	bra	g_main
	 nop
	.size	g_release_arm7, .-g_release_arm7

! The interrupt of the AICA (IRL 11), on the register bank of the handler: the interrupted code keeps every
! register, and nothing is pushed
	.type	g_irq, @function
g_irq:
	mov.l	.L_mcire, r1
	mov	#0x20, r0
	mov.l	r0, @r1			! MCIRE bit 5: the acknowledge (not a byte access)
	add	#1, r2			! counted
	mov	r2, r0
	and	#1, r0
	shll2	r0
	shll2	r0
	mov.w	.L_pan, r3
	or	r3, r0			! DISDL 15 and DIPAN 0x0F or 0x1F: one side, then the other
	mov.l	.L_ch1_pan, r1
	mov.l	r0, @r1			! the pan of the second channel: heard
	rte
	 nop
	.size	g_irq, .-g_irq

	.align	1
.L_pan:		.word	0x0F0F
.L_sample_done:	.word	0x0400
	.align	2
.L_sr_setup:	.long	0x500000F0
.L_sr_open:	.long	0x40000000
.L_vbr:		.long	g_irq - 0x600
.L_stack:	.long	0x8D000000
.L_armrst:	.long	0xA0702C00
.L_wave0:	.long	0xA0800000
.L_arm7_spin:	.long	0xEAFFFFFE	! b . for the ARM7
.L_scipd:	.long	0xA07028A0
.L_wave1:	.long	0xA0810000
.L_image:	.long	g_arm7_image
.L_image_end:	.long	g_arm7_image_end
.L_wave:	.long	g_wave
.L_wave_end:	.long	g_wave_end
.L_table:	.long	g_table
.L_table_end:	.long	g_table_end
.L_mcire:	.long	0xA07028BC
.L_ch1_pan:	.long	0xA07000A4

! What _start writes, in this order. Registers of the AICA are at 0xA0700000 + their offset and take 16 bits
	.align	2
	.type	g_table, @object
g_table:
	.long	0xA07028BC, 0x000007FF	! MCIRE: nothing is pending for the SH4
	.long	0xA07028B4, 0x00000020	! MCIEB: bit 5, the interrupt the ARM7 raises
	.long	0xA05F6924, 0x00000002	! SB_IML4EXT: the AICA's interrupt reaches the SH4 as IRL 11
	.long	0xA0702800, 0x0000000F	! MVOL 15
	! the first channel: 16-bit PCM at 0x10000, a loop of 32 samples
	.long	0xA0700004, 0x00000000	! SA, low part
	.long	0xA0700008, 0x00000000	! LSA
	.long	0xA070000C, 0x00000020	! LEA
	.long	0xA0700010, 0x0000001F	! AR 31, no decay: at full level with the first sample, and held
	.long	0xA0700014, 0x0000001F	! RR 31
	.long	0xA0700018, 0x00000000	! OCT 0, FNS 0: the ARM7 moves it between this and FNS 0x200
	.long	0xA0700024, 0x00000F00	! DISDL 15, to both sides
	! the second channel: the same wave an octave below, on one side
	.long	0xA0700084, 0x00000000
	.long	0xA0700088, 0x00000000
	.long	0xA070008C, 0x00000020
	.long	0xA0700090, 0x0000001F	! (an octave down the attack takes some samples)
	.long	0xA0700094, 0x0000001F
	.long	0xA0700098, 0x00007800	! OCT -1, FNS 0: the ARM7 moves FNS through 0, 0x100, 0x200 and 0x300
	.long	0xA07000A4, 0x00000F0F	! DISDL 15, DIPAN 0x0F: g_irq moves it between this and 0x1F
! Reserved for the DSP part (package E8), empty: MPRO, COEF, MADRS, the ring buffer (RBL, RBP), EFSDL / EFPAN, and
! IMXL / ISEL of the first channel go here, in front of the key on
g_table_dsp:
	.fill	80, 4, 0
g_table_dsp_end:
	! key on: KYONB of both channels, KYONEX with the second (LPCTL, 16-bit PCM, SA high part 1)
	.long	0xA0700000, 0x00004201
	.long	0xA0700080, 0x0000C201
g_table_end:
	.size	g_table, .-g_table

! One period of a sine, 32 samples of 16 bits, and its first samples again (the generator reads one behind the loop)
	.align	2
	.type	g_wave, @object
g_wave:
	.word	0, 1598, 3135, 4551, 5793, 6811
	.word	7568, 8035, 8192, 8035, 7568, 6811
	.word	5793, 4551, 3135, 1598, 0, -1598
	.word	-3135, -4551, -5793, -6811, -7568, -8035
	.word	-8192, -8035, -7568, -6811, -5793, -4551
	.word	-3135, -1598, 0, 1598, 3135, 4551
g_wave_end:
	.size	g_wave, .-g_wave

! The ARM7 program, as gen.sh assembled it from arm7.s
	.align	2
	.type	g_arm7_image, @object
g_arm7_image:
	.incbin	"arm7.bin"
g_arm7_image_end:
	.size	g_arm7_image, .-g_arm7_image
