# Save the logbook immediately on an owner claim (v17 bug fix)

**Status: built on `fix/v17-save-on-claim`; graded on hardware below.** Line refs at `74866aa`.

## The defect (v15 and v16)

**A newly claimed aircraft may be missing from Collection until the page is refreshed, sometimes
more than once.** Found by v16's fresh-boot acceptance on 2026-10-08. Daniel's first Collection
view after claiming E75L did not show it; it appeared only after a refresh.

- The Collection page is served **from NVS**: `Logbook::JsonStream` reads the stored blobs.
- A claim marks the book dirty, and it reaches NVS only at the next **10-minute** write
  (`PERSIST_INTERVAL_MS`, `Logbook.h:324`), or when a page load requests a save
  (`/logbook.json` sets `logbookFlushRequested`).
- **That requested save lands after the request's own response**: `Logbook.cpp:1018-1022`,
  *"it is the NEXT read that sees it"*. It is also rate-limited to one per 30 s
  (`FETCH_PERSIST_MIN_MS`, `Logbook.h:212`).
- On the acceptance board, BE30 (claimed 11:39:28) was missing from three page loads over 76 s,
  because its first fetch fell inside the 30 s window.

## Fix

`ClaimTappedAircraft` (`AircraftManager.cpp:5388`) calls `logbook.PersistNow()` after a claim
lands. It claims only on a **new type**, and the airline, country and route airports ride along
with it, so **one save per claiming tap** covers everything that tap claimed.

## Worst-case NVS writes

- **Size of one save:** the acceptance board's book was 2,183 B of blobs at 37 types. That is
  ~70 NVS entries of data plus ~10 for headers, so **~80 entries per save**. The s3-128's NVS
  holds 2,646 entries (21 pages).
- **What the book already writes:** one save per 10 minutes while dirty, and on a live sky it is
  always dirty. That is ~144 saves, or ~11,500 entries, per day.
- **How many claims there can be:** `ClaimType` succeeds **once per type for the life of the
  device** (until a factory reset). So the total of claim saves is bounded by the distinct types
  ever seen (hundreds), not by how often anyone taps.
- **Worst day:** a new owner tapping every unclaimed type in one session. The acceptance board
  saw 37 types in ~2.5 h; a busy day might be ~150. That is **~150 extra saves (~12,000
  entries)**, about double that day's normal wear. It is ~4-5 erases per NVS page, against
  100,000 cycles.
- **After the first week:** a few claims a day, under 5% added.
- **A debounce is not needed for wear.** It would also bring the defect back as a shorter window:
  any delay is a window in which the page is wrong, and the step-4 probe fetches right after the
  claim. So: **no debounce.**

## Predictions, frozen before any build or run

Graded with the rewritten fresh-boot acceptance step 4 (`scripts/fresh-boot-acceptance.sh
--probe-claim`): one `/logbook.json` fetch right after a **new** claim, before anyone opens the page.
- **P1, v16 (the released image):** the probe's verdict is **UNCLAIMED**, and step 4 is **red**.
- **P2, the fix (this branch):** a `[logbook] persisted` line follows the `[claim]` line within
  ~1 s, the probe's verdict is **CLAIMED**, and step 4 is **green**. The full acceptance re-run on
  the fix has step 4 green and exactly one boot after the power cut.
- **P3, sabotage (this branch with the `PersistNow()` call removed):** **UNCLAIMED**; step 4 is
  **red**.

**Outside the set:** CLAIMED on v16 or the sabotage build with **no** `persisted` line between the
claim and the probe (the model is wrong); or UNCLAIMED on the fix. Either: stop and report.
CLAIMED with a periodic `persisted` line that happens to fall between the claim and the probe is
an **invalid run** (redo it), not a result.

### Addendum, written after P1 and before P3 and P2 (2026-10-08 20:25 PDT)

**The probe waits 3 s after the `[claim]` line for P3 and P2.** On the fix, the `[claim]` line
is printed *before* `PersistNow()` starts. A probe fired 0.5 s after that line (P1's harness
timing) can land in the middle of the write. That would read UNCLAIMED on a working fix, and no
person can open Collection 0.5 s after tapping the device. The 3 s wait does not weaken the check:
the v16 and sabotage builds save only on a page fetch or every 10 minutes, so nothing saves in
those 3 s unless the periodic write lands there, and that would show in the log (an **invalid
run**). P1 ran at 0.5 s. Its UNCLAIMED is also what the model predicts at 3 s.

**The step-4 probe itself had a defect, found on the first real run and fixed before P1 was
redone.** It took the Microsoft Store `python3` stub, parsed nothing, and wrote an empty verdict,
which step 4 reported as the v16 failure. That run (PC12) is void. The fix is #385 `cb9839d`.

## Results (COM18, 2026-10-08)

Graded with the step-4 probe from #385. Each probe was fired automatically by a log watcher, so
operator timing played no part. Each run claimed a type that was new to the board. The response
body of each probe was kept, and is quoted from it below.

