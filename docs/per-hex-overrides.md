# Per-hex overrides — and what the military floor can and cannot do

## Read this first if a card says "US military" and nothing else

**That is the floor working correctly, not a bug.** The next person to see it will open
[proxy/src/military.ts](proxy/src/military.ts), find the hex sitting neatly inside a
military allocation range, confirm the table is right, and be no further forward. So,
stated plainly:

| | The floor asserts | The floor will never assert |
|---|---|---|
| ✅ | nationality + military status, from the **allocation block** | ❌ type |
| ✅ | a more specific operator from the **broadcast callsign** (RCH → Air Mobility Command) | ❌ registration |
| ✅ | a generic "Military" from **dbFlags** as catch-all | ❌ anything a database would have to know |

From the table's own header: *"we fill `op` with a truthful generic… **Never types, never
registrations** — only the fact the allocation itself proves."*

An address block proves who allocated it. It cannot prove what airframe is flying, and a
floor that guessed would produce a confident, wrong card — the failure mode that never
gets reported because it looks like data.

**So "US military and nothing else" means: the floor did its job, and both databases have
no record.** Type and registration require either an upstream that knows the airframe, or
an override.

### Diagnosing one in four commands

```sh
# 1. what we cached (empty fields with found:true is the classic military shape)
npx wrangler kv key get "ac:<hex>"  --binding=ENRICH_KV --env=production --remote --text
# 2. the Mictronics side-table
npx wrangler kv key get "mil:<hex>" --binding=ENRICH_KV --env=production --remote --text
# 3. ALWAYS an anchor -- a key you know exists. Without it, "absent" and
#    "cannot read KV" are the same observation, and 401s are common here.
npx wrangler kv key get "pptr:t:C172" --binding=ENRICH_KV --env=production --remote --text
# 4. the upstream. adsbdb states its negatives ("unknown aircraft"), so unlike an
#    empty positions array this answer can be trusted -- but run a hex you KNOW it
#    has (AE5006) in the same breath, or a broken call reads as a missing airframe.
curl -s https://api.adsbdb.com/v0/aircraft/<HEX>
```

## Where an override lives

`mil:<hex>` → `{ "r"?, "t"?, "tn"? }`, consulted at serve time in
[enrich.ts:296](proxy/src/enrich.ts#L296) **only when the live record resolved empty**.

Three properties make this the right home rather than a new namespace:

- **It cannot fight fresher data.** The gate is `if (!acR && !acT)`, so a real upstream
  record always wins.
- **It survives re-ingest.** [ingest-mildb.ts](proxy/scripts/ingest-mildb.ts) writes only
  the keys its export contains; a hex absent from Mictronics is not touched.
- **It yields when the upstream catches up.** If Mictronics later ships the airframe, the
  next ingest overwrites the hand-written row — which is the precedence we want.

Omit anything the type table already knows: `tn` resolves from `TYPE_NAMES[t]`
automatically, so `{"t":"P8"}` already yields "Boeing P-8 Poseidon" plus the type-keyed
stock photo. Writing `tn` by hand just creates a second copy to drift.

## When an override is justified

**Recurrence, measured in DISTINCT DAYS — never in lookup count.**

A one-off costs one degraded card. A repeat visitor is a permanently broken aircraft over
somebody's house. Lookups cannot tell those apart: a single long overflight generates one
per poll for as long as it is in range.

```sql
SELECT blob4 AS hex, COUNT(DISTINCT toDate(timestamp)) AS days,
       SUM(_sample_interval) AS lookups, MIN(timestamp), MAX(timestamp)
FROM blipscope_proxy
WHERE blob1='enrich_gap' AND blob2='type' AND timestamp > NOW() - INTERVAL '14' DAY
GROUP BY hex ORDER BY days DESC
```

> ### The measurement that proves the distinction is not academic
>
> [A4](enrichment-type-gap-2026-08-16.md) ranked four hexes as recurring "~9 times each"
> and queued all four for overrides — on **lookup count**. Re-measured by day
> (2026-08-17, 14-day window):
>
> | hex | days | lookups | span |
> |---|---|---|---|
> | a815d6 | 6 | 108 | 08-09 → 08-15 |
> | a590d8 | 5 | 27 | 08-12 → 08-17 |
> | ae67cc | 3 | 10 | 08-11 → 08-17 |
> | a7419f | 3 | 15 | 08-14 → 08-17 |
> | **a71203** | **1** | **15** | **08-16 21:39 → 21:55** |
>
> **`a71203` is a one-off** — every one of its 15 lookups inside a 16-minute window on a
> single evening. It has the same lookup count as `a7419f`, which appeared on three
> separate days. Queued on the wrong statistic; dropped.
>
> `ae67cc` has the *lowest* lookup count of the five and is unambiguously recurring.
> The two orderings disagree, and only one of them answers the question.

## Provenance rules

An override is hand-authored data in a namespace that otherwise holds an ingest, and
nothing in the row records where it came from. So the rules are tighter than the ingest's:

1. **Assert only what a source states.** The floor's discipline applies here too — if the
   best available source has no registration, the override has no registration.
2. **Community re-identification is acceptable for TYPE**, and is often the only source: a
   rotated Mode-S hex gets re-identified by milspotters well before it reaches a packaged
   Mictronics export. It is *not* acceptable for registration, where being wrong attaches
   a real tail number to the wrong airframe.
3. **Record every override below**, with its source and date. The KV row cannot carry it.

## The register

| hex | type | reg | days seen | source | added |
|---|---|---|---|---|---|
| `ae67cc` | `P8` | — *(none; ADSBX has none either)* | 3 (08-11 → 08-17) | ADSBX community curation; adsbdb `unknown aircraft`, no Mictronics row | 2026-08-17 |

**`ae67cc`** — a P-8 Poseidon transiting the Washington–Oregon corridor at FL290, the kind
of aircraft an owner notices and looks up. Both databases blank: adsbdb returns `unknown
aircraft` (verified against an AE5006 positive control and an FFFFFE negative control),
and there is no `mil:ae67cc` row (an explicit 404, not an unreadable probe). ADSBX carries
type P8 with `Reg: n/a` — so the registration gap is genuinely universal, not ours.
Written as `{"t":"P8"}`; `tn` and the square photo resolve from the existing library.

### Not overridden, and why

- **`a71203`** — one-off (see above). No override.
- **`a815d6`, `a590d8`, `a7419f`** — recurring by the day test and they *should* have
  overrides, but **there is no type source for any of them**: adsbdb returns `unknown
  aircraft` for all three (same controls as above). These are US **civil** addresses, so
  the military floor gives them nothing at all — they are the genuinely blank cards, not
  the degraded ones. Each needs manual identification before a row can be written, and an
  override invented without a source is worse than the blank it replaces.
