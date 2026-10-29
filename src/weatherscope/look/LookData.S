/* The light-look bench select's data (docs/weatherscope-stage3.md A12.27, A12.32): BENCH ONLY, never merged.
 * build-look/ is written by tools/weatherscope/screens/look_frames.py build-look and is not tracked; its two files'
 * sha256 are recorded in the plan. THE KNOWN TRAP (WxLoopFrames.S): SCons does not track an .incbin's file, so the
 * ELF is checked for both files' bytes, once each, before any board result is believed. */

    .section .rodata.look, "a"

    .balign 4
    .global look_frames_start
look_frames_start:
    .incbin "build-look/look-frames.bin"
    .global look_frames_end
look_frames_end:

    .balign 4
    .global look_pal_start
look_pal_start:
    .incbin "build-look/look-pal.bin"
    .global look_pal_end
look_pal_end:
