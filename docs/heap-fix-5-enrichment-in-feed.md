# Design cost: serving enrichment in the feed response (heap proposal 5)

**Design only. Nothing built.** Written at Daniel's instruction on 2026-08-19:
*"write up the design cost before committing — payload size at 40 aircraft versus the
fragmentation numbers."*

**Verdict up front: the simple form does not hold up, and the measurement that kills it
is one line of `[health]` output.** A scoped form might. See "What would rescue it".

## The proposal

Proposal 5 of five in [soak-com119-2026-08-19.md](soak-com119-2026-08-19.md): have the
Worker join enrichment into the position payload, so the device never opens a second
connection. *"Largest change, and it makes the device's heap irrelevant to whether cards
fill in."*

It is attractive for reasons beyond heap. The MIL flag and the photo pointer would ride
the position payload; `dbFlags` would reach the device without a new field on a second
endpoint; the enrichment gate, the retry ladder and the whole `CanHandshake()` apparatus
would stop being load-bearing.

## The measurement that decides it

COM119, 2026-08-19, shipping image, production backend, ac=40/40:

```
[perf]   polls=4 busy=3%(fetch=3% enrich=0%) parse=8ms/poll bytes=2313/poll ac=40/40
[health] heap free=92060 largest=44020 free8=92060 tlsOk=1 rej=0  tls=2/889  interval=15000ms
```

**`tls=2/889`.** Two handshakes. Eight hundred and eighty-nine reuses.

That is the whole argument. The handshake this proposal exists to remove from the hot
path **is not in the hot path**. It is a boot cost, paid twice, and thereafter the
keep-alive socket carries everything. Meanwhile `bytes=2313/poll` is paid **every 15
seconds, forever**.

So proposal 5 as written removes a cost incurred twice per boot and inflates one incurred
5,760 times a day.

## Payload arithmetic

Current: **2,313 B for 40 aircraft** ≈ 58 B/aircraft.

The enrich body is `{v,r,t,tn,op,o,d}` plus optional `p`/`pk`. Measured shapes from
production KV and the serve path:

| field | typical | notes |
|---|---|---|
| `r` registration | 7 | `N628TS` |
| `t` type | 4 | `B738` |
| `tn` type name | 25 | `Boeing 737-800` |
| `op` operator | 20 | `United Airlines` |
| `o`/`d` route | 8 | two IATA codes |
| `p` photo path | 40 | server-supplied opaque path |
| `pk` photo kind | 4 | `type` / `hex` |
| JSON overhead | ~48 | 8 keys × quotes/colon/comma |

≈ **156 B/aircraft added**, against 58 B today.

At 40 aircraft: **2,313 B → ~8,550 B, a 3.7× payload.**

## Why that number is dangerous specifically here

From [heap-fragmentation-2026-08-17.md](heap-fragmentation-2026-08-17.md), the budget did
not break at steady state. It broke when a **7 KB fetch transient landed on an already-
declined floor**: 25,588 → 18,420, against a 20,000 budget.

> *"The fetch needs ~7 KB of contiguous headroom, and the floor descends toward it."*

**Proposal 5 grows the fetch transient — and the fetch transient is the exact mechanism
that breaks the budget.** It would be trading a rare cost for a frequent one, on the
precise axis the defect runs along.

## The uncertainty I will not paper over

I do **not** know that the transient scales with payload size, and it may not.

`GetJson` parses **straight off the socket** rather than buffering the body, so the peak
working set is plausibly the socket buffer plus the parser's state — both roughly fixed —
not the total bytes. If that is so, a 3.7× payload could cost a longer fetch and almost no
extra peak, and this entire objection collapses.

That is a measurable question and it has not been measured. **Do not treat the arithmetic
above as a result.** The experiment: serve a synthetic 8.5 KB feed response to a bench
board and watch `largest` across the fetch, against the same board on the 2.3 KB response.
If the transient is flat, proposal 5 is back on the table at full strength.

Until that runs, this document argues from a real `tls=2/889` and an unverified assumption
about the parser — and only the first of those is evidence.

## What would rescue it, if the transient does scale

The full join is not the only shape:

1. **Delta only.** The device already caches enrichment per hex. Send enrichment **only
   for hexes new since the last poll** — typically a handful, not 40. Cuts the added bytes
   by roughly an order of magnitude in steady state, at the cost of the Worker tracking
   what each device has seen (which it does not do today, and which is state we have
   deliberately avoided).
2. **Nearest-N only.** Enrich the nearest 5–10 in the payload; leave the rest to the
   existing lazy path. Matches the nearest-first sweep order already in
   `ProcessMetadataLookups()`, and matches what a customer actually taps.
3. **Flags only, not text.** Ride `mil` and a photo pointer — a few bytes — and leave
   registration/type/operator on the lazy path. This is the cheapest version and it
   already delivers Phase 0's feed-derived MIL marker, which was the original ask.

**Option 3 is the one I would build**, and it is small. It gets the dbFlags marker onto
the payload for single-digit bytes per aircraft, with no measurable transient growth and
no new server-side per-device state.

## Recommendation

- **Do not build proposal 5 as written.** `tls=2/889` says it optimises the wrong thing.
- **Run the transient-scaling experiment** before ranking it, because if the peak is flat
  the objection above is wrong and should be discarded loudly.
- **Consider option 3 on its own merits**, as a Phase 0 feature rather than a heap fix.
  It is not a heap fix. Calling it one would be how a payload change gets waved through a
  heap review.
