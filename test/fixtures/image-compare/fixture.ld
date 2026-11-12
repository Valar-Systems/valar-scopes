/* image-compare fixtures: .text_hi is listed first so hi_* sections land there; --section-start moves it. */
SECTIONS
{
  .text_hi 0x40100000 : { *(.literal.hi_* .text.hi_*) }
  .text_lo 0x40000000 : { *(.literal .literal.* .text .text.*) }
  .rodata 0x3f000000 : { *(.rodata .rodata.*) }
  .data 0x3fc00000 : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
}
