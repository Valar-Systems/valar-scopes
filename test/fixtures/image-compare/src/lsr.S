/* lstr (amendment 1k): each function reads its datum through a LOCAL label with a non-zero addend, so the relocation
   names the label itself (gas keeps a local symbol for a non-zero offset into a merged section), as P-FLIP's mdns object
   does (.LC35+38). lsr: .LS0+4 in a merged-STRING section. lcs: .LC9+4 in a merged CONSTANT pool, not strings (its
   second word holds a zero byte, so a reader that wrongly took it for a string would find one). gen.sh asserts both. */
	.section .rodata.lsr.str1.1,"aMS",@progbits,1
.LS0:
	.string "abcdhello local string"

	.section .rodata.lcs.cst4,"aM",@progbits,4
	.align 4
.LC9:
	.word 0x11111111
	.word 0x00345678

	.section .text.lsr,"ax",@progbits
	.literal_position
	.literal .LLs, .LS0+4
	.align 4
	.global lsr
	.type lsr, @function
lsr:
	entry	a1, 32
	l32r	a2, .LLs
	l8ui	a2, a2, 0
	retw.n
	.size	lsr, .-lsr

	.section .text.lcs,"ax",@progbits
	.literal_position
	.literal .LLc, .LC9+4
	.align 4
	.global lcs
	.type lcs, @function
lcs:
	entry	a1, 32
	l32r	a2, .LLc
	l32i	a2, a2, 0
	retw.n
	.size	lcs, .-lcs
