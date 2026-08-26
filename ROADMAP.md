# Blipscope (Aviation) — Feature Roadmap & Ideas

Product roadmap for the **Aviation radar edition**. Captured from the 2026-07-16 firmware
review; update it as items ship or priorities change. (Edition-level roadmap — new
Editions — lives in [README.md](README.md#more-editions-on-the-way); proxy work items
live in [proxy/README.md](proxy/README.md#work-queue).)

The review's core finding: the firmware architecture is sound, and the cheap value is in
**data we already fetch, compute, or ship hardware for but never surface to the customer**.

---

## Open work, consolidated 2026-08-13

Everything outstanding across sessions, in one place, so the tracker is the tree's
state and not anyone's memory of a conversation. **Verified against the tree**, which
moved four items straight to done — see "Already done" at the bottom.

Grouped by what blocks what. Within a group, order is priority.

### A. Blocking / in flight

| # | Item | State |
|---|---|---|
| A1 | Deploy `c046e4b` + `1c76d4a` + `d513aa2` (auth removal + enrich fixes) | **committed, NOT deployed** — waiting on the shared-key 401 confirmation |
| A2 | Rotate `DEVICE_KEY_SECRET` + re-enroll both boards + re-derive the bench identity | pending — **procedure written: [docs/bench-key-rotation.md](docs/bench-key-rotation.md)**. Run it after v7, see the sequencing note |
| A3 | **Cloud 401 handler** | **built** — `4391170`. Code complete on 6 envs; **bench proof outstanding**, folded into A2 (same doc, steps marked **[A3]**) |
| A4 | Re-measure the type-gap list with non-ICAO excluded | **done 2026-08-16** — [docs/enrichment-type-gap-2026-08-16.md](docs/enrichment-type-gap-2026-08-16.md). The skip cut it ~30x: 534-855 lookups/day before, 19-27 after. What remains is ~10 hexes/day, 85% US civil / 15% US military, nothing foreign, nothing phantom |
| A5 | Update the operator environment: `BLIP_KEY` still holds the **dead 48-char shared key** and `BLIP_DEVICE` is unset at user level | **breaks `smoke-prod.sh` and `watch-upstream.sh` on next run** — both now refuse without it. Closed by A2 §8, which mints the replacement |

**A2's sequencing, and why A3's proof rides on it.** Rotating invalidates both bench
boards, the `beefbeefbeefbeef` identity and the operator `BLIP_KEY` *simultaneously*.
Doing it before A1 confirms means two candidate causes for any failure, which is the
thing this whole sequence has been arranged to avoid.

It must also come **after v7 is cut and verified**: a rotation and a release fail
identically from the outside — the board stops showing live data — and the natural
response to the ambiguity is a reflash, which destroys the evidence for whichever it
actually was. Serialise them and each has a clean control. (Same family as
"[watch for the fix whose failure mimics the bug](CLAUDE.md)".)

A rotation is also the **only** way to produce a real sustained server-side 401 on a real
board, so it is A3's bench proof rather than an obstacle to it: one run exercises
detection, both debounce gates, the banner's priority over the stale ladder, the config
page's third state, one-action re-verification, and in-place recovery. Testing A3
separately would test it twice and the cheaper test would be the less faithful one.

### B. Photos

| # | Item | State |
|---|---|---|
| B1 | **Orientation reject class** — into the picksheet criteria, `validateEntry`, and the `suggest-commons` scorer. Definition: the long axis points substantially at or away from the camera, so the wings foreshorten and the silhouette is unreadable. Three-quarter fine; head-on and tail-on out. `>=70% of frame width` passes a nose-on shot, which is how one got in | not built |
| B2 | Audit the 234 for orientation — batched, offenders named, count and list in one pass. **Requires looking at each image**; it cannot be inferred from metadata and an attempt to do so is what produced a false vetting sheet once already | not started |
| B3 | **Livery caption** — see the correction below; this is NOT the cheap win it looks like | blocked on a design call |
| B4 | CC BY-SA **ShareAlike** notice on the credits page, before a fifth BY-SA image lands | not built — attribution and `changesNoted` are already correct; only the SA clause is missing |
| B5 | **Photo sourcing list** — **B505** (Bell 505), **PA23** (Piper Apache/Aztec), **AS21** (#49), **C414** (#82), **T210** (#4 — see below). Each: no photo, no picksheet entry, no alias. K100/RV9/C180 were PUBLISHED 2026-08-16; the rest are below the stop line the coverage curve drew and are recorded rather than queued | **unblocked** — KV write confirmed working 2026-08-14 by a full production ingest |
| B6 | KV coverage delta: types with **no square** (re-ingest, mechanical) reported separately from types with **no photo at all** (sourcing) | **delivered 2026-08-14** — `verify-release.sh`'s square probe reports exactly those two as distinct outcomes (FAIL vs WARN), and `ingest-photos.ts --dry-run` now prints CHANGED vs already-current per row |

> **These get photographs, not aliases.** The alias table's rule is that
> turbo/retract/engine variants qualify and different airframes do not. Both candidates
> sit near a tempting neighbour and neither qualifies: the Bell 505 is a different
> fuselage from the 206/407, and the PA-23 is a different airframe from the PA-31 and
> PA-34 that *are* in the library — "another Piper light twin" is a category, not a
> variant. Aliasing either would be stretching the rule to fill a slot, and a library
> whose rule bends to fill slots stops meaning anything. They show the silhouette until
> a real photo lands, which is honest.
>
> **How these are being found matters.** All three surfaced from someone looking at a
> screen, not from a query — the coverage question ("which types have no photo at all?")
> is answerable mechanically against the manifest and nobody was asking it. Worth a
> one-off sweep against a live type-frequency sample rather than waiting for the next
> aircraft to fly past a bench board.
>
> **T210 is here having been rejected AFTER publication (2026-08-17), which is the part
> to learn from.** It was ingested on 2026-08-16 in the batch of four and the reject
> landed the next day, so the local manifest row, the picksheet row and `src/t210.jpg`
> are gone but **four production pointers were already live** and had to be deleted
> separately. The pick failed on **two things together**: ~30° nose-on foreshortening
> *and* the smallest source in the batch at 1050×705, which leaves no margin for the
> 480 px square on the 2.1" panel. Either alone might have passed; together it is
> borderline on the pilot SKU and poor on the largest. At 4.4% of the remaining gap —
> under 0.1% of sightings — leaving the silhouette costs nothing, and **the playbook rule
> is replace, not crop harder.**
>
> Two standing consequences:
>
> 1. **A search returning exactly one candidate is a WARNING, not a recommendation.**
>    `suggest-commons` handing back a single result is an *empty search with a
>    consolation prize* — the sole survivor of a filter, presented in the same shape as a
>    winner, and the shape is what gets acted on. Read a result count of one as "this
>    type has no good options yet" and go to a manual Commons search; do not ingest the
>    consolation prize because it was the only thing on the sheet. B1 is unbuilt, so
>    nothing automated catches a nose-on shot — the human look is the only gate there is.
>    **Now a standing rule in the sourcing playbook** rather than only an incident note
>    here — see [blipscope-military-photo-sourcing.md](blipscope-military-photo-sourcing.md).
> 2. **A publish is not a draft.** The reject-after-publish path costs a KV deletion the
>    ingest script cannot do (it publishes from the manifest; it does not prune), so
>    removing a row locally leaves the fleet serving it. Eyes on the contact sheet
>    *before* the ingest, not after.
>
> **C185 (#13, 20 lookups) — APPLIED 2026-08-17 as an alias to C180**, having been looked
> at first. The rule permitted it where it refused B505/PA23 because it distinguishes
> hulls rather than refusing everything: the 185 is a strengthened 180 on the same
> fuselage, the `TBM7: "TBM8"` relationship. One line, no sourcing, and it clears #13 off
> the gap list. The published 240 px square was judged **in the disc** rather than as a
> flat square or a source file, and the reservation is recorded in `photos.ts` beside the
> entry: the tail is the feature that would contradict the alias and the angle hides it,
> so this is a *"you cannot tell"* pass. **Re-check the alias if C180 is ever re-picked**
> — a more identifiable C180 is a worse stand-in for the 185.

### C. Logbook / collection

| # | Item | State |
|---|---|---|
| C1 | Reflash both bench boards — **onto `f745f1f` or later, not `9cf0855`** (the device-id row and the non-ICAO skip landed after the fold) | pending |
| C2 | Caps and eviction sizing, now that operator names are the measured cost driver rather than the stores that look biggest | not started |
| C3 | Logbook **restore** — `/logbook.json` export ships; restore does not | not built |
| C4 | Trophy-cabinet Collection UI — recency not denominator, newest-first, milestone bar only, intro rewritten. Same commit rewrites the `Logbook.cpp` claiming-principle comment explaining why the invariant inverts | not started |

### D. Round card

| # | Item | State |
|---|---|---|
| D1 | Full-bleed 240x240 build — blit measurement first | server side ships (`FULLBLEED_MIN_FW = 7`, `squareSizeFor`); the firmware-side build is the open part |
| D2 | 240x240 photo variant behind its flag, until the firmware can scale rather than clip | deferred |

### E. Deferred / ideas

E1 callsign-intent features (medevac first: substring match, quiet indicator, excluded
from scoring), then emergency squawks, military callsign blocks, registration prefix ->
country, track-geometry behaviours, rarity as memory, curated seasonal lists.
E2 Turnstile enrollment for DIY buyers — built; the open question is what a self-enrolled
key is worth to an abuser. Revisit with real fleet traffic.
E3 the 88 resident hexes, if the enrichment overlay is ever revisited (see
[docs/enrichment-gap-notes.md](docs/enrichment-gap-notes.md) — do NOT build it off the
1,994).
E4 #131 config-page frame spike, filed and unprioritised.

### Already done — carried on lists but true in the tree

Checked 2026-08-13, because a stale "outstanding" item costs more than a missing one:

- **C3 retirement, bucket A** — no C3 variant header (`include/variants/` is
  `Variant.h` + four `s3_*`), no C3 envs in `platformio.ini`, no bisect harness (only a
  bench log survives). `SERIALIZE_TOUCH_BUS` exists solely in a historical comment.
- **`ENRICH_TLS_HEAP_FLOOR` and its call sites** — gone. The only survivors are
  historical comments and `probe/HeapProbe.cpp`, which *deliberately* mirrors the
  constant to demonstrate why it never fired.
- **CLAUDE.md's "three hard constraints"** — already rewritten as "Memory, networking and
  touch — what is actually true now", stating which two were false and why the third is
  kept on a dead rationale.
- **`ExitDetail` sprite fix** — the photo sprite is retained on PSRAM boards, with the
  measurement recorded inline (`AircraftManager.cpp:~3696`).
- **Military table parity** — enforced since `d513aa2`; `check_range_parity.mjs` parses
  both real sources and CI runs it with a five-mode selftest.
- **`BLIP_KEYS`** — code path removed (`c046e4b`) and the production secret deleted
  2026-08-13. The shared key 401s; both bench boards kept authenticating.

`BANDED_RENDER` is deliberately NOT on the deletion list: no SKU sets it, but it is
already `if constexpr` everywhere and is kept as the hook for a future PSRAM-less board.

---

## Release readiness (assessed 2026-07-17) — HELD

A large batch has merged to `main` since the last release (`FW_VERSION = 4`): visual
alerts, window-up, night clock, tones, Logbook v2, TODAY stats, airports overlay +
`/v1/airports`, the full military enrichment program (P0–P3), the photo library,
Aircraft-of-the-Day, MQTT events, distinct watchlist tones, location profiles, the
spotting leaderboard, rarest catch, and the AMOLED scaffold. All shipping SKUs build.

**Decision: do NOT cut the OTA release yet** — but the reason has changed. The
[[s3-128-overnight-slowdown]] was **closed 2026-07-21 as not reproduced** (see below), so
it is no longer what's holding the release. What remains are the feed-sourcing LAUNCH
BLOCKERS below. `FW_VERSION` was bumped to **5** on 2026-07-31 ahead of the Release: the
50-unit pilot boards are being burned now and must carry a version the `v5` Release can
supersede, and bumping ships nothing on its own — an OTA only happens once a Release
publishes a matching `version.txt`.

> ### ☑ LAUNCH CHECKLIST — publish a Release ≥ `fw5` on the `s3-128` channel BEFORE pilot assembly
>
> Not a nice-to-have: a shipped unit that sits *ahead* of `version.txt` has a dead OTA
> path from the moment it is boxed. The gate compares the device's `FW_VERSION` against
> the published `version.txt`, so a device on 5 against a published 4 never updates —
> and it never will, because it can only ever move forward. Every such unit needs a
> USB flash by hand, which for 50 boards in customers' homes is not a recovery, it is a
> recall.
>
> This is observed, not theoretical. The bench board reports it today:
>
> ```
> [ota] channel=s3-128 current=5 latest=4
> ```
>
> Fine on a board sitting on a desk with a USB cable in it. Fatal on a shipped one.
> **Publish the Release first, confirm `latest` ≥ `current` on one assembled unit, then
> assemble the rest.**

> ### PRINT GATE: THE QR ENCODES THE SCOPES URL, NEVER THE APEX SHORT FORM
>
> **QR target: `https://scopes.valarsystems.com/blipscope`. Not
> `valarsystems.com/blipscope`.**
>
> The short form works and is fine for a *typed* URL, but it is a **301** and
> cannot be made anything else. Traced 2026-08-26: the apex is `A 23.227.38.65`,
> **DNS-only**, straight to Shopify — Cloudflare's proxy never sees that traffic,
> which is why the zone has no redirect rule for it. The 301 is **Shopify
> UrlRedirect 369360142395**, and Shopify URL redirects are *always* 301: the
> resource type has no status field. There is nothing to configure.
>
> A 301 is cached by the browser indefinitely, so every scan of a printed card
> would permanently bind that phone to whatever the redirect said on the day it
> was scanned. Encoding the scopes URL **deletes that hop from the printed path**
> rather than trying to repair it. The Shopify redirect stays for people who type
> the short form.
>
> Hop 2 (`scopes.valarsystems.com/blipscope` → `/blipscope/support`) is a **302**
> as of #258, deployed `d35f504` and verified in production with `curl -sS -o
> /dev/null -D -`. Note `curl -sI` **cannot** verify it: `-sI` sends HEAD and the
> Worker answers HEAD with 405, so it reports 405 whatever the redirect does.
>
> So the destination stays re-pointable forever, and the card is bound only to
> the hostname we control.

> ### RESTORE THE STORE LINK AT LAUNCH
>
> `valarsystems.com/products/blipscope` is **DRAFT and returns 404**. On 2026-08-25 it
> was linked from the support page footer, the editions hub footer, and README.md --
> i.e. from the two pages a confused customer is most likely to be on. All three were
> replaced with a contact route rather than left pointing at a dead page.
>
> **At launch, restore all three.** They are marked with `LAUNCH:` comments in
> `proxy/pages/support.html`, `proxy/pages/index.html` and `README.md`, so
> `grep -rn "LAUNCH:" ` finds every one.
>
> Editing the two HTML pages means regenerating: `node proxy/scripts/embed-pages.mjs`.
> The `--check` gate in CI will catch a forgotten regenerate, but only after the fact.

**[[s3-128-overnight-slowdown]] — CLOSED 2026-07-21, not reproduced.** The original report
was real (observed ~14 h uptime: planes barely moving, taps needing several tries, detail
card ~10 s to close), but it never recurred under observation across three clean
multi-hour windows on the current build — frame time pinned 28–32 ms with no creep, the
largest heap block flat or *rising*, `allocFail`/`hardFail` at 0, and zero `LOOP STALL`.
Most likely explanation: the [[ghost-tap-stale-deadreckon]] pathology fixed in PR #93
(unbounded dead-reckoning plus eviction that never ran on a fully dead feed), which
matches the reported symptoms — an overflowing tap hit-test is exactly what makes touch
need several tries and a card slow to dismiss. Not being able to prove that is why this is
"closed, not reproduced" rather than "root-caused and fixed". The `LOOP STALL` / `max=` /
`allocFail` / `hardFail` telemetry added during the hunt stays in, so a recurrence will be
caught with evidence rather than anecdote.

> **That decision paid off on 2026-08-17, and this is the entry to point at when someone
> asks whether leaving instrumentation behind is worth the noise.** Board `.55` logged
> `DATA STALE` with the `tls=` counter **identical across a 30 s interval** — no
> handshakes, no reuses, no HTTP of any kind — while `tlsOk=1` and `rej=0` said the heap
> gate was healthy and had never fired. That is the **2026-07-09** shape recorded beside
> this hunt: *"fetches silent 22 min, loop healthy, task never dequeued."*
>
> **We have a second sighting instead of a second mystery**, and it arrived with numbers
> attached rather than as "the radar seemed stuck earlier". The counters that made it
> legible were left in for exactly this and cost nothing in between.
>
> One thing the recurrence exposes that the original hunt did not: the deepest fields —
> `[soak-state]` and the took-request/finished-request bracket, the ones that adjudicate
> *never enqueued* vs *enqueued and never dequeued* vs *dequeued and never completed* —
> sat behind `-DSOAK_TEST`, which also arms `SoakHarness`'s **synthetic taps**. So the
> instrument that answers the question also perturbs the board, and could not be pointed
> at an idle one. Hence `-DFETCH_TRACE` (`d9fbd66`): the same printfs, no harness, on the
> shipping backend. **Instrumentation left behind should be reachable without also
> enabling behaviour** — that is the refinement this second sighting bought.

**Production backend stood up 2026-07-17.** `scopes.valarsystems.com` is live: Worker
deployed, `[env.production]` KV namespace created + wired, `BLIP_KEYS` secret set, all
three datasets ingested (68 photos, ~17k mil airframes, ~9.4k airport tiles), and the
full authed path verified (`/v1/config` → 200, correct per-model tier, `upstreamState:ok`;
`/credits` serves the photos; failover feeds off pending licensing).

**Production hardening 2026-07-21** (PR #108): `cpu_ms = 200` per-invocation kill switch
added (there was none on either env — verified live in `script_runtime.limits`), request
log sampling taken 0.05 → **1.0** for the pilot, `refresh-data` reworked to refresh
**both** envs weekly, throwaway `*-prodburn` firmware envs added, and
`proxy/scripts/smoke-prod.sh` written (reads the key from the operator's env; never
prints it).

> ### ⚠️ BLOCKER — `CLOUDFLARE_API_TOKEN` repo secret is NOT set
>
> Verified empty via `gh secret list` on 2026-07-21. **This fails silently and that is
> the danger:** `refresh-data.yml` guards on the token and no-ops without it, so the
> weekly job goes **green while refreshing nothing**. Left alone, the military airframe
> and airport datasets quietly rot in *both* production and staging — and a stale
> staging is exactly the misleading pre-flight rig the both-envs refresh was meant to
> prevent.
>
> Fix: repo secret `CLOUDFLARE_API_TOKEN`, scoped **Workers KV Storage: Edit**.
> Confirm with a manual run: Actions → refresh-data → Run workflow → `both`.

**Still pending on the production path:** (1) the `CLOUDFLARE_API_TOKEN` repo secret
above, (2) firmware repointed at production (`CLOUD_FEED_BASE` + a baked/ per-device key)
— no longer gated on the slowdown (closed 2026-07-21); now gated on the pilot burn-in.
Bench boards can run against production today via the throwaway `*-prodburn` envs without
touching the shipping `*-cloud` envs. (3) **Before the pilot ships:** an
Account Analytics **Read** API token so the `enrich_gap` points (PR #110) are readable —
they are being written now but are write-only until then, and the ranked "what to add to
the photo library next" query in the proxy README needs it. Keep it a *separate*,
read-only token from the KV-write one above: different blast radius. Not urgent while the
fleet is one bench board (a ranking off one location is meaningless); the data accumulates
either way and can be queried retroactively within Analytics Engine retention.

**Production feed findings (2026-07-18 bench session) — LAUNCH BLOCKERS:**
See [proxy/FEED-SOURCING.md](proxy/FEED-SOURCING.md) for the full analysis + outreach drafts.
- ### FEED SOURCING AT LAUNCH: TWO PERMITTED SOURCES, AND THE BINDING CONSTRAINT IS A RATE LIMIT, NOT A LICENCE

  **The authoritative version of this lives in
  [proxy/README.md](proxy/README.md#upstream-licensing-posture) and
  [relay/setup-relay.sh](relay/setup-relay.sh); this is the summary.** Both were ahead of
  this file, and a tracker that lags the artifact on the thing you read it for is worse
  than none — so on flash day, trust those two.

  - **adsb.fi — PRIMARY, permitted commercially in writing.** Samuli granted commercial
    use *including caching* on **2026-08-05** ("If you can keep usage within the Open Data
    API rate limit, I am happy to let you use it for your mentioned purpose, including the
    caching system"), where the stated purpose was a paid hardware product, and separately
    confirmed polling from both relay IPs. **The permission is conditional on the rate
    limit and on nothing else.** There is no flip to make and it is not a launch blocker.
    *(This entry twice said the opposite — first that adsb.fi 403s us, then that its terms
    forbade commercial use. Both were readings of the PUBLISHED TERMS rather than of our
    correspondence. Corrected 2026-08-13.)*
  - **adsb.lol — FALLBACK, ODbL 1.0.** Demoted on operational grounds, not licensing:
    68% 429 in the same soak where adsb.fi returned 0%. Kept precisely because ODbL is
    **a right no operator can revoke**, which is worth more behind us than in front, and
    `adsb_lol_b` is deliberately the terminal feed the breaker may never skip. Sponsored
    at **$50/mo unconditionally, regardless of chain position.**
  - **airplanes.live — PROHIBITED.** Written operator refusal, 2026-07-22. Hardcoded
    `enabled: () => false`; no env flip can revive it. Off the table, not undecided.

  So at launch there are **two permitted sources with a real failover between them**, and
  the licence question is answered. What remains is arithmetic.

  #### The rate model, and the one number that could still bite

  The constraint is **adsb.fi's 1 req/s per IP, with 4xx/429 responses counting toward
  it** — so a re-firing failure digs the hole deeper rather than merely failing. Upstream
  rate is **(distinct hot tiles) / `CACHE_TTL`**, *independent of device count*: the fleet
  collapses to one fetch per tile per TTL, so ten boards in one city cost what one does.

  **Measured now** (Analytics Engine, 2026-08-13, ~2 active boards) — Worker-level cache
  MISS, which is an **upper bound** since the relay collapses further:

  | | fleet-wide | per device |
  |---|---|---|
  | average | 0.050 req/s (181 req/h) | ~0.025 req/s |
  | busiest single minute in 30 d | 0.38 req/s (23 fetches) | ~0.19 req/s |

  **Projected at 50 boards** — and the linear ×25 is the WRONG model; tiles are:
  50 scattered boards ≈ 50 distinct 0.05° tiles ÷ 30 s TTL = **1.67 req/s**, against a
  two-IP budget of 2 req/s. **~17% headroom.** That is the sizing `CACHE_TTL=30s` was
  chosen for (resolved 2026-08-08, see the pinched-knob analysis in `setup-relay.sh`).

  **The condition to watch, stated plainly: the 2 req/s budget requires BOTH relay IPs to
  be carrying traffic.** That holds under `partitionOrder`'s hash split, but under *pure
  failover* — every request landing on relay-a — the budget collapses to 1 req/s and
  50 tiles / 30 s = 1.67 req/s is **67% OVER the limit**, on a source where overages
  count toward the limit and earn an IP restriction. Mitigation is a TTL raise to 50 s,
  which is why the knob is documented as pinched rather than tuned.

  **Split CONFIRMED 2026-08-13: relay-b 53%, relay-a 47%** over 24 h (Analytics Engine
  `blob3`, the leg the Worker actually dialled), with **zero** adsb.lol requests — no
  failover events at all. Both sanctioned IPs are live, so the 2 req/s budget is real and
  the 17% worst-case headroom stands.

  #### DECISION 2026-08-13: no commercial tier for the pilot. BUY AT ~60 UNITS.

  adsb.fi's commercial tier is **€1,500/year**. At 50 units that is $2.72/device/year
  against a $49 product; at 600 units it is $0.23 and obviously worth it. The pilot is
  inside the sanctioned free limit honestly — 42% of it in the realistic case, 83% in the
  pessimistic — so buying now would be paying for capacity we would sit on.

  **THE TRIGGER IS UNIT COUNT, NOT A USAGE READING, AND THAT IS THE WHOLE POINT.**
  Anyone who later checks a dashboard, sees low utilisation, and concludes there is room
  will have measured the wrong variable. Load does **not** scale with devices or with
  requests — it scales with **distinct hot tiles**, because every device inside one
  0.05° tile collapses to a single upstream fetch per TTL. Two bench boards in the same
  house added **zero** upstream load while doubling request volume; fifty boards in fifty
  towns add fifty tiles. So usage can look flat right up until the unit that crosses the
  line, and it crosses on geography, not traffic.

  The ceiling is arithmetic: `2 IPs x 1 req/s x 30 s TTL` = **60 simultaneously-hot
  tiles**. Worst case (one tile per unit, all awake) that binds at **60 units**;
  with metro clustering and diurnal spread, ~85-90. **Plan the purchase at 60** — the
  worst case is the one to plan against, because 4xx/429 responses count toward the limit,
  so an overshoot compounds into an IP restriction instead of degrading gracefully.

  Levers if the number needs moving before then, cheapest first: both relays (already
  banked — that is why we are at 83% and not 167%), then `CACHE_TTL` 30 s -> 45 s (linear,
  60 -> 90 tiles, but it is NOT a one-line change — see the order of operations in
  `setup-relay.sh`; backwards shows the whole fleet amber), then tile coarsening 0.05° ->
  0.1° (quarters the tile count, costs upstream bandwidth and edge relevance as the radius
  margin grows from 4 km to ~8 km).

  Still to do: `relay/measure.mjs` on both boxes for the adsb.fi-FACING figure. The split
  above is the Worker's view, one layer above the relay's nginx cache, so it answers
  "are both IPs live" but overstates the upstream rate.

  **What degradation costs the customer (measured over 30 d, 2026-07-14 → 08-13):**

  | | |
  |---|---|
  | `/blips` requests | ~76,000 |
  | served `STALE` | 3,320 (4.4%) — **normal**, this is stale-while-revalidate doing its job |
  | fast `503` ("warming") | 235 (0.31%) |
  | hours with any degradation | 201 of ~720 (28%) — but almost all of it is routine STALE |
  | hours that were a real **outage** | **1** — 2026-07-22 04:00 UTC, 111 of 137 requests 503 |

  **What a customer sees, and it is honest.** The device does not freeze a false picture
  or go blank; it escalates a three-stage ladder (`DrawStaleIndicator` /
  `CurrentStaleStage` in [AircraftManager.cpp](src/AircraftManager.cpp)):

  1. **`STALE DATA`** — quiet amber, deliberately no number. A few missed polls is routine
     on every source and does not deserve a countdown.
  2. **`STALE 12m` / `STALE 2h`** past 75 s — it earns a number, because "how long has this
     been wrong?" is the question someone actually has.
  3. **`NO DATA — 3h`** in red at the dead-reckoning cap (`MAX_DR_SECONDS`, bound to that
     constant rather than copied from it). That is the moment the sky freezes in place, so
     it is the moment the display stops implying the picture means anything.

  **Verdict: the ladder covers it.** A single outage hour in 30 days presents as amber
  "STALE DATA" and self-clears; it does not read as broken. With two permitted sources
  the residual exposure is a **simultaneous** failure of both — or, more plausibly, an
  adsb.fi IP restriction earned by exceeding the rate limit, which is the scenario the
  relay-split measurement above exists to prevent. In that case the chain falls to
  adsb.lol, whose measured 429 rate in the same soak was 68%, and the fleet would sit
  amber-to-red until the restriction lifted.

- **The 429 problem, for context.** adsb.lol shared-egress **429**s Cloudflare's outbound
  IPs (other tenants' traffic on the shared per-colo IP, not our volume), worst on the
  high-volume `/point`. Solved operationally by the dedicated-IP relays, not by a key: per
  adsb.lol's docs the API key is *future*, and their feeder API (`re-api`) is **IP-locked**
  to the feeding station, so a Worker cannot use it.
- **The open ask is a second source, and it is commercial, not technical.** Price a paid
  API (ADSBexchange/RapidAPI, FlightAware AeroAPI) as the SLA-backed fallback — a few
  ¢/device/month, baked into pricing — and/or obtain a written commercial grant from
  adsb.fi. Drafts in FEED-SOURCING.md.
- ~~**adsb.fi failover returns HTTP 403**~~ ~~**superseded by a licensing blocker**~~
  **FULLY RESOLVED — adsb.fi is the chain PRIMARY.** Both claims in this entry were
  wrong, in sequence, and both by the same mechanism:
  - The **403** was Cloudflare's shared egress, not an adsb.fi block. Both relay IPs
    (`67.205.155.80`, `104.238.156.243`) get **HTTP 200** unauthenticated. *(Resolved
    2026-07-29.)*
  - The **licence blocker** was a reading of adsb.fi's *published terms* while a written
    grant to us already existed in the thread. Samuli granted commercial use *including
    caching* on **2026-08-05**, conditional on the rate limit and nothing else. See the
    authoritative entry above.
  - The **"1 req/s is below fleet need"** figure was never measured against. It has been
    now: **170 and 167 upstream position fetches/hour** on relay-a and relay-b — **4.7 %
    and 4.6 %** of the limit, zero 429s, zero degraded runs *(measured 2026-08-13)*. Load
    scales with distinct hot tiles, not devices, so this is not a per-device figure.

  Both wrong readings came from a public page rather than from our own correspondence,
  and the rate-limit claim came from arithmetic nobody ran. The pattern is the entry, not
  the conclusion.
- ~~**BLIP_KEYS drift.**~~ **RESOLVED 2026-08-13 by deleting the secret.** The shared key
  is gone from production; per-device keys (`HMAC(DEVICE_KEY_SECRET, deviceId)` presented
  with `X-Blip-Device`) are the only auth path. Verified before removal: zero successful
  device requests in the preceding 6 h had used anything else. See
  [docs/device-enrollment.md](docs/device-enrollment.md).
- **PILOT BLOCKER — a rejected credential is invisible and unrecoverable.** Found while
  removing `BLIP_KEYS`. The device has **no handler for a sustained cloud 401**:
  `FetchResult.authFailed` is set for OpenSky only, and `AircraftManager.cpp` explicitly
  comments that "a cloud 401 is a key mismatch — retrying can't fix it". So a board whose
  key stops working goes quiet with **no on-screen indication and no config-page
  indication**, and the only recovery is a hand re-verify nobody knows to perform.

  The consequence is bigger than one board: **we currently have no way to rotate fleet
  credentials.** A leaked `DEVICE_KEY_SECRET` would mean asking every customer to
  re-verify by hand, and most won't. That makes the rotation lever theoretical, which is
  the same as not having it. Scope in "Credential recovery" under Tier 1.
- **Flash the photo-null fix** (PR #94, [[ghost-tap-stale-deadreckon]] sibling) to the bench
  and fold into the `v5` OTA — the bench still runs the ghost-tap build.

**Go criteria (both):** (1) a clean 24 h bench soak on the s3-128 with the full feature
set, (2) the shipping features get a bench pass (below). The former slowdown criterion is
retired with the bug (closed 2026-07-21, not reproduced) — but the 24 h soak is **kept**:
it was always broader than the slowdown hunt, and with the bug closed on absence-of-
evidence rather than a root cause, an unattended soak is the cheapest thing standing
between us and shipping a recurrence to the fleet.
When those clear: publish the `v5` Release per [RELEASING.md](RELEASING.md) — the CI matrix
already covers every SKU, and `FW_VERSION` is already at 5.

---

## Tier 1 — Quick wins ("alerts & polish" release)

Small diffs, immediate perceived value. Ship together as one minor release.

0. **Credential recovery — a rejected key must be visible and re-fixable** *(PILOT
   BLOCKER, scoped 2026-08-13, NOT YET BUILT)*. Without this the credential-rotation
   lever is theoretical: see the launch-blocker entry above.

   **The good news first: the recovery ACTION already exists.** The Verify button, the
   Turnstile popup, the `?id=` paste fallback and the `/enroll-key` landing endpoint all
   ship today, and enrollment is idempotent — a re-verify re-derives and overwrites
   `cloud-key-fac`. Nothing new is needed to *fix* a board. What is missing is the
   **condition that offers them**, which is why this is smaller than it looks.

   Three pieces:

   1. **Detect, with a debounce.** `FetchResult` gains a cloud-specific
      `authRejected` (set only on **401/403 from the cloud feed** — never a network
      error, a 503 "warming", or a captive portal, all of which already land in the same
      branch at `AircraftManager.cpp:~1707` and must NOT latch). Latch only when BOTH a
      consecutive-failure count **and** an elapsed-time floor are crossed (suggest 5
      consecutive *and* ≥15 min); any 2xx clears it instantly. Requiring both matters: a
      count alone is burned through in seconds at the fast post-touch poll cadence, and a
      timer alone latches on one blip that happens to straddle it.
   2. **Surface it in the config page** — cheap. `enrolled` (from `cloud-key-fac`) already
      drives the verify checklist; this makes the state **three-way** instead of two:
      never-enrolled / enrolled-OK / enrolled-but-rejected. The third reuses the existing
      Verify step with different copy. A never-enrolled board must NOT say
      "re-verify" — that is the whole reason it is three states and not a boolean.
   3. **Surface it on screen** — the real cost, because **there is no message/banner
      facility at all** (searched: no `ShowMessage`, no status overlay, no centered-string
      helper). Needs a design call on a 240 px round panel and a new drawing path. Gate on
      `FEATURE_CLOUD_FEED`; OpenSky-BYO and local-receiver devices are unaffected.

   **Cost:** (1) and (2) are small and contained — a field, a counter, one condition and
   some copy. (3) is the majority of the work and the only part needing a design decision.
   Worth splitting: (1)+(2) alone already turn "silently dead" into "the config page tells
   you and offers the fix", which is most of the value.

   **Failure modes to guard, since a false latch is worse than none:**
   - *Transient blip latches.* Mitigated by 401/403-only + both thresholds + instant clear.
   - *Fleet-wide latch during a deliberate rotation.* This is the DESIRED behaviour — but
     it means every customer sees the message at once, so the copy must be neutral
     ("this device needs re-verifying"), never accusatory or alarming.
   - *A revoked board latches permanently.* The device cannot distinguish revoked from
     stale-key (both are 401), so it must not claim to. "Needs re-verifying" is true in
     both cases; a revoked board then fails re-verification and the enrol page can say why.
   - *The banner becoming permanent furniture* on a device that legitimately cannot
     recover — decide up front whether it dims/collapses after N days, and mind burn-in.

1. **Visual alert system for military + emergency contacts** — **IMPLEMENTED
   2026-07-16** (`UpdateVisualAlerts`/`DrawVisualAlert` in `AircraftManager.cpp`;
   config keys `mil-visual`, `emg-visual`, `visual-night`; defaults: emergency = ring,
   military = off). Ring pulse bench-verified on the Kit S3 (s3-128) 2026-07-16; the
   flash burst and the night-dim override still want a bench pass before release. *(Priority was raised
   because the launch SKU is a 1.28" board without audio, so on-screen alerting is
   the primary attention channel.)* Per-class selector — **off / ring pulse / full
   flash** — for military and emergency-squawk contacts:
   - *Ring pulse (recommended default when enabled):* a color-coded band around the
     outer bezel (orange = military, red = emergency) pulsing ~1 Hz. Radar stays fully
     readable; the overlay pattern already exists (LOOK UP ring, emergency ping ring).
   - *Full flash:* 2–3 full-screen color pulses **edge-triggered when the contact first
     appears** (per-aircraft dedupe, same pattern as `watchNotified`), then settle to
     the ring pulse. Never a sustained strobe; stay well under 3 flashes/sec
     (photosensitivity / WCAG).
   - *Backlight pulse:* modulate `configuredBrightness` instead of drawing — zero
     render cost, works on every SKU, good as the gentle night variant.
   - *Night behavior toggle:* alerts override auto-dim, or respect it (bedroom vs.
     office device).
   Emergency squawks keep the always-on ping ring as the baseline; the selector adds
   intensity above it.
2. **ntfy alert for emergency squawks** — **IMPLEMENTED 2026-07-16** (config `emg-alert`,
   default off; one-shot per tracking session via the existing `QueueNtfyPost` path).
3. **Distance column in the List screen** — **IMPLEMENTED 2026-07-16** (callsign / type /
   distance / altitude columns, distance in the radar's unit).
4. **Surface route data outside the detail card** — **IMPLEMENTED 2026-07-16** as a new
   "Route" aircraft-info field (`info-route`, default off) drawn as `ORG>DST` on radar
   labels; cloud mode fills it from background enrichment, BYO/adsbdb after first inspect.
5. **Distinct alert tones** — **IMPLEMENTED 2026-07-16** (s3-146 / s3-21 only; inert on
   speaker-less SKUs). Chirp-pattern sequencer: new contact 1×40 ms, watchlist 2×40,
   military 2×70, overhead 3×40, emergency 4×80; master `tones` toggle (default on).

## Tier 2 — Logbook v2 (the headline feature of the next major version)

The Stats screen is entirely instantaneous and the logbook stores only set membership.
For a product whose emotional hook is plane-spotting, **history is the retention feature**.

6. **Logbook depth** — **IMPLEMENTED 2026-07-16** (Logbook v2): per-type first-seen date
   + sighting count, per-airline first-seen date, lifetime records (highest / fastest /
   closest ever with callsign + date, plausibility-bounded), all in the same debounced
   NVS pattern with legacy-blob migration; a compact "Best" line joins the Stats
   LIFELIST block. ("Rarest catch" needs global rarity data — deferred to the cloud.)
7. **Daily/session stats** — **IMPLEMENTED 2026-07-16**: a TODAY block on Stats with
   contacts-since-midnight, peak simultaneous count, busiest hour, and a 24-bar hourly
   sparkline. RAM-only (no flash wear; resets at local midnight/reboot, NTP-gated), and
   the whole Stats screen is now clock-guarded so blocks drop by priority on 240 px panels.
8. **"Airports seen"** — **IMPLEMENTED 2026-07-16**: fourth lifelist set (300-code cap),
   fed from route endpoints at both enrichment apply points (cloud + adsbdb), shown on
   the Stats LIFELIST block.
9. **Logbook export / web view** — **IMPLEMENTED 2026-07-16**: `GET /logbook.json` on the
   config server (full lifelist: types with first-seen dates + counts, airlines,
   countries, airports, records; ISO dates), read straight from NVS so it's async-task
   safe (≤1 debounce interval stale), linked from the config page's logbook section.

## Tier 3 — Bigger differentiators

10. **Airport overlay on the radar** — **IMPLEMENTED 2026-07-16** (baked ~260-entry
    major-airport table in `include/Airports.h`, dim markers + IATA codes under the
    aircraft layer, `airports` display toggle default on). **Long-tail follow-up also
    SHIPPED 2026-07-16**: `GET /v1/airports?lat&lon&r` serves the full OurAirports
    dataset (public domain; 48k airports pre-tiled into KV by `npm run
    ingest:airports`, priority-sorted L>M>S, capped at 60) and cloud devices fetch it
    once the location is known (daily refresh, `FetchKind::Airports` on the shared
    fetch task). While loaded it supersedes the baked table; small fields hide at wide
    zooms so the face never clutters. The baked majors stay as the BYO/offline fallback.
    **Follow-up (user request 2026-07-17):** an `airports-min` config select — **All /
    Medium+large / Large only** — filtering device-side on the `kind` field already on
    the wire (no proxy change). Driver: a busy-GA area shows ~20 small strips when the
    user only cares about the 2 with scheduled service; the zoom rule alone doesn't
    express that preference.
11. **Config apply without restart** — **ALREADY SHIPPED** (verified 2026-07-16): every
    web save raises `ConsumeConfigChanged` and `main.cpp` re-runs `Initialise()` live on
    the loop task, no reboot. The roadmap entry came from a stale header comment, now fixed.
12. **Receiver-health on the Stats screen** — **IMPLEMENTED 2026-07-16**: a FEED block
    (source, honest data age incl. server lag, STALE flag, poll cadence, hard-fail
    count), space-guarded so the small panel never collides. Makes a quietly failing
    feed diagnosable from the device.
13. **Night clock mode** — **IMPLEMENTED 2026-07-16** (config `night-clock`, default off):
    at solar night with an empty sky, the radar face becomes a big seven-segment clock;
    any traffic instantly restores the radar. The EAM seven-segment renderer was promoted
    to shared code (`include/SevenSegment.h` + `src/SevenSegment.cpp`, namespace
    `sevenseg`, EAM-compat shim).

## Proxy-side (no firmware change; already accepted in proxy/README.md)

- **Populate the stock-photo library** — **58 TYPES LIVE ON STAGING 2026-07-16**: the
  `npm run harvest` tool (Commons extmetadata → license-gated manifest rows) shipped
  three batches same-day — Tier-1 military, military batch 2 (Apache/Texan II/Chinook/
  C-130H/T-38/Osprey/P-8/Super Hornet; ~65% of the mil fleet photo-covered), and the
  civil long tail (737 + A320neo families, E-Jets, CRJs, Q400, widebodies, GA/bizjet/
  helos). Remaining: further long tail by proxy-log traffic, production ingest at launch.
- **Military enrichment deepening** — **P0 + P1 SHIPPED 2026-07-16** (empty-meta
  negative TTL; mil-block + dbFlags operator floor at serve time, `proxy/src/military.ts`);
  **deployed to staging + smoke-tested 2026-07-16**. **P2 static airframe dataset
  SHIPPED 2026-07-16** (license review passed: Mictronics/aircraft-database, ODC-By 1.0;
  `mil:<hex>` KV side table + `npm run ingest:mildb`, ~17.3k typed military airframes —
  type resolution also unlocks the existing military stock photos). Remaining: P3
  callsign-prefix fill.

## Idea backlog (unscheduled)

- **Public spotting leaderboard** — see the full concept below.
- **Stale-feed eviction gap** (found 2026-07-18): eviction only runs on a *successful*
  feed merge, so when the feed is fully down, contacts are never removed and dead-reckon
  in place. The 600 s DR cap (PR #93) now bounds their position so they can't overflow the
  tap hit-test, but they still linger on screen. Consider evicting after a hard stale
  threshold even without a merge.
- **Config-page env/key UX** (found 2026-07-18): switching `cloud-url` between environments
  silently keeps the old `cloud-key` (it's masked and skip-on-save), producing a confusing
  401 with no on-screen hint. Consider forcing key re-entry when the URL changes, and/or
  surfacing the active source URL + last auth status (`config rev` vs `401`) on the config
  page and the device FEED block.
- Watchlist match alert sound distinct per entry class.
- HA/MQTT: publish watchlist/emergency hits as Home Assistant *events*, not just state.
- ~~Compass rose / north-up vs. track-up toggle~~ — **shipped 2026-07-16 as "window-up"**:
  config `radar-up` sets the compass bearing at the top of the screen (0 = north-up), so
  the radar matches the view out the user's window.
- "Aircraft of the day" / notable-catch summary card (gamification, pairs with Logbook v2).
- **Trophy cabinet** (2026-08-10) — gamify the plane collection: the owner *earns trophies
  for accomplishing things*, rather than only watching four counters go up. Sits naturally
  on the logbook, which as of v5 has the storage to support it (raised caps, eviction that
  never spends a claim, and `claimDay` on every entry — so "when" is already recorded and
  a trophy can be awarded retroactively from existing data).

  **Which trophies, and how they are implemented, are both undecided and are NOT being
  designed yet.** Recorded here so the idea isn't lost, not as a spec. The open questions
  worth capturing while they're fresh: whether a trophy is derived on demand from the
  stores (cheap, no migration, always consistent) or persisted as its own award record
  (survives eviction, costs NVS entries and a schema); whether trophies are per-device or
  tied to the leaderboard account; and whether any of them can be earned by a device that
  simply runs longer, which would make them a measure of uptime rather than of spotting.
- Multi-location profiles (home/work) — probably niche; revisit on demand.
- 1.75" AMOLED premium SKU (466×466 + mic) — **SCAFFOLD SHIPPED 2026-07-17, BENCH-ONLY:**
  variant header (`include/variants/s3_175_amoled.h`), LGFX.h CO5300/FT5x06 blocks (both
  controllers are built into LovyanGFX — no custom driver), and the
  `blipscope-pro-s3-175-amoled` env all build. **Pins are UNVERIFIED** (seeded from the
  AMOLED family) — no CI row, do not flash a real board until the pin map is confirmed
  against the Waveshare wiki and bring-up is done. Remaining: pin verification + bench
  bring-up, then a `-cloud` sibling + CI row.

### Deliberately not pursuing (reviewed and parked)

- IMU gestures (shake/tilt navigation) — touch already covers navigation; novelty per
  effort is poor. The Stats tilt readout stays as-is.
- Features on the s3-128's extra hardware (RTC / SD / WS2812 LEDs) — that board is
  bench-only until the touch-wedge gate passes.
- More radar-view eye candy — the sweep/trails/fade layer is already rich; marginal
  value is now in data and history, not pixels.

---

## Concept: public spotting leaderboard

**The pitch:** make spotting competitive — who has seen the most aircraft, the most
unique types, the most airlines/countries. A public web leaderboard with lifetime and
monthly-season boards. Pairs naturally with Logbook v2 (the device already tallies
exactly the numbers a leaderboard needs).

**Feasibility: yes — and we already own the server.** The Cloudflare Worker
([proxy/](proxy/)) already fronts the fleet, has KV, auth, rate limiting, and a
precedent for public unauthenticated pages (`GET /credits`). No new infrastructure
class is required.

### What "an account" actually needs to be

Not a full account system. The minimum viable identity is:

- **Device identity:** the device already derives a unique name (`Blipscope-A1B2C3`,
  MAC-based). A leaderboard ID can be derived the same way (salted hash of MAC so the
  raw MAC never leaves the device).
- **Display name:** one new config-page field ("Leaderboard name") + an opt-in toggle.
  Claim-on-first-submit (first device to submit a name owns it, name pinned to its
  device ID in KV/D1). No email, no password, no PII.
- Later, if desired: link devices to a real valarsystems.com account to merge multiple
  devices — not needed for v1.

### Architecture sketch

```
device (opt-in) ──POST /v1/leaderboard (hourly, ~100 B: deviceId, name,
                     {types, airlines, countries, contacts})──> Worker ──> D1 (or KV)
browser ──GET /leaderboard (public HTML, like /credits)──> Worker
```

- **Firmware delta (small):** config toggle + name field; one queued POST per hour from
  the existing enrichment/ntfy task carrying the logbook tallies. Off by default.
- **Worker delta (moderate):** one authed submit endpoint (same `X-Blip-Key` + rate
  limits), a D1 table (`deviceId, name, tallies, updatedAt`), a public rendered
  leaderboard page + JSON. Monthly seasons = snapshot deltas by month.
- **Cost:** ~24 requests/device/day — noise next to the poll traffic in the existing
  cost model.

### The two honest problems (and the plan)

1. **Cheating.** The firmware is open source and v1 ships one *shared* fleet key, so
   the submit endpoint is spoofable by anyone who extracts the key. Mitigations, in
   order: (a) server-side plausibility caps (tallies can only grow, bounded growth rate,
   counts sanity-checked against what a real sky produces); (b) a **"verified" tier for
   cloud-feed devices** — the Worker serves those devices their blips, so it can
   sanity-check claimed growth against traffic it actually served; local-receiver
   devices show as "unverified" (or self-reported ☂ badge); (c) the real fix is
   **per-device keys**, which proxy v1 explicitly deferred — the leaderboard is the
   feature that eventually justifies them. Start honor-system with caps; a desk-gadget
   leaderboard doesn't need bank-grade integrity on day one.
2. **Privacy.** Our stated stance is operational-telemetry-only with an architectural
   opt-out ([README.md](README.md#privacy--telemetry)). The leaderboard must be
   **opt-in, off by default**, send **counts only — never location, never aircraft
   lists**, and the README privacy section gets a new paragraph describing exactly what
   an opted-in device sends. Local-receiver users who opt in knowingly open one
   narrow channel; everyone else stays at zero.

### Scoring design (settled 2026-07-16)

**Scoring is passive and derives entirely from the Logbook — never from user actions.**
Explicitly rejected: points for opening detail cards or any other interaction (instantly
grindable, and cloud devices background-enrich automatically anyway). You score by what
flies over your house and what your device logs; the only way to score more is to spot
more. That's the game.

**Spotter Score** (lifetime) = weighted lifelist:

| Category | Points | Why the weight |
|---|---|---|
| unique type | 10 × rarity | the core collectible |
| unique airline | 5 | secondary collectible |
| unique country | 25 | genuinely hard to grow |
| unique airport (route endpoints) | 2 | easy to grow, small spice |
| raw contacts | 0 | pure uptime/density — never scores |

**Rarity multiplier** (types only, computed server-side each season): a type seen by
<5% of opted-in devices scores ×5, <25% ×2, else ×1. This is the equalizer — a device
in rural Oregon can't out-volume one under the O'Hare approach, but a crop duster,
a warbird, or a C-17 on a odd routing is worth real points anywhere. Weights are
server-side only, tunable without firmware changes.

**Season score** (monthly): same formula over entries **new to your lifelist this
month**. Everyone restarts at 0 monthly — newcomers can win a season in their first
month while lifetime boards stay the long game.

**Radius fairness (settled 2026-07-17):** the configured radar radius would otherwise
be a score multiplier (bigger circle, more contacts). Fix: **a standardized scoring
radius of 30 miles (≈48 km), applied in the background** — never touching the user's
display radius, which is a viewing preference (a small circle is exactly right for
"what can I walk outside and see"). In cloud mode an opted-in device polls `/v1/blips`
at `max(userRadius, SCORING_R)`; contacts beyond the user's radius are **scoring-only**
— the logbook counts them but they never appear on the radar or list screens. Devices
with a radius already ≥ 30 mi score only from the inner 30 mi. Side benefit: every
verified competitor observes the same-sized sky, which makes the server's
plausibility check sharper. BYO/local devices can't be normalized (their receiver is
their radius) — one more reason they compete as "unverified".

**Badges** (the "interesting for everyone" layer — non-competitive, derived
server-side from the same submission): Century Club (100 types), Widebody Collector,
Warbird Spotter (10 mil types), Globetrotter (15 countries), Streak (30 consecutive
submission days with a new entry), First! (first device in the fleet ever to log a
type — permanent credit on the type). Badge case shows on the public profile.

### Surfaces

- **Config page:** one new section — opt-in checkbox (default OFF) + "Spotter name"
  field (claim-on-first-submit; server suffixes collisions) + a link to the public
  board and to the privacy paragraph.
- **On the scope:** a compact LEADERBOARD block on the existing Stats screen (no new
  screen): `RANK #42 · 1,240 PTS · SEASON #17 ↑3`, clock-guarded like the other Stats
  blocks. Optionally later: a one-shot toast when rank improves.
- **Public web:** `GET /leaderboard` (like `/credits`, unauthenticated HTML + JSON):
  season board default, lifetime tab, per-category leaders (most types / airlines /
  countries / airports), badges + verified check on each row; `/leaderboard/<id>`
  profile with the badge case. Shows **percentile framing** ("top 12%") so mid-board
  devices see progress, not just distance from #1.

### Wire + storage

Hourly `POST /v1/leaderboard` (same auth/rate limits): `{id, name, counts{types,
airlines, countries, airports}, typeCodes[]}`. `id` = salted hash of MAC (raw MAC
never leaves the device). **`typeCodes` (the ICAO type list, ~1 KB) is the one list
sent** — needed for rarity scoring, verification, and badges; airports/airlines/
countries stay counts-only because those lists fingerprint the user's location.
Never position, never per-aircraft sightings, never timestamps of sightings. The
README privacy section documents exactly this before launch. Storage: KV rows
(`lb:dev:<id>`) + a cron-triggered Worker aggregating top-N boards into `lb:board`
(D1 only if/when the fleet outgrows that).

### Anti-cheat (v1: honor system + caps)

Counts must be monotonic; growth-rate caps (**≥150 new types/day is implausible** — see
the calibration note below); type codes validated against the known-designator set;
**verified tier** for
cloud-feed devices (the Worker can sanity-check claimed type growth against traffic it
actually served that device); outliers shadow-flagged for review, not auto-banned.
Per-device keys remain the real fix and the leaderboard is what eventually justifies
them. **Foundation shipped 2026-07-17** (additive, server-side): `src/deviceauth.ts`
accepts HMAC-derived per-device keys (`X-Blip-Key` + `X-Blip-Device`) alongside the
shared `BLIP_KEYS`, gated on `DEVICE_KEY_SECRET` so the live fleet is untouched;
`npm run derive-device-key` mints them at manufacture; device-authed requests get their
own rate-limit bucket. Staged follow-ups: the firmware storing/sending its per-device
key, and keying the leaderboard "verified" tier off `deviceAuthed`.

How a device we did **not** flash gets a key — Turnstile-in-the-browser, why open
enrollment is rejected, and the open question that gates the abuse controls — is
[docs/device-enrollment.md](docs/device-enrollment.md) (planned, not built; DIY buyers
get an emailed key until then).

#### Calibration note: the new-types-per-day threshold (2026-08-08)

The old figure here was **< ~40 new types/day**, and it was an assumption. A bench board
in Bend, OR — an honest device under a GA-heavy sky — logged **113, 38, 30, 39** new
types on its first four days. Days 2–4 sit *on* the old threshold. It would have flagged
a real customer, which is the worst way for a plausibility check to be wrong: it burns
trust on the honest and teaches you to ignore it.

Raised to **150/day**, and the headroom is doing real work, because the measurement is
**censored**. `MAX_TYPES` is 220, and the board hit it on day 4 — so 39/day is a lower
bound on the rate at the moment observation stopped, not a peak. The curve was also not
decaying (38 → 30 → 39), so there is no basis for extrapolating a ceiling from it.

An uncensored rate is now being collected: the `[logbook] REFUSED since boot:` counter
(PR #198) records first-time entries the caps turn away, which is the growth a saturated
store otherwise hides. **Revisit this number once that data exists** — it is measured,
but measured against one location and one truncated week.

### Sequencing

Logbook v2 ✅ (shipped 2026-07-16) unblocked this. Build order:
1. **Worker:** submit endpoint + KV store + public board page + season cron (staging).
2. **Firmware:** config opt-in + name field; hourly submit from the enrich/ntfy task
   (one queued POST, off-loop like every alert).
3. **Season mechanics + rarity weights** — server-side only, no firmware change.
4. **Badges + profiles** — server-side only.
5. Later: per-device keys, real accounts for multi-device merge.

### v1 SHIPPED 2026-07-17

Everything above except the scoring-radius normalization landed as one feature:
- **Worker** ([proxy/src/leaderboard.ts](proxy/src/leaderboard.ts)): `POST /v1/leaderboard`
  (authed, monotonic-merge + growth caps + name-claim + first-type + streak), rarity/
  season/badge scoring, and public `GET /leaderboard` (HTML), `/leaderboard.json`,
  `/leaderboard/<id>` (profile) — all unauthed like `/credits`. Board is aggregated
  lazily on read with a 5-min KV cache (the cron-built board is the scale path, noted
  in-file; D1 when the fleet outgrows a per-row scan). 11 tests.
- **Firmware:** `DeviceIdentity::LeaderboardId()` (salted SHA-256 of the full MAC, 16
  hex); config-page opt-in (`lb-enabled`) + `lb-name`; hourly `EnrichKind::Leaderboard`
  POST off the loop (built from the Logbook tallies + type list); a gold LEADERBOARD
  block on the Stats screen showing rank/points/season, clock-guarded like the others.
- **README** privacy section documents exactly what an opted-in device sends.
- **PARTIAL — scoring-radius fairness:** the board is now transparent about each
  device's play radius (shown on the profile, flagged when under the 30 mi standard),
  and `STANDARD_RADIUS_KM` is defined server-side. The **automatic background
  normalization** (a 30 mi scoring poll) remains **DEFERRED**, though the reason has
  softened: it widens the *tracked* aircraft set, which was the render/heap path under
  investigation for the [[s3-128-overnight-slowdown]] (closed 2026-07-21, not reproduced).
  With that bug closed on absence of evidence rather than a root cause, widening the
  tracked set is still the one change most likely to resurrect it — and we now know
  first-hand that tracked-set size has real cost (raising the cloud blips limit to 60
  starved the TLS heap; PR #100 settled on 40). The safe implementation is a server-side
  count from already-served traffic (verified devices) rather than a wider device-side ring.
  Rarity weighting already does most of the radius equalizing meanwhile.

---

## Concept: corroborated user identifications for unresolved aircraft

**Design, not work. Nothing to build now** — see the trigger at the bottom.

**The problem it solves.** US military airframes rotate their Mode-S hex codes. The
milspotting community re-identifies them within days; Mictronics ships a packaged export
that lags. So a rotated hex resolves to the military floor's generic operator and nothing
else. `AE67CC` (a P-8 transiting the WA/OR corridor at FL290) and `AE6861` are both this.
ADSBX shows a **type** for `AE67CC` and **no registration**, which is exactly consistent
with the mechanism: community-curated type, tail not yet matched.
[Per-hex overrides](docs/per-hex-overrides.md) fix these one at a time. This is the
version that scales.

**The idea.** Our customers are the people who would know. Someone watching a P-8 transit
weekly, with a receiver and a Blipscope, is often the same person identifying it on a
forum. Let them submit an identification for an unresolved aircraft, and publish it once
**independent** users corroborate it.

### Design constraints, in order of how load-bearing they are

1. **Corroboration, never single-source.** One person's guess propagating to the fleet is
   worse than a blank field. The military floor already asserts only what the allocation
   proves; this must not weaken that.
2. **Independence matters more than count.** N users agreeing is weak evidence if they all
   read the same forum post — and for military hexes that is likely, because the community
   converges fast. Geographic spread is a usable proxy: three submissions from three metros
   beats three from one. **Decide what N is, and what independence means, before building
   anything.**
3. **A contested hex falls back; it does not pick a winner.** Two users, two different
   types → publish neither. Silhouette plus the floor's generic operator. Same rule as
   everything else here: say less rather than say wrong.
4. **Type only, never registration.** Same rule as the floor. A wrong tail number on a
   military aircraft is a worse failure than an empty field — and ADSBX does not have one
   either, so we would be inventing rather than lagging.
5. **No accounts.** We deliberately have none. Anonymous submission makes independence
   harder to establish. The device id already exists and already backs the leaderboard, so
   it is the obvious identity — but that is a **decision to make, not to inherit**, and it
   ties an identification to a board rather than a person.
6. **Review before publish while the fleet is small.** At 50 devices each one can be
   approved by hand; that does not scale to 600. **Corroboration is what replaces the
   reviewer**, so the threshold has to be trustworthy *before* the review step goes away.

### Do the cheap version first

A "report a wrong or missing aircraft" link on the config page, opening a pre-filled
email: hex, the type as we resolved it, device id, nothing else. **Zero infrastructure,
and it measures the volume before we build for it.** If the pilot surfaces five hexes,
corroboration is a platform for a spreadsheet. If it surfaces fifty, the week is obviously
worth spending.

### Trigger to build

Per-hex overrides becoming a **recurring chore rather than an occasional one**, or the
pilot surfacing enough unresolved recurring hexes that manual curation stops being an
afternoon.

### Related and unbuilt

- The **curated overlay**, deferred 2026-08-16 — military was 0.9% of gap hexes, all
  inside one contiguous allocation window. See also `E3` above.
- **Per-hex overrides**, whose current state is the argument for this concept rather than
  a list of pending work. As of 2026-08-17 the five that were queued resolve into three
  different categories, and only one of them was actionable:

  | hex | days seen | state |
  |---|---|---|
  | `ae67cc` | 3 | **overridden** — `{"t":"P8"}`, from ADSBX community curation |
  | `a71203` | **1** | **dropped** — one-off; all 15 lookups inside a 16-minute window |
  | `a815d6` | 6 | **blocked on data** |
  | `a590d8` | 5 | **blocked on data** |
  | `a7419f` | 3 | **blocked on data** |

  The three blocked ones are the sharpest argument for this concept. They **recur**, and
  they are US **civil** addresses — so the military floor gives them nothing at all and
  they render genuinely blank, not merely degraded. `adsbdb` returns `unknown aircraft`
  for all three. There is no database left to ask and no override that can honestly be
  written. **A customer who watches one of them fly over every week is the only remaining
  source of that identification.**
