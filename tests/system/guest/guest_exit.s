! Prints a line on the serial port, spends some time in a loop and returns to its caller: the BIOS.
! A raw image, loaded and entered at 8C010000.
	.text
	.global	start
start:
	mova	message, r0
	mov	r0, r1
	mov.l	scftdr2, r2
1:	mov.b	@r1+, r0
	tst	r0, r0
	bt	2f
	mov.b	r0, @r2
	bra	1b
	 nop
2:	mov.l	count, r3
3:	dt	r3
	bf	3b
	rts
	 nop
	.align	2
scftdr2:	.long	0xffe8000c
count:		.long	2000000
message:	.asciz	"guest: returning to the BIOS\n"
	.align	2
