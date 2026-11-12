# v20: weather and maps, on by default (a plan, drafted 2026-11-12)

**The plan as of 2026-11-12 17:40Z, written by the reviewer from every session's list.** The owner made v20 the
main track on 2026-11-12 (15:29Z), effective from v19's promote (15:38:46Z). Built on `docs/map-release-plan.md`
(2026-11-07). Nothing here is ruled except where it quotes the owner. The digest carries v20's done / next /
blocked / slip from here on; this file is updated when the date moves.

## Scope (the owner, 2026-11-12 15:29Z)

- **Weather and maps together, both on by default, town names on.**
- **The head-read fix** (#660, the same patch as v19's #651).
- **#630** (W2: X-Blip-Model is the OTA identity) and **#634** (map fetch, step A).
- **Everything else on main since C** (`3a322758eadc`): 26 commits as of `c73aee2f1a5b`. The code among them:
  - #630 W2, #634 step A, #650 step B's refusal (CB10), the FillRing fix (`f3fee349`), #660 the head read;
  - proxy/Worker: #643 /healthz for every weather product, #646, #649 W5 (Weatherscope NWS feeds), #639's builder
    variant, #633 ingest deploy claim, #653 merge-by-rule.
  The rest is docs.

## The cut's first gate (the owner)

#630 and #634 change OTA code that v19 lacks, so **v20's O11 pre-flight runs on the real v20 candidate**:
- a v19 board must accept v20;
- v20 must install a later image;
- anything touching board identity or the record format gets its own ruling first (`RELEASING.md`).

## The frame-rate gate (the owner, 2026-11-12)

"Drawing speed is a top priority for me. Make it a measured requirement." In his words:
- "Measure v19's frame times on E2:08 first, as the baseline."
- "With map and weather both on, v20's radar is held to a frozen bar, measured on every candidate. My starting
  point: p95 frame time 50 ms or less (20 fps or better)."
- "Bring me the baseline and the proposed bar before the map code is graded."

**The bar, frozen by the owner (2026-11-12 19:55Z), before the baseline was measured:** "I watched 10, 20, 24, 30
and 60 fps side by side. 20 fps is my minimum (p95 at or under 50 ms). 24 is the target. Nothing above 24 is
chased." "The bar is read with 40 aircraft on screen, map and weather on. 20 and 80 are reported beside it." The
firmware tracks at most 60 aircraft (`MAX_AIRCRAFT`), so 80 cannot be shown, and the high point is reported at 60
unless the owner rules otherwise.

**v20 as planned misses the bar.** Early readings put v19 alone at 74.5 ms p95 with 41 aircraft, and weather adds
about 13 ms. The owner chooses now, not at the pre-flight: "By tomorrow, p4 sizes two things: (a) reaching 20 fps at
40 aircraft with map and weather on; (b) v20 no slower than v19 at the same traffic. Then I choose: hold v20 for (a),
or ship (b) on time and make 20 fps v21's bar. The speed plan starts now, since both need it."

How it runs:
1. **The baseline** (map-client, which holds the frame-time instrument; one window on E2:08). It is registered before
   the window: the image and its identity (v19's behaviour plus the instrument, with the instrument's own cost
   measured); the scenes (the radar face at the bench's home and at a dense airport; a fixed zoom ladder that the
   radar's 10-minute idle zoom return cannot undo; the aircraft count printed with each sample); the frame
   definition (every frame, nearest-rank p50/p95/p99/max; map-rebuild frames counted separately and included in the
   gate's p95); sample counts; predictions; a control first (a planted per-frame delay of known size must move p95 by
   that size); and the escape hatch (anything outside the set stops).
2. **The bar** is the owner's, above: p95 at or under 50 ms at 40 aircraft drawn, map and weather on; 20 and the high
   point reported beside it. If he chooses (b), v20's bar is instead "no slower than v19 at the same traffic", and
   20 fps becomes v21's bar.
3. **Every candidate.** O11's pre-flight (blipscope-5c) re-runs the baseline's procedure unchanged, with map and
   weather on, and reads it against the frozen bar before the cut. A candidate over the bar does not ship: it goes
   to the owner with its numbers.
4. **The standing rule (the owner):** "every change to drawing code reports its frame time on the bench, before and
   after. A slowdown beyond the margin frozen in its registration stops it."

## Work remaining, by session

*(collected 2026-11-12; sizes are elapsed days, including review; each assumes a first reading as predicted)*

| owner | item | size | waits on |
|---|---|---|---|
| map-server | 1. Part 2 to its end (the sea path's evidence; KB4 under the owner's ruling, KB5, KB7, P2-M, the pictures) | 1-2 d | (B')'s registration (the owner ruled (B') 2026-11-12 16:57Z); his picture review |
| map-server | 2. #652 merge (docs) | 0.5 d | part 2's last read |
| map-server | 3. The Q-W4 code PR (dense-area code + the sea path in proxy/src; PARITY for the ingest clip) | 3-5 d | part 2 accepted; review; CI. **Longest server pole** |
| map-server | 4. The sea store on R2 (12,021 puts, ~1.76 GB; write, verify, repair, verify, seal) | 1 d | #3 merged; the month's Class A re-read |
| map-server | 5. Staging reading of #3 + #4 (16 points) | 1 d | a staging slot |
| map-server | 6. Deploy-inputs code (#562; not a release gate) | 1 d | nothing: Q-DB1-3 were ruled 2026-11-06 (the owner, as reviewer); `map-release-plan.md`'s "not yet given" was wrong |
| map-server | 7. D4: the map on in production | 1-2 d | #5 as predicted; the owner's announcement of production resources and his OK |
| map-client | 1. Step B's + C's bench: one no-hands window on E2:08 (~2 h 45 of board time), then the reads and records | 1 d | the window opened (requested 2026-11-12) |
| map-client | 2. CBF2, the owner's eyes on glass: the label's look (Q-MB5) and names under rain (Q-MB7) | 0.5 d once held | the owner's window |
| map-client | 3. The merges: #659 (step B), then step C rebased, re-read, its PR and CI | 1 d | row 1 (not row 2) |
| map-client | 4. D, the setting: the map on by default, town names on by default with their own switch; DO1-DO4, DOC1-DOC2, the config page, the off checks; a row asserting a board saved on v19 comes up with both on | 2 d | row 3; its registration ruled. Its merge waits on row 2 |
| map-client | 5. E, the bench and soak: DOC3, S1's two airports, memory, a soak on a fetched map | 3-4 d | **a production /basemap (map-server's D4) for the final pass**; row 4; E2:08 windows |
| map-client | 6a. The pre-ship per-function accounting with #656's comparator | 0.5 d | #656 |
| map-client | 6b. The pre-ship image-level text check (a third instrument, shown on controls first) | 1-1.5 d | nothing to start; its final read waits on the candidate |
| blipscope-5c | The proxy bill (proposal, Logs, a KV cache, KV writes, and /wx's F1: `latest` cached per colo) | 5-6 d | read-only to start; deploys under the claim; plan or billing changes are the owner's. **Before the cut** |
| blipscope-5c | The fresh-boot automation (commissioned) | ~3.5 d | the owner's three choices (the Wi-Fi password file, the claim tap, the config-page reset) |
| blipscope-5c | O11 pre-flight on the real v20 candidate | ~2 d after the candidate | the candidate; #630's identity confirmed on it |
| blipscope-5c | The cut (FW_VERSION 20, CHANGES-customer v20, v19's steps 2-7 shape) | 4-5 d after the content freeze | the content freeze; the proxy bill; O11 |
| blipscope-5c | Step 8 (v19's week after), daily to 11-19 | 7 d, ~15 min/day | nothing |
| blipscope-5c | **The card bug (the owner, 2026-11-12 17:26Z: v20 content):** a no-photo card with Follow offered drops route, type and operator on the 1.28"; fixed with a test that fails on today's code first, confirmed on a real board | ~2 d (#670; every no-photo or photo-loading card, since v9) | its before/after look to the owner before merge; a bench slot on E2:08 |
| blipscope-5c | Pro 2.1 bring-up (independent; build-only) | ~1.5 d | the owner's window |
| uk-data | 1. #641, stage 3's record with S3-7 | 0.5 d | approved at eddafad3; merges once main's CI re-run reads green |
| uk-data | 2. The OPERA outage watch (daily, read-only); on return, re-read the ten and the 14 opuk points | 0.5 d on return | OPERA (upstream since 2026-11-11 ~20:10Z) |
| uk-data | 3. If OPERA isn't back by 2026-11-14: assess EUMETNET's ORD API as a second way in (0.5 d); build and redeploy the ten (2-3 d) | 0.5 d + 2-3 d | the date; a ruling |
| uk-data | 4. Route the nine continental products (registration 0.5 d; nine deploys 3-5 d, or ~1 d if one deploy may carry all nine) | 1-5.5 d, NOT on v20's path | OPERA publishing; proxy slots. The owner, 2026-11-12: continental Europe is not on v20's path (adding it later needs no firmware, confirmed both sides) |
| uk-data | 5-6. The 7-day re-reads (~11-13 cost and MD-6; ~11-18 Class A), with the OPERA rows re-registered to 7 days after its return | 0.5 + 0.25 d | the dates |
| uk-data | 7. December's Logs price (bytes per event, read-only) | 0.5 d | nothing |
| uk-data | 8. Optional levers: cref map-from-R2 (2-3 d, ~$0.20/mo); /wx edge caching F1-F3 (2-3 d, scales with the fleet) | 2-3 d each | rulings; slots |
| weatherscope | Nothing on v20's path (what the weather layer uses from Weatherscope, WxPalette.h, WxBasemap.h's FillRing and WxCrop.h, is on main; the layer uses cref, opuk and mrms, not shsr) | 0 | nothing |
| weatherscope | **#656, the layout-invariant comparator (commissioned 2026-11-12). ON v20's CRITICAL PATH:** the owner ruled (2026-11-12 03:39Z) that the per-function accounting is redone with #656's comparator before maps are switched on in any customer image, and v20 does exactly that | ~1 working day (#669 open; step 3 running) | its CI; two conditions (a guard control, a licence line) |
| p4 | #648 (build-only P4 envs): HELD until after v20's cut (the owner, 2026-11-12 16:57Z) | 0 for v20 | |
| p4 | The big-screen build plan (`docs/big-screens.md`): starts after v20's content freeze, F + 19-21 days | 0 for v20 | F |

## Risks known so far

- **New York's map** rests on KB4 (B'), the owner's ruling: repair a crossing island, not drop it, with a frozen
  limit; Jones Beach Island is the case. The added bytes at each NY point sit near the 32 KB cap.
- **OPERA**, Europe's radar, has been down upstream since 2026-11-11 ~20:10Z. Continental Europe is off v20's path,
  but note that v20's UK weather (opuk) is ALSO an OPERA product. While OPERA is down, a UK board shows no radar
  (503, so nothing drawn) until it returns. If OPERA is still down at the cut, v20 ships with UK weather dark. Continental boards get "No weather
  coverage" until their products are routed.
- **The proxy bill** must land before weather goes fleet-wide (at 600 boards: Logs ~$166, KV reads ~$115, KV
  writes ~$54 a month). Log sampling must not drop the breaker/upstream_skip instruments.
- **The frame-rate gate** (above): v20 as planned misses the owner's bar. Early readings put v19 alone at 74.5 ms
  p95 with 41 aircraft. Weather adds about 13 ms. Map rebuilds at KSFO took 177-198 ms each. The owner's choice
  between (a) and (b) decides whether v20 holds for speed work.
- **The ingest's clipper (PARITY):** RESOLVED 2026-11-12: the hand-written rect clip and the full TS ingest equal
  GEOS and the dry run exactly; no dependency.

## Commissioned right after promote (the owner, item 4)

- **#656**, the layout-invariant image comparator: weatherscope. It feeds `RELEASING.md`'s fix-only skip and the
  map's pre-ship gate (per-function accounting before maps go into a customer image).
- **The fresh-boot automation plan**: blipscope-5c. So v20's cut doesn't need the owner's hands.

## The speed workstream (the owner, 2026-11-12): it starts now

This is shared by all boards, with targets measured on glass. The owner (19:55Z): 20 fps is the minimum, and "24 is
the target. Nothing above 24 is chased." So the targets are 24 fps on the 1.28" and on the P4, and at least 20
(ideally 24) on the 2.1". The number for the Android screens waits until the owner has looked at the 23.6-inch
screen itself. The methods come first: "static layers drawn once and reused, touch read on its own schedule, and
drawing overlapped with the panel push." "Its plan comes to me with sizes before any code." The plan is p4's, and it
takes in the 2.1's v21 steps (blipscope-5c), the static-layer method (weatherscope) and the frame-time instrument
(map-client). It was planned for after v20's freeze. The owner moved it to now, because both (a) and (b) need it.

## Brought to the owner early (item 5)

- **Weather's pricing**, free or part of a paid plan: about $0.05-0.08 per board per month once past the free
  tiers (`docs/wx-cost.md`); today's fleet ~$0.79/mo; the $2 allowance runs out at 50-84 boards; 600 boards
  ~$29-46/mo.
- **The SeamlessHSR price**: about $1.00-1.40 a month, flat in board count. The owner said yes (2026-11-12 17:12Z),
  and raised the Cloudflare overage allowance to $10 a month. R2 bills in whole millions, rounded up.
- **blipscope-5c's proxy bill work** (Logs, a KV cache, KV writes, /wx's `latest` cached): 5-6 days, before F.
- **The account-wide budget** (#671, uk-data): every cost estimate adds its line and states the account total; the
  digest flags any allowance over 80%.

## Critical path, owners, date

**Target: v20 promoted by 2026-11-30.** The likely range is November 25-30. The 11-07 estimate was November
21-29, so the late end slips by about a day, and the early end by four. Days are elapsed days from 2026-11-12.

**The critical path is the map's server side, then the client's final bench, then the cut:**

| # | step | owner | days | lands |
|---|---|---|---|---|
| 1 | Part 2 of the sea path to its end (KB4 under (B'), KB5, KB7, P2-M, the owner's picture review) | map-server | 1-2 | Nov 13-14 |
| 2 | #652 merged | map-server | 0.5 | Nov 14 |
| 3 | The Q-W4 code PR (dense-area code + the sea path; PARITY already passes) | map-server | 3-5 | Nov 17-19 |
| 4 | The sea store on R2 | map-server | 1 | Nov 18-20 |
| 5 | The staging reading of 3 + 4 | map-server | 1 | Nov 19-21 |
| 6 | D4, the map on in production (the owner's announcement and OK) | map-server | 1-2 | Nov 20-23 |
| 7 | The client's final bench pass and soak on a production map (E), then the pre-ship accounting (6a, #656) | map-client | 1-2 after D4 | Nov 21-25 |
| F | **Content freeze**: the map, weather on by default, the card fix, the proxy bill | everyone | | **Nov 21-25** |
| 8 | O11 pre-flight on the real v20 candidate, including the frame-rate gate, then the cut (fresh-boot, tag, prerelease, bench flash, verify, promote) | blipscope-5c | 4-5 after F | **Nov 25-30** |

**Off the critical path but due before F** (each fits inside steps 1-7):
- map-client: the B + C merges, D (the setting), 6b (the image-level text check);
- weatherscope: #656 (~1 day, Nov 13);
- blipscope-5c: the proxy bill (5-6 days, by ~Nov 18), the card bug #670 (~2 days), the fresh-boot automation
  (~3.5 days);
- uk-data: the account-budget file (#671).

**What would move the date:**
- the sea path (KB4 (B') and P2-M's memory at New York against the builder's limit). An off reading costs about
  half a day; a second one on a row goes to the owner;
- D4's production window and the owner's availability for its OKs;
- the client's soak on a production map: it can't start before D4;
- the frame-rate gate: if the owner chooses (a), v20 holds for the speed work; under (b) a candidate slower than
  v19 at the same traffic goes to him, and a fix or his ruling comes before the cut.
Not on the path: continental Europe (the owner, 2026-11-12: it goes live by deploys when OPERA returns); the Pro
2.1 (aimed at v21); the big-screen build (it starts after F).

## Carried gates (already ruled)

- **Before maps go into any customer image:** per-function accounting with #656's comparator (the owner,
  2026-11-12 03:39Z), and step C's image-level text check, whose tool passes its own controls first (the owner,
  2026-11-12 15:07Z).
- **KB4 (the sea path):** (A), (B') repair with a frozen limit then stop, repairs counted on the build record, the
  added bytes predicted at each NY point, and (C) shown failing first (the owner, 2026-11-12 16:57Z; his 16:24Z
  island-size stop is withdrawn).
- **The 1.28" look:** any change to it goes to the owner separately (2026-11-12 17:26Z). #670's fix brings him
  before/after pictures.