| run | image | predicted | observed |
|---|---|---|---|
| P1 | v16 (the released image) | UNCLAIMED, red | **UNCLAIMED**: A21N, `first=yes`, http 200 |
| P3 | sabotage (this branch minus `PersistNow()`), `bac7b785` | UNCLAIMED, red | **UNCLAIMED**: C172, `first=yes`, http 200 |
| P2 | the fix (plus #383), `c9e67cef` | `persisted` within ~1 s, CLAIMED, green | **CLAIMED**: B39M, `first=yes`, http 200 |

**P1, v16.** The claim of A21N landed at 20:18:46.519. The probe 0.5 s later was the first fetch.
The body listed A21N with `"claimed": false`, and 43 types against the board's 44: it was
served from what was saved before the claim. The `persisted` line at 20:18:46.928 came
after that response; it is the save the probe's own fetch requested. That is the model:
the save lands after the response.

**P3, sabotage.** The claim of C172 landed at 20:23:08.994. Nothing was saved in the 3 s before
the probe, which then read C172 `"claimed": false`. The `persisted` line at 20:23:12.332
follows the probe's own fetch. **Removing the immediate save turns step 4 red.**

**P2, the fix, as part of a full fresh-boot acceptance.** Factory reset, Wi-Fi, location, then
the claim of B39M at 20:36:13, with `[logbook] persisted` printed in the same second. The probe
3 s later was the first fetch and read B39M `"claimed": true`. Daniel then opened Collection:
"1 claimed of 3 seen, B39M x1". The acceptance scored **8 passed, 1 failed**:

- step 4 (the first view after the claim included it): **pass**
- step 4 (Collection was opened after the claim): **pass**
- step 5 (the collection survived the power cut): **pass**
- step 6 (disabling flushed first): **pass**
- step 5 (exactly one boot after the power cut): **fail**. The second boot was the serial
  capture's reopen resetting the board (`rst:0x15`), not the firmware. Under the ruling of
  2026-10-08, that check moves off serial to the Worker's boot rows; that is #385. It does not
  bear on this fix, whose result is step 4.

**A void run (PC12).** The first P1 attempt fetched http 200 but wrote an empty verdict. The
probe had picked up the Store `python3` stub. It is excluded, and the probe was fixed (#385
`cb9839d`) before P1 was redone.

**Outside the set:** none. Every graded run matched its prediction.

## v17.1 re-run: predictions, frozen before any re-run image is built (2026-10-14)

**Why a re-run.** The 2026-10-08 P2 did not meet its own second clause ("exactly one boot after the power cut"): the serial reopen reset the board. That moved #387 out of v17. It ships first in v17.1. The re-run grades that clause from the Worker's boot rows (#385); no serial reopen happens before everything is graded. **v17 shipped with the defect**, so the baseline image is now v17's own release binary.

**Two tokens, because the probe's verdict depends on whether the saved copy already holds the type** (v17 acceptance, 2026-10-14):
- **fresh logbook:** the type is not in the last saved copy, so the probe reads **ABSENT**;
- **existing type:** the type is in the saved copy as seen-but-unclaimed, so the probe reads **UNCLAIMED**.

The first case was observed on v17 on 2026-10-14 (C150, ABSENT) **before** these predictions; it is marked as observed.

**Setting up the existing-type case** (no firmware help needed):
1. Fetch `/logbook.json` once. That asks the device to save the logbook. The request is consumed on the next loop pass and dropped if the logbook is not dirty or a save happened in the last 30 s (`ConsumeLogbookFlushRequest` + `MaybePersistForFetch`). **It does not linger, so it cannot cause a save after the claim.**
2. Wait for the `[logbook] persisted` line in the capture.
3. Fetch again and record which types the saved copy holds, claimed or not.
4. Daniel claims an aircraft whose type is in that list.

The **case is decided by that pre-claim body**: a claimed type absent from it makes the run a fresh-logbook run, whatever was intended.

| image | fresh logbook | existing type |
|---|---|---|
| **B**: v17 release `firmware-s3-128.bin` (f3080caa7e66) | **ABSENT** (observed 2026-10-14, before freezing) | **UNCLAIMED** |
| **F**: this branch on main 523cd73 (the fix) | **CLAIMED**, with `[logbook] persisted` within ~1 s of `[claim]` and before the probe | **CLAIMED**, same |
| **S**: F with the `logbook.PersistNow()` call removed | **ABSENT** | **UNCLAIMED** |

**F, full fresh-boot acceptance** (factory reset first):
- step 4 green (`verdict=CLAIMED`);
- steps 5 and 6 `verdict=CLAIMED`;
- boots `n=1 reasons=POWERON verdict=ONE_POWERON`, from Worker rows;
- `--assert-only` all passed;
- the `[collect]` reopen comes only after grading.

**Invalid run (redo, not a result):** on B or S, a periodic `[logbook] persisted` line between the claim and the probe.

**Outside the set:** any other token in any cell, CLAIMED on B or S with no save between the claim and the probe, ABSENT or UNCLAIMED on F, or F's acceptance failing any other step. Stop and report.

## Reviewer decisions, 2026-10-20 (recorded as decisions, not results)

- **#387 was approved to merge at `545dbbe` on its graded 2026-10-08 run**, provided CI was green on the merge result. CI ran again on the merge with main `217d7b5` (28 passed, 3 skipped). It merged as `f0cb9f8`.
- **Still owed: "exactly one boot after the power cut"**, the clause that kept #387 out of v17. It is graded from the Worker's boot rows in the **v18 prerelease's fresh-boot acceptance**, and **v18 is not promoted unless it passes**.
- **Remaining cells of the v17.1 re-run table:**
  - **S** (flashed): fresh-logbook and existing-type claims. It stays the red control for F.
  - **B**'s remaining cell (existing type) is **dropped as redundant with S**. B and S both lack the immediate save, and B's fresh-logbook cell was already observed. This is a reviewer decision, not a result.
  - **F**'s cells (both tokens, boots from the Worker's rows) are graded on the **v18 prerelease image**, in place of this branch's build.
