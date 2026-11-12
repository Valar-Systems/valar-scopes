/* callswap (amendment 1i), variant a: caller calls fa. _call8 keeps the assembler from making it a long call, so the
   swap between A and B is one instruction of the same length (the owner's control: one call target, the same size) */
	.section .text.caller,"ax",@progbits
	.literal_position
	.align	4
	.global	caller
	.type	caller, @function
caller:
	entry	a1, 32
	mov.n	a10, a2
	_call8	fa
	mov.n	a2, a10
	retw.n
	.size	caller, .-caller
