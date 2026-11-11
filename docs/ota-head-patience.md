# v19's head read waits out an empty read (predictions first, 2026-11-11)

**For:** the reviewer (the orchestrator). **Status:** the registration, frozen before any code. Nothing is coded yet.
**Ruled by the owner (2026-11-11):** approve the fix; v19 may be tagged from a release branch, C plus this fix only;
and the fresh-boot test is skipped for a fix-only candidate whose diff stays inside a scope frozen here (section 6).

## 1. The defect, measured

- **`ReadHead` stops at the first empty read** (`include/OtaInstall.h:69-78`). On the TLS download, an empty read is also
  "nothing has arrived yet": `NetworkClientSecure::read` returns -1 with nothing buffered, and `NetworkClient::readBytes`
  then returns 0 at once (framework-arduinoespressif32 3.3.9). So a body that is merely late is refused as truncated.
  The chain is quoted with file:line in `docs/v19-cut.md`, step 1.
- **On E2:08, through bench image A (C's code):** 13 of the 19 boots that reported refused a correct update as truncated.
  That is window 3, `docs/v19-cut.md`.
- **`writeRest` already waits** (`src/OtaUpdater.cpp:236-256`): 300 empty reads, 100 ms apart. The head read, the first read
  of the same stream, doesn't. It is a second path that skips what the first establishes.

## 2. The change

- **One helper, in `include/OtaInstall.h`:** `ota::ReadSome(src, buf, n)`.
  - It reads. A read that returns 0 calls `src.waitEmpty()` and tries again.
  - It returns the count of the first read that brings bytes, or 0 after `kEmptyReads` empty reads in a row.
  - `kEmptyReads = 300` and `kEmptyWaitMs = 100` are defined there, once.
- **`ReadHead`** loops over `ReadSome` until it has `want` bytes, or until `ReadSome` returns 0. Then `Decide` refuses with
  `Truncated`, as today. The empty count restarts whenever bytes arrive, so the bound is 300 empty reads IN A ROW, about 30 s
  on the device.
- **`writeRest`** reads each chunk through `ReadSome`. At 0 it records `UPDATE_ERROR_STREAM` and aborts, as today. Its
  behaviour doesn't change: the same bound, the same wait, the same order, and the same restart on data. Its literals
  `300` and `100` go.
- **The sources gain `waitEmpty()`:**
  - `StreamSource` and the bench `SerialBudget` wait `delay(kEmptyWaitMs)`;
  - the host `FakeSource` counts the waits instead of sleeping.
- **A bench-only stall** for B-STALL: under `#ifdef OTA_BENCH_HEAD_STALL`, `StreamSource::read` returns 0 while the head is
  being read, and prints an `[ota-bench]` line. `check-no-bench-hooks.sh` already scans every shipping image for that
  marker. No env sets the flag; the bench image is built with it on the command line.
- **Nothing else changes:** not `Decide`, not the parse, not `OtaIdentity.h`, and nothing the device draws.
- **The watchdog:** `src/main.cpp:322-328` subscribes only CPU0's idle task, and `delay()` yields, so a 30 s wait can't
  trigger a reboot. `writeRest` waits the same way today. B-STALL checks this on the board instead of trusting this line.

## 3. The host rows (in `test/host/test_ota_install.cpp`, built and run by `test/host/run.sh`)

| row | the source | predicted |
|---|---|---|
| **HP1** | 5 empty reads, then the `s3-21` head | `Foreign` (today: `Truncated`). With the own head: installed, the flash log `begin,write,writeRest,end` |
| **HP2** | empty for ever | `Truncated`, after exactly 300 empty reads and 299 waits: in `writeRest`'s order today, the 300th empty read ends the read without a wait |
| **HP3** | the head one byte per read, an empty read between each | `Foreign`, after exactly 351 waits |
| **HP4** | 299 empty, 1 byte, 299 empty, 1 byte, and so on to 352 | the head completes: never 300 empty in a row |
| **HP5** | 300 empty, then the head | `Truncated`, and exactly 300 reads made (the head is never read) |
| **HP6** | `ReadSome` alone: 2 empty, then 5 bytes | returns 5, after 2 waits |
| O1-O10, MAP1-MAP3 | unchanged | all pass unchanged |

**Wiring rows,** in `test/host/sabotage_ota_install.py`, read from the source:
- **W-RS1:** `ReadHead` calls `ReadSome` and calls `src.read` nowhere else.
- **W-RS2:** `writeRest` calls `ota::ReadSome` and keeps no empty counter, no `300` and no `delay(100)`.

## 4. The plants (in `test/host/sabotage_ota_install.py`, each edit asserted, the whole red list compared)

| plant | the edit | red set |
|---|---|---|
| **P-NORETRY** | `ReadSome` returns 0 on the first empty read | {HP1, HP2, HP3, HP4, HP5, HP6} |
| **P-BYPASS** | `ReadHead` calls `src.read` directly, as at C | {HP1, HP2, HP3, HP4, HP5, W-RS1}. HP6 stays green, because `ReadSome` itself is intact, which is what tells the two plants apart |

The existing plants (S-O8a, S-O8b, S-ID3) keep their registered red sets.

## 5. The bench rows (after the merge, on bench image A′: the fix's merge commit with the prerelease pin)

- **B-AFTER:** the same window as step 2 (`docs/v19-cut.md`): F and "20" on the bench prerelease, 20 YKUSH cycles of port 3,
  no serial port opened.
  - **Predicted:** 20 `POWERON` boot rows, each with one `ota` row at the same second, **all `refused-foreign`: 0 of 20
    truncated.**
  - **The stops are step 2's:** a boot with no `ota` row; another result; a boot count other than 20; no ping within 180 s;
    a missing anchor. Anything else means the instrument is unreliable.
  - **What it can say:** 0 of 20 against 13 of 19 before. If the rate were unchanged (about 68%), 0 of 20 would come up
    about once in 10^10.
  - A missing `ota` row is still a stop, even though it may be the separate one-shot-report gap and not this fix. The
    truncated count among the reporting rows is printed either way.
- **B-STALL:** one power cycle of the stall image (A′ built with `OTA_BENCH_HEAD_STALL`).
  - **Predicted:** exactly one boot row, and one `ota` row, `refused-truncated`.
  - **The boot row lands about 30 s later than normal:** 52-65 s after power-on, against 22-31 s in window 3, because the
    refusal now waits out its bound before the first check-in.
  - The board answers ping throughout. No second boot row, so no watchdog reboot.
  - Graded at the Worker only.

## 6. The fresh-boot skip: the scope, frozen before the diff is read

**The candidate** is the squash of this fix into `release/v19`. The check compares C (`3a322758eadc`) with that commit.

**Allowed to change. Anything else is out of scope.**

| file | within it, only |
|---|---|
| `include/OtaInstall.h` | `kEmptyReads`, `kEmptyWaitMs`, `ReadSome` (new), `ReadHead` |
| `src/OtaUpdater.cpp` | `StreamSource` (with `waitEmpty()` and the `#ifdef OTA_BENCH_HEAD_STALL` block), `UpdateFlash::writeRest`, the bench `SerialBudget` (`waitEmpty()`) |
| `test/host/test_ota_install.cpp` | the host rows and `FakeSource` |
| `test/host/sabotage_ota_install.py` | the plants and wiring rows |
| `docs/ota-head-patience.md`, `RELEASING.md` | docs |

**The source check.** `git diff --stat C <candidate>` must list only the files above. A function-level diff of the two code
files must change only the names above.

**The artifact check.** It runs on both shipping images, `blipscope-s3-128` and `blipscope-pro-s3-21`, C's ELF against the
candidate's, in two layers:
- **(name, size) per function.** Window 1 showed that this layer alone is BLIND to a same-size change: A and S had identical
  sets but differed in one immediate.
- **Normalized per-function disassembly.** Each function's instructions are compared with addresses replaced by the symbol
  they point at, so a change in layout doesn't count as a change in code.

Only the OTA path may differ: `MaybeUpdateFirmware` (into which `OpenAndInstall`, `Install`, `ReadHead` and `ReadSome`
inline), any out-of-line `ota::ReadSome`, `StreamSource` or `writeRest` instance, and functions whose only difference is
an address the normalization can't name. **Every differing function is named in the result.** A difference outside that set
is out of scope.

**The controls, shown failing before the check is believed:**
- **K1, source:** a planted change in `src/main.cpp`'s `setup()` (the task-watchdog timeout, 10000 -> 10001, in a scratch
  copy) must read OUT OF SCOPE.
