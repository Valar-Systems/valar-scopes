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
