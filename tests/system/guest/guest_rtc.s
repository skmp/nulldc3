! Prints the real-time clock of the AICA (00710000: high half, 00710004: low half) as "rtc=XXXXXXXX" on the
! serial port and returns to its caller: the BIOS.
! A raw image, loaded and entered at 8C010000.
	.text
	.global	start
start:
	mov.l	rtc, r4
	mov.l	@r4, r5
	mov.l	@(4,r4), r6
	shll16	r5
	extu.w	r6, r6
	or	r6, r5
	mov.l	scftdr2, r2
	mov	#114, r0		! r
	mov.b	r0, @r2
	mov	#116, r0		! t
	mov.b	r0, @r2
	mov	#99, r0			! c
	mov.b	r0, @r2
	mov	#61, r0			! =
	mov.b	r0, @r2
	mov	#8, r7
	mov	#10, r1
1:	rotl	r5
	rotl	r5
	rotl	r5
	rotl	r5
	mov	r5, r0
	and	#15, r0
	cmp/hs	r1, r0
	bf	2f
	add	#7, r0
2:	add	#48, r0
	mov.b	r0, @r2
	dt	r7
	bf	1b
	mov	#10, r0
	mov.b	r0, @r2
	rts
	 nop
	.align	2
rtc:		.long	0xa0710000
scftdr2:	.long	0xffe8000c
