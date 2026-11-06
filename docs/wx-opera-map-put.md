# A failed map put keeps the frame (predictions first, 2026-11-06)

**For:** the reviewer chat. **Status:** predictions, frozen before any code. It is the follow-up the ~20:01Z ruling
asked for on stage 1c (`docs/wx-opera-map-r2.md`):

> Follow-up, not blocking, in its own small PR: a failed map put should not fail the storing check. Catch it, count
> it on the line, and keep the frame.

## What changes

- **Today** (`mapVia`, `proxy/src/wx/ingest.ts`): after building a map, the ingest writes it to R2 with
  `bucket.put`. That runs inside `buildOperaFrame`, before the frame's own put, so **an exception from the map put
  ends the storing check with no frame stored.** The check reads as a `wx_ingest_error`, and the next check builds
  the frame again from scratch.
- **New:** the map put is wrapped in a `try`. On an exception, the build goes on with the map it has, and the frame
  is stored as usual. The `wx_ingest` line carries:
  - **`mapPutErrors: 1`**, a count, so that a future product writing more than one map per check counts the same
    way;
  - **`mapPutErr`**, the error's text, cut to 80 characters. It is free text, so a prefix is still a true prefix.
- **A put that returns `null`** (the create-only condition failed because another isolate wrote first) **is not an
  error.** It is counted nowhere, as now.

## The checks (vitest, `proxy/test/wx-opera.test.ts`)

| check | what | predicted | sabotage | predicted under it |
|---|---|---|---|---|
| **MF1** | a storing check of the DRY frame, cold, on a bucket whose `put` throws for keys under `wx/opuk/map/` only | status **`stored`**; the frame object exists and equals MP2's bytes; `map` **`built`**; **`mapPutErrors` 1**; `mapPutErr` non-empty; no map object | **MF1s:** the catch removed (`IngestX.mapPutNoCatch`, test-only) | **red:** the check throws, and no frame is stored |
| **MF2** (control) | the same check on a bucket whose `put` works | `stored`, `map` `built`, **no `mapPutErrors`**, one map object | | |
| **MF3** | a lost race: a cold check whose map GET misses, simulated by a bucket whose `get` returns nothing for map keys while an object sits under the key, so the create-only put returns `null` | `stored`, `map` **`built`**, **no `mapPutErrors`**; the object already there is left as it was | | |

**CI predicted: 34 = 31 success, 3 skipped, 0 failed.**
- Baseline: #577's finished PR run on `3f6b81056aa7`, which touches `proxy/`.
- Last job-set change: `84409651` (#552). Last workflow change: `259e8688` (a step). Re-read at the push.
- Merge by the rule.

**Deploy:** none of its own. It goes to production with whichever `opuk` deploy next carries main, registered there.
