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
