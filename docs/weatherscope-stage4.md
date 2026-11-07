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
