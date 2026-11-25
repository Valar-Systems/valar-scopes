/* THE FRAME BENCH (REG-W-sep Corrections 3 and 4; bench/fp-port-wsep only, never merged): copy Wi of W's per-contact
   path, WHOLE, in IRAM. Every code section of src/WsepI.cpp's object goes to .iram0.wsep, right after .iram0.text, so
   it lies inside _iram_text_start .. _iram_text_end, which sections.ld closes later. A template instantiation cannot
   carry IRAM_ATTR, so the placement is made here, by object. E-IRAM reads it from the ELF.
   Read between memory.ld (which declares iram0_0_seg) and sections.ld: read after sections.ld, ld 2.43.1 says
   ".iram0.text not found for insert". scripts/bench_wsep_iram.py puts it there. */
SECTIONS
{
  .iram0.wsep :
  {
    . = ALIGN(4);
    *WsepI.cpp.o(.literal .text .literal.* .text.*)
  } > iram0_0_seg
}
INSERT AFTER .iram0.text;
