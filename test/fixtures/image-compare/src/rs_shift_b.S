/* rodshift, B: f0's constant inserted ahead of f1's; f1 itself unchanged */
	.section .rodata
	.align	4
.Lc0:	.word 0x33333333, 0x44444444
.Lc1:	.word 0x11111111, 0x22222222
	.section .text.f0,"ax",@progbits
	.literal_position
	.literal .Lk_f0, .Lc0
	.align	4
	.global	f0
	.type	f0, @function
f0:
	entry	a1, 32
	l32r	a2, .Lk_f0
	l32i	a2, a2, 0
	retw.n
	.size	f0, .-f0
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
