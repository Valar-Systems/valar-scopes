/* swap (amendment 1f), variant op: one instruction in three encodings -- A mov.n, B the same move as a wide or, op a different operation */
	.section .text.sw,"ax",@progbits
	.literal_position
	.align	4
	.global	sw
	.type	sw, @function
sw:
	entry	a1, 32
	mov.n	a6, a2
	_add	a12, a6, a6
	addi	a2, a12, 5
	bnez	a2, 1f
	movi.n	a2, 7
1:
	retw.n
	.size	sw, .-sw
