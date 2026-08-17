# A4 — aircraft seen but not enriched, measured

Measured 2026-08-16. Answers: **which aircraft does a device show a blank card for, how
many are there, are they military / foreign / something else, and do they recur or are
they one-offs.**

A blank card is worse than a silhouette. The silhouette says *"no photo"*; the blank
says *"this device does not know what it is looking at."*

## The headline

**The type gap is now ~10 aircraft a day, fleet-wide, and all of them are American.**

The `isNonIcaoAddress` skip deployed on 2026-08-13 removed ~96% of what used to be in
this bucket. What remains is small, real, and almost entirely US civil registrations
that adsbdb simply has no record for.

| | before the skip (08-09 → 08-12) | after (08-14 → 08-16) |
|---|---|---|
| lookups/day | 534 – 855 | **19 – 27** |
| distinct hexes/day | 100 – 166 | **5 – 12** |

```
2026-08-09   lookups=534   hexes=100
2026-08-10   lookups=555   hexes=107
2026-08-11   lookups=654   hexes=131
2026-08-12   lookups=855   hexes=166
2026-08-13   lookups=371   hexes=90     <- A1 deployed
2026-08-14   lookups= 23   hexes=11
2026-08-15   lookups= 27   hexes=12
2026-08-16   lookups= 19   hexes=5
```

## Composition, post-deploy only

22 distinct hexes / 69 lookups across the clean window:

| bucket | hexes | lookups | share |
|---|---|---|---|
| civil: United States | 16 | 59 | **85.5%** |
| military: US military | 6 | 10 | **14.5%** |
| non-ICAO block | 0 | 0 | **0%** — the skip is total |
| foreign (any state) | 0 | 0 | 0% |

**Nothing foreign, nothing phantom.** Every remaining unenriched aircraft is a US
address, and five sixths of them are civil.

### Recurrence

- **7 of 22** seen once — genuine one-offs.
- **4 of 22** seen five or more times.
- The top four recur ~9 times each: `a590d8`, `a71203`, `a815d6` (US civil), `a7419f`.

So the tail is one-offs, but the head is a handful of aircraft a local owner would keep
meeting — which makes them worth a per-hex override rather than a database wish.

### The military sixth is not fully blank

Those six get the **military floor** applied at serve time: the operator is filled from
the broadcast callsign's designator, then the hex allocation table, then `dbFlags`. They
show an operator and no type — degraded, not empty. The 16 civil ones are the truly
blank cards.

## What to do about it

1. **Per-hex overrides for the recurring four.** They are stable identities a customer
   sees repeatedly; a `hex`-kind photo/type row fixes each permanently.
2. **Nothing for the one-offs.** Seven aircraft seen once each, fleet-wide, in three
   days. There is no lever proportional to that.
3. **Re-measure after the pilot.** 22 hexes from two boards in Bend is a thin sample and
   the composition is guaranteed to shift when the fleet is not one GA field.

## Method, and the trap it walked into first

Source: Analytics Engine `enrich_gap` points where `blob2='type'`, grouped by `blob4`
(the hex). Classified against three tables **parsed from the source that serves them**,
never restated: `src/IcaoCountry.cpp` (188 country blocks), `proxy/src/military.ts` (20
military ranges), `proxy/src/icaoalloc.ts` (the non-ICAO range).

> ### ⚠ The first measurement was wrong, and it was wrong the same way as the photo sweep
>
> A 7-day window gave **434 hexes / 2,591 lookups**, of which **62.7% sat in the
> non-ICAO block that is supposed to be skipped**. Read literally, that says the skip is
> broken and the fleet is drowning in phantom track IDs.
>
> It is not. The window **straddled the 2026-08-13 deploy**. Querying the non-ICAO
> population per day shows it stopping dead: 692 lookups on the 12th, 180 on the 13th,
> and **zero** on the 14th, 15th and 16th.
>
> This is the second time in two days that the same shape produced a wrong answer — the
> photo sweep's raw list ranked a type we already owned first, for exactly the reason.
> **`enrich_gap` is a log of past states.** Any window spanning a fix reports the world
> before the fix, mixed with the world after it, and the mixture looks like a current
> measurement.
>
> **So: always plot the daily series before believing an aggregate.** A step change in
> it is the only thing that distinguishes "this is how the system behaves" from "this is
> how the system behaved until Tuesday". The aggregate cannot show you that, and it is
> the number you were about to act on.

## What this does not cover

Two boards, one of them intermittently offline, in Bend, Oregon, over a ~2.5-day clean
window. The absolute rate is a fleet of two and does not scale by simple multiplication —
enrichment is per-hex and cached fleet-wide, so more devices in the *same* sky add
almost nothing, while devices in *new* skies add their own local unknowns.

The composition claim (all US, 85/15 civil/military) is the part most exposed to the
sample. A European pilot device would put foreign addresses into this bucket on day one.
