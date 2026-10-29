/* BUILD Q ONLY (docs/weatherscope-stage3.md A12.25, Q30): the Bend loop's 15 crops and its synthetic dry crop,
 * embedded so their decode is timed on the board ('Q'). Sorted name order: the 15 frames oldest first, then
 * synthetic-clear. THE KNOWN TRAP (WxLoopFrames.S): SCons does not track an .incbin's file; the ELF is checked for
 * each crop's bytes, once each, before any result is believed. This branch is never merged. */

    .section .rodata.wx_bend, "a"

    .balign 4
    .global wx_bend_00_start
wx_bend_00_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-191000.wxc"
    .global wx_bend_00_end
wx_bend_00_end:

    .balign 4
    .global wx_bend_01_start
wx_bend_01_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-191400.wxc"
    .global wx_bend_01_end
wx_bend_01_end:

    .balign 4
    .global wx_bend_02_start
wx_bend_02_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-191800.wxc"
    .global wx_bend_02_end
wx_bend_02_end:

    .balign 4
    .global wx_bend_03_start
wx_bend_03_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-192200.wxc"
    .global wx_bend_03_end
wx_bend_03_end:

    .balign 4
    .global wx_bend_04_start
wx_bend_04_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-192600.wxc"
    .global wx_bend_04_end
wx_bend_04_end:

    .balign 4
    .global wx_bend_05_start
wx_bend_05_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-193000.wxc"
    .global wx_bend_05_end
wx_bend_05_end:

    .balign 4
    .global wx_bend_06_start
wx_bend_06_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-193400.wxc"
    .global wx_bend_06_end
wx_bend_06_end:

    .balign 4
    .global wx_bend_07_start
wx_bend_07_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-193800.wxc"
    .global wx_bend_07_end
wx_bend_07_end:

    .balign 4
    .global wx_bend_08_start
wx_bend_08_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-194200.wxc"
    .global wx_bend_08_end
wx_bend_08_end:

    .balign 4
    .global wx_bend_09_start
wx_bend_09_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-194600.wxc"
    .global wx_bend_09_end
wx_bend_09_end:

    .balign 4
    .global wx_bend_10_start
wx_bend_10_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-195000.wxc"
    .global wx_bend_10_end
wx_bend_10_end:

    .balign 4
    .global wx_bend_11_start
wx_bend_11_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-195400.wxc"
    .global wx_bend_11_end
wx_bend_11_end:

    .balign 4
    .global wx_bend_12_start
wx_bend_12_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-195800.wxc"
    .global wx_bend_12_end
wx_bend_12_end:

    .balign 4
    .global wx_bend_13_start
wx_bend_13_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-200200.wxc"
    .global wx_bend_13_end
wx_bend_13_end:

    .balign 4
    .global wx_bend_14_start
wx_bend_14_start:
    .incbin "test/fixtures/wx/bend-loop/shsr-20240602-200600.wxc"
    .global wx_bend_14_end
wx_bend_14_end:

    .balign 4
    .global wx_bend_15_start
wx_bend_15_start:
    .incbin "test/fixtures/wx/bend-loop/synthetic-clear.wxc"
    .global wx_bend_15_end
wx_bend_15_end:
