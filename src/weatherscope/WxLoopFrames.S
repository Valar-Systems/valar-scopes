/* Weatherscope 3.3d: the past-hour loop's 15 stored crops (docs/weatherscope-stage3.md A8.10, A8.20), embedded as
 * read-only data straight from test/fixtures/wx/loop/, oldest first.
 *
 * WHY .incbin AND NOT board_build.embed_files: platformio.ini is a file the Blipscope weather work (#445) also
 * changes, so 3.3d keeps out of it (A8.20). THE KNOWN TRAP: SCons does not track an .incbin's file as a dependency,
 * so a changed crop would NOT rebuild this object. The crops are fixed and checked (D1: make_loop.mts --check), and
 * the built ELF is checked for each crop's bytes, once each, before any board result is believed (A8.20). If a crop
 * ever changes, touch this file.
 *
 * Paths are relative to the project directory, where PlatformIO runs the assembler. */

    .section .rodata.wx_loop, "a"

    .balign 4
    .global wx_loop_00_start
wx_loop_00_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-210400.wxc"
    .global wx_loop_00_end
wx_loop_00_end:

    .balign 4
    .global wx_loop_01_start
wx_loop_01_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-210800.wxc"
    .global wx_loop_01_end
wx_loop_01_end:

    .balign 4
    .global wx_loop_02_start
wx_loop_02_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-211200.wxc"
    .global wx_loop_02_end
wx_loop_02_end:

    .balign 4
    .global wx_loop_03_start
wx_loop_03_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-211600.wxc"
    .global wx_loop_03_end
wx_loop_03_end:

    .balign 4
    .global wx_loop_04_start
wx_loop_04_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-212000.wxc"
    .global wx_loop_04_end
wx_loop_04_end:

    .balign 4
    .global wx_loop_05_start
wx_loop_05_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-212400.wxc"
    .global wx_loop_05_end
wx_loop_05_end:

    .balign 4
    .global wx_loop_06_start
wx_loop_06_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-212800.wxc"
    .global wx_loop_06_end
wx_loop_06_end:

    .balign 4
    .global wx_loop_07_start
wx_loop_07_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-213200.wxc"
    .global wx_loop_07_end
wx_loop_07_end:

    .balign 4
    .global wx_loop_08_start
wx_loop_08_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-213600.wxc"
    .global wx_loop_08_end
wx_loop_08_end:

    .balign 4
    .global wx_loop_09_start
wx_loop_09_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-214000.wxc"
    .global wx_loop_09_end
wx_loop_09_end:

    .balign 4
    .global wx_loop_10_start
wx_loop_10_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-214400.wxc"
    .global wx_loop_10_end
wx_loop_10_end:

    .balign 4
    .global wx_loop_11_start
wx_loop_11_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-214800.wxc"
    .global wx_loop_11_end
wx_loop_11_end:

    .balign 4
    .global wx_loop_12_start
wx_loop_12_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-215200.wxc"
    .global wx_loop_12_end
wx_loop_12_end:

    .balign 4
    .global wx_loop_13_start
wx_loop_13_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-215600.wxc"
    .global wx_loop_13_end
wx_loop_13_end:

    .balign 4
    .global wx_loop_14_start
wx_loop_14_start:
    .incbin "test/fixtures/wx/loop/shsr-20240521-220000.wxc"
    .global wx_loop_14_end
wx_loop_14_end:
