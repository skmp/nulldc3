! Prints a line on the serial port and counts in a word of RAM, for ever.
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
2:	mov.l	counter, r1
	mov	#0, r3
3:	add	#1, r3
	mov.l	r3, @r1
	bra	3b
	 nop
	.align	2
scftdr2:	.long	0xffe8000c
counter:	.long	0x8c020000
message:	.asciz	"guest: running\n"
	.align	2