- **v18 is promoted only if every F cell matches and S stays red.**
- Board: E2:08 (`90706931E208`), per Daniel. Its backup was verified before any run: NVS parses, 186 entries, 0 CRC failures, `cloud-key-fac` present, 12 logbook entries, sha256 `0e5a0cb1c3bc…`. The Collection before the run: 1 claimed type of 28 seen. The backup is written back only to E2:08, and the count is confirmed afterwards.

## v18 bench results (E2:08, 2026-10-20)

**Board:** E2:08 (`90706931E208`). **Images:**
- S: `r387-S-724dad6.bin`, sha256 `424ce11b1df6…`;
- the v18 prerelease: `firmware-s3-128.bin`, sha256 `5a5f662bd51b…`, equal to the release asset's digest.

Each image was written to app0 only. NVS and app1 (v17) were never touched by a flash. Each claim was probed automatically 3 s after its `[claim]` line, and each probe was the first `/logbook.json` after the claim. **A case is decided by the pre-claim body:** a second fetch inside the 30 s rate limit, right after a save, so it returns the saved copy exactly and triggers nothing.

| cell | image | predicted | observed |
|---|---|---|---|
| S, existing type | S | UNCLAIMED | **UNCLAIMED, twice.** C172 at 18:26:35Z (pre-claim body: 80 types, 18:23:28Z); C152 at 18:57:07Z (body: 29 types, 18:56:16Z). Both valid: no save between the body and the probe |
| **S, fresh type** | S | ABSENT | **NOT MEASURED: a gap, not a pass** (reviewer, 2026-10-20). Every attempt landed on a type already in the saved copy. A finger cannot reliably pick one plane on the 1.28" screen (#436) |
| F, existing type | v18 prerelease | CLAIMED, saved within ~1 s, before the probe | **CLAIMED:** P28A at 18:30:35Z (body: 80 types), saved in the same second. Also B38M in the acceptance (body: 6 types), saved in the same second |
| **F, fresh type** | v18 prerelease | same | **MEASURED, CLAIMED:** RV9 at 18:43:58Z. The body (14 types, 18:39:52Z) does not contain RV9. No save between the body and the claim; saved in the same second; probe first=yes |
| fresh-boot acceptance | v18 prerelease | every step green; boots `ONE_POWERON` | **10 passed, 0 failed.** The clause owed since v17, exactly one POWERON boot from the cut to the end of step 6, **passed** at the Worker. The `[collect]` reopen did not reset the board |

**Why S's saved types went from 73 at setup to 80 at the first claim.**
- The first pin (17:59:40Z, 73 types) went stale before any claim. The claim watcher crashed on a file-sharing read error (fixed with a retry), and while I waited for the List photo, the board made its own 10-minute saves at 18:09:40Z (77 types) and 18:19:40Z (79 types).
- So I re-pinned at 18:23:27Z: a fetch-triggered save, 80 types. The C172 claim was graded against that 80-type body, and no save fell between it and the claim.
- The types grew because the board was logging a live sky.

**Reviewer decision (2026-10-20): promote may go ahead despite the gap.**
- S's existing-type cell went red, so the probe can see a claim that wasn't saved.
- F's fresh-type cell was measured and matched, so the shipped behaviour was seen working.
- The most the missing cell could hide is a fresh-type claim waiting for the next 10-minute save, as in v17. That is not a regression.

**Observation, outside the frozen cells.** After the B38M claim, v18 saved **twice**: at the claim (18:38:25Z), and again 3 s later (18:38:28Z, types 13 → 14), with no fetch in between. That contradicts this doc's "one save per claiming tap", which the NVS-wear estimate above relies on. The cause is not known yet.

**The board afterwards:** E2:08 was restored from its verified 16:16Z backup.
- The files matched their recorded hashes (NVS `0e5a0cb1c3bc…`, otadata `b7e293bb607d…`), and the restore was written back only to E2:08.
- It boots app1 (v17), the logbook is unticked, and Collection is **1 claimed of 28 seen**, with every count identical to before the run.
- The restore also reverts what happened on v17 between 16:28Z and 17:55Z: the logbook being switched on, and two claims.
