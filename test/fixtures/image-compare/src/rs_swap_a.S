/* rodswap (1h'), A: two byte-identical constants, f1 -> the first, f2 -> the second */
	.section .rodata
	.align	4
.Lca:	.word 0x55555555, 0x66666666
.Lcb:	.word 0x55555555, 0x66666666
	.section .text.f1,"ax",@progbits
	.literal_position
	.literal .Lk_f1, .Lca
	.align	4
	.global	f1
	.type	f1, @function
f1:
	entry	a1, 32
	l32r	a2, .Lk_f1
	l32i	a2, a2, 0
	retw.n
	.size	f1, .-f1
	.section .text.f2,"ax",@progbits
	.literal_position
	.literal .Lk_f2, .Lcb
	.align	4
	.global	f2
	.type	f2, @function
f2:
	entry	a1, 32
	l32r	a2, .Lk_f2
	l32i	a2, a2, 0
	retw.n
	.size	f2, .-f2
