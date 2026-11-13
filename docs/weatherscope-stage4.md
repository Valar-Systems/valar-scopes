# Weatherscope Stage 4: live data, and the road to a product (plan, drafted 2026-11-07)

**This is a draft for review. Nothing in it is ruled, and no code starts until it is.** It covers the live data path, the first-boot
legend, the cost, and everything between today and a Weatherscope Daniel can sell. 3.5c closed with A12.171: the screens work on
stored data, on the glass and under a finger, and no board has yet shown live data.

## The short version

**A Weatherscope Daniel can sell is about 3 to 6 weeks away, November 29 - December 20.**

| | days from today | rough date |
|---|---|---|
| **Stage 4's own work,** one session, one step after another | 18-29 | November 25 - December 6 |
| **v19, the identity release,** from #606's plan | 14-36 | November 21 - December 13 |
| **After both:** slug, first image, activation, how a sold board gets it, the shop | 4-7 more | **November 29 - December 20** |

**Three things are most likely to move that date:**
1. **v19.** Weatherscope can't publish before it, because a device on v19 or later refuses any image built earlier. v19 itself waits on
   the map client and the ocean fix.
2. **The map for any location.** Weatherscope draws a stored Bend package today. Selling it anywhere needs `/basemap` in production, and
   the map server still has an open water finding (a Boston or Chicago package can lose its water) and no dense-area answer for New York.
3. **Whether a Worker can reach `api.weather.gov` reliably.** Nothing has measured that. The captures so far ran from this PC.

**Cost:** a Weatherscope board costs about **6 to 9 cents a month** once the free tiers are gone, or **$2.30-$3.40 over three years.** On
top of today's 17 boards, Daniel's $2 runs out at **34-62 Weatherscope boards** (section 3).

**The decision that matters most is Q113, for Daniel:** `/wx` doesn't serve the radar product every Weatherscope screen was built on, and
adding it crosses R2's free Class A tier.

## What exists today

| piece | state |
|---|---|
| **The screens** | Map, forecast, conditions, alerts, legend, on stored data. Merged and on `6E:64` (A12.171) |
| **The device's feeds** | Every feed is stored. `src/weatherscope/WxFeedFactory.cpp` is the one file that chooses them (D1), and the image holds no NOAA or NWS host (D2) |
| **`/wx`** | Live for Blipscope: `GET /api/v1/blipscope/wx/<product>?lat=&lon=&r=`, one WXC1 crop of the **newest frame only.** Products: `cref` and `opuk` |
| **Forecast, conditions, alerts in the Worker** | **Nothing.** No route reads `api.weather.gov`. Only local tools do |
| **`/basemap`** | Built, not in production. No firmware requests it yet |
| **Enrolment** | Device side and Worker exist. The enrol popup and `/enroll-key` are compiled for the radar only |
| **Activation** | The allowlist exists (`act:<id>`), edition-blind. Production is in watch mode by Daniel's choice |
| **OTA identity** | On main, not released. v19 is the release that carries it, and O11's predictions are frozen (A12.172) |
| **A release path** | `weatherscope-s3-128` is `build-only`, no slug, publishes nothing |

## 1. The live data path

### 1.1 The model header, agreed with blipscope-5c (section 7's contract)

**Today:** `X-Blip-Model` is `variant::SLUG` (`src/CloudFeed.cpp`), so a Weatherscope s3-128 would read as a Blipscope s3-128 in the Worker's
per-model config, its metrics and the `fw:` rows.

**Proposed: the header is the OTA identity, from the one function that already builds it.** `ota::Request(variant::SLUG, FW_OTA_PREFIX)`
gives `weatherscope-s3-128` here and plain `s3-128` on the radar, because the radar's prefix is empty.
- **The radar's header doesn't change by a byte,** so no Blipscope device or config moves.
- **The URL, the identity check and the model can't disagree,** since all three come from one request.

**The Worker needs no change to accept it.** It reads the header as free text: an unknown model gets `BASE` from `handleConfig`, and metrics
and `fw:` rows carry the string as it comes.

**What blipscope-5c is asked to agree:**

| item | why it's theirs |
|---|---|
| The value, `weatherscope-s3-128` | the fleet's tools read it |
| Whether `MODEL_DEFAULTS` gets an entry | Weatherscope reads no `/blips`, so I propose none |
| What `reconcile-fleet.py` does with a second model on one board | it's their tool, and a board count must not double |
| The shared-code change: one function for the model string | it touches `CloudFeed.cpp`, so it goes through them |

### 1.2 The radar

**The query is `/wx`'s, unchanged:** `lat` and `lon` at 2 dp (`wireloc::Center`), `r` in whole km, polled every 60 s with an ETag, through
the proxy's device auth. One crop at r = 97 km covers both zooms, since the 15 mi view sits inside the 60 mi one, as the stored crops do.

