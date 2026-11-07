# A crash address in the boot report: the bench proof, and #582's bench row

**For v20 or later; v19 does not wait for this.** v18 already writes a crash dump to flash on every panic, and nothing reads
it. This document proposes reading it, and freezes the predictions for the bench proof that comes before any code. The
same E2:08 window also runs #582's bench row (Daniel, 2026-11-07 ~02:36Z). The two keep their own rows.

## What is already there

Read from the tree and the installed framework, nothing assumed:

- **The framework** (`framework-arduinoespressif32-libs/esp32s3/sdkconfig`): `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y`,
  ELF format, CRC32, `CONFIG_ESP_COREDUMP_CHECK_BOOT=y`, `CONFIG_ESP_TASK_WDT_PANIC=y`, up to 64 tasks.
- **The s3-128's partition table:** `coredump`, `0xFF0000`, 64 KB.
- **v18's source:** no use of `esp_core_dump`. So the crash PC and backtrace of every panic are in flash, unread.
- **`setup()`:** the task watchdog is 10 s, with `trigger_panic = true`.

The fleet's four crash boots (`docs/device-activation.md`) are why this matters: device #2 panicked twice within about 10
minutes of a power-on, and a reset reason says that it crashed, not where.

## The proposal

**On the device.** On a boot whose reason is `PANIC`, `INT_WDT`, `TASK_WDT`, `WDT` or `CPU_LOCKUP`, with a dump that checks
valid: read `esp_core_dump_get_summary()` and send `<pc>:<task>:<elf sha8>` in a new `X-Blip-Crash` header, under
`BootReportPolicy`'s deliver-until-acknowledged rule. Erase the dump only after the acknowledgement, so one crash is one
report and a lost report is retried.

**The design condition (reviewer, 2026-11-06 ~20:25Z):** the reporter checks the dump's build hash and **erases, without
reporting, any dump from an image it does not know.** A factory board can carry a leftover from a factory test program.

**`TASK_WDT`.** The dump names the context that ran the watchdog, not the task that starved it. So the starved task's name
is stashed in an `RTC_NOINIT` word by `esp_task_wdt_isr_user_handler()` and sent instead.

