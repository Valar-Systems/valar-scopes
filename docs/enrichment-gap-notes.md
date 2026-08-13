# Enrichment gaps: what the backlog is actually made of

Notes from the 2026-08-12 investigation, which started with one customer question
("why is there no data for AE6861?") and ended somewhere else entirely. The
headline: **the aircraft that prompted the question is 0.7% of the problem, and
half the backlog is not a gap at all.**

Measured over 30 days from the `enrich_gap` points
([metrics.ts](../proxy/src/metrics.ts)'s `recordEnrichGap`), production dataset
`blipscope_proxy`:

| gap | lookups | distinct hexes |
|---|---|---|
| `type` | 8,676 | 1,994 |
| `photo` | 3,388 | 451 |
| `name` | 1,591 | 336 |

`type` is the root gap — no type means no friendly name and no type photo — so
everything below is the `type` list.

## It is three populations, not one

### 1. Non-ICAO surveillance addresses — ~49% of hexes, ~44% of lookups. NOT A GAP.

912 hexes in `2bxxxx` plus 71 in `29xxxx`. Every one enriches completely empty
(no reg, no type, no operator), and they are **not aircraft identities at all**:

```
Mictronics entries total:  447224
entries starting 2B:            0      entries starting 40:  12415
entries starting 29:            0      entries starting A0:  25314
entries starting 2A/2C:         0      entries starting 3C:   3497
```

Zero across the whole 0x29–0x2C space while neighbouring real blocks hold
thousands. adsbdb answers `unknown aircraft` for every one sampled. These are
TIS-B / ADS-R rebroadcast track IDs — transient surveillance identities, not
airframes — reaching us **without** the `~` prefix that
[enrich.ts](../proxy/src/enrich.ts)'s validator already knows about.

No registry will ever have them. They cannot be fixed by any overlay, and while
they sit in the metric they make the backlog look ~2x its real size. They are
also **growing**: on 2026-08-12 they were 692 of 855 type-gap lookups.

### 2. Real aircraft whose type somebody already has — the fixable part

- **742 hexes (37.2%), 2,833 lookups (32.7%)** are in the Mictronics export
  *with a type*, and we serve them typeless.
- Sampling the recurring ones (>= 5 lookups) against adsbdb directly: **22% have
  an `icao_type` we are not showing.** These are not obscure airframes — they
  are mainline regional traffic:

```
a37d56 n=111  ours(t)=""  ours(r)="N324BS"  adsbdb=BE20
a462f1 n= 91  ours(t)=""  ours(r)="N382CG"  adsbdb=C130
a34c2f n= 35  ours(t)=""  ours(r)="N311SY"  adsbdb=E75L
a4d97e n= 24  ours(t)=""  ours(r)="N411SY"  adsbdb=E75L
a9aebc n= 27  ours(t)=""  ours(r)="N723AL"  adsbdb=B39M
aaed0c n= 20  ours(t)=""  ours(r)="N803AK"  adsbdb=B38M
```

SkyWest E175s and Alaska MAX 8/9, each requested 20-111 times in the window and
answered with an empty type every single time.

**This is a caching defect, not a data gap.** Two facts combine:

1. The adsbdb type backfill landed in `fa6a910` on **2026-07-18** — 25 days
   before this measurement, against `AC_TTL_S` of **30 days**. Records cached
   before it shipped are still alive.
2. `resolveMeta` decides the TTL with
   `hasContent = !!(built.r || built.t || built.tn || built.op)`. **A
   registration counts as content**, so a record that resolved a reg but no type
   gets the full 30 days and the backfill is never re-attempted.

(1) drains on its own by ~2026-08-17. (2) does not: any fill where adsbdb is
briefly unavailable still locks in a typeless record for a month. Type is the
field that unlocks the name and the photo, so an entry missing it should take
the short TTL regardless of what else resolved.

### 3. Military — 18 hexes, 65 lookups. 0.9% of hexes, 0.7% of lookups.

The population that prompted the investigation is a rounding error, and it is
**one source, not a scatter**: all 18 sit inside a single contiguous window
`0xae6791..0xae687f`, 239 addresses wide. That is one fleet/squadron allocation,
consistent with US military airframes rotating their Mode-S codes — the
community DBs (ADS-B Exchange) re-identify them within days, a packaged export
lags.

None of the 18 are in the Mictronics export, so the `mil:<hex>` side table
cannot help. The table itself is healthy and loaded — verified against
production, two hexes from the same block:

```
AE1460 -> r=06-6162 t=C17  tn="Boeing C-17A Globemaster III"  + type photo
ADFEB8 -> r=98-0002 t=B752 tn="Boeing 757-200"                + type photo
AE6861 -> r=""      t=""   tn=""                              (absent from source)
```

## Recurrence: within-day, not across-day

Only 9% of gap hexes were seen once; 1,357 were seen 2-4 times and 46 twenty or
more. But **1,804 of 1,994 hexes appear entirely inside a single day** — the
repetition is one aircraft overhead being polled repeatedly, not a resident that
comes back.

Only **88 hexes span >= 7 days**, and those 88 carry 2,102 lookups (24% of the
total). If a hand-curated overlay is ever built, that is its input — not the
1,994.

## What this means for the curated overlay

**Don't build it yet.** In priority order:

1. Make `hasContent` require a **type**, so a reg-only record takes the short
   TTL and re-attempts the backfill. Cheapest fix, largest measured share.
2. Drop non-ICAO addresses from the gap metric — and consider not enriching them
   at all, which also stops devices spending requests on identities that can
   never resolve.
3. **Re-measure.** The overlay decision belongs against the corrected list. On
   today's numbers an overlay would be built to serve a backlog that is ~half
   phantom and ~a third a two-line caching fix.

## The exclusion range that almost shipped, and why it is a category

The first draft of the non-ICAO table listed **five** regions, all of them with
zero registered airframes in the Mictronics histogram. It broke 27 tests: the
enrich suite uses `f40001` as a fixture, which sits inside the `0xea0000` range.

Worth recording beyond the code comment, because the *shape* generalises: **a
wrong exclusion range produces the identical symptom to the bug being fixed.**
This table exists to stop enriching addresses that can never resolve. An address
wrongly excluded is a real aircraft with a blank card — which is exactly what the
30-day-typeless-cache defect above looks like. Had it shipped, the evidence that
the fix was broken would have read as evidence the fix hadn't finished draining
yet, and the natural response (wait longer) is the one that never resolves it.

That is the same shape as two failures already recorded in CLAUDE.md — a test
that requests the path the test itself chose, and a rehearsal whose environment
couldn't produce the failure it was rehearsing. In all three, **the broken and
the working state produce the same observation**, so no amount of looking at that
observation tells them apart. It is now written up there as a standing practice.

The specific lesson for exclusion lists: the two error directions are not
symmetric. A missing entry costs one pointless lookup — the status quo. A wrong
entry silently removes something real. So an entry earns its place by **positive
evidence it was observed in live traffic**, never by absence from a registry
snapshot. Only `0x230000-0x2fffff` ships, and it covers 100% of what was seen.

## Why there is no callsign -> aircraft lookup

This question recurs ("can't we just look the callsign up?"), so: **a callsign is
a flight number, not an airframe.** It is reused daily across different
aircraft — `RCH123` is a C-17 today and a C-5 tomorrow, `ASA465` is whichever
737 the schedule assigned. There is no stable callsign -> type mapping to pull.

The airframe's durable identity is its **ICAO 24-bit hex**, burned into the
transponder, which is why every type lookup keys on hex. What we do key on
callsign is only what genuinely varies per *flight*:

| table | key | gives |
|---|---|---|
| `mil:<hex>` (17,303 rows, Mictronics) | **hex** | registration, type, description |
| `TYPE_NAMES` + `tn:<CODE>` KV | type code | friendly name |
| `MIL_CALLSIGN_OPS` ([military.ts](../proxy/src/military.ts)) | callsign **prefix** | operator only — `RCH` -> Air Mobility Command |
| routes (adsb.lol routeset -> adsbdb) | callsign | origin / destination |

Note the third row is the near miss: a military callsign prefix does resolve an
*operator*, which is why `RCH` cards look better than bare ones. It never
resolves a type, and it must not be extended to try.