**Q113, the product (Daniel's, with a cost).** Every screen was built and graded on SeamlessHSR, and `/wx` serves `cref`, a composite.

| | (a) add `shsr` (recommended) | (b) use `cref` |
|---|---|---|
| **The look** | what Daniel has seen and accepted, nearest the ground | a composite, which can show rain aloft that doesn't reach the ground. Crops run larger: 4,033 B against 3,040 B at the same KDFW minute |
| **Server work** | a row in `products.ts` and its code mapping, 1-2 days | none |
| **Cost, flat** | **$0.47-$0.57 a month,** whatever the board count | $0 |
| **Why it's a question** | R2 Class A goes from 929,006 to about 1.06 M, past the free 1 M. `docs/wx-server.md` says that crossing goes to Daniel first | the screens' K rows would be re-graded on a new fixture, and Daniel would need to look again |

**Q115, the loop.** A tap plays the past hour's 15 frames, and `/wx` has no history.
- **(a) A ring on the device (recommended).** Keep every other new frame in PSRAM: 15 crops at up to 32 KB is 480 KB at most. No server change and no cost. The loop is short for the first hour after a boot, and that's stated by a rule: under 3 frames, a tap does nothing.
- **(b) A history query on the server.** The loop is full from the first boot, at the cost of 15 more 200s on each tap, and a new route.

### 1.3 Forecast, conditions and alerts: the Worker's feeds (the device never talks to NWS)

**The sources are as ruled for the swipe screens:** `api.weather.gov` `/points`, `/gridpoints` forecast and hourly, `/stations` observations, and
`/alerts?point=`. All are free and keyless. They ask for a User-Agent with a contact, which is `support@valarsystems.com`, as the capture used.

**Proposed routes, each with an ETag and a 304:**

| route | device polls | Worker holds it | keyed by |
|---|---|---|---|
| `GET /api/v1/weatherscope/alerts?lat=&lon=` | every 2 min | 60 s | the quantised point |
| `GET /api/v1/weatherscope/conditions?lat=&lon=` | every 10 min | 5 min | the station |
| `GET /api/v1/weatherscope/forecast?lat=&lon=` | every 60 min | 30 min | the forecast grid |

- **Three routes, not one bundle.** They change at different rates, and a failure in one must not hide the other two.
- **Keyed by NWS's own grid and station, never by device,** so boards near each other share one upstream call, and no id reaches NWS.
- **Held in the Cache API,** which costs nothing. It's per colo, so each colo fetches once. `/points` is held 30 days, since a location's grid rarely moves.
- **The body is the shape `wx::Forecast`, `wx::Observation` and `wx::Alert` already read,** so the device's parsers stay small. `dallas-day.json` holds all three in about 8 KB, under `GetBytes`' 32 KB.
- **Route and Worker (Q116):** these live in the proxy Worker, for its device auth. `docs/web-url-convention.md` puts an edition's APIs under `/api/v1/{edition}/`, so I propose that prefix here, with `/wx` reached through an alias, for the same reason.

**What this has to get right, each a rule this repo has already paid for:**

| rule | here |
|---|---|
| **An empty result must not mean two things** | NWS's `features: []` means no alerts. Only a 200 that parses may say so. Anything else is `unavailable`, never an empty list |
| **A port of Worker math pins its rounding** | `trim_nws.py`'s conversions (°C to °F, km/h to mph) move to TypeScript. `Math.round` and Python's `round` disagree on halves, so the rounding is pinned by a cross-implementation test |
| **Health must be able to go red** | `/healthz` carries each NWS source, with its breaker, and `evt:"upstream_skip"` says why a call didn't happen |
| **An instrument needs a reader** | the routes get their own labels in `KNOWN_ROUTES`, as #590 gave `/wx`, or they vanish into `/other` |
| **Raw captures carry coordinates** | fixtures are synthetic or trimmed. The raw NWS documents stay outside every repo |

**Q114, coverage.** NWS covers the US, and `/wx`'s `shsr` would cover the lower 48. `/wx/cref` also answers for the UK, but there's no forecast
source there. **I recommend selling in the US only at first,** with a location outside coverage told so at setup rather than left on empty
screens. A forecast source abroad is a project of its own.

### 1.4 Staleness: what each feed shows when it stops

**An absence must never read as good news.** This is the one row in the plan that could mislead someone about their safety.

| feed | fresh for | past that, the device shows | it must never show |
|---|---|---|---|
| **Radar** | 30 min, as built | `RADAR STALE`, as built | an old frame as current |
| **Conditions** | 90 min | the observation's time in orange, as built (A9.1 B) | a temperature with no time on it |
| **Forecast** | 6 h, then labelled with its age; at 24 h, `NO FORECAST` | the age, and `FirstCurrent` already skips an ended period (Q110) | a period that has ended |
| **Alerts** | 10 min | **`ALERTS UNAVAILABLE`** | **`NO ACTIVE ALERTS`, or a blank list** |

**The alerts row is structural, not remembered.** A test asserts that with the feed stale, the drawn text can't contain the all-clear string,
with a control that the page still says something. A warning already on the map stays until its own expiry, even if the feed stops.

The Worker serves last-good with its age when NWS fails, and the device decides from that age, so one rule covers both a dead Worker and a
dead upstream.

### 1.5 Enrolment, activation, and a board with no key

**Weatherscope can't run without a key, which Blipscope can.** A Blipscope has its owner's own receiver to fall back on. A Weatherscope with
no device key shows nothing live, so setup has four states, and each needs a screen: no Wi-Fi, no location, not enrolled, live.

- **The change:** widen the two radar-only guards around the enrol popup and `/enroll-key` (section 1.3 of Stage 3), checked by S1 so that no other edition's image moves.
- **Activation needs nothing new.** `act:<id>` is keyed by device id and blind to edition, so a Weatherscope unit goes on the batch list like any other.
- **Refusal has to be readable:** a board refused while activation enforces must say so on the glass, because it can't fall back to anything.

### 1.6 Telemetry and disclosure

- **The OTA report (`X-Blip-OTA-Mem`) and the boot reason ride Weatherscope's requests,** closing A3.2's blind spot for this edition: today a refused update on a Weatherscope would show only on serial.
- **Usage counts:** the same digits-and-commas rule, if any are added. Adding one changes a published statement, so `README.md` and the support page change in the same commit.
- **The location goes to the Worker at 2 dp,** as a Blipscope's does. The README's privacy section has to say so for this edition.

## 2. The first-boot legend

**Parked by ruling until the screens existed. They exist now.** Daniel's question was whether the legend shows once at first boot.

**Q117, recommended yes:** show it once, the first time the map has live data, until a tap or 20 s. It's the same legend page swipe reaches, so
no new screen is drawn.

| row | predicted, before its code |
|---|---|
| shown | once, on the first boot with a key, a location and a frame |
| dismissed | by a tap or at 20 s, and the flag is written then, not when it appears, so a power cut re-shows it |
| reboot | not shown again |
| factory reset | shown again |
| during a warning | **never:** a warning takes the screen, and the legend waits for the next boot |

The last row matters most: a first-boot courtesy must not cover a tornado warning.

## 3. The cost, against Daniel's $2

In `docs/wx-cost.md`'s form, with its prices and its base. The base reproduces that doc's $0.79 before anything is added.

**A Weatherscope board, a month:**

| | requests | CPU | R2 Class B | billed |
|---|---|---|---|---|
| **`/wx`, every 60 s** (measured there; a 200 at 21-51 ms) | 43,200 | 0.50-1.15 M ms | 86,400-129,600 | $0.05-$0.08 |
| **Alerts, every 2 min** | 21,600 | | 0 | |
| **Conditions, every 10 min** | 4,320 | | 0 | |
| **Forecast, every 60 min** | 720 | | 0 | |
| **The three feeds together** (estimated) | **26,640** | 0.06-0.13 M ms | 0 | **about $0.01** |
| **A board** | **69,840** | 0.56-1.28 M ms | 86,400-129,600 | **$0.063-$0.093** |

- **The feeds' CPU is an estimate:** 2.27 ms a request, `/wx`'s measured 304, up to 5 ms. W3 replaces it with a reading.
- **Three years is $2.28-$3.36, and five is $3.80-$5.60,** with no subscription behind it. Those are marginal figures, past the free tiers.
- **A Weatherscope is cheaper than a weather Blipscope:** it makes none of the radar's 107,000 other requests a month.
- **The map adds next to nothing:** a package is fetched once per location. #587 put the wave at $0.08 at 600 boards, once.
- **Not counted, as in `docs/wx-cost.md`:** Analytics Engine writes one point per request. Check before quoting a figure at 600.

**The whole account, a month, Weatherscope boards on top of today's 17:**

| | requests | CPU | R2 Class B | total | with F1 |
|---|---|---|---|---|---|
| **Today** | 2.5 M | 69.6 M ms | 6.1-6.7 M | **$0.79** | |
| **+10** | 3.2 M | 75-82 M ms | 7.0-8.0 M | **$0.90-$1.05** | $0.90-$1.05 |
| **+50** | 6.0 M | 98-134 M ms | 10.4-13.2 M | **$1.51-$3.22** | $1.36-$2.44 |
| **+200** | 16.5 M | 182-326 M ms | 23.4-32.6 M | **$9.80-$16.01** | $6.69-$12.90 |
| **+600** | 44.4 M | 407-840 M ms | 57.9-84.5 M | **$35.13-$53.32** | $25.80-$43.99 |

- **Q113 (a) adds $0.47-$0.57 a month to every line,** so $1.26-$1.36 today.
- **The $2 runs out at 34-62 Weatherscope boards,** before that add-on. Radar is most of it: the three new feeds are 10-17 % of a board's cost.
- **Blipscope boards added at the same time count too.** These lines hold the rest of the fleet flat, and they shouldn't be read as a forecast of it.
- **Daniel's ~02:36Z rule already covers this:** the count is read at the cut, and `/wx` gets cheaper before the fleet passes about 50 boards. F1 is in blipscope-5c's hands, and Weatherscope's launch makes it matter sooner.

## 4. Everything between today and a Weatherscope Daniel can sell, in order

**Stage 4's own work.** "Days" are elapsed days, including review, as in #606.

| step | what it is | rough estimate | waits on |
|---|---|---|---|
| **W1. This plan reviewed** | Q113-Q120 ruled | 1 day | the reviewer; Daniel on Q113, Q114, Q117, Q118 |
| **W2. The model header agreed** | section 1.1 | under 1 day | blipscope-5c |
| **W3. Can a Worker reach NWS?** | staging, read-only, predictions first: reach, latency, and the feeds' real CPU | under 1 day | a staging slot |
| **W4. The radar product** | `shsr`'s row and its ingest, if Q113 is (a) | 1-2 days | Daniel's OK on the Class A crossing; a deploy slot |
| **W5. The Worker's feeds** | section 1.3's routes, breakers, `/healthz`, tests and sabotages | 3-4 days | W2, W3 |
| **W6. Feeds to staging, then production** | predictions first, then the lock | 1-2 days | the deployer lock |
| **W7. Radar live, and enrolment** | `ProxyWxFeed`, the ring, the enrol guards, the four setup states | 2-3 days | W2, W4; shared-code changes through blipscope-5c |
| **W8. Forecast, conditions, alerts live** | the three feeds behind `WxFeedFactory.cpp`, one worker, section 1.4 | 2-3 days | W6 |
| **W9. The map for any location** | fetch a package, keep it across boots, draw it | 3-5 days | `/basemap` in production (#606 step 6); #606's client step A, so the fetch is written once |
| **W10. Legend, telemetry, disclosure** | sections 1.6 and 2 | 1-2 days | Q117 |
| **W11. The bench** | real weather on `6E:64`, a fresh boot, a soak | 3-5 days | weather that does something; a 72 h soak can't be hurried |

**Stage 4's total: 18-29 days.**

**The release and the sale.**

| step | what it is | rough estimate | waits on | whose |
|---|---|---|---|---|
| **R1. v19, the identity release** | O11 on its candidate, then Daniel's R gate | #606: 14-36 days from today | the map client and the ocean fix; O11; **A3.1's reader, not yet on main** (A12.172); Q112 | blipscope-5c |
| **R2. A slug and a release channel** | `weatherscope-s3-128` goes `shipping`, publishing `firmware-weatherscope-s3-128.bin` | 1 day | R1 | mine |
| **R3. The first published image** | a version bump. `version.txt` is shared, so it moves every SKU, and a failing Weatherscope leg withholds it for all of them (A2.2) | 1-2 days | R2, W11; Daniel's OK | blipscope-5c cuts |
| **R4. Activation, end to end** | a fresh board: enrol, list, data, on production | 1 day | R3; a stock board | mine, with Daniel |
| **R5. How a sold board gets it** | Q118 | 1-2 days, or 5 or more | Daniel | see Q118 |
| **R6. The cost read at the cut** | the board count, and F1 if it's near 50 | 1 day | R3 | blipscope-5c |
| **R7. The shop** | store page, price, `/weatherscope/support`, NWS credit, photos, and what the page promises about alerts | 2-4 days | R3 | Daniel |

**After both W11 and R1: 4-7 days.** R2 and R3 run in order, then R4 to R7 side by side. If Q118 goes to the chooser, that path is its own plan and this date doesn't cover it.

## 5. How the estimates were made

From what comparable work took, and they're rough.

| work | PR | elapsed | size |
|---|---|---|---|
| Weather server, `/wx` | #443 | 27 hours | +2,636 |
| Activation allowlist | #548 | 7 hours open | +2,663 |
| The swipe screens | #591 | about 10 hours, plan approved to board run | +3,672 |
| Radar weather layer | #445 | 17 days, with waits | +12,681 |

- **W5 is sized like #443,** and W7-W9 like #591 each, stretched for a first real network path.
- **Each merge takes about 15 minutes of main's CI, plus retries:** #597 lost several slots on 2026-11-07.
- **Each step assumes it reads as predicted the first time.** An off reading has cost about half a day so far, and Stage 3 had several.

## 6. Open inputs

| input | status | if it goes badly |
|---|---|---|
| **A Worker's reach to NWS** | not measured | relays, as the aircraft feeds needed, and a few more days |
| **NWS's tolerance** | unpublished. At 600 boards the alerts route alone could ask for thousands of calls an hour across colos | longer holds, or one national alerts fetch on a cron |
| **The feeds' CPU** | estimated | small: they're 10-17 % of a board's cost |
| **The water finding and dense areas** | the map server's, open | W9 can't sell a city without them |
| **A crop's size in heavy rain, 15 kept** | 14 KB seen, 32 KB is the cap | the ring holds fewer frames, and a shorter loop |
| **A 72 h soak with real TLS traffic** | this edition has never held a socket open | the same family of faults the radar spent months on |

**WXC1 v2 (#451) isn't on this road.** The ruling stands: the map draws rain only and the headline says `PRECIP WITHIN 15 MI` until the wire
carries a type, while `SNOW NOW` comes from the observation. A snowstorm draws in rain's colours. Q120 asks whether that's sellable.

## 7. What this plan needs

| Q | from | the question | recommended |
|---|---|---|---|
| **Q113** | **Daniel** | The radar product: add `shsr`, about $0.50 a month flat and past the free Class A tier, or use `cref` | **(a) `shsr`** |
| **Q114** | **Daniel** | Sell in the US only at first | **yes** |
| **Q115** | the reviewer | The loop: a ring on the device, or a history query | **(a) the ring** |
| **Q116** | the reviewer, blipscope-5c | Routes under `/api/v1/weatherscope/`, in the proxy Worker. Who writes W4 and W5: it's their Worker, and I propose writing them here with blipscope-5c reviewing, deploys through Daniel as usual | **yes, and yes** |
| **Q117** | **Daniel** | The first-boot legend, once | **yes** |
| **Q118** | **Daniel** | How a sold board gets Weatherscope. Daniel's direction is one factory image and a chooser at setup, but no chooser exists, and the factory's boards carry v18. (a) Flash Weatherscope on the first units, 1-2 days of work. (b) Build the chooser and edition switching first, 5 days or more, which serves every scope | **(a) for the first units,** (b) on its own plan |
| **Q119** | the reviewer | Who writes the map fetch: #606's client step A and W9 need the same code. Write it once, in whichever comes first | **shared, first come** |
| **Q120** | **Daniel** | Is rain-only radar sellable before WXC1 v2 | **yes, for the first units,** and the shop page says so |

**Also waiting, from A12.172:** Q112 (how E2:08 comes back after O11), and blipscope-5c's reader for R.

## 8. What gets frozen before each step's code

Each step gets its own addendum, with rows and sabotages, as Stage 3's did. Nothing below is a prediction yet. It's the shape they'll take.

| step | the rows it must hold | a sabotage that must go red |
|---|---|---|
| **W2** | the radar's image is unchanged outside the known stamps; Weatherscope sends `weatherscope-s3-128` | the header built from the slug alone |
| **W5** | a fixture's numbers cross both languages unchanged; a failed upstream never reads as no alerts; two devices near each other make one upstream call | a failed fetch answered with an empty list |
| **W7, W8** | D1: nothing outside the factory knows a feed is live; D2: 0 NOAA or NWS hosts in the image | a stale alerts feed that draws the all-clear |
| **W9** | a package survives a reboot; a torn write falls back to no map, not a bad one | a package with a wrong key accepted |
| **W11** | the screens agree with NWS's own page at the same minute, by eye, for a real alert | none: it's the glass, and Daniel's to judge |

---

## Addendum S4.1, 2026-11-08: the rulings, one correction, and W2 and W3 frozen before any code

**The rulings (2026-11-07 ~23:23Z and ~23:25Z), all as recommended:** Q112 to Q120. The record is `docs/weatherscope-stage3.md` A12.174.
Two carry conditions:
- **Q113:** blipscope-5c reads the SeamlessHSR server work for conflicts.
- **Q116:** blipscope-5c reads each push of the routes for conflicts with its own, before the push.

### S4.1.1 A correction to section 4's R1, and to the short version

**R1 said v19 waits on the map client and the ocean fix,** citing #606. #609 records Daniel's ~16:03Z decision: v19 ships when it's ready,
without weather, and weather and the map ship together later. So **R1 no longer waits on the map.**
- If v19 is cut before Stage 4's own work ends (November 25 - December 6), **the sale is about November 29 - December 13.**
- **v19's own date isn't stated anywhere I read,** so the upper end still rests on it.

### S4.1.2 W2: the model header

**The change:**
- One pure function, beside `ota::Request`, returns the model string: the scope followed by the board, which is what the device asks OTA for.
- `CloudFeed.cpp`'s header calls it instead of `variant::SLUG`.
- Weatherscope's own client calls it in W7.

**The contract, for blipscope-5c to agree before any code (section 1.1):**

| item | proposed |
|---|---|
| the value | the OTA identity, so `weatherscope-s3-128` here, and the radar's unchanged |
| `MODEL_DEFAULTS` | no entry: Weatherscope reads no `/blips` |
| `reconcile-fleet.py` | counts a board once, whatever its model reads, since a scope switch changes the model and not the board |
| who writes the device change | me. It touches `CloudFeed.cpp`, so blipscope-5c reads it first |

**Predicted, once agreed:**

| row | predicted | its sabotage |
|---|---|---|
| **M1** (host) | over O1′'s 15 identities (A12.103), the model is exactly scope followed by board, and every radar identity's model equals its `variant::SLUG` | **S-M1:** the board alone. Every identity with a scope goes red, and no radar row does |
| **M2** (image) | every radar env's image is byte-identical outside A5.6's R1 and R2 stamps, by S1 v2's method | **S1's covering sabotage,** a planted byte in `CloudFeed.cpp`, changes exactly the envs that compile it |
| **M3** (staging, read-only) | a request carrying `X-Blip-Model: weatherscope-s3-128` gets `/config`'s `BASE`, and its metric carries the string unchanged | none: it reads blipscope-5c's code, and changes nothing there |

**M2 needs builds of every radar env, at base and head,** and C: is under 30 GB tonight. M2 waits for the disk, and nothing else in W2 does.

### S4.1.3 W3: can a Worker reach NWS?

**Why first:** if NWS refuses or throttles Cloudflare's egress, W5's design changes, so this should be found out before W5's code, not during
it. The aircraft feeds needed relays for exactly this reason.

**The probe:** about 40 lines, outside the proxy, with no binding and no route. From each of 5 public airports (KBOS, KDEN, KDFW, KMIA, KSEA), it
fetches the 6 calls W5 will make:
- `/points`;
- the forecast;
- the hourly forecast;
- the stations list;
- the latest observation;
- `/alerts/active?point=`.

It does that in 3 rounds 10 minutes apart, which is 90 calls in all. It returns only each call's status, time, size and `Cache-Control`.
**No body leaves it,** since they name offices, grids and coordinates. The User-Agent is `Weatherscope (support@valarsystems.com)`.

**Q121, how it runs (the reviewer's, with Daniel):**
- **(a) `wrangler dev --remote` (recommended):** the probe runs on Cloudflare's edge as an ephemeral preview. It's no deploy and touches no Worker, KV or route.
- **(b) A route on the staging proxy:** this is a deploy, so it needs the lock and a rollback target.

Either way it's announced through Daniel first, because it uses the account.

| row | predicted | if not |
|---|---|---|
| **W3-1** | 90 of 90 calls answer 200 | a 403 or 429 means NWS refuses Cloudflare's egress, and W5 goes through the relays: **stop and report** |
| **W3-2, the control** | the same call **without a User-Agent** answers **403** | a 200 means the control is blind (NWS no longer checks), **recorded and not a stop,** with W3-1 read on its own |
| **W3-3** | a median under 1,000 ms per call, and none over 5,000 ms | slower sets W5's hold times longer, and doesn't stop |
| **W3-4** | each kind's `Cache-Control` max-age, recorded | it sets W5's hold times: no prediction |
| **W3-5** | each kind's raw size, recorded. The trimmed body must fit 32 KB, so a raw forecast over 200 KB gets a note | |
| **anything else** | | **outside this set: the probe is unreliable at this scale, stop and decide.** Nothing is read into it on the spot |

**Not in W3:** the feeds' CPU. That needs W5's trim code, so it's read in W5's staging reading.

### S4.2, 2026-11-08: W3 ruled, restated before it runs, and announced

**Ruled ~02:33Z (Q121):**
- **(a): `wrangler dev --remote`,** announced through Daniel. It isn't a deploy, so it takes no claim.
- **If (a) reads refused across the board,** (b) re-reads on a staging route before anything is concluded, since a preview's egress may not be a deployed Worker's.

**W2:** blipscope-5c reads S4.1.2's contract table and agrees or objects through Daniel, before any code.

**What changed since S4.1.3, before any W3 reading:**
- **7 kinds of call, not 6.** The probe looks up each airport's position in NWS's own station record (`/stations/<ICAO>`), so no coordinate appears in its source or its output. That makes **105 calls** (7 × 5 airports × 3 rounds), plus 3 controls.
- **The control** is `/stations/KDFW` with no User-Agent, once a round.

**The probe's code was rehearsed, not reasoned:** one round under local `wrangler dev`, so its traffic left from this PC and not Cloudflare. **That run tests the probe, and is not a W3 reading:**

| | read |
|---|---|
| the calls | 35 of 35 answered 200 |
| the control | **403**, so the probe can see a refusal |
| sizes | hourly forecast **162-163 KB**; stations list up to 106 KB; forecast about 14 KB; alerts 0.2-6 KB |
| NWS's max-age | alerts 5 s; observation 8-71 s; forecast up to about 47 min |

**For W5:** the raw hourly forecast is five times the device's 32 KB, so the Worker's trim is required, not an optimisation. NWS holds alerts only about 5 s, so W5's 60 s hold is the Worker's own choice and must be stated as one.

**The rows, restated:**

| row | predicted | if not |
|---|---|---|
| **W3-0, the rig** | `wrangler dev --remote` starts and answers on its local port | **the instrument,** reported. (b) needs its own ruling |
| **W3-1** | **105 of 105** answer 200 | **refused across the board:** (b) re-reads on staging before anything is concluded. **Partly refused:** stop and report |
| **W3-2, the control** | **3 of 3 answer 403** | a 200 means the control is blind, **recorded and not a stop** |
| **W3-3** | each kind's median under 1,000 ms, and none over 5,000 ms | recorded; it sets W5's holds, and doesn't stop |
| **W3-4** | each kind's max-age, recorded | |
| **W3-5** | the hourly forecast 150-175 KB, every kind under 200 KB | recorded; W5's trim budget is re-read |
| **anything else** | | **outside this set: stop and decide.** Nothing is read into it on the spot |

**The window, announced through Daniel before it opens:**
- **What runs:** about 25 minutes. `wrangler dev --remote` from a folder outside the repo, with no binding, route, KV or secret, then three rounds 10 minutes apart. About 36 calls to `api.weather.gov` a round, with `support@valarsystems.com` in the User-Agent.
- **What it touches:** an ephemeral preview on the account. Nothing is deployed, and no Worker, route or KV changes.
- **The close:** the local process tree is stopped and checked gone by command line, and the preview ends with it.
- **The record:** statuses, times, sizes and max-ages only. No body, URL or coordinate.

### S4.3, 2026-11-08: W3 stopped at W3-0, the rig. No W3 reading exists. One question (Q122)

**The window:** 20:00:17Z to 20:11:23Z, on Daniel's go (~19:07Z).

**W3-0 read as the rig:** `wrangler dev --remote` stopped at `Starting remote preview...` with `[ERROR] Could not create remote preview
session on your account.`
- **No preview ran, so no call reached NWS from Cloudflare,** and W3-1 to W3-5 have no reading.
- As S4.2 registered, this is the instrument. **(b) needs its own ruling,** and I haven't run it.
- The process tree was stopped by parent id and checked gone, and port 8799 is free. Nothing else was touched.

**What it can't tell apart, all named:**

| cause | what would show it | can it be read from here? |
|---|---|---|
| **The account has no `workers.dev` subdomain,** which a remote preview needs. The proxy runs with `workers_dev = false` | the account's subdomain setting | **No.** It needs an API token with Workers scope, and none is on this machine, by rule |
| **The login lacks a scope** the preview session needs | the login's scopes | Only by printing account details, which I won't |
| **A transient Cloudflare failure** | a second attempt that starts | **Yes,** by one retry |

**Ruled out: wrangler's suggested replacement,** `wrangler dev --x-remote-bindings`. It runs the Worker locally, so its calls would leave
from this PC, which isn't what W3 measures.

#### Q122. How W3 is read now

- **(a) One retry of `dev --remote`, as frozen,** announced the same way. If it starts, the transient cause is excluded and W3 reads as S4.2 says. If it fails the same way, it rules out nothing else, and (b) follows.
- **(b) Q121's (b): the probe as a route on the staging proxy** (recommended). It's a deploy, so it takes the claim (#618) and has a rollback target, and blipscope-5c reads it for conflicts (Q116). It measures a deployed Worker's egress, which is what W5 will be. S4.2's rows stand, read on that route.

**Recommended: (b),** because it reads the real path and answers the question either way. (a) is cheaper, but only excludes the least
likely cause.

### S4.4, 2026-11-08: W3 on a staging route (Q122 (b)), frozen before the window

**Ruled ~20:13Z:**
- W3-0 is recorded as the pre-registered rig outcome, with no retry of `dev --remote`.
- **Q122 (b):** the probe runs as a route on the staging proxy, behind staging's existing device auth, and makes no call unless asked.
- blipscope-5c reads its diff for conflicts before the deploy. The Worker-input diff is main plus the probe alone.
- **Under the deploy claim, as `weatherscope`:** staging's rollback target is recorded first. After the reading, staging rolls back to it and is read back, and only then is the claim released.
- The start and the end are announced through Daniel.

**The probe, as code:** branch `weatherscope/w3-probe`, one commit on main `abd7c67b3ebb`, **never merged.** A draft PR carries it for
blipscope-5c's read and for CI.

| file | what |
|---|---|
| `proxy/src/weatherscope/w3probe.ts` | new: the probe. One request runs one round; it returns status, time, size and max-age only |
| `proxy/src/index.ts` | one import, and one dispatch line after `basemap`, behind the same auth and per-key limit as every API route |
| `proxy/src/types.ts` | `W3_PROBE?: string` |
| `proxy/wrangler.toml` | `W3_PROBE = "on"` in `[env.staging.vars]` only. The edit asserted it's absent from production's section and the top level |
| `proxy/test/w3probe.test.ts` | W3T1-W3T6 |

**The path is `/api/v1/blipscope/w3-nws-probe`,** not under Q116's `/api/v1/weatherscope/`. The router knows one edition prefix today,
and adding a second is W5's work. A temporary probe that rolls back shouldn't carry it.

**Read locally before the push:** `tsc --noEmit` exit 0. **W3T1-W3T6 6 of 6** (one file, behind the vitest gate):

| test | holds |
|---|---|
| W3T1 | no auth gives 401, even with the switch on |
| W3T2 | with the switch off, as production is, an authenticated call gets 404 and no NWS call is made |
| W3T3 | a round outside 1-9 is refused before any call |
| W3T4 | one round is 35 calls with a User-Agent and 1 without, in W5's order |
| W3T5 | no URL, body or coordinate in the response, with a control that it still says something |
| W3T6 | a refused station marks its airport's other 6 calls skipped, not failed |

**The tests can fail:** two plants turned exactly their rows red. Dropping the switch turned W3T2 red, and leaking a URL into a row turned W3T5 red. The file was restored from a byte copy and checked by sha256.

**CI, predicted before the push:** the last job-set change is still `caf63cc9d9cc`. The four workflow commits since added steps only, and
no job line. A `proxy/` change runs `workers` as well, so the baseline is #596's and #618's finished runs:

| run | total | success | skipped | failed |
|---|---|---|---|---|
| the draft PR | **35** | 32 | 3 (promote, version, ship-alert) | 0 |

#### The window's rows

| row | predicted | stop |
|---|---|---|
| **D-claim** | `session-claim.py take deploy --covers proxy:staging` as `weatherscope` succeeds | held by another session: wait, never take it |
| **R0, the target** | before anything, `wrangler deployments list --env staging` names the live version, and `/healthz` gives its commit. Both are recorded | can't be read: no deploy |
| **D0** | `deploy.sh staging` exit 0: a clean tree, on a remote branch, not behind main in `proxy/`, `DEVICE_KEY_SECRET` present | any refusal: stop, and nothing was deployed |
| **D1** | staging's `/healthz` reports the probe's commit, read twice a minute apart | another commit: roll back, stop |
| **A0, the auth control, live** | the route with no credentials answers **401** | a 200: **roll back at once,** stop |
| **W3-1 to W3-5** | as S4.2, on the deployed Worker: **105 of 105** answer 200, **3 of 3** controls answer 403, medians under 1,000 ms and none over 5,000 ms, max-age recorded, the hourly forecast 150-175 KB and every kind under 200 KB | as S4.2: refused across the board means NWS refuses Cloudflare's egress, and W5 goes through the relays; partly refused, or anything outside the set, stops |
| **RB, the rollback** | `wrangler rollback` to R0's version, then `/healthz` reports R0's commit, read twice. The probe route answers 404 | not R0's commit: **report at once,** and keep the claim until it is |
| **D-release** | the claim released; `session-claim.py show` lists no deploy claim of mine | |

**The rollback runs whatever the reading says,** off-prediction included. A rollback is never refused, and it's announced.

**Calls go as the test identity** (`docs/test-identity.md`): the token file's `test-device` with `test-key-staging`, through `curl -K -`, so
no key appears on a command line or in output.

**One thing staging loses for the window.** Staging runs main plus the probe for about 35 minutes, in place of whatever it runs now, then
goes back to exactly that version. A session reading staging in that window would read main plus the probe. That's why the start and the
end are announced.

### S4.5, 2026-11-08: two errors of mine in W2's frozen rows, caught before any code. One question (Q123)

**W2 is cleared to go (~20:13Z),** with blipscope-5c's contract agreed, on its condition for W7. Before writing any code I re-read
S4.1.2 against main and found two of its rows wrong.

**1. M1 counted a population that no longer exists.** It said "over O1′'s 15 identities (A12.103)". `test/fixtures/ota/identities.txt` on
main lists **8**, because #549 retired the `s3-146` envs. I quoted the number from memory instead of reading the table.

| identities on main | count |
|---|---|
| with a scope (`eam-` ×2, `weatherscope-`) | **3** |
| without (the radar's 5) | **5** |

**The fix is structural, not a new number:** M1 reads `identities.txt` itself, the same table `check_ota_identity.py --check-table` holds to
the fleet. So the count can't go stale this way again.

**2. M2 predicted something the change it described can't do.** S4.1.2's change made `CloudFeed.cpp` call a function instead of sending
`variant::SLUG`. That changes the code that builds the header in every radar image, so "byte-identical outside the stamps" is false by
construction. An unreferenced helper in `OtaUpdater.cpp` would also leave its `.eh_frame` record in every other edition's image (M8,
A12.139). The row would have read off, and it would have been my design, not the code, that was wrong.

#### Q123. How the radar keeps its header

| | (a) a compile-time check (recommended) | (b) the call, as S4.1.2 described |
|---|---|---|
| **radar** | `CloudFeed.cpp` keeps `variant::SLUG`, and gains `static_assert(sizeof(FW_OTA_PREFIX) == 1, …)`. Every env that compiles it is radar, with an empty prefix, so the slug **is** its OTA identity. A scoped build that ever compiled it would fail to compile | the header comes from a function built on `ota::Request` |
| **Weatherscope** | its W7 client calls a header-only `ota::ModelString(ota::Request(variant::SLUG, FW_OTA_PREFIX), …)` | the same |
| **radar images** | **byte-identical outside A5.6's R1 and R2:** a `static_assert` emits nothing, and an inline function no one calls emits nothing | every radar image changes, so M2 can only check the header's value on the wire |
| **the contract** | unchanged: the model is the OTA identity, and the radar's is the slug | unchanged |

**Recommended (a):** the contract holds, the radar's images don't move, and the one way it could break is refused by the compiler.
blipscope-5c agreed the contract's terms, not this mechanism, so it reads (a) for conflicts too.

**The rows, under (a):**

| row | predicted | its sabotage |
|---|---|---|
| **M1** (host) | for every row of `identities.txt` (8 today), `ModelString` is scope followed by board, and for the 5 without a scope it equals the board | **S-M1,** the board alone: exactly the 3 scoped rows go red, and none of the 5 |
| **M1b** (host) | `ModelString` refuses a buffer too small for the result and writes nothing, rather than cutting it short | **S-M1b,** truncation instead of refusal turns it red. The same reason as `leaderboard.ts`'s condition: a cut model is a different model |
| **M2** (image) | every `skus.yml` env that compiles `CloudFeed.cpp` (5 expected, derived from each env's filter at the run) is byte-identical outside R1 and R2, by S1 v2's method | **S-M2:** a build with `FW_OTA_PREFIX="x-"` on one radar env must fail to compile. That proves the `static_assert` can fire |
| **M3** (staging, read-only) | as S4.1.2 | |

**M2 needs builds:** 5 envs at base and head. C: is at about 32 GiB, so they fit, and each env's build is deleted as soon as its pair is
compared.

### S4.6, 2026-11-08: W2's code, and its counts frozen before the suite first runs

**Ruled ~20:41Z:**
- **Q123 (a).** The radar keeps `variant::SLUG`, with the compile-time `static_assert`, so radar images stay byte-identical and M2 holds as first written.
- Weatherscope's client uses the header-only `ota::ModelString`, and blipscope-5c reads the mechanism as the radar's owner.
- S4.5's two corrections are accepted, and on record as my misses.

**The code:** branch `weatherscope/w2-model`, from main `abd7c67b3ebb`.

| file | change |
|---|---|
| `include/OtaIdentity.h` | `inline bool ModelString(const Identity&, char*, size_t)`: scope followed by board, or false with nothing written |
| `src/CloudFeed.cpp` | `#if defined(FW_OTA_PREFIX)` then `static_assert(sizeof(FW_OTA_PREFIX) == 1, …)`. No other line changes |
| `test/host/test_ota_model.cpp` | new: M1 and M1b, reading `identities.txt` |
| `test/host/sabotage_ota_model.py` | new: S-M1 and S-M1b, with `--anchors` and `--control` |
| `test/host/run.sh` | one block, after the OTA identity block. The driver is found by `run.sh`'s own glob |

**A design choice, caught by applying S4.5's rule before running.** As first written, `M1b-fit` checked the model's text as well as its
acceptance, so S-M1 would have turned it red too: 4 rows, against S4.5's frozen 3. M1b grades refusal against acceptance only, and the
text is M1's. So `M1b-fit` now checks acceptance only, and the frozen sets hold as written.

**Predicted, from the table, before any run:**

| row | predicted |
|---|---|
| **the suite** | **16 checks, 0 failures:** 8 rows, plus 5 without a scope, plus 3 for M1b. MinGW and WSL g++ 13.3 |
| **S-M1** | exactly `M1-missileer-s3-128`, `M1-missileer-photo`, `M1-weatherscope-s3-128` red |
| **S-M1b** | exactly `M1b-short` red |
| **the driver** | the control 0 failures, then 2 of 2 as predicted, both compilers |
| **the anchors** | every driver and both board plants `--anchors` pass first, now one driver more |
| **run.sh under WSL** | exit 2, with exactly the two named blocks and towns SKIP in both passes, plus the new block at 16 |
| **M2** | as S4.5, by `s1.py compare2` |
| **S-M2** | a radar env built with `FW_OTA_PREFIX="x-"` fails to compile, and the message names the `static_assert` |

### S4.7, 2026-11-08: W2's rows read, all as predicted. One more error of mine, in an instrument, caught before it cost anything

**W2 is at `6a068d085846`** on `weatherscope/w2-model`, from main `abd7c67b3ebb`.

| row | predicted (S4.6) | read |
|---|---|---|
| **the suite** | 16 checks, 0 failures, MinGW and WSL g++ 13.3 | **as predicted,** both: `8 rows; 16 checks, 0 failures` |
| **S-M1** | exactly the 3 scoped M1 rows red | **as predicted,** both compilers |
| **S-M1b** | exactly `M1b-short` red | **as predicted,** both compilers |
| **the driver** | the control 0 failures; 2 of 2 | **ALL AS PREDICTED,** both compilers; the real header unchanged |
| **the anchors** | every driver and both board plants pass first | **35 drivers, 2 plants, 0 failed;** `sabotage_ota_model.py` 2 anchors once each |
| **run.sh under WSL** | exit 2; the two named blocks; towns SKIP in both passes; the new block at 16 | **as predicted:** exit 2, the failed blocks exactly "the same TU, for the board" and "Weatherscope R0 and R0b", towns SKIP in both passes; `== OTA model ==` reads `8 rows; 16 checks, 0 failures`, and `sabotage_ota_model.py` passes its anchors, control and driver |
| **the calibration** | `blipscope-s3-128` at the base, twice: 0 bytes | **0 bytes** |
| **M2** | every env with the cloud feed byte-identical outside R1 and R2 | **5 of 5.** Four differ only in R1 and R2 (65 bytes, the ELF's hash and the checksum); `animtest-s3-128` doesn't differ at all |
| **S-M2** | a radar env built with `FW_OTA_PREFIX="x-"` fails, naming the `static_assert` | **exit 1, the message named twice** |

**The error: M2's env set came from the wrong property.** S4.5 said the set is derived "from each env's filter". My first derivation did
exactly that, and reported all 8 built envs as compiling `CloudFeed.cpp`, Missileer's and Weatherscope's included. That's true, and it's not
what matters. The whole file sits inside `#ifdef FEATURE_CLOUD_FEED`, so in an env without that flag it compiles to nothing. If the
filter-based reading were the real one, the `static_assert` would have broken Missileer's and Weatherscope's builds. The host tests can't
see a build, so CI would have been the first to find out.
- **The property that matters is `FEATURE_CLOUD_FEED`.** I read it from PlatformIO's own resolved config (`pio project config
  --json-output`), not by expanding `${…}` and `extends` by hand, which is how the first reading went wrong.
- **The set is `blipscope-s3-128`, `-wxlook`, `blipscope-pro-s3-21`, `animtest-s3-128` and `blipscope-s3-128-joindiag`.** It's 5, as S4.5
  expected, with `animtest` in and the AMOLED out. I had assumed the AMOLED.
- **Across all 46 envs in `platformio.ini`,** 22 define the cloud feed and **0 of those has a scope.** So the `static_assert` breaks nothing that exists, and S-M2 shows it fires on the one thing it exists for.

**M3,** staging read-only, waits for the W3 window to close, so staging is read on the version it normally runs.

#### W2's PR, its CI predicted before the push

The last job-set change is still `caf63cc9d9cc`. W2's paths are `include/`, `src/` and `test/host/`, the same workflows as #609's. That
adds `web-renderer`'s `build` (success) and its `publish` (skipped).

| run | total | success | skipped | failed |
|---|---|---|---|---|
| the PR | **34** | 30 | 4 (promote, ship-alert, version, publish) | 0 |
| main on the squash | **34** | 31 | 3 (version, promote, publish) | 0 |

blipscope-5c reads the mechanism as the radar's owner (Q123), before any merge.

### S4.8, 2026-11-11: #628 brought up to main before the window, its CI predicted before the push

**Ruled ~01:12Z:**
- **#628 conflicts with main** in `proxy/src/index.ts`, where #623 (merged 21:38Z Nov 8) changed the import beside the probe's. Main is
  merged in with both sides kept, so `index.ts` differs from main only by the probe's import and route lines.
- **CI is predicted on the new head,** then read twice.
- **Staging's current version is recorded before the deploy** and rolled back to after the read-back. The window deploys main plus the
  probe, never older proxy code. Then the reviewer gives Daniel the start announcement.
- **#630 is approved at `6a068d085846`.** The approval takes effect when blipscope-5c's read of the mechanism comes back with no objection.
  It merges third, after #631 and #624, and only through `scripts/merge-by-rule.sh`.

**The merge is `1db0659e9a50`.** Its parents are `c2992b238c5b` (the probe) and main `81cfbd092ec2`. The one conflict was the import:
#623 made it `{ PRODUCTS, ROUTED }`, and the probe's import sits on the next line. Both are kept. Against main, the merge differs in the
probe's 5 files only, 187 insertions and 0 deletions. In `index.ts` that's 3 lines:

```
+import { handleW3Probe } from "./weatherscope/w3probe";
+  // W3 (docs/weatherscope-stage4.md S4.4): STAGING ONLY and temporary -- 404 unless W3_PROBE is "on". Never merged.
+  if (api.suffix === "w3-nws-probe") return handleW3Probe(request, env);
```

**One error of mine, caught before the commit.** The resolver asserted the working-tree file had no CRLF. This worktree checks out with
`core.autocrlf=true`, so the assertion fired and the resolver wrote nothing. That was correct, but the `git add` on the next line wasn't
chained to it, so it staged the file with its conflict markers. Nothing was committed. The resolution was redone in the file's own CRLF
bytes. The staged blob then had 0 markers and 0 CRLF, the same as both parents' blobs.

**Read locally on the merge:** `tsc --noEmit` exit 0. **W3T1-W3T6 6 of 6**, behind the vitest gate (no other vitest, TIME_WAIT 106).

**CI, predicted before the push:**
- **The last job-set change is still `caf63cc9d9cc`.** Since the probe's base `abd7c67b3ebb`, three main commits (#617, #623, #629) touched
  four workflow files. Each only added steps, and every file's job count is unchanged.
- **`workers.yml`'s `paths` gained two entries,** `.github/workflows/photo-drift.yml` and `scripts/reconcile-fleet*.py`. This PR touches
  neither.
- **The PR's diff against its new base is the same 5 `proxy/` files,** so the same workflows run.
- **Main at `81cfbd092ec2` has finished:** 39 = 36/3/0.

| run | total | success | skipped | failed |
|---|---|---|---|---|
| #628 at `1db0659e9a50` | **35** | 32 | 3 (promote, version, ship-alert) | 0 |

**For the window, S4.4's rows stand with two readings made exact:**
- **D1:** `/healthz` reports `1db0659e9a50`, or whatever #628's head is at the deploy.
- **R0:** staging's live version and commit are read and recorded before anything is deployed, whatever they are, and RB returns to exactly
  that version. If main moves before the window, `deploy.sh` refuses a tree behind main in `proxy/`. Then main is merged in again and CI is
  predicted and read again before any deploy.

### S4.9, 2026-11-11: W3's window. A Worker reaches NWS, and the User-Agent is required

**Ruled ~03:37Z:** #628 was cleared for W3. Read from git, its `proxy/` differed from main (`947e1d3825fc`, #624, docs only) by the probe's 5
files alone, and S4.8's CI (35 = 32/3/0) stood. The start was posted the same minute.

**The window, about 22 minutes on the probe's version:**

| step | time (Z) | read |
|---|---|---|
| claim | 03:38:51 | `proxy:staging`, until 04:23:51 |
| **R0** | 03:38:59 | version `eb36fe73-ed1e-433a-aafa-1e3f3188f210` at 100%, deployed 2026-11-07 18:13; `/healthz` `fdf62ada` |
| `deploy.sh` | 03:39:27 | exit 0; version `1b216cf7-7f32-421c-a33d-8e5d1a6d59ca`; its own confirm read `/healthz` `1db0659e`; 2 commits behind main, neither in `proxy/` |
| **D1** | 03:39:51, 03:40:51 | `1db0659e` twice; the route check answered 400, so the probe was live |
| **A0** | 03:40:52 | no credentials: 401 |
| rounds 1-3 | 03:40:54, 03:50:57, 04:01:05 | 200 each |
| **RB** | 04:01:42 | `wrangler rollback` to R0's version, exit 0 |
| read-back | 04:01:58 to 04:03:01 | `/healthz` `fdf62ada` twice, a minute apart; wrangler shows R0's version at 100%; the probe route 404 |
| claim released | 04:04:22 | both claims free |

**The readings, against S4.4's rows, all as predicted:**

| row | predicted | read |
|---|---|---|
| **W3-1** | 105 of 105 answer 200 | **105 of 105** |
| **W3-2, the control** | 3 of 3 answer 403 | **3 of 3** |
| **W3-3** | each kind's median under 1,000 ms, none over 5,000 ms | medians 18-159 ms; the slowest single call 3,567 ms (alerts) |
| **W3-4** | each kind's max-age, recorded | below |
| **W3-5** | the hourly forecast 150-175 KB, every kind under 200 KB | hourly 162,485-163,559 B, in range whether a KB is 1,000 or 1,024 B; the next largest kind, stations, 105,569 B |

Per kind, 15 calls each (5 airports, 3 rounds):

| kind | ms median | ms max | bytes | max-age seen (s) |
|---|---|---|---|---|
| station | 46 | 124 | 2,135-2,153 | 20,313-40,998 |
| points | 21 | 54 | 3,915-3,936 | 22,835-75,149 |
| forecast | 24 | 94 | 13,047-13,929 | 96-3,524 |
| hourly | 32 | 139 | 162,485-163,559 | 248-3,237 |
| stations | 66 | 1,041 | 29,356-105,569 | 32,181-86,400 |
| observation | 18 | 135 | 4,308-4,516 | 65-272 |
| alerts | 159 | 3,567 | 232-234 | 5 |

**What it settles for W5:**
- **A Worker can call NWS directly.** 105 calls from Cloudflare's egress over 20 minutes, none refused or throttled, so W5 needs no relay at
  this rate.
- **The alternative is that the 200s came from a cache in front of NWS.** The control excludes a cache keyed on the URL: the same call
  without a User-Agent got NWS's 403 in every round.
- **The User-Agent is required.** NWS refuses a call without one, so W5 sends one on every call.
- **Hold times (W3-4):** alerts carry a max-age of 5 s, observations 1 to 5 minutes, the forecasts up to about an hour, and points, stations
  and the station up to a day.
- **Size (W3-5):** the hourly forecast is about 163 KB raw, under the 200 KB note. W5's 32 KB trimmed body is unchanged.

**The rollback, and the gap it exposed:**
- RB was typed as its own command, and the claim hook did not log it. This session's project folder, `C:\Github\Blipscope`, is on main at
  `6d7823e11c24` (#580), from before #618: it has no `.claude/settings.json` and no `scripts/claim-hook.py`, so the hook never loaded. A
  hook pointing at a missing script would have refused every Bash call, and none was refused.
- `deploy.sh`'s own claim check ran ("deploy: yours"), so the deploy was claim-checked.
- On the reviewer's ruling (~04:10Z), the `rollbacks.log` line was entered by hand, labelled as such, and the rollback was announced. Until
  blipscope-5c's fix, every session without the hook deploys only through `deploy.sh`, and after any rollback adds a labelled line by hand
  and announces it.

**One change to the read-back, made before it ran.** The script first read `/healthz` once, straight after the rollback. Isolates drain
after a version change (`deploy.sh`'s confirm polls for that reason), so one early read could have shown the probe's commit after a good
rollback. Before running it, I made the first read a poll: every 15 s, up to 5 minutes, with anything later counted as off. It read
`fdf62ada` on the first poll.

**After the window:**
- #628 is closed unmerged, naming this record. Its branch stays at `1db0659e9a50` as the record of what ran on staging.
- M3, staging's `/config` read-only, is next.
- #630 is first in the merge order once blipscope-5c's read is back, then #632.

### S4.10, 2026-11-11: W2 merged; M3 off as recorded; staging's fleet defaults moved the three fields

**#630 (W2) merged at 04:51:51Z**, first in the order. This followed the reviewer's ~04:39Z go on the approval at `6a068d085846`, after
blipscope-5c's read came back with no objection. It went through `merge-by-rule.sh`:

- squash `68dc169a39ee`; parent `947e1d3825fc` (H);
- tree `6521a5476452`, equal to the merge-tree of H and `6a068d085846`;
- the branch was deleted.

**Main's push CI at the squash**, frozen at 04:50:45Z, before the merge (push-event runs only):

| | predicted | read at 06:59:43Z and 07:00:49Z; both reads agree |
|---|---|---|
| workflows | device-ids 1, firmware 30, no-traffic-captures 1, web-renderer 2 | the same: 1, 30, 1, 2 |
| jobs | 34 = 31 success / 3 skipped / 0 failed | **34 = 31 / 3 / 0** |
| skipped | firmware: version, promote; web-renderer: publish | firmware/version, firmware/promote, web-renderer/publish |

**An instrument error of mine delayed that reading by two hours. It produced no false reading.**

- **What happened.** The wait script ran from outside any checkout, so every `gh` call failed. The script never checked them: it polled for
  two hours, then printed an empty tally and "the two reads agree". I took that as no reading.
- **A second defect.** `gh run list --commit` matches only the full 40-character sha. A 12-character sha returns no runs, with exit 0.
- **The fix.** The script now:
  - names the repo;
  - stops on a `gh` error or an empty tally;
  - resolves the full sha itself.
- **The check.** Run on the 12-character sha it had failed on, the fixed script gave the same tally (07:02:17Z and 07:03:24Z).

**M3 is off as recorded (ruled ~04:46Z: "A frozen prediction isn't restated after the read").** The three requests ran as the test identity
at 04:36:55Z, and all answered 200:

| | pollActiveMs | pollIdleMs | pollNightMs | idleAfterMs | staleFactor | minStaleMs | enrich |
|---|---|---|---|---|---|---|---|
| BASE, predicted for W | 5000 | 15000 | 60000 | 600000 | 3 | 45000 | full |
| **W** `weatherscope-s3-128` | **15000** | **30000** | 60000 | 600000 | **4** | 45000 | full |
| **E**, no model header | 15000 | 30000 | 60000 | 600000 | 4 | 45000 | full |
| **P** `s3-21`, the control | 15000 | 30000 | **45000** | **900000** | 4 | 45000 | full |

| row | predicted | read |
|---|---|---|
| **M3a** | W equals BASE | **off**: pollActiveMs, pollIdleMs and staleFactor moved |
| **C1, the control** | P has pollActiveMs 2000, so it differs from W | **off on its value**, since P has 15000. P still differs from W in pollNightMs and idleAfterMs (the baked `s3-21` values, `proxy/src/config.ts`), so the endpoint does answer by model |
| **C2** | E equals W on all seven | **holds** |
| **M3b** | in Analytics Engine, exactly 1 `/config` row for `weatherscope-s3-128`, no other row containing `weatherscope`, and exactly 1 for `s3-21` | **holds**: 1, 1 and 1, with no rows on other routes for these models. The first poll was empty, and the second found both |

**The cause, as ruled:** the prediction assumed staging's fleet layer was empty, and staging's `cfg:fleet` is at rev 2. C2, C1 and M3b hold
M3's purpose:

- the model string reaches staging unchanged and is recorded once;
- it selects nothing for `weatherscope-s3-128`, which gets exactly what a request with no model gets.

**My three errors, on record:**

1. **M3a and C1 assumed staging's fleet layer was empty.** The code's resolution order shows that layer, and I didn't check what was in it.
2. **The grader's verdict sentence was false.** It said "the control is blind (P == W)" whenever C1's value missed, even though P differed
   from W in two fields. Its exit code (off) was right.
   - Fixed separately, on the ruling. Each row that is off now prints its own sentence, and that sentence says only what its row shows.
   - C1's sentence names the fields where P differs. "Blind" prints only when P equals W on all seven.
   - The exit codes are unchanged across the seven rehearsals and on M3's data. On M3's data it now prints "OFF on the letter -- W == E but
     not BASE" and "C1's predicted pollActiveMs 2000 is off (P has 15000), but P differs from W in ['idleAfterMs', 'pollNightMs']".
3. **My first metric script failed before it sent any query.** tsx compiled it as CommonJS, where top-level await fails. I renamed it to
   `.mts` and the same query ran.

**The `cfg:fleet` read (approved ~04:46Z; read-only).** The table below was frozen before the read. The anchor came first: the read's `rev`
must be 2, the rev `/config` served. Any other rev, or a failed or non-JSON read, would be the instrument, not a reading.

| row | reading | meaning |
|---|---|---|
| 1 | `defaults` holds pollActiveMs 15000, pollIdleMs 30000 and staleFactor 4, and the test device is **no** | the fleet **defaults** moved the three fields |
| 2 | the test device is **yes**, and `defaults` does not hold all three | the test device's own **devices** entry moved them |
| 3 | neither, including both at once | **stop** and report |

**How the read ran:** at ~04:53Z, through `--binding ENRICH_KV --env staging --remote`. It printed only the five approved items. The test
device's id was compared inside the process, and it appears in no output, command line or record.

| printed | read |
|---|---|
| rev | **2** (the anchor holds) |
| defaults | pollActiveMs 15000, pollIdleMs 30000, staleFactor 4 |
| models key names | none |
| devices entries | 0 |
| the test device has an entry | no |

**Row 1: the fleet defaults moved the three fields.** The alternative is a devices entry, and the data rules it out: there are 0 devices
entries, so no entry exists to move them.

**For every session's staging `/config` predictions:**
- Predict from BASE, then the model's baked `MODEL_DEFAULTS`, then `cfg:fleet`'s defaults. At rev 2, those defaults are pollActiveMs 15000,
  pollIdleMs 30000 and staleFactor 4.
- So on staging every model gets those three, including `s3-21`, whose baked 2000/10000 are overridden. Only pollNightMs, idleAfterMs,
  minStaleMs and enrich still vary by baked model.
- `cfg:fleet` can change with no deploy. `/config`'s `rev` shows whether it has moved.

**The hook gap, still open for this session:** my project folder, `C:\Github\Blipscope`, is still at `6d7823e11c24`, with no
`.claude/settings.json` and no `scripts/claim-hook.py`. Until blipscope-5c's fix, I deploy only through `deploy.sh`, under the interim rule.

**Next:**
- O11 runs when blipscope-5c cuts v19's candidate.
- Nothing of mine is in the merge order.

### S4.11, 2026-11-11: W5's addendum, frozen before any code. The wire, the rows, the sabotages, and one question (Q124)

**Ruled ~18:25Z by the orchestrator session, now the reviewer:** W5's addendum is next, docs only, on this branch. W4's addendum
follows, after a read of whether the weather server already serves Q113's product. No code starts before either one. W5's inputs, W2
and W3, are both done. Q116's condition holds: blipscope-5c reads each push of the routes for conflicts with its own, before the push.

**What W5 is:** the three routes of section 1.3, with:
- their NWS fetches and the trim;
- the holds and the last-good copies;
- the breakers, `/healthz` and the metric labels;
- the tests and a sabotage driver.

It is merged, not deployed; W6 deploys it. **What W5 is not:** the radar's alias (W7), the device's parsers (W8), the CPU reading (W6's
staging reading), and any deploy.

#### S4.11.1 What main says, read at `e0941a90c4e4` before this was written

Each of these shapes something below.

1. **`apiEndpoint()` knows two prefixes,** `/api/v1/blipscope/` and the legacy `/v1/`. The auth gate keys on its answer, and
   `missileer.ts` says it must never be widened to `/api/v1/<any>/`. So Weatherscope gets one explicit third prefix, never a pattern.
2. **`wx::Observation` has `present[]`,** the raw METAR strings. `test_wx_feeds.cpp` reads it, and the band's `RAIN NOW` / `SNOW NOW`
   depends on it. `trim_nws.py` keeps no `presentWeather`, so the conditions body must carry what the stored fixture never did.
3. **`wx::Alert`'s `headline` has no reader,** and `trim_nws.py` drops it on purpose, because it names offices and areas. The wire carries
   none.
4. **`wxlogic::Warn` reads more than `wx::Alert` holds.** It reads:
   - `messageType` and `expiresIso`;
   - the polygon;
   - the motion and four hazard fields.

   `Counts()` needs `Covers(polygon, here)`. Only the four ranked warnings use any of these. All four are storm-based and always carry
   a polygon (screens doc, section 3). So those fields travel for those four events only.
5. **`AlertList` has no state for "unavailable".** The wire must carry one, and W8 gives the device one (section 1.4's alerts row).
6. **The radar route keys a point with `quantise()` in `wx/crop.ts`:** `Math.round(x * 100) / 100`, written with `toFixed(2)`, from
   the device's 2 dp. W5 keys a point with the same function.
7. **The breakers in `upstreams/types.ts`** are per isolate: 3 failures, a 30 s cooldown, and transitions logged as `evt:"breaker"`.
   W5 uses them and writes none of its own.

#### S4.11.2 The wire

**Every body starts** `{"v":1,"status":...,"fetchedUtc":...}`:

| status | means | data fields |
|---|---|---|
| `ok` | NWS answered 200 and it parsed. `fetchedUtc` is when | present |
| `stale` | NWS failed, or its breaker is open. This is the last good copy, with its own `fetchedUtc`. The device decides from its age (1.4) | the last good copy's |
| `unavailable` | NWS failed and there is no last good copy. Carries `reason`: `upstream_failed`, `breaker_open` or `too_large` | **none, not an empty list** |
| `out_of_coverage` | `/points` answered 404, so the point is outside NWS (Q114). It is an answer, not a failure: no breaker moves | none |

**Why `fetchedUtc` and not an age:** an age changes on every serve, which would change the ETag and defeat the 304. The device already
keeps NTP time, which `Ended()` needs anyway.

**Alerts** (`items`, in NWS's order):

| field | for | source |
|---|---|---|
| `event`, `startUtc`, `endUtc` | every alert | `startUtc` is `onset`, else `effective`. `endUtc` is `ends`, else `expires` (`trim_nws.py`'s rule) |
| `messageType`, `expiresUtc`, `expiresIso` | the four ranked warnings | `expiresIso` exactly as NWS writes it, with its own offset (Q7) |
| `polygon` | the four ranked warnings | the ring as `[lat, lon]` pairs, **whole** |
| `motion`, `maxHailSize`, `maxWindGust`, `tornadoDetection`, `tornadoDamageThreat`, `flashFloodDetection` | the four ranked warnings, each only when NWS sends it | `parameters`, first value, as NWS writes it |

No headline, area, zone, id, URL or sender, as `StoredFeeds.cpp` already rules for stored data. **A ring is never cut.** If an alerts body
would pass 32 KB, the answer is `unavailable` with `too_large`, never a shorter ring or a shorter list.

**Conditions:** `timeUtc`, then `present`: up to 4 entries, each NWS's `rawString`, in NWS's order. Then `tempF`, `windDirDeg`,
`windMph`, `gustMph` and `humidityPct`, each with its `has` flag.
- The numbers follow `trim_nws.py`: °C and km/h are converted with the rounding pinned as `floor(x + 0.5)`, and the wind direction
  is taken mod 360.
- **A `present` entry that isn't METAR-shaped is dropped, never cut.** It is drawn from a known set, so a cut entry would be a different
  entry (CLAUDE.md, "a truncated row is a lie").
- **The station:** the first in NWS's `observationStations` list for the point's grid, as the captures used. A stale observation goes
  out with its own time, and the device shows it in orange (A9.1 B). Q124 asks whether to try the next station instead.

**Forecast:** `issuedUtc` (NWS's `updateTime`), then `offsetMin`, taken from the first period's ISO offset. Then:
- `hourly`: up to 48 hours of `startUtc`, `tempF`, `hasTemp`, `pop` and `hasPop`;
- `periods`: up to 14, each with `name`, `startUtc`, `isDaytime`, `tempF`, `hasTemp`, `pop`, `hasPop`, and `short` cut to 24
  characters. `short` is free text, so a prefix is a true prefix.

These are `dallas-day.json`'s own `hourly` and `periods`. **The body is atomic:** both NWS documents fresh, else the last good copy,
else `unavailable`.

**Every body** stays under 32 KB, the device's `GetBytes` cap. It has an ETag on its bytes, answers 304 to a matching `If-None-Match`,
and is sent with `Cache-Control: no-cache`, as `/wx` is.

#### S4.11.3 Fetching, keys and holds

| kind | NWS call (W3's own) | keyed by | the Worker holds it | NWS's max-age, W3 |
|---|---|---|---|---|
| points | `/points/{lat2},{lon2}` | the quantised point | 30 days; a 404, 1 day | 6-21 h |
| alerts | `/alerts/active?point={lat2},{lon2}` | the quantised point | **60 s, the Worker's own choice** | 5 s |
| stations | the point's `observationStations` | the grid | 1 day | 9-24 h |
| observation | `/stations/{id}/observations/latest` | the station | 5 min | 1-5 min |
| forecast | the point's `forecast` and `forecastHourly` | the grid | 30 min | up to about 1 h |

- **Holds use the Cache API, per colo.** Each key also keeps a last good copy for 24 h, which serves `stale`.
- **Concurrent requests for one key in one isolate share one fetch.** The Cache API alone doesn't coalesce, so two boards asking in the
  same moment would otherwise make two calls.
- **Every call carries W3's User-Agent,** `Weatherscope (support@valarsystems.com)`. NWS refuses a call without one.
- **Nothing about the device reaches NWS.** The only request-derived value in any NWS URL is the quantised point: no id, no key, no
  model.

#### S4.11.4 Routes, auth, health and logs

- **The routes:** `GET /api/v1/weatherscope/{alerts,conditions,forecast}?lat=&lon=`.
  - They go through the same IP limit, auth and key limit as every API path.
  - The prefix is added to `apiEndpoint()` by name, and the edition is carried, so a Weatherscope suffix can only reach a Weatherscope
    handler.
  - `/v1/` stays Blipscope-only alias space.
- **Validation:** `lat` and `lon` are checked exactly as the radar route checks them (`400 bad_lat`, `400 bad_lon`), before any fetch.
- **Breakers:** `nws_points`, `nws_alerts`, `nws_stations`, `nws_obs` and `nws_forecast`.
  - A skip logs `evt:"upstream_skip"` with `reason:"breaker_open"`.
  - `/healthz` gains `weatherscope`, listing each breaker and its state, so a dead NWS turns this edition red rather than leaving it
    vouched for by the green beside it.
- **Metrics:** the three paths go into `KNOWN_ROUTES`, or they vanish into `/other`.
- **Logs** carry the kind, status and ms only. A grid or a station is a location to within a few km, so neither is logged, and no
  coordinate is either.

#### S4.11.5 The rows

All are host rows in `proxy/test/weatherscope.test.ts` under vitest, with NWS mocked and every fetch counted, except WS13's Python half.
Each row is checked against the whole value, never a substring.

| row | predicted |
|---|---|
| **WS1** | each route: no key gives 401; a valid key gives 200 against the mock |
| **WS2** | `/api/v1/weatherscope/blips` gives a JSON 404; `/api/v1/blipscope/alerts` gives 404; `/v1/alerts` gives 404; a POST to any of the three gives 405 |
| **WS3** | a bad `lat` or `lon` gives 400 and **0** fetches |
| **WS4** | `routeTemplate()` labels each of the three paths as itself |
| **WS5** | NWS answers 200 with `features: []`: `ok`, `items: []`. The all-clear is only ever an `ok` |
| **WS6** | for each route, NWS fails with no last good copy: `unavailable`, and **no data key at all**. NWS fails after a good fetch: `stale`, with the good copy's data and its `fetchedUtc` |
| **WS7** | a breaker open: **0** fetches, one `upstream_skip` per skip, the body as WS6 |
| **WS8** | `/points` answers 404 three times: `out_of_coverage` each time, `nws_points` still closed, and a 4th request inside the hold makes **0** fetches |
| **WS9** | two points in one grid make **1** forecast fetch, both one after the other and concurrently in one isolate |
| **WS10** | inside the hold a repeat makes 0 fetches; past it (clock advanced), 1 |
| **WS11** | every NWS request carries the User-Agent, and its URL holds no request-derived value but the quantised point |
| **WS12** | the alerts key's point equals `quantise()`'s `lat2` and `lon2` for the same query |
| **WS13** | **across languages:** the TS trim of the synthetic raw documents equals the committed golden JSON, and `trim_nws.py --check-golden` rebuilds the golden from the same documents and exits 0. The golden holds the halves that separate the languages: 2.5 °C gives **37** °F (Python's built-in `round` gives 36), -22.5 °C gives **-8** °F, and a direction that rounds to 360 gives **0** |
| **WS14** | `present`: METAR-shaped entries are kept in order, up to 4; a malformed one is dropped, and none is cut |
| **WS15** | the four ranked warnings carry their fields and their ring, whole; any other alert carries exactly `event`, `startUtc` and `endUtc`. No body anywhere has a key outside the S4.11.2 sets (key-set equality) |
| **WS16** | each route's largest synthetic case is under 32,768 B. An alerts case built to pass it answers `unavailable` with `too_large` |
| **WS17** | the same body gives the same ETag and a 304 with no body; the ETag doesn't move between two serves of one copy |
| **WS18** | `/healthz` lists the five breakers, all closed. After 3 alerts failures `nws_alerts` is open, and `evt:"breaker"` `open` is logged **once** across 4 requests |
| **WS19** | across the whole file, no captured log line holds the test's `lat` or `lon` string, its station or its grid. **CONTROL:** the mock does see the `lat` string in the NWS URL it received |
| **WS20** | `test/fixtures/wx/nws/w5/{alerts,conditions,forecast}.json` equal the TS trim's output for the synthetic case. W8's device parser test reads these same files, so its input comes from this side |

**The fixtures are synthetic.** No raw NWS document enters the repo, because raw captures carry coordinates.

#### S4.11.6 The sabotages, registered before code

The driver will be `proxy/scripts/sabotage-weatherscope.mjs`, on `sabotage-lbmodel.mjs`'s pattern:
- each edit is asserted to apply exactly once;
- the red set must equal the registered set;
- the file is restored from a byte copy and its sha256 checked;
- a crash is an instrument failure, never a red.

**Each sabotage must turn red at least the row named here.** The exact sets are frozen with the code's counts, before the driver first
runs, as S4.6 did for W2.

| sabotage | the plant | must turn red |
|---|---|---|
| **S-WS-EMPTY** | a failed fetch answered `ok` with `items: []` | WS6 |
| **S-WS-UA** | the alerts call without the User-Agent | WS11 |
| **S-WS-KEY** | the forecast keyed by the point, not the grid | WS9 |
| **S-WS-ROUND** | `Math.trunc` in the °F conversion | WS13 |
| **S-WS-PREFIX** | `apiEndpoint()` widened to `/api/v1/<any>/` | WS2 |
| **S-WS-404** | a `/points` 404 counted as a failure | WS8 |
| **S-WS-LOG** | the `lat` added to a log line | WS19 |
| **S-WS-CUT** | a malformed `present` entry cut to 4 characters, not dropped | WS14 |
| **S-WS-RING** | the polygon cut to 8 vertices | WS15 |
| **S-WS-AGE** | an age in the body in place of `fetchedUtc` | WS17 |
| **S-WS-PY** | `trim_nws.py`'s `r()` replaced by Python's `round` | WS13 (`--check-golden` exits 1) |

**Outside the set:** a row off, or a sabotage whose red set isn't its registered set, stops the step, and it is reported. Nothing is read
into it on the spot.

#### Q124. Which station the conditions route reads

| | option |
|---|---|
| **(a)** | the first in NWS's list for the grid, always. A quiet station shows its own old time in orange, as built (A9.1 B) |
| **(b)** | the first whose latest observation is under 90 minutes old, trying at most 3; otherwise the first, with its old time |

**Recommended: (a).** The captures and the screens were built on it. It is one fetch, and its failure is already drawn honestly. (b) is
up to three times the calls on the 10-minute route, for a case nobody has measured yet. W6's day of real calls can count how often the
first station is stale, and (b) can follow on that number.

#### Next, in order

1. This addendum, read by the reviewer, and by blipscope-5c for conflicts with its routes (Q116).
2. W4's pre-read: does the weather server already serve Q113's product?
3. W5's code on its own branch. Its counts and the exact sabotage sets are frozen before the suite first runs, and its CI is predicted
   before the push. blipscope-5c reads each push.

### S4.12, 2026-11-11: S4.11 ruled, Q124 (a); W4's addendum, and a price the owner ruled on without its largest line

**Ruled ~18:38Z by the orchestrator (the reviewer), on S4.11 at `aa9f997cd430`:**
- **S4.11 stands as written.**
- **Q124 is (a):** the conditions route always reads the first station in NWS's list.
- **One printed row is added to W6,** named here before W6 runs. It is printed, not graded:

  | row | printed |
  |---|---|
  | **W6-STN** | over every point W6's staging read covers: how many conditions answers came from a first station whose latest observation is older than 90 minutes, out of how many answers |

  If that is common, Q124 reopens with the number.
- **Q116's conflict read:** blipscope-5c reads S4.11's routes after its O11 window 2 closes. That read is a condition on W5's code.
- **W4 is next,** docs only. The products.ts change is coordinated with uk-data before it is registered, and Q113's condition stands:
  blipscope-5c reads the SeamlessHSR server work.

**Ruled ~18:46Z, after uk-data's answers:**
- **The overlap:** W4's health change and uk-data's `ROUTED` additions touch the same lines (`products.ts` 96-100, `index.ts` 92,
  `wx-region.test.ts` 172-175).
  - Whichever is approved first merges first.
  - The second rebases, and re-reads its own predictions against the new main before asking for approval.
  - Each PR updates the pinned `ROUTED` test itself.
  - The order is agreed with uk-data.
- **No second literal list:** the health set is derived from `PRODUCTS` and `ROUTED`. uk-data's health change is ruled the same way.
- **`shsr`'s cost is the owner's gate.** It is priced below with `tools/wx/account_cost_read.py`'s model, and goes to the owner before any
  `shsr` deploy. Nothing registered here assumes his yes.

#### S4.12.1 W4's pre-read: the weather server does not serve `shsr`

Read from main at `e0941a90c4e4`:
- **`PRODUCTS`** is `cref` plus the ten OPERA products. **`ROUTED`** is `{cref, opuk}`.
- **No `shsr` anywhere:** nothing under `proxy/` names it, and no branch is named for it. The two open PRs that mention it, #451 and
  #437, are docs.
- **#635 to #637 merged** as docs. None touches `products.ts` or the region table.

**Four facts that shape W4, the last three confirmed by uk-data's own read:**
1. **`products.ts`'s header** says a second product, "Weatherscope's, say", is "a row here plus its code mapping and nothing else".
2. **The look the owner accepted can be served as encoded.** The stored loop (`test/fixtures/wx/loop/`, 15 SeamlessHSR frames) was encoded
   by the proxy's own `mrms.ts` and `crop.ts` at `2484bda4b108`.
   - Neither file has changed since.
   - The loop tools call `codeOf(v)` with no missing value, which is the same function as `codeOfComposite(v)`.
3. **No running ingest changes when the row lands.** Every `checkAll` caller passes an explicit list: cref's Worker `CREF_WORKER`, and
   each OPERA Worker its own product. `operaProduct()` rejects a non-OPERA product, so an OPERA Worker can't host `shsr`.
4. **The region generator reads only `source === "opera"` rows,** and indexes them among themselves. An MRMS row leaves the region table
   byte-identical wherever it sits, so a sabotage that moves it could never go red, and none is registered. It is appended after the
   OPERA rows anyway.

#### S4.12.2 The price, and my error in Q113's

**My error.** Q113 put `shsr` at **$0.47-$0.57 a month**. That figure is R2 Class A alone: 929,006 operations going to about 1.06 M,
past the free million. **It left out the ingest's CPU,** the largest line. cref's ingest is **27.9 M of the account's 39.9 M CPU ms a
month, 70 %** (`docs/wx-account-cost.md`, A2), and `shsr` is a second CONUS MRMS ingest of the same grid and cadence. The owner ruled
Q113 (a) on the smaller number.

**The price, with the model's own `line_cost()` and `PRICE`,** on the recorded read (A1-A8) and the model's `TEN`:
- **The estimate:** one cron a minute (43,200 requests), and CPU 0.8 to 1.2 times cref's measured 27.9 M ms.
- **Class A:** from uk-data's 43 k (a frame and `latest` per frame) up to 0.140 M. That upper bound is `blipscope-wx`'s whole bucket,
  cref's frames included.
- **Logs:** 3.00 events an invocation (A8).

| the account | Read P (proportional) | Read A (R2 billed in whole millions) |
|---|---|---|
| **with the ten OPERA ingests, as today** | **+$0.64 to +$1.30 a month**: CPU $0.45-$0.67, Class A $0.19-$0.63 | **+$0.45 to +$0.67**: the ten already took Class A past 1 M, and `shsr` stays inside the second million |
| without the ten, for comparison | +$0.45 to +$0.68 | +$0.45 to +$0.68, or **+$5.17** if Class A reaches the upper bound and crosses 1 M |

**Against the account's bill.** At today's 17 boards with the ten, with no Weatherscope board yet:
- **Read P:** $6.26-$6.74, which goes to about **$6.90-$8.04**.
- **Read A:** $10.88-$11.14, which goes to about **$11.33-$11.81**.

Requests, Logs and storage add nothing either way: each stays inside its included amount.

**This is an estimate, not a reading.** CPU is the largest line, and nothing has measured an SeamlessHSR ingest. X10 below reads it after
a deploy, if the owner allows one. uk-data's S3-7 re-reads the account tomorrow, and this price is re-done on that read before it goes to
the owner.

#### S4.12.3 The change, written to wait for the owner

**W4's code and its deploy both wait on the owner's answer to the price.** Nothing below is built before it.

| piece | what |
|---|---|
| **the row** | `shsr`: `source: "mrms"`, `noaaPrefix: "CONUS/SeamlessHSR_00.00"`, `noaaStem: "MRMS_SeamlessHSR_00.00"`, `cadenceS: 120`, `maxAgeS: 600`, `codeOf: codeOfComposite`, no `deviceMaxAgeS`. Appended after the ten OPERA rows |
| **serving** | `/wx/shsr` is served by `shsr` itself, since `servedProduct()` dispatches only `cref`. **Not in `ROUTED`** |
| **health** | **No health code in W4.** uk-data's ruled change, which goes first (S4.12.6), makes `/healthz` cover every `PRODUCTS` row, marked routed true or false, with its frame's age and a stale flag, derived from `PRODUCTS` and `ROUTED`. The `shsr` row appears there with no further code |
| **metrics** | `/api/v1/blipscope/wx/shsr` goes into `KNOWN_ROUTES`. Weatherscope's own alias is W7's |
| **the ingest** | an `[env.shsr]` in `wx-ingest.wrangler.toml`, so `ingest_deploy.sh` needs no change. Its Worker is `blipscope-wx-ingest-shsr`, its claim `wx-ingest-shsr:production`. `main` points at a new MRMS entry that takes its product from a `PRODUCT` var and refuses anything but an MRMS row, with `WX_INGEST = "off"` until switched. An `[env.shsr-inert]` (same name, ingest off) is its first stop, because a new Worker has nothing to roll back to. **cref's top-level Worker is untouched.** A deploy that forgets `--env` aims at `wx-ingest:production`, which the claim doesn't cover, so it is refused |
| **the read-back** | `r3p.py` keys its minute statistics on `cref`, and #641's S3 tools are OPERA-shaped. W4 parametrises the first and reuses none of the second |

**Why `cref`'s timing values:** SeamlessHSR publishes every 2 minutes, like the composite. The device judges a radar frame stale at 30
minutes on its own (1.4).

#### S4.12.4 The rows

Host rows run under vitest, in `proxy/test/wx-shsr.test.ts` and the existing `wx` tests.

| row | predicted |
|---|---|
| **X1** | `productById("shsr")` returns the row. `cref` and the ten OPERA rows keep their positions and values |
| **X2** | `gen-wx-region.mjs --check` exits 0, with the region table byte-identical; G1 holds |
| **X3** | `codeOfComposite(v)` equals `mrms.ts`'s `codeOf(v)` over a sweep: -999, -99, -0.5, 0, 14.999, 15, 19.999, 20, 79.999, 80, 200 |
| **X4** | `servedProduct(shsr, p)` is `shsr` at every test point, and every existing region and dispatch test passes unchanged |
| **X5** | `/healthz` lists `shsr` as routed false, in uk-data's shape. With no frame its age reads null, never a made-up age. `ROUTED` is exactly what main has when W4 rebases |
| **X6** | `routeTemplate("/api/v1/blipscope/wx/shsr")` is itself |
| **X7** | the MRMS entry with `PRODUCT=shsr` checks exactly `[shsr]`, and refuses an OPERA id. `CREF_WORKER` is still exactly `[cref]` (FT1). With `WX_INGEST` not `"on"`, it logs `wx_ingest_off` and makes **0** fetches |
| **X8** | the NOAA URL `ingestOnce` builds for `shsr` comes from the row alone, and no SeamlessHSR literal exists in `proxy/src` outside `products.ts` |
| **X11** | the tests that pin `PRODUCTS` move with it, and each still goes red under its own sabotage: DP2's id list gains `shsr`, and DP2s is still red; PS1 reads 12; FX finds every id checked by exactly one Worker, `shsr` by the `[env.shsr]` entry, with FX's own CONTROL still passing |

**One reading before the PR, not in CI: X9, the look.** On W4's code, the 15 stored loop frames are re-encoded through the `shsr` row from
their NOAA archive keys, each key checked against the manifest's `gribSha256` first. All 15 must give the manifest's crop `sha256`
exactly. A key whose archive copy no longer matches is the instrument, not a reading.

**After a deploy, printed, not graded: X10, the cost.** Over the first full day:
- the `shsr` Worker's CPU ms per invocation, against cref's 645;
- its Class A operations, against the 43 k-140 k a month assumed above.

If either lands outside S4.12.2's range, the price is re-done and goes back to the owner.

#### S4.12.5 The sabotages

Registered before code, on `sabotage-lbmodel.mjs`'s pattern. Each must turn red at least the row named. The exact sets are frozen with the
code's counts, before the driver first runs.

| sabotage | the plant | must turn red |
|---|---|---|
| **S-X-CODE** | the row's `codeOf` sends -99 to 15, as `codeOf(v, -99)` would | X3 |
| **S-X-HEALTH** | the health set built from `ROUTED` alone, planted in the code uk-data's change writes | X5 |
| **S-X-CREF** | `CREF_WORKER` widened to `cref` and `shsr` | X7 (and FT1) |
| **S-X-SWITCH** | the MRMS entry ignores `WX_INGEST` | X7 |
| **S-X-LITERAL** | the SeamlessHSR stem written into `ingest.ts` | X8 |

**Outside the set:** a row off, or a sabotage whose red set isn't its registered set, stops the step, and it is reported.

#### S4.12.6 The order with uk-data, agreed ~18:55Z

1. **uk-data's `/healthz` change merges first.** It touches `products.ts`'s comment at 96-100, `index.ts` 92, `handler.ts`'s `wxHealth`
   and the tests pinning `/healthz`'s `wx` shape. It does not change `ROUTED`.
2. **W4 second.** It rebases on main after that merges, and re-reads these predictions against the new main before asking for approval.
   It updates the tests that pin `PRODUCTS` to eleven (DP2, PS1, FX) itself.
3. **If W4 were approved first,** it would go first, as ruled, and uk-data would rebase. That is unlikely, because W4's code waits on the
   owner's answer to the price.

#### Next

1. **The order with uk-data** is agreed (S4.12.6), and goes to the reviewer.
2. **The price, re-done on S3-7's read tomorrow, goes to the owner** through the reviewer. W4's code and deploy wait for his answer.
3. **After blipscope-5c's O11 window 2 closes,** it gets S4.11's conflict read (a condition on W5's code) and this addendum's (Q113).
4. **W5's code** once blipscope-5c's read is back. W5 doesn't depend on W4.

### S4.13, 2026-11-11: blipscope-5c's conflict reads, and three rows added to W5 before its code

**blipscope-5c read S4.11 (W5) and S4.12 (W4) after O11 window 2 closed** (~19:05Z), against main and its own work. Main had moved to
`78c2bec21dfa` by one docs commit (#640), with nothing under `proxy/`, so both addenda's reads stand.

**Its finding: no conflict blocks either step.** It has no work in flight in `proxy/src`, `index.ts`, `metrics.ts`, the `wx` code or the
ingest toml. Its next proxy work, a bill proposal, comes after v19 and rebases on whatever has merged.

**For W4:**
- **No conflict.** The agreed order, uk-data's `/healthz` change (#643) first, matches its own read of #643.
- **No firmware change, and no released image asks for `/wx/shsr`,** so W4 doesn't touch v19.
- **The Logs line in S4.12.2's price assumes no sampling.** If blipscope-5c's sampling proposal is ever ruled, that line shrinks, so the
  estimate stays the conservative one.

**For W5, three constraints, each made a row here before any code:**

| row | predicted | why |
|---|---|---|
| **WS18b** | `/healthz`'s body holds exactly **one** match of `"commit":"[^"]*"`, the same pattern `confirm-deploy.sh` (line 74) and `smoke-prod.sh` (line 259) use, and the `weatherscope` field has no key named `commit` | both scripts read the deploy stamp from the whole body. A second match makes `LIVE` two lines, and every deploy then reads "wrong sha" |
| **WS21** | W5's diff **adds** files under `proxy/test/` and modifies none: `git diff --name-status` against W5's base lists only `A` there. The existing tests, the alias-contract tests across ten files among them, all pass unchanged | `index.ts` promises one call site per handler and byte-identical bodies on both prefixes. If an existing test has to change, that change is the conflict, and it is named and shown to blipscope-5c before the push |
| **WS22** | a request to a Weatherscope route carrying `X-Blip-Boot`, with `X-Blip-Model: weatherscope-s3-128`, writes one boot row carrying that model, just as a radar request does | see below |

**The shared block, stated as blipscope-5c asked.** `recordFleetFirmware`, `recordOtaMem`, `recordUsage` and `recordBoot` run on every
authenticated request, before dispatch, so Weatherscope's routes pass through them too.
- **What changes:** a Weatherscope board's calls write firmware, boot, OTA and usage rows, carrying the model `weatherscope-s3-128`.
- **Why that's wanted:** it closes A3.2's blind spot for this edition (section 1.6). A refused update on a Weatherscope would otherwise
  show only on serial.
- **What readers will see:** the fleet readers (`reconcile-fleet.py`, the OTA-result reader) see `weatherscope-s3-128` rows beside the
  radar's. W2's contract (S4.1.2) says `reconcile-fleet.py` counts a board once whatever its model reads. Whether that is implemented
  is not checked here, and W5's doc says so, so nobody reads it as established.

**One sabotage is added:**

| sabotage | the plant | must turn red |
|---|---|---|
| **S-WS-COMMIT** | a `commit` key inside `/healthz`'s `weatherscope` field | WS18b |

**blipscope-5c's question, answered from main.** Does a Weatherscope board's model pass the activation gate's model list? **There is no
model list.**
- Activation is keyed by device id alone (`act:<id>`, `activation.ts`). It is consulted at enrolment through `claimActivation`, in
  `enroll.ts`.
- No gate on the authenticated path refuses on `X-Blip-Model`. `config.ts` reads it only to choose `/config`'s defaults.
- So a Weatherscope board passes or fails activation by its id on the batch list, like any unit (section 1.5). WS1's "a valid key gives
  200" is a key the test mints, and activation is not on that path.

**Its sampling note, recorded as its own to resolve:** Logs head sampling at 0.1 would drop transition-only lines like `evt:"breaker"`
and `evt:"upstream_skip"` 9 times in 10. WS18 and its own route breakers rely on those lines. Its proposal has to keep them before it
can be ruled, and nothing changes in W5.

**How W5's pushes are read (Q116):** at every push, blipscope-5c gets the diff of the shared files:
- `index.ts`: `apiEndpoint`, the dispatch, `handleHealth`;
- `metrics.ts`: `KNOWN_ROUTES`.

It reads the new files under `proxy/src/weatherscope/` too.

**Next: W5's code.** Its condition, blipscope-5c's read, is back. Before the suite first runs, the counts and the exact sabotage sets
are frozen in an addendum of their own, as S4.6 did. Its CI is predicted before the first push.

### S4.14, 2026-11-11: WS18b re-scoped before any code

**Ruled 19:13Z by the orchestrator, on S4.13 at `c5ee18f84202`.** WS21, WS22 and the activation answer stand. **WS18b is re-scoped,** and
this commit freezes the rewrite before W5's code.

**Why.** S4.13's WS18b said `/healthz`'s whole body holds exactly one `"commit"` match. blipscope-5c is changing `confirm-deploy.sh` and
`smoke-prod.sh` to read only the top-level `commit` key, because basemap's block carries its own `commit` once the builder answers. After
that change, a second `"commit"` in the body is legitimate. An exactly-one row would then:
- go red on basemap, for a reason that isn't W5's;
- or freeze the old grep's assumption into a test, the very hazard being removed.

**WS18b, as it now stands** (it replaces S4.13's):

| row | predicted |
|---|---|
| **WS18b** | the `weatherscope` field, and anything else W5 adds to `/healthz`, holds **no key named `commit` at any depth.** It is checked by a recursive walk of that subtree's keys, not by a pattern over the body |

**Its sabotage is unchanged:** S-WS-COMMIT plants a `commit` key inside the `weatherscope` field, and must turn WS18b red.

**Next: W5's code,** on its own branch from main `78c2bec21dfa`. Its counts and the exact sabotage sets are frozen before the suite first
runs, and its CI is predicted before the first push. Nothing deploys: W5's deploys get their own slot later.

### S4.15, 2026-11-11: W5's code written; its counts and red sets frozen before the suite first runs

**Ruled 19:13Z: W5's code goes ahead,** on `weatherscope/w5-feeds` from main `78c2bec21dfa`. Nothing deploys. **The suite has not run yet.**
The test file was type-checked only (`npm run typecheck` and `tsconfig.scripts.json`, both exit 0), and the driver's `--anchors` mode
found each of its 12 plants exactly once. **A whole-suite run waits for uk-data's controls** (orchestrator, ~19:17Z: one at a time,
TIME_WAIT under 2,000). Single-file runs may go ahead.

#### S4.15.1 What was written

| file | what |
|---|---|
| `proxy/src/weatherscope/trim.ts` | the pure trim: forecast, conditions and alerts. Every number shared with `trim_nws.py` is computed as that script computes it |
| `proxy/src/weatherscope/fetch.ts` | one NWS call shape (W3's User-Agent, an 8 s timeout, NWS's own origin only), the hold-and-last-good store, coalescing per isolate, the five breakers |
| `proxy/src/weatherscope/handler.ts` | the three routes, the status rules, the 32 KB cap, the ETag and 304, and `/healthz`'s `weatherscope` field |
| `proxy/src/index.ts` | `apiEndpoint()` gains the named prefix and an `edition`; the leaderboard POST check requires the Blipscope edition; one dispatch line ahead of Blipscope's unchanged table; one `/healthz` field |
| `proxy/src/metrics.ts` | the three paths in `KNOWN_ROUTES` |
| `proxy/test/weatherscope.test.ts` | the rows. **New. No existing test file is touched (WS21)** |
| `proxy/scripts/sabotage-weatherscope.mjs` | the driver, with `--anchors` and a TIME_WAIT gate under 8,000 before each single-file run |
| `proxy/scripts/gen-w5-bodies.mts` | writes WS20's three body fixtures, with `--check` |
| `tools/weatherscope/nws/trim_nws.py` | gains `--check-golden` and `--write-golden` |
| `test/fixtures/wx/nws/w5-raw/{warm,cold}/` | the SYNTHETIC NWS documents, written by hand. Every location is at 2 dp near KMCI, as the `/wx` tests use |
| `test/fixtures/wx/nws/w5-golden/`, `test/fixtures/wx/nws/w5/` | the golden from `trim_nws.py`, and the route's three bodies |
| `.github/workflows/workers.yml` | a `trim_nws.py --check-golden` step for the proxy worker; `tools/weatherscope/nws/**` and `test/fixtures/wx/nws/**` added to both path filters. Without them, a change to `trim_nws.py` alone would never run the golden |

#### S4.15.2 Decisions made in code that S4.11 didn't state, recorded before any run

1. **`unavailable`'s `fetchedUtc` is the time of the attempt.** It has no data, so it has no fetch time of its own.
2. **An empty station list is an upstream failure,** counted against `nws_stations` and answered `unavailable`, `upstream_failed`. S4.11's
   reasons have no `no_station`, and I didn't add one.
3. **A ranked warning whose geometry isn't a `Polygon` goes out without `polygon`.** No ring is invented. It stays in the list, and
   `Covers()` can't call it "here".
4. **A URL taken from `/points` must be on `api.weather.gov`,** or the answer is refused as malformed before any socket opens.
5. **One log line per NWS call:** `{"evt":"nws","kind","status","ms"}`, nothing more.
6. **WS2 asserts one case more than S4.11 froze:** a POST to `/api/v1/weatherscope/leaderboard` is a 405. It holds the leaderboard's
   edition check.
7. **The warm fixture's ring has 10 vertices.** Predicting S-WS-RING's set showed a 5-vertex ring could not be cut to 8, so that plant
   could never go red. The golden is unaffected, because `trim_nws.py` keeps no polygon.
8. **Each test checks only the mocks it registered itself,** so a sabotaged test that leaves mocks behind is red alone. Without that,
   the leftovers would turn every later test red.
9. **Halves in the golden that Python's built-in `round` would get wrong:**
   - 12.5 % → **13**, and 0.5 % → **1**;
   - 2.5 % → **3**;
   - 36.5 °F → **37**;
   - 180.5° → **181**;
   - and -0.6 °F truncated to **0**, never -0.

#### S4.15.3 Predicted

| read | predicted |
|---|---|
| `npx vitest run test/weatherscope.test.ts` | **22 passed (22)**, 0 failed |
| `python -I tools/weatherscope/nws/trim_nws.py --check-golden` | exit 0, `golden: 2 cases, 0 differ` |
| `trim_nws.py --selftest` | `6 checks, 0 failures` |
| `npx tsx scripts/gen-w5-bodies.mts --check` | exit 0, `w5 bodies: 3 files, 0 differ` |
| **WS21:** `git diff --name-status 78c2bec21dfa -- proxy/test` | exactly one line, `A	proxy/test/weatherscope.test.ts` |
| **the whole proxy suite**, when the slot frees | **Test Files 75 passed, 1 skipped (76); Tests 1061 passed, 1 skipped (1062).** That is main's 74/1 (75) and 1039/1 (1040), read from main's last `workers.yml` run (`d46c25c43244`, with no `proxy/` commit since), plus this one file of 22 |
| **`sabotage-basemap.mjs S-MKAE`**, the one existing plant in a file W5 changed (`metrics.ts`), with the whole suite | as registered, red `{MK7}`. Its anchor is found once in W5's `metrics.ts` |

**The sabotage driver, `node scripts/sabotage-weatherscope.mjs`:** CONTROL 22 of 22 and the golden exit 0, then each set exactly:

| plant | red, exactly |
|---|---|
| S-WS-EMPTY | WS6, WS7 |
| S-WS-UA | WS11 |
| S-WS-KEY | WS9 |
| S-WS-ROUND | WS13, WS20 |
| S-WS-PREFIX | every row with a route request: WS1, WS2, WS3, WS5-WS12, WS14-WS20, WS22. Not WS4, WS13 or WS18b |
| S-WS-404 | WS8 |
| S-WS-LOG | WS19 |
| S-WS-CUT | WS14 |
| S-WS-RING | WS15, WS16, WS20 |
| S-WS-AGE | WS5, WS6, WS7, WS8, WS15, WS17, WS20 |
| S-WS-COMMIT | WS18b |
| S-WS-PY | `--check-golden` exit 1, `golden: 2 cases, 2 differ` |

**Outside the set:** a count off, a red set off, or an instrument failure stops the step, and it is reported. A crash is an instrument
failure, never a red. Nothing is pushed until every row above reads as predicted, or is ruled.

**Next:**
1. The single-file run and the driver, now.
2. The whole suite and S-MKAE when uk-data says done.
3. The CI prediction, then the push to a PR, with the shared-file diff sent to blipscope-5c first (Q116).

### S4.16, 2026-11-11: two decisions ruled, WS23 added, and W5's CI predicted before the push

**What has run so far,** on W5's local commit `4490d9fb`, after S4.15 was pushed at 19:33:46Z. All read as S4.15 predicted:
- `vitest run test/weatherscope.test.ts`: **22 passed (22)**.
- The golden: 0 differ. The selftest: 6 checks, 0 failures. The bodies check: 0 differ.
- **WS21:** exactly one line under `proxy/test`, `A proxy/test/weatherscope.test.ts`.

**Still held for uk-data's "done":**
- the sabotage driver;
- the whole suite;
- `S-MKAE`.

**Ruled by the orchestrator, on S4.15's two flagged decisions:**
1. **An empty NWS station list answered `unavailable` / `upstream_failed`, with no new reason, is accepted,** provided a row pins that exact
   body. No row did, so **WS23** is added below, before it runs.
2. **The `workers.yml` change is accepted:** the golden step, and `tools/weatherscope/nws/**` and `test/fixtures/wx/nws/**` in both path
   filters.
   - It goes to blipscope-5c by name, in the shared-file diff.
   - The CI prediction counts the workflow's own change, derived from the filters as they stand at W5's head.
   - It adds a step, not a job. But the filters change which PRs run `workers.yml` at all, so once it merges, the orchestrator tells every
     session that baselines for PRs touching those paths move.

**WS23, and why it asserts two things.** Any NWS failure on the conditions route ends as `unavailable` / `upstream_failed`. So the body
alone can't tell an honest empty list from one papered over with a made-up station: that station's observation would fail too, and give
the same body. **What only the honest version has is that no observation is asked for.**

| row | predicted |
|---|---|
| **WS23** | `/points` and a station list with `features: []`: the body is exactly `{v: 1, status: "unavailable", fetchedUtc: T0, reason: "upstream_failed"}`, and **0** calls reach `/observations/` |

| sabotage | the plant | red, exactly |
|---|---|---|
| **S-WS-NOSTATION** | an empty station list answered with a made-up station, `KZZZ` | WS23 |

**Two of S4.15's frozen sets grow by WS23,** each because of what WS23 is, re-derived before any run:
- **S-WS-PREFIX:** WS23 makes a route request. Its set is now every route-request row: WS1, WS2, WS3, WS5-WS12, WS14-WS20, WS22, WS23.
- **S-WS-AGE:** `unavailable()` builds its head with `fetchedUtc`, and WS23 compares the whole body. Its set is now WS5, WS6, WS7, WS8,
  WS15, WS17, WS20, WS23.
- **The others are unchanged:**
  - S-WS-EMPTY plants in `bodyOf()`, which the station path doesn't use.
  - S-WS-LOG is read only by WS19, which still runs last.
  - The rest touch no code WS23 reaches.

**The counts, superseding S4.15's:**

| read | predicted |
|---|---|
| `vitest run test/weatherscope.test.ts` | **23 passed (23)** |
| the whole proxy suite | **Test Files 75 passed, 1 skipped (76); Tests 1062 passed, 1 skipped (1063)** |
| the driver | CONTROL 23 of 23 and the golden exit 0; then 13 plants, each with its set as above |

#### W5's CI, predicted before the push

**How it's derived:**

1. **The three workflows that run on every PR** (`device-ids`, `firmware`, `no-traffic-captures`). Read from #643's checks at head
   `f34b082d90f4`:
   - #643 is docs-only, on the same base as W5 (`78c2bec21dfa`). That base is after main's last workflow change, `e2c34daf`, which
     added steps to `device-ids.yml` and no job.
   - The read: **32 = 29 success / 3 skipped** (firmware's `version`, `promote` and `ship-alert`) **/ 0 failed**. That is device-ids 1,
     firmware 30, no-traffic-captures 1.
2. **`workers.yml`, from its filters as they stand at W5's head:**
   - Its `pull_request` paths include `proxy/**`, its own file, and now `tools/weatherscope/nws/**` and `test/fixtures/wx/nws/**`. W5
     touches all four, so **the filter change doesn't decide whether it runs on this PR.** `proxy/**` alone would trigger it.
   - It has 3 jobs, `manifest-drift` and `test` over `[proxy, dashboard]`, none with a job-level condition, so all 3 run.
3. **`web-renderer`, `print-cards` and `provisioner` don't run:** W5 touches none of their paths. `photos`, `photo-drift` and
   `refresh-data` have no `pull_request` trigger.

| run | total | success | skipped | failed |
|---|---|---|---|---|
| **the PR** | **35** | 32 | 3: firmware's `version`, `promote`, `ship-alert` | 0 |
| **main on the squash** (push) | **35** | 33 | 2: firmware's `version`, `promote` | 0 |

The main row takes firmware's push shape from #630's main run (30 = 28 success / 2 skipped; `ship-alert` runs on a push), drops
web-renderer (no `src/` path), and adds `workers.yml`'s 3. Push events only; scheduled runs are not counted.

**Outside the set:** any count or set off stops the step, and it is reported. Nothing is improvised.

### S4.17, 2026-11-11: #649 brought up to main after #643, both changes kept on index.ts:92; predictions before the push

**What #649's first CI read, and why it no longer decides anything.** At head `1e7502e9096c`, read twice at 23:24:29Z and 23:25:35Z:
**35 = 32 / 3 / 0**, as S4.16 predicted.
- **It tested an older main.** The tested merge, read from every job's log, was `6a862257c4bf`: `c2cf7cfcdd99` plus the head, built at the
  original push, 20:32:34Z.
  - The reopened runs reused it, although `refs/pull/649/merge` had since been rebuilt on `bb6f7994e5c5`.
  - I first reported the ref as what was tested, and corrected that from the logs.
- **#643 then merged** at 23:21:56Z (main `03632992a132`), and #649 conflicted with it on `proxy/src/index.ts`. That is the overlap
  recorded at 20:3xZ: whichever was approved first merged first, and this PR keeps both.

**The CI stall that came before it,** on record:
- #649's first four runs (43096672215, 43096672172, 43096672157, 43096672156) sat queued with 0 jobs from 20:32:37Z.
- Under the owner's stalled-run OK, the cancel, the force-cancel and (a) a re-run were each refused.
- (b), closing and reopening the PR at 22:53:45Z and 22:53:52Z, gave runs with jobs.
- The four are set aside by id as zero-job GitHub leftovers.

#### The merge

**`db55a49f195c`**, a merge of main into the branch. Parents: `1e7502e9096c` (the PR's head) and `03632992a132`
(main, #643). Tree: `7588ee11a3c2`.

**The resolution, the one conflicting hunk.** It keeps #643's argument change and W5's key:

```
  return jsonResponse({ ok: true, commit: BUILD_COMMIT, upstreams: feedHealth(env), wx: await wxHealth(env, PRODUCTS, ROUTED), basemap: basemapHealth(env),
                        weatherscope: weatherscopeHealth() });
```

- Against main, `index.ts` now differs by W5's own changes alone.
- #643's comment above the line auto-merged unchanged. It forbids a `commit` key inside `wx`, and W5's own field holds none (WS18b).
- **`workers.yml` auto-merged.** It carries W5's two path-filter entries and its golden step, and #643's new step
  (`wx-products-lines.selftest.sh`).
- **#643 changed no path filter and added no job,** so the `workers` job set doesn't move: 3 jobs.

**Read before this was written** (reads, not predictions):
- W5's driver, `--anchors`: 13 plants, each found exactly once on the merged tree.
- #643's `sabotage-wx-healthz.py`, its three anchors: each found exactly once. Its H1s anchor sits on the very line resolved above, and
  survives as a prefix.

#### Predicted, before any run on the merge

| read | predicted |
|---|---|
| `vitest run test/weatherscope.test.ts` | **23 passed (23)** |
| the whole proxy suite, behind the gate | **Test Files 75 passed, 1 skipped (76); Tests 1064 passed, 1 skipped (1065).** That is main's 74/1 (75) and 1041/1 (1042), read from main's own push run at `03632992` (43119262759, 23:29:09Z), plus W5's one file of 23 |
| `python scripts/sabotage-wx-healthz.py`, #643's driver, which plants in `index.ts` | exit 0, every sabotage as it registered |
| `trim_nws.py --check-golden`; `gen-w5-bodies.mts --check` | 0 differ; 0 differ |
| **WS21**, against main `03632992` | still exactly one line under `proxy/test`, `A proxy/test/weatherscope.test.ts` |

**W5's own driver is not re-run.** The only code it plants in that the merge changed is `index.ts`, and S-WS-PREFIX's anchor line there is
unchanged. The reviewer can ask for the re-run.

**CI at the new head:**
- **How it's derived:** the PR's diff against main `03632992` covers the same areas as before (`.github/workflows`, `proxy/`,
  `test/fixtures`, `tools/weatherscope`). So the same four workflows run: `device-ids`, `firmware` and `no-traffic-captures` on every PR,
  and `workers` through `proxy/**`.
- **No job-set change on main since `78c2bec21dfa`:** #643 added a step to `workers.yml`, and `e2c34daf` added steps to `device-ids.yml`.

| run | total | success | skipped | failed |
|---|---|---|---|---|
| **the PR, at `db55a49f195c`** | **35** | 32 | 3: firmware's `version`, `promote`, `ship-alert` | 0 |
| **main on the squash** (push) | **35** | 33 | 2: firmware's `version`, `promote` | 0 |

**The tested merge will be named from a job's log, not from the ref:** `HEAD is now at …`, its parents read through the API. Its first
parent is predicted to be main as it stands at the push. If it isn't, that is reported.

**Outside the set:** any count off, a set off, or a tested merge on any other main stops the step, and it is reported.

### S4.18, 2026-11-11: main merged in again (#650), both plant drivers re-predicted, and the rule for reading what CI tested

**The cloud reviewer, via the owner (23:32Z), relayed by the orchestrator at 23:45Z:**
- The resolution of `/healthz`'s line is main's `wx` argument with #649's key added: the one S4.17 made.
- **`wx: await wxHealth(env, PRODUCTS, ROUTED), basemap` stays together on one line,** because #643's H1s anchors on exactly that text, and CI
  never runs that driver.
- Both plant drivers go into the re-prediction and run on the merged tree.
- **A rule for every session, from my correction in S4.17:** the commit a run tested is read from its checkout log (the sha and its
  parents), never from `refs/pull/N/merge`, which GitHub recomputes afterwards.

**S4.17's predictions, read on the intermediate merge `db55a49f195c`, after S4.17 was pushed at 23:32:34Z:**
- `test/weatherscope.test.ts`: **23 passed (23)**.
- The golden and the bodies: 0 differ each.
- WS21 against `03632992`: one `A` line.
- `sabotage-wx-healthz.py`: unsabotaged `([], 17)`; H1s, H2s and H3s each read their predicted set. **ALL AS PREDICTED**, exit 0.
- **The whole suite never ran on that tree.** Its gate stayed shut from 23:33:02Z, because another session's vitest was running. It was
  stopped at 23:43Z once main had moved, and its orphaned wait process was ended by pid. That tree is not pushed.

#### The merge at the head

**`978856bc61f9`:** main `337732beee0e` (#650) merged into `db55a49f195c`, with no conflict. #650's files (a doc, `src/AircraftManager.h` and
three under `test/host`) share none with W5.

**The check the reviewer asked for, `git diff 337732be..978856bc` against #649's original diff (`78c2bec21dfa..1e7502e9096c`):**
- the same 25 files;
- and the added and removed lines are identical in every file **except one hunk**: the `/healthz` line in `proxy/src/index.ts`.
  - **Removed:** the line now carries main's `wxHealth(env, PRODUCTS, ROUTED)` where it carried `PRODUCTS.filter(...)`.
  - **Added:** W5's continuation, `weatherscope: weatherscopeHealth() });`, is byte-identical in both.

**`workers.yml`** keeps:
- #643's `wx-products-lines.selftest.sh` step;
- W5's two path-filter entries;
- W5's golden step.

#### Predicted at `978856bc61f9`, before any run on it

| read | predicted |
|---|---|
| `vitest run test/weatherscope.test.ts` | **23 passed (23)** |
| `sabotage-weatherscope.mjs` (W5's, plants into `src/index.ts` among others; not in CI) | CONTROL **23/23** with the golden exit 0; **all 13 as registered**, with S4.16's sets; the tree clean after |
| `sabotage-wx-healthz.py` (#643's, plants into `src/index.ts`; not in CI) | unsabotaged `([], 17)`; **H1s, H2s and H3s each as registered**; exit 0 |
| `trim_nws.py --check-golden`; `gen-w5-bodies.mts --check` | 0 differ; 0 differ |
| **WS21** against `337732be` | one line under `proxy/test`, `A proxy/test/weatherscope.test.ts` |
| the whole proxy suite, behind the gate | **Test Files 75 passed, 1 skipped (76); Tests 1064 passed, 1 skipped (1065).** #650 touched no `proxy/` file, so main's proxy suite at `337732be` is the 74/1 and 1041/1 read at `03632992`, plus W5's one file of 23 |

**CI at `978856bc61f9`:**
- **How it's derived:** the PR's diff against `337732be` is the same 25 files. They trigger `device-ids`, `firmware` and
  `no-traffic-captures`, which run on every PR, and `workers` through `proxy/**`. `web-renderer` doesn't run: there is no `src/` or `include/`
  file in the diff.
- **No job-set change on main since `78c2bec21dfa`:** #650 touched no workflow.

| run | total | success | skipped | failed |
|---|---|---|---|---|
| **the PR** | **35** | 32 | 3: firmware's `version`, `promote`, `ship-alert` | 0 |
| **main on the squash** (push) | **35** | 33 | 2: firmware's `version`, `promote` | 0 |

**The tested merge is named from a job's checkout log** (`HEAD is now at …`), with its parents read through the API. Its first parent is
predicted to be main as it stands at the push. If it isn't, that is reported, not explained away.

**Outside the set:** any count or set off stops the step, and it is reported.

### S4.19, 2026-11-12: W5 merged; its rows for the production deploy that will first carry it

**#649 merged at 00:41:34Z,** by rule, at the approved head `978856bc61f9`:
- squash `af871ecb67b6`, parent `aa9eb5e509d9` (H), tree `879976ba135d`, equal to the merge-tree;
- the tested merge, read from the checkout logs, was `a18a79f1526c` (the head into `ed452654`);
- the baseline announcement for `workers.yml`'s two new path filters was relayed to every session.

**Ruled 00:43:32Z, option (a):** W5 rides the next production proxy deploy. That is uk-data's, carrying #643 and #649, in the slot after
v19's step 4' closes. W5's rows below are frozen into that deploy's record before the slot opens.

- **Why it needs rows at all.** The three routes are dormant: no released firmware calls them, they sit behind device auth, and no NWS
  call happens unless an authenticated request asks. But one change is live from the first request: `apiEndpoint()` now returns an edition,
  and `isLeaderboardSubmit` requires the Blipscope edition. That code runs on every production request, and no deployed Worker has run it
  yet.
- **The conditions:** STAGING FIRST, in the same slot. Every row reads on staging before production. A staging row off stops the slot
  before production. The rows that exercise the changed path run on staging only.

**W6 becomes** the staging measurement (W6-STN, the feeds' CPU, real NWS) plus the go-live by W7/W8's firmware. It has no production deploy
of its own.

#### The rows. `$BASE` is the environment being read; requests go as the test identity, as `smoke-prod.sh` sends them, unless "no key"

**On staging, then on production (no row writes anything):**

| row | request | predicted, exactly |
|---|---|---|
| **C1** | `GET /healthz` | the body's `weatherscope` is `{"breakers":[{"id":"nws_points","state":"closed"},{"id":"nws_alerts","state":"closed"},{"id":"nws_stations","state":"closed"},{"id":"nws_obs","state":"closed"},{"id":"nws_forecast","state":"closed"}]}`, with no `commit` key at any depth inside it |
| **C2** | `GET /api/v1/weatherscope/{alerts,conditions,forecast}?lat=39.30&lon=-94.71`, **no key** | each **401**, body `{"v":1,"error":"unauthorized"}` |
| **C3** | `GET /api/v1/weatherscope/blips?lat=39.30&lon=-94.71` | **404**, body `{"v":1,"error":"not_found"}`. A Weatherscope suffix reaches only Weatherscope's handler, which knows no `blips`, so no NWS call is made |
| **C4** | `POST /api/v1/weatherscope/leaderboard`, **no key**, body `{}` | **405**, body `{"v":1,"error":"method_not_allowed"}`, refused at the method gate before auth |
| **C5** | the deploy's own Blipscope smoke rows (`smoke-prod.sh`) | exactly what that record predicts without W5. W5 changes no Blipscope smoke row's expected output |

**On staging only:** they exercise the changed path itself. They are POSTs, though none writes: each is refused before any KV write.

| row | request | predicted, exactly | what it proves |
|---|---|---|---|
| **S1** | `POST /api/v1/blipscope/leaderboard`, body `{}` | **400**, `{"v":1,"error":"bad_submission"}`, as before W5 | the Blipscope edition still passes the method gate. A **405** here would mean the edition check broke Blipscope's submit, and stops the slot |
| **S2** | `POST /v1/leaderboard` (the legacy alias), body `{}` | **400**, `{"v":1,"error":"bad_submission"}`, byte-identical to S1 | the legacy alias still carries the Blipscope edition (the alias contract) |
| **S3** | `POST /api/v1/weatherscope/leaderboard`, body `{}` | **405**, `{"v":1,"error":"method_not_allowed"}` | a non-Blipscope edition's submit is refused as designed |

**Why S1-S3 send an empty body.** A full submit would write the test identity's leaderboard row and rebuild the board. `{}` reaches the
handler and stops at `bad_submission`, before any write. That separates the only two outcomes that matter (400 through the gate, 405 at it)
and writes nothing.

**Outside the set:** any row off on staging stops the slot before production, and is reported. On production, a row off is reported at
once, and the deploy's own rollback rule applies.

### S4.20, 2026-11-12: two of S4.19's rows can't tell a W5 Worker from an older one

**Found by uk-data's pre-deploy controls on staging,** before W5 was deployed (relayed by the orchestrator, 04:07:52Z): **C4** and **S3**
already read **405** on the Worker without W5.

**Why.** Without W5, `/api/v1/weatherscope/` is not an API prefix, so `api` is null and `isLeaderboardSubmit` is false. Any POST to that path
falls to the same method gate, so the answer is the same 405 either way.

**So C4 and S3 are non-discriminating.** They show W5 doesn't change that answer. **They are not evidence that W5 is deployed, and must never
be counted as such.** They stay as registered for this deploy (ruled: no change).

**What each S4.19 row can show:**

| row | answers without W5 | answers with W5 | kind |
|---|---|---|---|
| C1 | no `weatherscope` field in `/healthz` | the five breakers, closed | **discriminating** |
| C2 ×3 | not an API path, so the 404 page (read off on the old shape) | 401 | **discriminating** |
| C3 | the 404 page (read off on the old shape) | a JSON 404 | **discriminating** |
| C4 | 405 | 405 | **non-discriminating** |
| S1, S2 | 400 `bad_submission` (read on staging before the deploy) | 400 `bad_submission` | **regression rows by design:** they hold the Blipscope submit unchanged through the edition check, and are meant to read the same before and after |
| S3 | 405 | 405 | **non-discriminating** |
| C5 | the deploy's own rows | the same | regression |

**My miss.** I wrote each row's answer with W5, and never derived its answer without W5. A row that is evidence of a change needs that
second answer, which is the control that shows it can fail. Same family as CLAUDE.md's "a check that cannot fail is a check nobody can
trust". The discriminating rows above are the ones that carry it.

### S4.21, 2026-11-12: FillRing drops a pixel on a zero-width bridge; the fix, its failing control, and every suite's prediction

**Found by map-server (05:18Z, relayed by the orchestrator):**
- `wxbase::FillRing` (`src/weatherscope/WxBasemap.h`, the crossing line in its scanline loop) computes each crossing from the edge's FIRST
  endpoint.
- A zero-width bridge (one segment walked out and back) is crossed twice, once in each direction. Through a pixel centre, the two doubles
  can differ by about 1e-14.
- Then `ceil(c - 0.5)` ends one span at the pixel and starts the next one after it, so the pixel is never drawn.
- **The owner's images today:** Weatherscope isn't shipped, and maps are off in customer builds. So no image a customer has is affected.
  `include/MapLayer.h` reaches `FillRing`, and `src/AircraftManager.cpp` compiles it. The fix therefore changes the radar images' bytes in
  `FillRing`'s code. v19 is tagged at C (`3a322758eadc`), before any of this.

#### The control, reproduced by bytes before anything else (scratch, on main `e3c2508eb8db`; nothing committed)

- **The set:** map-server's set at `C:\Github\_handoffs\map-server\kb1-case`:
  - 59 files, each verified against its `SHA256SUMS`;
  - `SHA256SUMS` itself `dbed2d33843287c1b166fbebac7e99b4b320aafcfb16b3efa915a300ba89f2cd`;
  - 24 synthetic views (no real place: synthetic origin (0,0), screen px), plus its 10-vertex minimal case.
- **The build:** main's own `wxbase::FillRing` against the fix, compiled with `run.sh`'s exact flags under WSL g++ 13.3.
- **The reading:**

| case | main's FillRing, missing | the fix, missing | extra, either |
|---|---|---|---|
| view-162-90 | 14 | 0 | 0 |
| view-162-270 | 6 | 0 | 0 |
| view-81-0 | 5 | 0 | 0 |
| view-40.5-0 | 1 | 0 | 0 |
| min | 5: (26,26) (31,31) (128,128) (133,133) (134,134) | 0 | 0 |
| the other 20 views | 0 | 0 | 0 |
| **total** | **31** | **0** | **0** |

**Every missing pixel list equals map-server's** (`missing.json` and its message), pixel for pixel.

#### Every consumer, old against new, derived in scratch before this was written

- **The instrument:** a scratch copy of main's `include/` and `src/weatherscope/` in which `FillRing` runs BOTH formulas on every call. It
  keeps the old result, so each suite behaves as on main, and logs every pixel the fix would move.
- **Its controls:** on map-server's cases it reports 5 (min) and 14 (view-162-90). Each suite's stdout is byte-identical to main's.

| suite | `FillRing` calls | pixels written | pixels the fix moves |
|---|---|---|---|
| `test_wx_basemap` (92 checks) | 11 | 4,975 | **0** |
| `test_map_layer` (160) | 215 | 11,677 | **0** |
| `test_wx_k35b` (368, pinned render hashes) | 1,350 | 12,857 | **0**, so no pinned hash can move |

**The three drivers that plant near this code,** run on main and on main + the fix:
- `sabotage_map_layer` and `sabotage_map_draw`: byte-identical logs.
- `sabotage_wx_basemap`: identical except its last line, which prints `WxBasemap.h`'s own sha (`d024c8f59c7a`, then `500eab71ae75`).
- Every plant reads as registered on both.

#### The change

In `FillRing`'s scanline loop, every crossing is computed from the edge's lower-y endpoint, so an edge walked in either direction gives the
same double:

```
            if ((ya > py) != (yb > py)) cross[m++] = (ya < yb) ? xa + (py - ya) * (xb - xa) / (yb - ya)
                                                              : xb + (py - yb) * (xa - xb) / (ya - yb);
```

#### The new rows, in `test/host/test_wx_basemap.cpp`'s FL section (`kCases` 92 → **96**)

| row | predicted |
|---|---|
| **FL7** | map-server's minimal bridged ring (10 vertices, inline) filled by `FillRing` **equals** an in-test reference: a pixel-centre even-odd test of the outer square minus the island, each without a bridge. The reference doesn't use `FillRing`'s scanline code. 47,959 px |
| **FL7c, CONTROL** | a verbatim in-test copy of the OLD crossing, on the same ring, misses exactly (26,26) (31,31) (128,128) (133,133) (134,134) against that reference, and adds none: the case can fail |
| **FL8** | view-162-90's bridged ring (22 vertices, inline, map-server's round-trip doubles) filled by `FillRing`: **28,420** px, map-server's reference count, including all 14 pixels it listed |
| **FL8c, CONTROL** | the old copy on the same ring: **28,406** px, lacking exactly those 14 |

**The sabotage, added to `sabotage_wx_basemap.py`:** **S-BRIDGE** reverts the crossing to the first endpoint. It must turn **exactly {FL7,
FL8}** red. The controls stay green, since they use their own copy.

**Existing plants, re-derived:**
- **S-FILL** (the corner sample and the span rule) grows from {FL1, FL2, FL3, FL5b, FL6} to **{FL1, FL2, FL3, FL5b, FL6, FL7, FL8}**. It
  changes `FillRing`'s output against FL7's independent reference and FL8's count. FL7c and FL8c use their own copy, so they don't move.
- **S-UNION** (an even-odd image write): **unchanged, {FL6}.** FL7 and FL8 fill one ring per call, whose spans never overlap, so no pixel is
  written twice.
- **The other 14 plants** don't reach these rows, which call `FillRing` directly on fixed coordinates.
- **The driver's total:** "17 device sabotages and 2 validator sabotages: ALL AS PREDICTED".

#### Predicted, on the change's branch

| read | predicted |
|---|---|
| `test_wx_basemap` | **96 checks, 0 failures** |
| `test_map_layer` | 160 checks, 0 failures; its CB rows unchanged |
| `test_wx_k35b` | 368 checks, 0 failures; no K row's hash moves |
| `sabotage_wx_basemap.py` | 17 device + 2 validator, ALL AS PREDICTED, with S-FILL's and S-BRIDGE's sets as above |
| `sabotage_map_layer.py`, `sabotage_map_draw.py` | AS PREDICTED, unchanged |
| `run.sh` under WSL | its known blocks only: exit 2 from the xtensa and Pillow steps, and `sabotage_wx_towns` SKIPs. Its comment's count reads 96 |

**CI:**
- **How it's derived:** the PR touches `src/weatherscope/WxBasemap.h`, `test/host/test_wx_basemap.cpp`, `test/host/sabotage_wx_basemap.py`
  and `test/host/run.sh`. That runs `device-ids`, `firmware` and `no-traffic-captures`, which run on every PR, and `web-renderer`, through
  `src/**`. There's no `proxy/` path, so not `workers`.
- **No job-set change on main since `78c2bec21dfa`.**

| run | total | success | skipped | failed |
|---|---|---|---|---|
| the PR | **34** | 30 | 4: firmware's `version`, `promote`, `ship-alert`; web-renderer's `publish` | 0 |
| main on the squash | **34** | 31 | 3: firmware's `version`, `promote`; web-renderer's `publish` | 0 |

**The tested merge is named from a job's checkout log.** Map-server pins `FillRing` from the squash once it's on main.

**Outside the set:** any row, count, set or hash off stops the step, and it is reported.

### S4.22, 2026-11-12: the readers of test_wx_basemap's count, all of them, before the driver's one re-run

**What stopped S4.21's run.** On the branch (`weatherscope/fillring-bridge` at `156f6bb54fb2`, not pushed), every suite read as predicted:
- `test_wx_basemap` 96/0, with FL7, FL7c, FL8 and FL8c ok at their exact counts;
- `test_map_layer` 160/0;
- `test_wx_k35b` 368/0;
- `sabotage_map_layer` and `sabotage_map_draw` as predicted.

**`sabotage_wx_basemap.py` read the RIG:** "96 cases ran, not 92". It pins the suite's count, and its control refuses any other before a
single plant runs. So no red set was read. **Ruled 05:35Z:** the fix is approved, and so is one more reader I had missed.

**My miss.** I searched `run.sh` for count pins, but not the drivers. Worse than the driver run itself: `run.sh` runs EVERY sabotage
driver's `--control` against its own count table, and CI's host tests run `run.sh` on every PR. So S4.21's CI prediction would have read off
in the host tests with these pins left at 92.

#### The sweep: every tracked file on main that mentions the suite or its drivers, for `92`

| file:line | what it is | action |
|---|---|---|
| `test/host/sabotage_wx_basemap.py:30` | `CASES = 92`: the driver's control count, and its `CONTROL_TABLE` (line 265) reads it | **96** |
| `test/host/sabotage_wx_basemap.py:12` | its docstring: "the control: 92 cases" | **96** |
| `test/host/sabotage_wx_themed_draw.py:39` | `"basemap": ([WX], 92)` in `SUITES`; that driver's `CONTROL_TABLE` (line 151) is derived from it | **96** |
| `test/host/test_wx_basemap.cpp:3` | the header: "held to 92 named cases" | **96** |
| `test/host/test_wx_basemap.cpp:9` | the header: "a case count other than 92" | **96** |
| `test/host/run.sh:2248` | the block's comment | already reads 92 + 4 = **96** |
| `docs/healthz-commit-read.md:9` | `proxy/src/index.ts:92`, a line number | not a reader |
| `tools/weatherscope/screens/look35.py:30, 31, 70` | colour components (96, 92, 86) | not a reader |

**Readers that don't grep as 92:**
- **`lint_driver_counts.py`,** run by `run.sh` over every `sabotage_*.py`. Its L1 rule refuses a `CONTROL_TABLE` count written as a literal;
  its L2 rule refuses any other integer literal equal to a table count. Neither driver holds a literal 96 today, so the change adds no L2
  hit.
- **`run.sh`'s "sabotage controls" and "sabotage anchors" blocks,** which run every driver's `--control` and `--anchors`.
- **CI's host-tests job,** which runs `run.sh` on two images (ubuntu-24.04 and ubuntu-26.04).
- **`sabotage_wx_themed_draw.py`'s `SUITES`,** the only table in it with this suite's count. It holds no sum or total over the suites.

#### Predicted, after the five edits above, before either driver re-runs

| read | predicted |
|---|---|
| `sabotage_wx_basemap.py --anchors` | every anchor found once, S-BRIDGE's included |
| `sabotage_wx_basemap.py --control` | `test_wx_basemap` 96 checks, against its table's 96 |
| **`sabotage_wx_basemap.py`, the re-run** | CONTROL 96 cases, 0 failures; **17 device + 2 validator, ALL AS PREDICTED**; S-BRIDGE {FL7, FL8}; S-FILL {FL1, FL2, FL3, FL5b, FL6, FL7, FL8}; S-UNION {FL6}; every other set as registered |
| `sabotage_wx_themed_draw.py --anchors` | S-NL's and S-KD's anchors found once |
| `sabotage_wx_themed_draw.py --control` | each suite at its table's count: k35 79, k35b 368, replay 195, layout 73, **basemap 96**, theme 72; the colour scan exit 0 |
| **`sabotage_wx_themed_draw.py`, its run** | the control: no suite failing. **S-NL as registered:** exactly 1 hit, at the plant's line. **S-KD exactly its frozen set:** the 25 Dallas K screens and the 57 Bend rows (k35b), and **nothing in the basemap suite.** FL7 and FL8 fill with value 1 directly, untouched by any theme ink. "2 sabotages: ALL AS PREDICTED" |
| `lint_driver_counts.py`, over every driver, and its `--control` | exit 0, as on main |

**This is the driver's one re-run (ruled).** A second off reading on it goes to the reviewer, and on to the owner.

### S4.23, 2026-11-12: run.sh read more blocks than S4.21 predicted; the control's outcomes, frozen before it lands

**S4.21 predicted** that `run.sh` under WSL hits only its known blocks: exit 2 from the xtensa and Pillow steps, and
`sabotage_wx_towns` SKIPs.

**On the branch (`383aa9bcd876`, a fresh worktree with no `.pio`) it read exit 2 with more:**
- the usage report and GameConfig are BLIND, "no ArduinoJson under .pio/libdeps";
- R0/R0b have no Pillow;
- seven-segment "1 vs I", and "the same TU, for the board";
- the Wi-Fi QR test is BLIND;
- the "sabotage controls" and "sabotage drivers, in full" blocks fail on ONE driver, `sabotage_wx_fb_check.py`: `wx-fb-check.py` reads "12
  ok, 0 FAIL, exit 4" against its table's "ok 35".

Every other driver read as predicted, these two included. **My miss:** I predicted `run.sh`'s blocks from memory of an older environment,
not a fresh worktree's.

**The control:** the same full `run.sh`, the same WSL, on main's own tree (`e3c2508eb8db`, clean). **Outcomes, frozen before it lands
(ruled 05:49Z):**

| outcome | reading | then |
|---|---|---|
| **E** | main fails EXACTLY the same blocks with the same lines | they are the environment. Recorded, and the PR goes up with CI's Linux host tests (which install the deps) as the authoritative run, predicted 34 = 30/4/0 as S4.21 |
| **D** | any block or line differs between main and the branch | the change: stop and report |
| **X** | anything else (main fails fewer, a different exit) | stop |

Also to be read, not inferred: why `wx-fb-check.py` reads 12 where its table says 35.

### S4.24, 2026-11-12: the control read E, so run.sh's extra blocks are the environment; the FillRing PR goes up

**The control (S4.23):** the same full `run.sh`, the same WSL, on main's own tree (`e3c2508eb8db`, clean):
- exit 2;
- **the same seven failed blocks**, and the summaries are identical;
- **all 38 failure-type lines identical** to the branch's (FAIL, BLIND, RIG, SKIP, a nonzero exit), compared line by line.

**Outcome E, as frozen:** they are the environment, not the change.

**Why `wx-fb-check.py` reads 12 where its table says 35, read and not inferred:** its `--selftest`, run directly, passes 12 checks and then
exits 4 with `RIG: no LovyanGFX glcdfont.h under .pio/libdeps (run pio run once)`. A fresh worktree has no `.pio`. That is the same cause
as GameConfig's and the usage report's "no ArduinoJson under .pio/libdeps". WSL's Python also has neither Pillow nor numpy (R0/R0b).

**So the authoritative host run is CI's,** whose Linux host-tests job installs these. **The PR goes up (ruled 05:49Z, E),** with its CI
predicted as S4.21: 34 = 30 / 4 / 0, its tested merge named from a job's checkout log.

### S4.25, 2026-11-12: W4 is approved; its rows re-read against main, and its price re-done on cref's measured CPU

**Ruled, in the owner's words, relayed by the reviewer at 17:13:24Z:** "US radar (SeamlessHSR): yes. Weatherscope's W4 may start."
The owner also raised the overage allowance to **$10 a month**, which covers Europe plus SeamlessHSR at about $5.70-5.90.
- **Sequencing (the reviewer):**
  - #656 comes first, because it is on v20's critical path. W4 runs alongside it, never ahead.
  - W4's deploy needs a slot from the reviewer, and none runs during map-client's bench window on E2:08 (to about 19:30Z).
  - Anything that would project the account past $10 a month goes to the reviewer before it ships.
- **Q113's condition is met:** blipscope-5c's read in S4.13 found no conflict, and no released image asks for `/wx/shsr`.

**The price, re-done.** S4.12.2's CPU line was 0.8 to 1.2 times a design estimate. This one uses cref's MEASURED ingest CPU,
**21.9-28.2 M ms a month** (uk-data's `cpu_split`, 11-11 and 11-12), and uk-data's S3-7 read of Class A. It uses
`account_cost_read.py`'s `PRICE`:

| `shsr`'s line | Read P (proportional) | Read A (R2 in whole millions) |
|---|---|---|
| CPU: 21.9-28.2 M ms at $0.02 a million. The account is past its included 30 M, so all of it bills | $0.44-$0.56 | $0.44-$0.56 |
| Class A: about 0.131 M a month (cref's measured 182 an hour) | $0.59 | **$0 while OPERA runs**: the account goes from about 1.10 M to 1.23 M, inside the second million. **$4.50 in a month OPERA is down**: from 0.955 M to 1.09 M, so `shsr` makes the crossing itself |
| Requests (43,200), Logs (about 130 k events), storage | $0: inside the included amounts | $0 |
| **`shsr`, a month** | **$1.03-$1.15** | **$0.44-$0.56; $4.94-$5.06 in a month OPERA is down** |

- **What this table is:** `shsr`'s increment only. The account's whole overage against $10 is uk-data's read, which the
  reviewer holds.
- **X10, after the deploy:** it reads the Worker's own CPU per invocation and its Class A. A reading outside this table
  re-prices it before anything else ships.

**Re-read against main at `71156714ba44`.** S4.12's rows were written at `e0941a90c4e4`.
- **uk-data's `/healthz` change has merged (#643).** `wxHealth(env, PRODUCTS, ROUTED)` lists every `PRODUCTS` row with
  `routed` and `stale`. X5's shape exists on main, and W4 adds no health code, as registered.
- **`ROUTED` is still `{cref, opuk}`.** `shsr` stays out of it, as registered.
- **`PRODUCTS` is 11, and becomes 12 with `shsr`.** Every reader of the count, found by grepping the bare number across
  `proxy/test` and `proxy/scripts`:

  | reader | today | with W4 |
  |---|---|---|
  | DP2, `wx-dev-products.test.ts:83` ("exactly the 11") and its id list | 11 | 12, `shsr` last |
  | PS1, `wx-ingest-dev.test.ts:20` and `:24` | 11 | 12 |
  | **`wx-endpoint.test.ts:170`, `/healthz`'s `wx` keys. New since S4.12, from #643** | 11 | 12 |
  | FX, `wx-ingest-split.test.ts:124`: the tomls' whole lists | cref + ten OPERA envs | gains `[env.shsr]` and `[env.shsr-inert]` |
  | `check-wx-plant.mjs` PB2: "the dev config's 11 deploy targets" | 11 | **unchanged**: W4 adds no dev target. If the code adds one, PB2 moves, and that is a new prediction |
  | `wx-region.test.ts:152`, OPERA's 10 | 10 | unchanged: `shsr` is MRMS |

  So X11 gains `wx-endpoint.test.ts:170`. Every other row of S4.12.4 and S4.12.5 stands as written.

**Next for W4,** worked whenever #656 waits on a build or on CI:
1. The code, on a branch from main, under S4.12.3-S4.12.5 and this re-read, with CI predicted before the push.
2. X9, the look, read before the PR.
3. The deploy only on the reviewer's slot, through `ingest_deploy.sh` under the claim, `[env.shsr-inert]` first.

### S4.26, 2026-11-12: W4's code, its first development run's stop, and everything frozen before the next run (21:07Z)

**The code** is on local branch `weatherscope/w4-shsr`, from main `4b00c97b3dbd`, as S4.12.3 registered it:
- the `shsr` row, appended after the ten OPERA rows;
- `src/wx/ingest-mrms-worker.ts`, which checks its `PRODUCT` only if it is an MRMS row, behind `WX_INGEST`;
- `[env.shsr]` and `[env.shsr-inert]` in `wx-ingest.wrangler.toml`. Each sets `name = "blipscope-wx-ingest-shsr"`
  (without it wrangler would name the inert one `…-shsr-inert`), its own `main`, triggers, limits, observability,
  vars and bucket. That is the OPERA toml's explicit shape: nothing relies on inheritance;
- `/api/v1/blipscope/wx/shsr` in `KNOWN_ROUTES`.

`tools/wx/ingest_deploy.sh` needs no change, read: it already takes an env for `wx-ingest.wrangler.toml` and reads the
Worker's name from that env, so the claim is `wx-ingest-shsr:production`. The proxy is not deployed by W4.

**The stop (21:03Z, a development run of the five affected files): 48 passed, 1 failed, H3.**
- H3 (`wx-endpoint.test.ts:218`) requires the live `/healthz` `wx` projection to equal the committed fixture
  `test/fixtures/wx/healthz-h3.json`, which names the 11 products.
- The same fixture feeds **H6**, `scripts/wx-products-lines.selftest.sh` (CI: `workers.yml:111`), whose whole-output
  `EXPECT_A` and `EXPECT_B` list the 11 products line by line.
- **The miss is my method's:** S4.25's list was found by grepping the bare count, and these pins enumerate ids.
- **Ruled (the reviewer, after 21:05Z):** fixes 1-4 below, frozen here before the next run. Any reader found after this
  freeze is a stop, not an edit.

**The method, from now on:** a reader list is found by grepping the count AND an id only the full list contains (here
`opsn`, the last OPERA id). Every file naming `opsn` was read:
- the four pins below move;
- `region-points.json`, `wx-dev-products.test.ts:38` and `:57`, `check-wx-plant.mjs:38` and `tools/wx/*.py` are
  OPERA-only and unaffected.

### X11, amended (replaces S4.12.4's X11 and S4.25's reader table)

| reader | today | with W4 | predicted |
|---|---|---|---|
| DP2, `wx-dev-products.test.ts:81-83` (the id list, "exactly the 12") | 11 | 12, `shsr` last | passes; DP2s still red |
| PS1, `wx-ingest-dev.test.ts:20`, `:24` | 11 | 12 | passes |
| H1, `wx-endpoint.test.ts:170` | 11 | 12 | passes |
| **H3**, `wx-endpoint.test.ts:218`, and its fixture `healthz-h3.json` | 11 entries | `"shsr": {"frameAgeS": null, "routed": false, "stale": null}` last, the MRMS shape (no `coveredShare`, as `cref`) | passes |
| **H6**, `wx-products-lines.selftest.sh` `EXPECT_A`, `EXPECT_B` | 11 lines | each gains `     wx.shsr routed=false age=null stale=null` last. Case (b) is the fixture with `stale:true` → `false` (`BODY_B`, read), and the function prints in the body's order (read) | (a) as expected, **12 lines, 2 WARN**; (b) **12 lines, 0 WARN**; (c) 2 lines; (d) 1; (f), (f2), (e) unchanged; H6s detected; `SELFTEST PASSED` |
| FX, `wx-ingest-split.test.ts` | 22 targets | 24: `wx-ingest.wrangler.toml:shsr` and `:shsr-inert` after cref's top level; `shsr` checked by exactly `[env.shsr]` | passes, its CONTROL too |
| `sabotage-wx-split.py`'s `TESTS = 5` (the split file's test count) | 5 | **5, unchanged.** X7's rows live in `wx-shsr.test.ts` (S4.12.4: "`wx-shsr.test.ts` and the existing wx tests") | its five sabotages as before |
| `check-wx-plant.mjs` PB2 (the dev config's 11 targets) | 11 | unchanged: W4 adds no dev target | — |

### The run, frozen

- **The five files** (`wx-shsr`, `wx-endpoint`, `wx-ingest-split`, `wx-dev-products`, `wx-ingest-dev`):
  **49 passed, 0 failed.** That is wx-shsr 11 (X1, X3, X4, X6, X7a-d, X8a-c), wx-endpoint with X5 added, and the split
  file at 5.
- **X2:** `node scripts/gen-wx-region.mjs --check` exits 0.
- **H6:** as in the table.
- **The whole proxy suite and `tsc`,** as CI runs them: all green.

### The sabotages' exact red sets (`proxy/scripts/sabotage-wx-shsr.mjs`, written before it first runs)

Each anchor was counted exactly once, LF, with no test run.

| sabotage | the plant | red, exactly |
|---|---|---|
| **S-X-CODE** | the `shsr` row's `codeOf` sends -99 to 15 | **X1, X3**. X1 because the row's function is no longer `codeOfComposite` |
| **S-X-HEALTH** | `wxHealth` keeps only the routed products | **H1, H2, H3, X5**. `opmt`, `opse` and `shsr` vanish. "The newest frame's age" (cref, routed) and `not_configured` stay green |
| **S-X-CREF** | `CREF_WORKER` widened to `cref` and `shsr` | **FT1, FX, X7d**. FT1 logs shsr. FX finds shsr checked by two targets |
| **S-X-SWITCH** | the MRMS entry ignores `WX_INGEST` | **FX, X7c**. The inert env now checks shsr, so two targets; X7c touches the guarded fetch |
| **S-X-LITERAL** | the SeamlessHSR stem written into `ingest.ts` | **X8c** |

**Outside the set:** a row off, or a sabotage whose red set isn't its frozen set, stops the step and is reported.

### CI, predicted for the W4 PR

**35 = 33 success / 2 skipped / 0 failed.**
- Paths: `proxy/src`, `proxy/test`, `proxy/scripts`, `proxy/wx-ingest.wrangler.toml`. Only `workers.yml` filters on
  `proxy/**`; `web-renderer.yml`'s `src/**` is the firmware's root `src`.
- So: workers 3, device-ids 1, no-traffic-captures 1, firmware 28 + 2 skipped. Read from #678's PR run
  (`proxy/scripts` + docs), which shows that job set exactly.
- Against main at the push; re-read if main moves.

**Then, before the PR:** X9, the look (S4.12.4). **The deploy** only in a slot granted by the reviewer:
`[env.shsr-inert]` first, through `tools/wx/ingest_deploy.sh` under the claim. Its cost line is stated with the account's
total, under the owner's budget rule.

### S4.27, 2026-11-12: W4 on main; its deploy and every read-back, frozen before the slot is asked for (22:2xZ)

**#683 is merged** by rule as `05746deca023` (parent `f3dd6b62b652`, tree `a7b06ec78503`, equal to the approved tree).
**Main's push CI there is finished and green:** 35 = 33 success / 2 skipped.

**What deploys, from a checkout of main at `05746deca023`, never a PR head:** the ingest Worker
`blipscope-wx-ingest-shsr`, in two steps, each through `tools/wx/ingest_deploy.sh` under the claim
`wx-ingest-shsr:production`. Before the first command, the deployer lock is confirmed free in chat; a granted slot is
not the lock.

**What does NOT deploy here: the proxy.** Until the proxy's next deploy from main, `/wx/shsr` answers 404 and
`/healthz` lists no `shsr`. Frames are built into R2 (`wx/shsr/…`) but are not served. That is the reviewer's to
schedule.

#### Step 1: `[env.shsr-inert]` (WX_INGEST off), the new Worker's first version, so there is something to roll back to

| row | read | predicted |
|---|---|---|
| **I1** | `ingest_deploy.sh wx-ingest.wrangler.toml shsr-inert` | exit 0; "blipscope-wx-ingest-shsr runs now: [(<version>, 100)]" |
| **I2** | `r3p.py first blipscope-wx-ingest-shsr <deploy ISO>` | the first scheduled invocation within 120 s of the deploy |
| **I3** | `r3p.py lines blipscope-wx-ingest-shsr <deploy+2 min> <deploy+7 min> --minutes shsr` | `lines_by_product` {} (a `wx_ingest_off` line carries no product); `errors` []; `shsr_minutes_both` 0 |

#### Step 2: `[env.shsr]` (WX_INGEST on), only after I1-I3 hold

| row | read | predicted |
|---|---|---|
| **O1** | `ingest_deploy.sh wx-ingest.wrangler.toml shsr` | exit 0; one version at 100 |
| **O2** | `r3p.py lines blipscope-wx-ingest-shsr <deploy+1 min> <deploy+11 min> --minutes shsr` | `lines_by_product` {"shsr": 16-20}; `shsr_minutes_both` at least 8 of 10; `statuses` include `shsr:stored` or `shsr:backfilled` (the first check backfills the last 20 min); at most 2 `errors`, and none in two minutes running (NOAA's own blips, as cref sees them) |

**The stop, fixed now:**
- `wx_ingest_error` in 3 minutes running, or no `shsr` line in 3 minutes running, means `[env.shsr-inert]` is
  redeployed at once (a rollback does not wait), then reported.
- **Outside the set** (an exit that isn't 0, a version not at 100, any row outside its band): stop and report. Nothing
  is fixed before the ruling.

**After the deploy, X10 (S4.12.4), printed, not graded:** the first full day's CPU ms per invocation (against cref's
645) and Class A (against 43 k-140 k a month), from `r3p.py cpu` and the budget's read. The numbers go to uk-data, the
budget file's owner, to replace `build_budget.py`'s "projected from cref" line.

#### The cost line, with the account's total (#671's budget on main, Read A, overage a month)

| scenario | total | source |
|---|---|---|
| today: the ten OPERA silent, shsr absent | **$0.12** | `docs/account-budget.md`, as read |
| OPERA running, shsr absent | **$5.26** | the same |
| **OPERA running + shsr** | **$5.79** | the same |
| **today + shsr** (the ten silent, shsr built), which the budget does not list | **about $5.06-$5.18** | S4.25's line: shsr alone crosses the included million Class A ($4.94-$5.06), plus today's $0.12 |

Every scenario is inside the $10 allowance. The table's flags stand as read: `cpu_ms` OVER and `r2_class_a` OVER with
OPERA running.

#### S4.27a, 2026-11-12 ~22:36Z: I1-I3 as read; I2 OFF; O2 re-anchored before O1 (the reviewer's ruling)

**I1 holds.** `shsr-inert` from `05746deca023`: deploy start 22:25:53.382Z, end 22:25:57.654Z, exit 0; "runs now:
[('19b85e60', 100)]"; `PRODUCT` shsr, `WX_INGEST` off, schedule `* * * * *`.

**I2 reads OFF, recorded as off, no re-grade.** The first invocation was at **22:31:41Z, 344.1 s after the deploy**,
against "within 120 s".
- Reads: 22:28:37Z null; 22:31:00Z null; 22:34:00Z first 22:31:41Z; 22:34:30Z the same.
- **The anchors, in the same command:** cref's Worker (`blipscope-wx-ingest`) first at 22:26:05Z and opuk's at
  22:26:14Z, present at every read. So a Logs lag is excluded.
- **The read is excluded too:** the service appeared under its own name.
- **What remains:** the brand-new Worker's cron took about 5.7 min to start. My band did not allow for that, so the
  miss is my prediction's.
- **Since its first run it fires every 60 s, consecutively:** 22:31:41Z, 22:32:41Z, 22:33:41Z, with gaps 60 and 60,
  exactly like opuk's control (22:26:14Z, 22:27:14Z, …).
- **Its lines are `wx_ingest_off` only.** There is no ingest and no error line: the inert behaviour, exactly.

**I3 holds.** Window 22:27:57Z to 22:32:57Z: `lines_by_product` {}, `errors` [], `shsr_minutes_both` 0, 4 events.

**The ruling: O1 may proceed.** O2 is re-registered here, before O1's command:
- **O2's 10-minute window starts at the FIRST invocation of the NEW version** (its `$workers.scriptVersion.id`, read
  from Workers Logs by the same reader as I2's list), not at deploy + 1 min. The bands are unchanged: `shsr` 16-20
  lines, `shsr_minutes_both` at least 8 of 10, `stored` or `backfilled` seen, at most 2 errors and none in two minutes
  running.
- **O2 is re-read until stable,** never once.
- **No invocation of the new version within 6 min of O1's deploy is outside the set:** `shsr-inert` is redeployed and
  reported. A version change on an existing trigger is not expected to re-propagate.
- **S4.27's stop rule is unchanged:** errors in 3 minutes running, or no `shsr` line in 3 minutes running, means
  `shsr-inert` is redeployed at once.

#### S4.27b, 2026-11-13: O1 and O2 as read, after the owner's go (06:12Z)

**The go.** The owner, in this session at 06:12Z: "Go for SeamlessHSR's live step". The reviewer granted a new slot,
06:15-07:15Z, for `wx-ingest-shsr:production`, with O1 and O2 exactly as S4.27a re-registered them.

**The lock.** The reviewer could not read it this time, so I read it myself immediately before O1:
- `session-claim.py show` at 06:15:05Z: deploy free, merge free.
- I took the claim at 06:15:09Z, until 07:15:00Z.
- I released it at 06:28:13Z, after my own read-back.

**The checkout.** A detached main at `b7e7fdd99884` (#691), which includes #685, rather than `05746deca023`.
- The Worker's inputs are byte-identical between the two: `git diff --quiet` passes over `proxy/src`,
  `proxy/wx-ingest.wrangler.toml`, `proxy/package.json` and `proxy/package-lock.json`.
- `tools/wx/ingest_deploy.sh` is unchanged.
- Between the two commits, only the readers moved to `tools/token_file.py`.
- `token_file.py --check` passed. It prints booleans and key names only.

**The baseline, before O1, on the #685 reader.** Window 06:07:36Z to 06:12:36Z:
- `shsr-inert` `19b85e60` alone, 5 invocations 60 s apart, `wx_ingest_off` lines only, ABR 1.
- cref's Worker present on the same reader.

**O1 holds.** The deploy ran 06:15:16.124Z to 06:15:22.762Z: exit 0, stderr 0 bytes, "runs now:
[('c6d27f8f', 100)]". `PRODUCT` shsr, `WX_INGEST` on, schedule `* * * * *`. `19b85e60` (shsr-inert) is the
rollback version.

**The anchor.** The first invocation of `c6d27f8f` had scheduledTime 06:15:41Z, 18 s after the deploy ended. The 6 min
bound held, and the cron did not re-propagate, as S4.27a expected.

**O2 holds, stable.** Window 06:15:41Z to 06:25:41Z.
- Read at 06:27:01Z and again at 06:28:02Z. The two reads are byte-identical (sha256 `33faa6283b07` both).
- ABR 1, 30 events, all `c6d27f8f`. 10 invocations, 60 s apart.

| band | read |
|---|---|
| `shsr` lines 16-20 | 20 |
| `shsr_minutes_both` at least 8 of 10 | 9 (one 1, none 0) |
| `stored` or `backfilled` seen | stored 6, backfilled 6, already-stored 8 |
| at most 2 errors, none in two minutes running | 0 errors; minutes with an error 0 |

The first storing check was at 06:15:45Z. `mapPutErrors` was 0.

**The stop rule never fired.** It was read twice inside the window:
- to 06:18:31Z: 6 lines, 0 errors, no minute without a line;
- to 06:21:31Z: 12 lines, 0 errors, no minute without a line.

**A note on the reader, not a re-grade.** r3p buckets a check by the wall minute in which the check started
(`timestamp - ms`).
- shsr's cron fires at :41, so each invocation's `:30` check starts at :11 of the NEXT minute. There it pairs with the
  next invocation's `:00` check.
- So the first minute of a window anchored at an invocation can only read "one". The ceiling on this window is 9 of 10,
  not 10.
- The raw `check` and `ms` fields over 06:15:41Z to 06:18:31Z show that every invocation logged both checks.
- The band (at least 8) was registered without knowing this, and it holds with one to spare.
- cref's cron fires at :05, so its reads are unaffected.
- Queued as P3: a line in r3p.py's docstring when the file is next touched.

**Still open:**
- **The proxy's `/wx/shsr` route** deploys only when the reviewer schedules it. Until then `/wx/shsr` answers 404:
  frames are built into R2 but not served.
- **X10 (S4.12.4):** the first full day's CPU ms per invocation and Class A, after 2026-11-14 06:16Z. Printed, then
  sent to uk-data.

#### S4.27c, 2026-11-13: one proxy deploy carrying /wx/shsr (#683) and the routes age (#695), frozen before the owner answers (08:0xZ)

**The ask.** At 08:00Z the reviewer asked for one combined registration. Main is `5b0d27fd5000` (#695 merged), so any
proxy deploy from main ships both changes. The reviewer has asked the owner to approve ONE combined deploy and to name
the session (weatherscope by default). **Nothing below runs until the owner answers in this session.** The
shsr-only attempt at 06:40Z was refused by this session's permission check before it ran. Nothing was deployed then.

**What deploys.** The proxy, from a clean detached checkout of main at the sha read at the run (`5b0d27fd5000` or
later), through `proxy/scripts/deploy.sh`, staging first, then production. The claim is
`proxy:staging,proxy:production`, taken right before and released after D5.

**Carried, `e3c2508e` (production and staging today) to `5b0d27fd5000`: 25 commits.** Three touch the bundle or its
config:

| commit | PR | bundled files | runtime effect |
|---|---|---|---|
| `05746deca023` | #683 | `wx/products.ts` +13 (the shsr row), `metrics.ts` +1 (KNOWN_ROUTES), `wx/ingest-mrms-worker.ts` +45 (not reachable from the proxy's entry) | `/wx/shsr` served (index.ts:422-423 → handleWx → `productById` → region.ts:81 returns a non-cref request as itself); `/healthz` `wx.shsr` |
| `4abb65082f5e` | #694 | `fleet.ts`: comment only (a runbook's two command lines) | none |
| `5b0d27fd5000` | #695 | `index.ts` +6, `routeshealth.ts` +76 | `/healthz` gains top-level `routes` from `meta:routes` |

- #693 is docs only.
- Deploy machinery carried:
  - `e4b41fff` #678, deploy.sh's per-target behind check (`deploy-inputs.mjs`). This is its first production proxy
    deploy.
  - #694's `session-claim.py` (the hook sees the helper).
  - `smoke-prod.sh` gains #695's routes block, which is warn-only.
- If main moves before the run, the list is re-read. Any further bundled commit is outside the set: stop and report.

**Which deploy.sh.** The run uses the one on main at the run's commit.
- At `5b0d27fd5000` it is #678's, which calls `npx wrangler` (allowlisted "migrates in PR B").
- If #697 merges first, the run uses its helper-path deploy.sh, and does not start until 5c's staging rehearsal of it is
  recorded as passed (the reviewer's hold).

**Before (at the run, in this order):**

| row | read | predicted |
|---|---|---|
| R-main | main's head, the carried list, the worktree | as above; clean at that sha |
| **R0** (as sent by uk-data) | "before anything: production's current version id, re-read and recorded as the rollback target, by the deploying session." | `0f4cf9bb` at 100% (read 06:33:56Z and 06:40:08Z), unless it has moved; staging's id is recorded the same way |
| **B0** (as sent by uk-data) | "the smoke BEFORE the deploy (main's smoke-prod.sh against production, as the test identity). Record PASS/FAIL/SKIPPED and the exit code. The new block runs against the OLD Worker, so it must read `RESULT: WARN -- /healthz has no routes field (a Worker before this change)   (mode warn)`, with the counts unaffected (warn mode is information only)." | my counts: PASS 38 / FAIL 0 / SKIPPED 0, exit 0 (06:34:22Z's run, the same checks). A SKIPPED from an empty sky over Bend is recorded and the run goes on: D-P3 compares with B0 itself. **Any FAIL in B0 stops the run before any deploy.** |
| B-probe | `shsr_probe.sh` (smoke's Bend tile, the test identity) against production | shsr 404 `not_found`; cref 200 WXC1 with an ETag |
| B-claim | `session-claim.py show`, `check`, `take` | free. If held by anyone: stop and tell the reviewer |

**Staging:**

| row | read | predicted |
|---|---|---|
| S1 | `deploy.sh staging` | exit 0; confirm-deploy settles on the sha |
| **D-S1** (route-mirror-age.md §5) | staging `/healthz` `routes` | `builtAt` `2026-08-25T23:31:26.817Z`, `rows` 619103, `stale` true, `maxAgeDays` 14, `ageDays` = floor(t − builtAt), at the read's own time t (79 before 11-13T23:31:26Z, 80 after) |
| S2 | staging `/healthz` `wx` | exactly one new line, the last: `wx.shsr routed=false age=<0..600> stale=false` |
| S3 | `shsr_probe.sh` with BASE staging (test-key-staging; staging binds the same `blipscope-wx` bucket) | shsr 200 WXC1, an ETag, frame age ≤ 600 s; cref 200 WXC1 |

Production starts only when S1, D-S1, S2 and S3 all hold.

**Production:**

| row | read | predicted |
|---|---|---|
| D1 | `deploy.sh production` | exit 0; confirm-deploy settles on the sha |
| **D-P2** (route-mirror-age.md §5) | production `/healthz` `commit` | the deployed sha, by `healthz-commit.sh` |
| **D-P1** (route-mirror-age.md §5) | production `/healthz` `routes` | `builtAt` `2026-08-26T17:25:54.476Z`, `rows` 619103, `stale` true, `maxAgeDays` 14, `ageDays` = floor(t − builtAt): 79 before 11-13T17:25:54Z, 80 after |
| **D-P4** (as sent by uk-data) | "the RH8 control, in production: the body still carries the top-level `commit`, `upstreams`, `wx`, `basemap` and `weatherscope`, and `routes` is the only new top-level key." | keys {basemap, commit, ok, routes, upstreams, weatherscope, wx}; shsr adds none (it sits inside `wx`) |
| D2 | production `/healthz` `wx` | exactly one new line, the last: `wx.shsr routed=false age=<0..600> stale=false`; the other 11 keep their routed/stale shape |
| **D-P3** (as sent by uk-data) | "the smoke AFTER: the block reads `RESULT: WARN -- STALE: built 2026-08-26T17:25:54.476Z, A d old (limit 14 d)   (mode warn)`. PASS/FAIL/SKIPPED and the exit code are identical to B0." | **my delta to it is 0:** main's smoke-prod.sh has no shsr check; shsr adds one information line in the wx section and moves no count |
| D4 | `shsr_probe.sh` against production, the first read after confirm | shsr 200 WXC1, ETag `wx1-shsr-<epoch>-44.06_-121.32_160`, frame age ≤ 600 s (the server's own `wx_stale` rule); frames have been in R2 since 06:15:45Z |
| D5 | the probe 3 times, 150 s apart | shsr's `X-Wx-Frame` advances at least once (120 s cadence); cref 200 WXC1 at each read |

**One rule for both, over every row above:**

| reading | action |
|---|---|
| a fleet-facing red: deploy.sh nonzero after the upload; `/healthz` not settling on the sha in confirm-deploy's window; smoke FAIL > 0 or counts ≠ B0; `/wx/cref` not 200 WXC1 | **roll back first**, to R0's version (`node scripts/wrangler-run.mjs rollback <R0> --env production` from proxy/, under the held claim), then report |
| any off reading in uk-data's rows (D-S1, D-P1, D-P2, D-P3, D-P4) | **roll back first**, then report (route-mirror-age.md §5: "Any off reading: roll back first, then report"); on staging, to staging's recorded version |
| only shsr off (S2's or D2's shsr line, S3's or D4's shsr, D5's frame) | stop and report; **no rollback**: no device calls `/wx/shsr` |
| anything else outside the set | stop and report; nothing is fixed before the ruling |

- A crash is an instrument failure, never a red.
- A count off from its own list: report both.

#### S4.27d, 2026-11-13: the migrated ingest_deploy.sh's first real run, on the dev Worker (08:35Z)

**Why.** #697 moved `tools/wx/ingest_deploy.sh` onto the wrangler helper (`proxy/scripts/wrangler-run.mjs`). The
reviewer ruled that its first real run is the DEV Worker, on a slot, before any production ingest deploy uses the
helper path.
- The predictions (DV0-DV4) and their rules were sent and accepted at 08:17Z.
- The slot opened at 08:35Z, on main `b7184243fc60`'s finished push CI: 36 = 34/2/0, four workflows completed, read
  by the reviewer and again by me at 08:33:59Z.

**What ran.** `bash tools/wx/ingest_deploy.sh wx-ingest-dev.wrangler.toml off`, from a clean detached main at
`b7184243fc60`. The claim `wx-ingest-dev:production` was taken at 08:35:14Z and released at 08:46:48Z. The deploy was
identical to what the dev Worker already ran: `off`, with `MODE` off, `WX` `blipscope-wx-dev`, no cron and no route.

| row | predicted | read |
|---|---|---|
| DV0 | dev `7459e9be` at 100, `MODE` off, `WX` blipscope-wx-dev, no invocations; `~/.wrangler/logs` 34,093 files; 0 `wrangler-log-*` dirs | as predicted at 08:16Z and again at 08:35:08Z. The 08:06-08:16Z read had 0 dev invocations, with shsr's 11 as the anchor |
| DV1 | exit 0; "runs now: [(<new>, 100)]", with new ≠ `7459e9be`; stderr 0 bytes | deploy 08:35:26.794Z to 08:35:30.314Z, exit 0; "blipscope-wx-ingest-dev runs now: [('a36a8c1e', 100)]"; wrangler's stderr 0 bytes, the script's 0 bytes |
| DV2 | logs still 34,093, read right before and right after; 0 dirs left | 34,093 to 34,093, and still 34,093 after DV3's reads and at DV4; 0 dirs |
| DV3 | the new version's bindings: `MODE` off, `WX` blipscope-wx-dev, nothing else | `a36a8c1e-ff52`: exactly those two |
| DV4 | 0 dev invocations in the 10 min after | 08:35:30Z to 08:45:30Z, read at 08:46:40Z, ABR 1: 0 events. The anchor on the same reader: shsr 10 invocations, all `c6d27f8f` |

**All four held.** No rollback was needed; `7459e9be` remains the previous version. The helper path through
`ingest_deploy.sh` is now proven once for real. That covers the deploy and its `deployments status` read-back, and it
left nothing in `~/.wrangler/logs`. The next production ingest deploy may use it.

**Not affected:** the proxy. Its combined deploy (S4.27c) waits for the owner's answer and for the lift of the hold on
the new deploy.sh (5c's staging rehearsal, which starts after this claim's release).

#### S4.27c′, 2026-11-13 ~08:55Z: S1 made read-only (staging already runs the bundle), amended before the run

**The hold is lifted** (the reviewer, 08:53:50Z): 5c's staging rehearsal of #697's deploy.sh passed B0-B6 at
08:48-08:53Z. S4.27c now waits only on the owner's answer in this session.

**The fact, read by me at 08:54:23Z:**
- Staging's `/healthz` reports commit `b7184243`, with top-level keys {basemap, commit, ok, routes, upstreams,
  weatherscope, wx}.
- `deployments status --env staging`, through the helper, reads `1f1a0fae` at 100.
- Main is `b7184243fc60`. `git diff --quiet 5b0d27fd5000 b7184243fc60` over every bundle and config path passes: #697
  touched none of them.

**So staging already runs the exact bundle production would get, and S1 changes. The other staging rows stand:**

| row | was | now |
|---|---|---|
| S1 | `deploy.sh staging`: exit 0, confirm-deploy settles on the sha | **read only:** staging's `/healthz` commit equals the run's sha (`b7184243`), and its version is the one recorded at the run (`1f1a0fae` unless it has moved). If the run's main has any bundled commit beyond `b7184243`, it is outside the set (as S4.27c already says) |
| D-S1, S2, S3 | as in S4.27c | unchanged; read on staging as it stands |

- **Carried, at `b7184243`:** #697 joins the deploy machinery: deploy.sh and deploy-inputs.mjs on the helper,
  rotate-staging-provision-token.sh, and my three `tools/wx` scripts. It adds nothing to the bundle.
- **Which deploy.sh:** the production deploy (D1) uses #697's deploy.sh, now rehearsed on staging.
- **Rollback for staging:** none can be needed for a read. An off D-S1, S2 or S3 stops the run before production, and
  is reported.

#### S4.27c″, 2026-11-13 16:33Z: the owner's go, and main one commit past the bundle (amended before any reading)

**The owner's go, in this session, 15:35Z:** "go for the production proxy deploy carrying the SeamlessHSR route and
the route-age line on the health page. Run it by the usual rules (predictions, rollback target, staging green, one
deploy at a time) and not during Sunday Nov 15's bench slots (18:00-19:30Z and 20:00-20:50Z)." The run is on
Friday 11-13, outside both windows.

**Main moved, read at 16:31:50Z:** `aa40aa43d8c9` (#698), one commit past `b7184243fc60`.
- It touches three files: `.github/workflows/workers.yml`, `docs/locate-page-chrome-wait.md` and
  `proxy/scripts/check-locate-page.mjs`.
- None is bundled.
- None is in deploy-inputs.mjs's proxy machinery set.
- smoke-prod.sh does not read any of them: `check-locate-page.mjs` is called only from `workers.yml`.
- `git diff b7184243fc60 aa40aa43d8c9` over the bundle, config, package, machinery, smoke-prod.sh and test-identity.sh
  paths: 0 files.
- Push CI at `aa40aa43d8c9` finished: four workflows, 37 jobs = 35 success / 2 skipped / 0 failed (read 16:32Z).

**So the bundle is the one staging already runs, and the run deploys main's head, `aa40aa43`:**

| row | was (S4.27c′) | now |
|---|---|---|
| R-main | main at the run, carried list re-read | `aa40aa43d8c9`, clean detached; #698 is carried and outside the bundle. Any further commit on main before D1: re-read, and a bundled one stops the run |
| S1 | staging's commit equals the run's sha (`b7184243`) | staging's `/healthz` commit is `b7184243`, the bundle-path diff `b7184243..aa40aa43` is empty (above), and staging's version is `1f1a0fae` at 100 unless it has moved. Staging is not redeployed |
| D1 | `deploy.sh production` | from `aa40aa43`; deploy-inputs' behind check has nothing behind (HEAD = origin/main); exit 0; confirm-deploy settles on `aa40aa43` |
| D-P2 | the deployed sha | `aa40aa43` |

Every other row (R0, B0, B-probe, B-claim, D-S1, S2, S3, D-P1, D-P4, D2, D-P3, D4, D5), the rollback rule and the
outcome table stand as in S4.27c and S4.27c′.
