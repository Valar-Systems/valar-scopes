# Logbook: why a store stops growing

Notes from the 2026-08-12 investigation into the operators store. Written down
because the next person to notice a store that stopped growing will reach for
the same three hypotheses we did, and on one board the answer was none of them.

## `.32` stopped learning operators — NOT A DEFECT

**Symptom.** Bench board `.32` (`Blipscope-326E20`, s3-128) held 120 operators
against a cap of 220 — a hundred free slots — and had not added one in eleven
days. Over the same window it added 107 new *types* (08-02 → 08-04). A store
that stops growing with room to spare looks exactly like a bug.

**The three hypotheses, and why each was wrong.**

1. *`NoteOperator` is not being reached.* It is. `NoteType` and `NoteOperator`
   are adjacent unconditional calls in the same `if (logbookEnabled)` block
   ([AircraftManager.cpp](../src/AircraftManager.cpp), the enrichment-landed
   path). New types arriving proves the block runs.
2. *`operatorName` is arriving empty.* It is not. Production enrich was queried
   for eight hexes taken off `.32`'s own tail — `op` was populated on all eight.
3. *A firmware version gate.* `.32` ran FW 6 through the whole window, which
   looked causal. It is not: `FULLBLEED_MIN_FW` gates only `squareSizeFor()` →
   `resolvePhoto()` and never touches `op`, and `doc["op"]` has been parsed
   since `CloudFeed.cpp` was created. FW 6 → 7 was the full-bleed card, a UI
   change. **Coincidence.**

**The actual cause: the sky repeats.** Seven of the eight operators from
`.32`'s live traffic were already in its book. `NoteOperator` was returning
early at `operators.count(op)` — working correctly, reporting "not a fresh
catch". Under FW 6's 24-character cut, `LEADING EDGE FLIGHT ACADEMY LLC`
truncated to `LEADING EDGE FLIGHT ACAD`, which was already the stored key.

The 107-types-zero-operators split, which looked like the strongest evidence of
a fault, is the ordinary shape of airline traffic: **many types per operator.**
SkyWest alone flies E75L, CRJ9 and E175 under one name.

`.32` sits over two flight schools flying circuits, a few regionals, and the
same fire-aviation and DOI aircraft. 120 operators is close to complete
coverage of what actually flies there. **A saturated catalogue, not a broken
store**, and reflashing will not change it — there are no new operators to
learn.

## The distinction worth keeping

Three different things present identically as "the store stopped growing":

| | how to tell |
|---|---|
| **Saturated catalogue** | store is BELOW its cap; the arriving names are already keys. Not a defect. |
| **Cap reached, eviction churning** | store is AT its cap. `evictOneSeen` evicts the MOST RECENT unclaimed entry, so the newest slot is a revolving door — each new entry evicts the previous one and the export shows only the survivor. Looks like "one new entry ever". |
| **Genuine fault** | the arriving field is empty, or the call is not reached. |

Bench board `.55` was the second case, not the first: 220/220 operators, one new
entry dated the final day. Its stored count was inflated by co-owner strings —
see `normOperator`'s comma truncation in [Logbook.cpp](../src/Logbook.cpp).

**A count alone cannot separate these.** `rejected[]` and `evicted[]` can, and
they are RAM-only and reset on every boot — so they are only readable on a board
that has been running, from the `[logbook] REFUSED since boot` / `EVICTED since
boot` serial lines. Reading them is the first thing to do, before hypothesising.

## What the HTTP export does and does not carry

`GET /logbook.json?download=1` carries the contents — entries, first-seen dates,
claims, counts. It does **not** carry `rejected[]`, `evicted[]`, or the NVS
free-entry figure from the `[logbook]` persist line. So an export is a backup of
the collection and not a diagnosis of pressure on it; the pressure telemetry only
exists on a live board's serial.
