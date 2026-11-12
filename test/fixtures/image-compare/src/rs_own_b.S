/* rodown (1h'), B: f1's own constant changed -- f1 must read CHANGED */
	.section .rodata
	.align	4
.Lc1:	.word 0x11111112, 0x22222222
	.section .text.f1,"ax",@progbits
	.literal_position
	.literal .Lk_f1, .Lc1
	.align	4
	.global	f1
	.type	f1, @function
f1:
	entry	a1, 32
	l32r	a2, .Lk_f1
	l32i	a2, a2, 0
	retw.n
	.size	f1, .-f1
