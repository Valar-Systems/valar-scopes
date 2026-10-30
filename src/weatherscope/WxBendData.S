/* Weatherscope 3.5b: stored mode's second place, Bend (docs/weatherscope-stage3.md A10.4, A12.53), embedded as
 * read-only data straight from test/fixtures/wx/: the loop's 15 SeamlessHSR crops (bend-loop/, oldest first), its
 * SYNTHETIC all-dry crop (Q33), and its basemap package (basemap/bend.bmpk). Reached only through the stored factory's
 * Feeds (D1), and like stored mode they must be unreachable on a customer unit before any Weatherscope ships.
 *
 * WHY .incbin: as WxLoopFrames.S, which keeps out of platformio.ini. THE KNOWN TRAP: SCons does not track an
 * .incbin's file as a dependency, so a changed crop or package would NOT rebuild this object. The files are fixed and
 * checked (make_loop.mts --check, make_package.py --check), and the built ELF is checked for each one's bytes, once
 * each, before any board result is believed (A12.53). If one ever changes, touch this file.
 *
 * Paths are relative to the project directory, where PlatformIO runs the assembler. */

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

/* Bend's SYNTHETIC all-dry crop for the stored Bend dry scenario (Q33), at the loop's geometry: never a radar. */
    .balign 4
    .global wx_bend_clear_start
wx_bend_clear_start:
    .incbin "test/fixtures/wx/bend-loop/synthetic-clear.wxc"
    .global wx_bend_clear_end
wx_bend_clear_end:

/* The stored Bend basemap package (BMPK v1, docs/basemap-package-format.md), validated on the device at boot. */
    .balign 4
    .global wx_bend_package_start
wx_bend_package_start:
    .incbin "test/fixtures/wx/basemap/bend.bmpk"
    .global wx_bend_package_end
wx_bend_package_end:
