# Photo library coverage — where to stop sourcing

Measured 2026-08-14, immediately after the square-variant ingest published all 234
library rows. Answers one question: **which types have no photograph at all, ranked by
how often a customer would actually see them, and at what point does sourcing more stop
being worth it.**

## The headline

**The library covers 97.3% of sightings.** The entire remaining gap is **2.7%**, spread
across ~90–110 types with a very flat tail.

That reframes the question. There is no cliff to chase and no backlog to burn down —
there is one standout, a short head, and then a long tail of types a customer will
mostly never meet.

| | |
|---|---|
| Types seen (board .32, all-time) | 253 |
| Total sightings | 22,816 |
| Types with **no photo and no alias** | 89 |
| Sightings landing on a gap | **619 — 2.7%** |

For contrast, the covered head: C172 (2,346 sightings), E75L (2,199), B738 (1,695),
B38M (1,210), B739 (998), A21N (954). The types customers actually see are done.

## Where to stop

| Sourcing effort | Buys you (of the *remaining gap*) | Buys you (of *all sightings*) |
|---|---|---|
| **K100 alone** | 18% | 0.28% |
| **Top 4** (K100, RV9, C180, T210) | ~34% | ~0.9% |
| Top 11 | 50% | ~1.4% |
| Top 62 | 90% | ~2.4% |
| All 89–111 | 100% | 2.7% |

**Recommendation: do K100, then the next three, then stop.** K100 is already picked and
eyeballed (VH-ICZ) and needs only ingesting; it is 18% of the entire remaining gap on
its own, which nothing else comes close to. After the top four the curve flattens so
hard that the 58 types between #5 and #62 are collectively worth 1.5% of sightings.

Reopen the question when the pilot gives a type mix from somewhere that is not Bend.

## Fleet-ranked gaps (7-day enrichment lookups, contamination removed)

| # | type | lookups | cum % of remaining gap |
|---|---|---|---|
| 1 | **K100** | 209 | 18.1% |
| 2 | RV9 | 77 | 24.8% |
| 3 | C180 | 51 | 29.2% |
| 4 | T210 | 50 | 33.5% |
| 5 | S61R | 37 | 36.7% |
| 6 | SH36 | 31 | 39.4% |
| 7 | RV12 | 30 | 42.0% |
| 8 | CL2T | 30 | 44.6% |
| 9 | M7 | 24 | 46.7% |
| 10 | BE99 | 23 | 48.7% |
| 11 | HUSK | 23 | 50.7% |
| 12 | RV8 | 21 | 52.5% |
| 13 | C185 | 20 | 54.2% |
| 14 | C175 | 17 | 55.7% |
| 15 | GLF2 | 16 | 57.1% |
| 16 | CH7B | 15 | 58.4% |
| 17 | CRER | 14 | 59.6% |
| 18 | S61 | 14 | 60.8% |
| 19 | EA50 | 13 | 62.0% |
| 20 | CH60 | 13 | 63.1% |

Full list: `scratchpad/fleet-gaps.json` (111 types) and `scratchpad/photo-gaps.json`
(the .32 logbook view, 89 types).

**The two types found by eye rank low.** B505 is #44 (4 sightings on .32), PA23 is #56
(3). Both are real gaps and both stay on the list — but they are tail, and finding them
by looking at a screen is exactly why this sweep exists. K100, the single biggest gap in
the library, was already on the list from the same process; the other 108 were not.

## Method, and the one trap in it

Two independent measures, which agree on the head of the list:

1. **Analytics Engine**, `enrich_gap` points where `blob2='photo'`, 7 days, grouped by
   type. This is fleet demand — what devices actually asked for.
2. **Board .32's `/logbook.json`**, which carries a real sighting `count` per type.

Both were intersected with the **published manifest** (the artifact) plus
`TYPE_PHOTO_ALIAS` parsed out of `photos.ts`, so a type covered by an alias is not
counted as a gap.

> ### ⚠ The gap telemetry records what WAS true, not what IS true
>
> **34% of the raw Analytics Engine gap list — by lookups — was already fixed.**
>
> `recordEnrichGap` fires at request time. Every enrichment served to a FW ≥ 7 device
> before the square ingest logged a `photo` gap, because `resolvePhoto()` was returning
> null for *everything*. Those points are still in the dataset and still look exactly
> like real gaps.
>
> Taken raw, the list ranks **C152 first** (269 lookups) — a type that has had a
> photograph the whole time. K100, the actual biggest gap, comes second. Sourcing off
> the raw query would have started with a photo we already own.
>
> **So any use of this dataset must be re-validated against the current library before
> it is believed.** It is a log of past states, and the fix it recommends may already
> have happened. Same family as the rest of CLAUDE.md: the record states intent at a
> moment, the artifact states fact now.

## What this sample cannot tell you

**The "fleet" is two bench boards, and one of them was offline.** `.55` did not answer
`/logbook.json` during this run, so the logbook view is a single board in Bend, Oregon.

That means the AE view and the logbook view are **not independent samples** — they are
the same sky measured two ways (enrichment lookups vs. logged sightings). Their
agreement confirms the *method*, not the *generality*.

The direction of the bias is knowable, though, and it is favourable: Bend is a GA field,
so this sample is heavily weighted toward exactly the light-aircraft types that make up
the gap. A customer under an airline approach path sees more of what is already
covered. **2.7% is therefore an over-estimate of what a typical customer would miss**,
not an under-estimate.

Re-run this after the pilot, when the type mix comes from 50 locations instead of one.

## Re-running it

The queries are in `proxy/README.md` ("Finding enrichment gaps"). The two corrections
this document adds:

- filter the results against the **published manifest and the alias table**, or a third
  of the list is types that already have photographs;
- `LIMIT 25` hides the shape — the cumulative figure is the answer to "where do I stop",
  and it needs the whole list.
