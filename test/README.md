# Tests

Two kinds, and they answer different questions.

| | what it needs | what it proves |
|---|---|---|
| [`host/`](host/) | a workstation | pure logic, on every commit |
| [`../docs/bench-runbook-device-game.md`](../docs/bench-runbook-device-game.md) | the board, and Daniel | what the hardware actually does |

Everything that *can* be a host test should be. The bench is the scarce
resource — one person, one board, one sitting — so the runbook exists to make a
session produce every measurement at once rather than three sessions producing
one each.

---

## Reproducing the host rig

A suite nobody can run stops being run, and then stops being true. So this is
the whole of it, from a clean machine:

```sh
# 1. A host compiler. PlatformIO's own, so nothing lands system-wide.
pio pkg install -g -p windows_x86

# 2. Run.
bash test/host/run.sh
```

That is the entire setup. To undo it, delete
`~/.platformio/packages/toolchain-gccmingw32`.

**Why that compiler.** This machine has only cross-compilers —
`xtensa-esp32s3-elf-g++` and friends — which produce binaries the workstation
cannot execute. The MinGW package lives inside a tree the toolchain already
owns, is scoped and reversible, and needs no administrator. Set `CXX` to
override it with a system `g++` or `clang++` if you have one.

**Expected output**, so a broken run is recognisable rather than merely
different:

```
compiler: .../toolchain-gccmingw32/bin/g++.exe
== DrillMachine ==
  ---- rail 1: a traffic burst enters no animating phase
  ...
1100 checks, 0 failures
== purity gate ==
ok   the gate fires: Arduino.h is NOT FOUND from the pure build
== forbidden symbols ==
ok   no Arduino-core symbols in the pure TU
ALL HOST TESTS PASSED
```

**Exit codes are three, not two.** `0` all good · `1` a test failed · `2` **the
rig itself is broken**. The third exists because a binary that will not launch
is not a failing test, and reporting it as one sends the reader to the wrong
place — which it did, once, before the code told them apart.

---

## What the host rig guards

1. **The tests** for each pure translation unit.
1b. **The same TU, compiled for the board**, with the xtensa cross-compiler and
   the same narrow include path. In the *same gate* on purpose: the host suite
   proves the logic and not that the code compiles for the device, and for one
   commit the header claimed otherwise while the suite was green. Split across
   two places, the cheaper number becomes the whole answer. A missing
   cross-compiler is reported as the rig being **incomplete**, never skipped.
2. **The purity gate.** `src/game/` compiles with `-I src/game` and nothing
   else. `host/no_arduino.cpp` includes `<Arduino.h>` and **must fail**; a
   success is reported as the failure it is.
3. **A forbidden-symbol scan**, because an include path stops a *header*, not
   somebody hand-declaring `extern "C" unsigned long millis();`.

Adding a TU: add its `.cpp` to the build line in `host/run.sh` and a
`test_*.cpp` beside it. If it needs a new `-I`, **write the reason next to it**
— the narrow include path *is* the guarantee.

---

## The rig got itself wrong three times, and that is the point of writing it down

- **The gate reported success while `Arduino.h` was reachable.** Its
  right-reason check grepped the log for `"Arduino.h"`; with the include path
  widened, the build failed two levels deeper on `freertos/FreeRTOS.h` and the
  log's first line read `In file included from .../Arduino.h:33`. The grep
  matched. It now requires the specific *not-found* diagnostic.
- **The symbol scan matched its own comments**, and `micros` inside
  `microseconds`, reporting a correct file as impure.
- **The host suite was green while the device build failed.** `DrillMachine.h`
  was missing `<stddef.h>`; MinGW pulls `size_t` in transitively and xtensa
  does not. Nothing was broken about the rig — it proved what it covers. The
  header's claim was wider than the rig, which is the inverse of the other two
  and is why the cross-compile now runs in the same command.
- **A binary that would not launch** (exit 127, missing MinGW DLLs) was reported
  as a failing test. Binaries are now linked static and the rig separates its
  own breakage from a real failure.

All four are the same lesson as
[`valar-eam-feed/docs/verification-ledger.md`](https://github.com/Valar-Systems/valar-eam-feed/blob/main/docs/verification-ledger.md):
**run the control before believing the result** — and the fourth adds: *check
that what you claim about the result is no wider than what the control covered.*