- **K2, artifact:** the same plant built into a scratch image must name `setup()` as differing, and with it the (name, size)
  layer, which can't see a same-size immediate.
- **K3, artifact, from data already in hand:** window 1's A and S ELFs must differ in exactly `MaybeUpdateFirmware`, which
  proves the disassembly layer sees a one-immediate change.

**The verdict:**
- **Everything in scope, in source and in both images, with K1-K3 failing as they must:** step 4 is skipped, and O11's
  record says why.
- **Anything out of scope, or a control that doesn't fail, or a layer that can't decide:** step 4 runs, with the owner,
  scheduled through the reviewer.

## 7. Where it goes

- **`release/v19`** was created at C, `3a322758eadc` (2026-11-11). This PR targets it.
  - The registration commit comes first, then the code.
  - CI is read twice by the full sha, at least 60 s apart.
  - It is approved at its sha, and merged by `scripts/merge-by-rule.sh` once that script accepts a base other than `main`.
    **Today it doesn't:** it reads `main` as H (`:43`, `:48`) and checks the squash's parent against it (`:54-56`). On
    this PR it would merge into `release/v19` and only then report "the squash is not the approved merge". The base
    parameter is its own PR, ruled first, and nothing merges here before it.
  - The tag, at step 7, is made from that merge commit.
