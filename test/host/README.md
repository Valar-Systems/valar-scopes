# Host tests

Pure translation units, compiled and run on the workstation. No board, no
PlatformIO, no framework.

```sh
bash test/host/run.sh
```

Exit: `0` all good · `1` a test failed · `2` the rig itself is broken. The
distinction matters — a binary that will not launch is not a failing test, and
reporting it as one sends the reader to the wrong place.

## The compiler

PlatformIO's own MinGW package, so nothing is installed system-wide:

```sh
pio pkg install -g -p windows_x86
```

Removing `~/.platformio/packages/toolchain-gccmingw32` undoes it. Set `CXX` to
override. Binaries are linked static (`-static-libgcc -static-libstdc++`) so
they do not need the toolchain's DLLs on `PATH`.

## What the run does

1. **Builds and runs the tests** for each pure TU.
2. **The purity gate.** `src/game/` is compiled with `-I src/game` and nothing
   else — no Arduino core, no ESP-IDF, no LovyanGFX. `no_arduino.cpp` includes
   `<Arduino.h>` and **must fail to compile**; a success there is reported as
   the failure it is.
3. **A forbidden-symbol scan**, because an include path stops a *header* and
   not somebody hand-declaring `extern "C" unsigned long millis();`.

## Adding a TU

Add its `.cpp` to the build line in `run.sh` and a `test_*.cpp` beside it. If it
needs a new `-I`, write the reason next to it — the narrow include path *is* the
guarantee.

## Two things this rig got wrong first, both worth keeping in mind

**The gate reported success while Arduino.h was reachable.** The right-reason
check grepped the log for `"Arduino.h"`. Running the control for that control —
widening the include path to reach the real Arduino core — the build still
failed, but two levels deeper on `freertos/FreeRTOS.h`, and the log's first line
reads `In file included from .../Arduino.h:33`. The grep matched. The gate
printed `ok`. It now requires the specific *not-found* diagnostic.

**The symbol scan matched its own comments** and the word `micros` inside
`microseconds`, reporting a correct file as impure. Comments are stripped first
and the patterns require a call or a member access.

Both are the same lesson as `valar-eam-feed/docs/verification-ledger.md`: run
the control before believing the result.