**In the Worker.** A new `recordCrash()` validates by shape and rejects anything else, never truncating (CLAUDE.md, "a
truncated row is a lie"): a PC inside `0x40370000`-`0x42FFFFFF`, a task name from a fixed charset, an 8-hex hash. Its own
`crash` index. A code address, a task name and a build hash hold no user data.

**Decoding.** `addr2line` against the ELF of that build. Whether release ELFs are archived is still to verify; a rebuild of
the tag differs only in timestamp bytes (CLAUDE.md, the v9 accounting), so the hash names the build and a rebuild decodes it.

**Disclosure.** `README.md`'s Privacy & telemetry section and `proxy/pages/support.html` change in the same PR.

## The window (E2:08, about 40 minutes, Daniel not needed)

The time is chosen by this session and **announced in chat before the first flash**. Never the feeder. E2:08 ends on the
shipping build, verified, with its settings as they were, and its normal cloud feed, as in #445's windows. A planted image
ends no window: stopping early means restoring first.

**The images are local-only, on a bench branch, never merged.** They need no `skus.yml` row and no launch-gate case, since
they ship in nothing, and `bench-flash.ps1` flashes each app-only:

| image | what it adds to the shipping env |
|---|---|
| `wxkey` | at boot, one line: the keys in NVS namespace `config`, as **names and a count**, and whether `wx-layer` is absent, equals `"true"`, equals `"false"`, or is something else. A comparison, **never a value** |
| `wxclear` | as `wxkey`, and it first removes `wx-layer`. Nothing else: the count falls by exactly 1 and the names differ by exactly that key |
| `crashbench` | at boot, `[crash-bench] dump check=<err> pc=... task=... sha8=...`; and `/crash-bench?do=panic`, `do=starve`, `do=erase` on the board's LAN page |

**Why NVS is not read from the PC.** `esptool read_flash` of that partition would put the Wi-Fi credentials and the device
key in a file on disk. Daniel's rule is names and counts only, so the board compares and the PC never holds the bytes.

**Why a page trigger, not serial.** The trace holds the only serial port, once, for the whole measurement.

**The rig.** `bench-trace.ps1`, one open per step, never repeated; `read_mac` afterwards, because closing a trace leaves an
s3-128 in download mode. The dump is read back two ways, so the board is not trusting itself: the boot line, and
`esptool read_flash 0xFF0000 0x10000` with `esp-coredump` 1.16.0 against the bench ELF. That partition holds task stacks,
which can hold anything the firmware held, including a key: **the dump file stays local, is never committed or quoted, and
is deleted after the read.** Only `pc`, `task` and sizes are recorded.

### #582's rows, on the candidate (the merged #582 or its approved head)

| row | step | predicted |
|---|---|---|
| **BW0** | flash `wxkey` | `wx-layer` equals `"true"`: E2:08's saved value from #445's windows, the control that the read can see a key |
| **BW1** | flash `wxclear`, one boot | `wx-layer` removed; the count falls by exactly 1; the names differ by exactly `wx-layer`; the board rejoins Wi-Fi |
| **BW2** | flash the candidate, **no save** | the page shows the box ticked; `[wx] buffers` allocated; a first `[wx]` line with status 200; the layer rebuilt |
| **BW3** | untick, save, reboot | only `wx-layer` changes; no `[wx]` fetch for 3 minutes; the page shows the box unticked |
| **BW4** | flash `wxkey` | `wx-layer` equals `"false"` |
| **BW5** | restore: tick, save, shipping build | banner shipping, `[wx-bench]` absent, layer on, look A, Cloud URL blank |

**"Weather draws" is graded on the lines, not the glass.** A setting of the Cloud URL is a save, and a save writes the key,
so the replay proxy cannot be used in BW2. If the sky over E2:08 is dry the layer draws nothing to see, and that says so.
The `use` of the switch, which WD5b cannot see, is what BW2 and BW3 prove between them: on with the key absent, off with
it `"false"`.

### K rows

| row | step | predicted |
|---|---|---|
| **K0** | flash `crashbench`; if a stale dump is found, `do=erase` and reboot | **new control:** `check` reads "no valid dump" before any planted crash. A check that has only read "found" proves nothing |
| **K1** | `do=panic` | reason `PANIC`; `check` OK |
| **K2** | the same dump | fits: no "not enough space" in the panic log, `esp-coredump` parses it, its size stated against 64 KB. **The risk row:** if it does not fit, the partition must grow, and that is a factory-image and OTA question |
| **K3** | the same dump | `pc` in `0x42000000`-`0x42FFFFFF` or `0x40370000`-`0x403FFFFF`, and `addr2line` on the bench ELF names the planted line |
| **K4** | `do=starve` | reason `TASK_WDT`; the summary's task is **not** `starved` |
| **K5** | with a dump held: the shipping image, then `crashbench`, both app-only | the dump is still valid. The factory half is arithmetic: the v18 file ends at `0x1F6C00`, below `0xFF0000` |
| **end** | `do=erase`; shipping build; boot read | no dump left; banner shipping |

**Why E2:08, not COM16:** K2 depends on the tasks running, and COM16 has no Wi-Fi, so its count would flatter the answer.

**Outside the set:** no dump after a planted panic with K2 satisfied means the linked config is not what the sdkconfig
reads: stop and read the ELF for the core-dump symbols. Any row reading otherwise: stop, restore, report.

## Results (E2:08, 2026-11-07)

### #582's bench row: every row as predicted (03:12-03:38Z, on `3a868b966fcf`)

| row | read |
|---|---|
| **BW0** | `wx-layer` equals `"true"`, 82 keys |
| **BW1** | the one-time image: `wx-layer` absent, keys 82 to 81, the only name lost is `wx-layer`, none gained, Wi-Fi rejoined |
| **BW2** | the candidate with **no save**: box ticked, buffers allocated, 4 `[wx]` lines with status 200 and 304, the layer rebuilt 3 times |
| **BW3** | untick, save, reboot: no buffers, no fetch in 4 minutes, box unticked |
| **BW4** | `wx-layer` equals `"false"`, 82 keys |
| **BW5** | shipping banner, no bench line, layer on, look A, Cloud URL blank |

BW2 and BW3 together cover the use of the key, which WD5b cannot see. No value was printed, and the PC never held the NVS bytes.

### K: the first window stopped at K1 on my plant

K0 read as predicted (check 260, size 0). `do=panic` answered 200 and **the board never crashed**: every line read `reset=11`,
and production's boot record shows no boot after it. **The cause, read from the ELF: the compiler deleted the plant.**
`volatile int* p = nullptr; *p = 1` is a constant null store, which is undefined, so the ELF had no `PlantedPanic` and `Loop()`
returned after the flush. The route, the flag and the loop all worked. **My miss:** I flashed an image without reading it.

### K: the second window (05:52-05:58Z, bench commit `fbc95485`)

**Changed before it ran:** the pointer is volatile too, so the store is real. The driver reads the ELF first and refuses to
flash if the plant or its call is missing. The route prints the parameter it received and answers with the board's dump
line, so state is read over the LAN: a `read_mac` to recover a lost trace would overwrite the reset reason. The boot timer I
had proposed was dropped: with the cause found it tested nothing, and it risked a reboot loop. K3 was restated to the
symbol range, because `addr2line` returned `??:?`.

| row | read | verdict |
|---|---|---|
| **K0** | check 260, size 0 | as predicted |
| **K-R** | route lines `ping`, `panic`, and `state` after the reboot; the planted line seen | as predicted |
| **K1** | reset `PANIC`, check `ESP_OK` | as predicted |
| **K2** | **the dump fits: 29,412 B of 65,536**; `esp-coredump` parsed it | as predicted |
| **K3** | pc inside `PlantedPanic`, task `loopTask`; **the two readers' PCs differ by 3 bytes** | **OFF** |
| **K4, K5, end** | not run | unread |

**The 3 bytes, and what they mean.** `esp-coredump` reads the faulting store itself, at offset 20. The board's summary reads
offset 17: `exc_pc` comes back 3 bytes lower. I predicted they would agree; that is my miss. **For the reporter: the address in
the summary needs that adjustment, or a decoder lands one instruction early.**

`esp-coredump` did what the frozen K3 asked, through a tool I had written off: it named `include/BenchWindow.h:31`, the planted
line, with the backtrace `loop`, `Loop`, `PlantedPanic`, `exccause 0x1d`, `excvaddr 0x0`. Only `addr2line` could not.

**The dump window 2 planted is still in E2:08's flash.** K-end's erase did not run. Nothing reads it, and the next crash
overwrites it. E2:08 ended on the shipping build, verified at 05:58:44Z. The session's files hold no address and no key
shape, with a planted control found. No dump file remains on the PC.