- **`main` gets the same patch for v20:** `git cherry-pick` of the release branch's squash, with no hand edit, as its own
  PR. Its predictions RUN the proof:
  - (a) `git patch-id --stable` of the two squashes. They match if the context matches; if not, (b) and (c) carry the proof,
    and (a)'s difference is reported.
  - (b) The helper's and the tests' files are byte-identical on both. The bodies of `ReadHead`, `ReadSome`, `writeRest` and
    `StreamSource` are text-identical, compared by a function-level extract.
  - (c) The same host-test results on both trees, the plants included.
  - **A conflict that needs a hand edit is a STOP.**
- **Then v19's steps 2, 3 and 5 repeat on the new candidate,** and step 4 too unless section 6 skips it. B-AFTER and
  B-STALL run in O11's window on the new candidate. Then steps 6-8. `release/v19` takes no other commit, and after the tag
  it is frozen.
- **p4's #648** merges into `main` only after the candidate is cut, as ruled.

## 8. CI, predicted before each push (base `release/v19` = C)

C's job set is main's, except #633's added steps in one existing job. So:
- **The registration commit (docs and `RELEASING.md`):** 32 = 29 success / 3 skipped / 0 failed, the docs-only shape (#638,
  bdcc6bac2d2b).
- **The code commit** (`include/`, `src/`, `test/host/`): 34 = 30 / 4 / 0, the shape of a `src/` change, as #631 read at C
  itself.
- **Out of the set, at any step:** stop and report.

## 9. Amendment 1, made before any code was committed and before any C..candidate diff was read (2026-11-11 ~23:00Z)

**Ruled (a) by the reviewer at 22:57Z,** on blipscope-5c's report of ~22:55Z, which said in full:
- "THE GAP: the six HP rows go into test/host/test_ota_install.cpp, which moves that suite's check count from 36 to 42.
  TWO drivers pin that count for their --control pass:
  - test/host/sabotage_ota_install.py (`INSTALL_CHECKS = 36`): in scope.
  - test/host/sabotage_ota_identity.py:114 (`INSTALL_CHECKS = 36  # test_ota_install's count, frozen in A12.104`): NOT in
    the scope frozen in #651 section 6."
- "WHY IT'S MY MISS: I froze the file list without searching for other readers of the suite's count. That's the same
  'find every reader' step I did for /healthz's commit, which I skipped here."

**The state when this was written:** some of the in-scope code existed locally (`include/OtaInstall.h`), uncommitted. No
code was committed, and no diff from C to a candidate had been read.

**The scope, amended.** Added to section 6's table, and nothing else: **`test/host/sabotage_ota_identity.py` —
`INSTALL_CHECKS` and its comment only.** It is a host-only test driver: it ships in no image, and touches none of setup,
Wi-Fi, the logbook, claims or touch.

**The reader sweep the ruling asked for.** It covers every symbol, constant, count, file and source type the fix touches,
run as `git grep -nIE '<pattern>' 1c1f57547463 -- . ':!docs'`:

| pattern | hits | readers found |
|---|---|---|
| `ReadHead\|ReadSome\|kEmptyReads\|kEmptyWaitMs\|waitEmpty` | 2 | `ReadHead`'s definition and its one caller, `Install` (`include/OtaInstall.h:69`, `:125`) |
| `OtaInstall\.h` | 15 | included by `src/OtaUpdater.cpp` and `test/host/test_ota_install.cpp`; copied by the two sabotage drivers; named in comments in `run.sh` and `scripts/reconcile-fleet.py` (its result tokens, which this fix doesn't change) |
| `ota::Install\(\|Install\(src\|FinishImage\|writeRest` | 22 | `Install` from `OpenAndInstall` (`src/OtaUpdater.cpp:331`) and the bench (`:419`); `writeRest` on the device (`:236`) and in the host's `FakeFlash` (`test/host/test_ota_install.cpp:108`) |
| `INSTALL_CHECKS\|test_ota_install` | 14 | the count is pinned in `sabotage_ota_install.py:134` and `sabotage_ota_identity.py:114`; the suite is built by `run.sh:2015` and both drivers |
| `OTA_BENCH_HEAD_STALL\|\[ota-bench\]` | 13 | the marker is in `check-no-bench-hooks.sh`'s list; `tools/weatherscope/board/ota_bench.py` parses only the `more` and `result=` lines, so a `head stall` line can't be misread |
| `StreamSource\|SerialBudget\|struct [A-Za-z]*Source` | 5 | exactly three source types reach `Install`: `StreamSource`, `SerialBudget` (bench) and `FakeSource` (host). All three gain `waitEmpty()` |

**What the sweep changed, beyond the count.** Both are made here, before code, so neither can surface quietly later:
1. **HP1, HP3 and HP4 use the device's OWN head and expect `Installed`.** As registered, HP1 and HP3 expected `Foreign`
   from the `s3-21` head. But `sabotage_ota_identity.py`'s S-ID2b (`Accept` compares the scope only) makes that head
   acceptable, so S-ID2b would turn HP1 and HP3 red and its registered red set would have to grow, outside even the
   amended scope. The rows test the patience, not the board check, so they use the own head:
   - HP1: 5 empty reads, then the own image; `Installed`, the flash log `begin,write,writeRest,end` (at C: `Truncated`);
   - HP3: the own head one byte per read with an empty read between each, then the rest; `Installed`, after exactly 351
     waits;
   - HP4: 299 empty, 1 byte, and so on to 352 bytes, then the rest; `Installed`.
   - HP2, HP5 and HP6 are unchanged.
   - Under S-ID1b and S-ID2b the own head is accepted, so both identity plants keep their registered sets.
2. **S-O8a's registered red set gains {HP2, HP5}.** S-O8a writes first and decides after, so a truncated head shows up in
   the flash, not the decision: HP2 reads `flash-failed`, and HP5 reads well past 300 reads. It is the same reason I6 is
   already in that set. **S-O8b, S-ID3, S-ID1b and S-ID2b keep their registered sets exactly.**
   - P-NORETRY's red set stays {HP1-HP6}, and P-BYPASS's stays {HP1-HP5, W-RS1}.
   - `INSTALL_CHECKS` becomes 42 in both drivers.

Section 6's check otherwise runs exactly as registered. Anything else out of scope, or an instrument that can't decide,
means step 4 runs with the owner.

## 10. Amendment 2: S-O8b's plant compiles again (ruled (b) at 23:04Z; made before the code commit)

**The reading that stopped the local run** (WSL, g++ 13.3, uncommitted code). S-O8b "did not compile (rig)", with
`OtaInstall.h:150:20: error: 'head' may be used uninitialized [-Werror=maybe-uninitialized]` at the planted line
`if (!StartImage(flash, total, head, got)) ...  // SABOTAGE S-O8b`. A plant that doesn't compile never ran, so it gives no
verdict. Everything else read as predicted: the suite 42/0; S-O8a 22/22; S-ID3 9/9; P-NORETRY 6/6; P-BYPASS 6/6; the wiring
controls; `--control` 42; `--anchors` 5 of 5.

**The cause, checked against C.** The original driver, run on C's exact committed files (`git cat-file` blobs, the same
compiler), read S-O8a 20/20, **S-O8b 14/14** and S-ID3 9/9, all as predicted. So the change introduced the warning.
- **The mechanism:** S-O8b's plant hands `head` to `StartImage` before deciding, even when the head read returned 0 bytes.
  Once `ReadHead` loops over `ReadSome`, GCC can no longer prove `head` is written on that path.
- **It is a false positive on the plant's path alone.** The unplanted suite builds clean with `-Werror` under the same
  compiler, the real `Install` uses `head` only after `Decide` has accepted 352 bytes, and with `got` = 0, `StartImage`
  writes 0 bytes.

**The change, in S-O8b's replacement text only** (`test/host/sabotage_ota_install.py`), around exactly the planted lines:
```
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    <the four planted lines, unchanged>
#pragma GCC diagnostic pop
```
It is not a change to `Install`, which isn't in section 6's scope, and not a driver-wide flag, which would hide the same
warning for every plant.

**Proved to take before the driver runs S-O8b:** a compile-only check of the planted source under g++ 13.3 (staged with the
driver's own functions) **compiles with `-Werror`, with 0 warnings without it.**

**Predicted:**
- **S-O8b's registered red set is unchanged:** I1-I6 each `-log` and `-target`, O10 and MAP2.
- **HP2 and HP5 still read `Truncated`,** decided before `FinishImage`.
- **HP1, HP3 and HP4 still install.**
