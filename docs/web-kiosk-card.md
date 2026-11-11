# The web kiosk's card, idle, Follow switch and demo (K1-K4)

**Read this first.** The kiosk side asked for four more web-build changes after #631, for the release after v19. The reviewer
passed them on 2026-11-11 ~01:21Z with these terms: design first, predictions frozen before code, everything web-only behind `kWeb`,
and each new key added to `check-web-only.sh`'s device scan. Device images gain no new strings, and device behaviour does not change.
- **K1, card size:** a new key, `screen-diameter-mm`, holds the visible diameter in whole millimetres. It is read only when `kiosk`
  is `"true"`. The card is drawn about 150 mm wide, never wider than the screen, as an overlay with the radar running around it.
  When the key is unset or invalid, or the kiosk is off, the card is today's.
- **K2, kiosk idle:** with the kiosk on, 60 s without a touch closes the card, leaves Follow and sends the zoom home.
- **K3, Follow off:** a new key, `kiosk-follow`. The value `"false"` removes every way into Follow in the web build. (`follow` is
  already a device key, the configured target, so it is not reused.)
- **K4, demo:** a method on the object `mount()` returns, which opens a given hex's card for N seconds (kiosk only). That object
  also fires an event when a card opens and when Follow starts, and the events carry no aircraft identity.

**Out of scope, as ruled:** photos in the web build, and any privacy filter in the renderer.

**Unchanged:** the site rules (`check_site_rules.mjs`): nothing is posted, nothing is stored, and no request goes anywhere but
`apiBase`.

This page freezes the design and the predictions. No code exists yet.

**Re-frozen 2026-11-11, after v19's step 4, with the reviewer's rulings** (~04:10Z and ~04:39Z; listed under "Rulings" at the end):
the card sits below the screen's centre by the ruled offset (K1); a visitor's touch puts the device-raised Follow face under the
60 s rule (K2); and KI6 names all five rebuild regions. Every row that changed says so.

## The one idea under all four: what "the card is open" means

`inDetail` appears on 25 lines of `src/AircraftManager.cpp` at `947e1d3825fc`: 15 reads, 4 writes, and 6 comment or format-string
lines. `AircraftManager.h` reads it twice more, in `IsRadarView()` and `DetailCardOpen()`. Re-read at the re-freeze: the same
25 + 3 lines at main `e2c34daf82a1`, and at #634's head `3de4367a3c51`, which merges ahead of this work. Today two meanings share it:
- **open:** a card exists, so taps and swipes belong to it, the night clock stays off, and its lookups flow;
- **covers:** the card hides the screen, so the radar can stop working.

They are always true together today. K1 makes the second false while the first stays true: the card is open and the radar runs
around it. So every read is classified below, **by function name** (line numbers move), and only the "covers" reads change.

| read, by function | meaning | under K1's overlay |
|---|---|---|
| `Update`: the card's idle close | open | stays; its timing is K2's |
| `Update`: `if (inDetail) return;` before `ConsumeFetchResult` | **covers** | **skipped**: the fetch, alerts and enrichment keep running |
| `IsRadarView()` (the sweep, and the weather layer, which is off in the web build) | **covers** | the sweep draws around the card |
| `Draw`: the card block and its early return | **covers** | the screen draws beneath, and the card draws in a layer (below) |
| `NightClockActive`, `LongPressEligible`, `DrawTouchUnavailable`'s Connect test | open | unchanged |
| `UpdateClaimToast`'s dwell hold | open | unchanged: the toast's seat is under the card |
| `HandleFollowTransition`: "never over a card" | open | unchanged |
| `HandleTap` / `HandleSwipe`: the card branches | open | unchanged; K1's touch mapping is in the shim (below) |
| `ProcessDetailLookups`, and `ApplyEnrichment`'s reveal-then-claim | open | unchanged, apart from K4's demo exclusion |
| `DetailCardOpen()` (the soak harness) | open | unchanged |
| `Update`: `PreventiveRebootDue`'s argument | n/a | always false in the web build (#629) |
| the `[soak-state]` log (`RecordFrameUs`) and the `[touch]` log (`ProcessTouchSample`) | n/a | logs only |

That is all 17 reads.

**The card block also establishes four things beyond drawing,** and the overlay path must keep each one (the second-path rule):
1. `[card] open t=` on the first frame that draws the card;
2. the alert ring, rank toast, Follow-decline toast and touch-unavailable strip are drawn over the card;
3. `ExitDetail()` runs when the selected contact has left the feed;
4. the early return that keeps the screen's own draw from running.

Under the overlay, item 4 is deliberately inverted. Items 1-3 run in the layer path, so each happens exactly once per frame.

**How the device stays unchanged by construction.** A new pure header, `include/KioskRules.h`, uses `kWeb` from
`include/PollCadence.h`. Every changed "covers" read goes through `kioskrules::CardCovers(inDetail, overlay)`, which is
`inDetail && !(kWeb && overlay)`. In a device build that folds to `inDetail` at compile time. Every new member, key read, export
and method sits inside `#ifdef BLIPSCOPE_WEB_RENDERER` or in `tools/web-renderer/`. So the claim is stronger than "same behaviour":
**the device image's bytes are the same apart from the rebuild regions**, and KI6 measures exactly that.

## K1: the card as an overlay

**The rule (`kioskrules::CardScale(kiosk, stored)`):**
- `s = 150 / D` when the kiosk is on, `D` is valid, and `D > 150`.
- Otherwise `s = 1`, which is today's card in every respect, with no overlay.
- **Valid** means 1 to 4 ASCII digits with no leading zero: `^[1-9][0-9]{0,3}$`, so 1 to 9999 mm. Anything else is invalid and
  leaves the card as today's. It is never coerced: `"300.5"`, `"300mm"`, `" 300"`, `"0300"`, `"+300"`, `"10000"`, `"3e2"` and the
  empty string all give `s = 1`. A value that is not a whole number of millimetres is not one, and truncating it would invent one.

**Where the card sits (`kioskrules::CardCentreDy(S, s)`, ruled Q-K1a):** horizontally centred, with its centre `dy` panel px
below the screen's centre:
- `dy = (1 - s) * S/2 - 0.02 * S` when that is positive, and `0` (centred) when it is not.
- So the card's bottom edge sits `0.02 * S` above the screen's edge, and its top edge `S * (0.48 - s)` below the centre.
- `dy > 0` exactly when `s < 0.96`: `D >= 157`. `D` from 151 to 156 draws a centred overlay; `D <= 150` is today's card.
- The screen's centre is uncovered exactly when `s < 0.48`: `D >= 313`. Both venue screens (399 mm and 599 mm) are above that;
  a smaller screen keeps the same formula, as ruled.
- In a device build `s` is always 1, so `dy` is always 0.

**The draw.**
- **Where the card sits:** a disc of diameter 150 mm (`s * S` panel px), centred at `(S/2, S/2 + dy)`.
- **What is beneath it:** the screen it was opened from keeps drawing, sweep included.
- **How it reaches the page:** the display list gains two ops.
  - `LAYER_BEGIN` = 16, `[len, op, 0, cx, cy, s]`, where `(cx, cy) = (S/2, S/2 + dy)` is where the card's centre lands. Ops
    that follow are in the card's own panel coordinates, centred at `c0 = (S/2, S/2)`, mapped by `p -> (cx, cy) + s * (p - c0)`
    and clipped to the disc of radius `S/2` about `c0` in those coordinates. The list is float32 (`HEAPF32`), so `cy` is not
    rounded.
  - `LAYER_END` = 17, `[len, op, 0]`.
- **Why the card needs no new styling:** it already draws its own edge ring and fills its disc, so at `s < 1` it reads as a disc
  over the radar.
- **`render.js`:** `LAYER_BEGIN` is `save` / `translate(cx, cy)` / `scale(s)` / `translate(-S/2, -S/2)` / `clip`, and
  `LAYER_END` is `restore`. The seam width inside a layer is one backing pixel in the layer's own units.
- **`pushSprite(dst, x, y)`:** its existing default case already offsets `cx, cy` correctly.
- **The frame:** `web_frame` draws the screen through `Draw` (which, under the overlay, does not take the card branch). It then
  emits `LAYER_BEGIN`, calls a web-only `DrawCardLayer` (the card plus items 1-3 above), and emits `LAYER_END`.

**The touch.** The shim's `getTouch` applies the inverse map `q -> c0 + (q - (cx, cy)) / s`.
- **Its one fixed point** (the screen point that maps to itself) is `(S/2, S/2 + dy / (1 - s))`. At `D = 600` that is `y = 233.6`
  on a 240 panel and `467.2` on a 480, below the Follow glyph's tap target (`y` 164-194 and 328-388). So a press at the glyph's
  drawn position and a press at its card-coordinate position stay different presses, which is what KW-1b's control needs.
- **When it applies:** to every sample of a press that **began** while the overlay was drawn. The decision is latched at the press,
  so a press cannot change coordinate systems halfway through.
- **What a tap on the drawn card means:** what it means on a device, the Follow glyph included.
- **What a tap outside the card does:** it lands outside the card's disc in card coordinates. Today's rule for a non-glyph tap on
  a card applies, and with no photo in the web build, that rule closes it.
- **What swipes do:** they are measured in card pixels, so on a large screen a shorter physical swipe crosses the card's threshold.

## K2: kiosk idle

`kioskrules`, kiosk on (web build only):

| what | device, and web with the kiosk off | web, kiosk on |
|---|---|---|
| card closes | 3 min after the last touch | **60 s** after the last touch |
| a **session** Follow (one a touch started) | never left on idle | **left after 60 s**, by `ClearSessionFollow()`, the one function both of today's dismissals use |
| the Follow face the device raised for a **configured** target | its own dwell (20 s) | **its own dwell until a visitor touches it; from that touch, the 60 s rule** (ruled Q-K2a) |
| zoom goes home | 10 min | **60 s** |

All three clocks are `lastTouchActivityMs`, the one clock today's two idle rules already share.

**Why the untouched device-raised face is exempt.** The idle return exists to end a visitor's session. That face was never a
visitor's: it was raised by the device, on a dwell that returns it. This file's own rule applies: a rule that fires identically
on a device-initiated path and a visitor-initiated path is wrong on one of them.

**Once a visitor touches it, it is theirs (ruled Q-K2a).** Today a tap on that face clears nothing and leaves the dwell running,
and a vertical swipe cancels the dwell and leaves the face with no exit but a sideways swipe. At a venue the second is a face
stuck on screen. So, with the kiosk on, the same move as the demo card:
- The first touch sample while the device-raised face is up (the dwell running, no session Follow) sets a web-only
  `kioskRaisedTouched` and cancels the dwell. From then on the face is under the 60 s rule, read from `lastTouchActivityMs`.
- The two never govern one face at the same time.
- **The leave is the dwell's own return, brought forward:** it sets `followAutoUntilMs` to now (never 0), and the existing expiry
  branch in `HandleFollowTransition` returns the screen to `followAutoReturnTo` exactly as a dwell does. There is still one copy
  of that return, and the device path is not edited.
- `kioskRaisedTouched` clears when the screen leaves Follow or a session Follow starts.
- **Kiosk off, and every device:** a tap still clears nothing and the dwell still runs, as today.

**The demo card (K4) is not on this clock until someone touches it.** It opens with no touch, so `lastTouchActivityMs` may be hours
old, and a 60 s rule read from the last touch would close it on the next frame.
- An untouched demo card closes at exactly its own N seconds.
- The first touch sample after it opens makes it the visitor's card, cancels the demo timer, and puts it under the 60 s rule.
- The two never govern one card at the same time.

## K3: Follow off

`kioskrules::FollowAllowed(kiosk, stored)` is false only for the kiosk on and `kiosk-follow` exactly `"false"`. Every way into
Follow, as the code has them:

| way in | under `"false"` |
|---|---|
| the Follow glyph on the card | `DrawFollowFaceGlyph` takes its existing "not drawn" branch, which sets the hit box to -1 every frame, so there is no target |
| swipe down on the card (`OpenFollowFromCard`, the alias) | `OpenFollowFromCard` returns false first, and the swipe closes the card like any other swipe |
| the configured target (`follow` key), which the device auto-surfaces | `followTarget` is read as empty, so there is nothing to surface |
| `FOLLOW_BENCH`'s self-enable | compiled out: `build.sh` never defines `FOLLOW_BENCH` |
| the carousel | Follow is not in it today |

Unset, or any other value, keeps Follow as today. **Kiosk off:** the key is not read at all, the same as K1's key.

## K4: demo, and the events

**`radar.demo(hex, seconds)` returns `true` if it opened a card, and `false` (opening nothing) when:**
- the kiosk is off;
- `hex` is not exactly 6 hex digits. Either case is accepted and lowercased; a non-ICAO `~` id cannot be demoed;
- `seconds` is not an integer from 1 to 600. Out-of-range values are refused, never clamped;
- the hex is not tracked right now;
- a card is already open, the Follow face is showing, the reset menu is open, or a touch is down.

**What a demo open does, against what a touch open establishes.** Both of today's open paths, the radar tap and the list tap, do
the first four rows. The radar tap also does the fifth:

| a touch open | a demo open | why |
|---|---|---|
| `selectedIcao`, `inDetail = true`, `detailPage = 0` | the same | it is a card |
| `cardOpenLogPending = true` | the same | so `[card] open t=` and the event fire on its first frame |
| `usageStore.CardOpened()` | **not called** | a demo is not use. This is the usage rule's own exclusion for counts the customer did not cause |
| `ClaimTappedAircraft()` at the open, and again when enrichment lands | **neither** | a demo spotted nothing; the reveal-then-claim test excludes a demo card |
| `tapCycleIndex` / `lastTapX` | untouched | they belong to the finger |

**The events.** The object `mount()` returns gains `addEventListener` / `removeEventListener`, delegating to its own `EventTarget`.
- **`cardopen`** fires on the first frame that draws a newly opened card: the same moment as `[card] open t=`. Its `detail` is
  exactly `{ by: "touch" }` or `{ by: "demo" }`. A card opened and lost to the feed before it ever drew fires nothing: nobody saw
  it.
- **`followstart`** fires when a session Follow starts. Its `detail` is exactly `{ by: "touch" }`. The device raising the face for
  a configured target does not fire it: that is not a start.
- **No identity:** no hex, callsign, registration, type, position or count. The wasm keeps three counters (touch opens, demo opens,
  Follow starts), and `render.js`'s exported `drainEvents(mod, state, target)` turns the change in each counter into events. Both
  `frame()` and `step()` call it.

## Predictions

### On the host (`test/host/test_kiosk_rules.cpp`, device build; `test_kiosk_rules_web.cpp`, `-DBLIPSCOPE_WEB_RENDERER`)

| row | build | predicted |
|---|---|---|
| **KC1** | device | over the whole grid (kiosk on and off; every diameter below; `inDetail` and overlay both ways; session or not; a device-raised face touched or not; ms since touch 0, 59999, 60000, 179999, 180000; every `kiosk-follow` value below; `S` 240 and 480), every rule is today's: `CardScale` 1, `CardCentreDy` 0, `CardCovers` == `inDetail`, card idle 180000, zoom idle == the device value passed in, `LeaveFollowDue` false, `FollowAllowed` true. *(Re-frozen: `CardCentreDy`, the touched face and `S` added to the grid.)* |
| **KC2** | web | kiosk on: `CardScale` is exactly `150.0f / D` for `"151"`, `"300"`, `"600"`, `"9999"`; 1 for `"1"`, `"32"`, `"150"`; 1 for `""`, `"0"`, `"0300"`, `"300.5"`, `"300mm"`, `" 300"`, `"300 "`, `"-300"`, `"+300"`, `"10000"`, `"3e2"`, `nullptr`. Kiosk off: 1 for `"600"` |
| **KC3** | web | kiosk on: card idle 60000, zoom idle 60000. `LeaveFollowDue` true at 60000 and false at 59999 for a session Follow **and for a touched device-raised face**; never for an untouched device-raised face. Kiosk off: 180000, the device value, never. *(Re-frozen: the touched face, Q-K2a.)* |
| **KC4** | web | kiosk on: `FollowAllowed` false for exactly `"false"`, true for `""`, `"true"`, `"FALSE"`, `"0"`, `"no"`, `nullptr`. Kiosk off: true for `"false"` |
| **KC5** | web | `DemoArgsValid` true for `"a4f2c1"`/30, `"A4F2C1"`/1, `"a4f2c1"`/600; false for seconds 0, 601, -1, and for `"a4f2c"`, `"a4f2c1d"`, `"~a4f2c"`, `"g4f2c1"`, `""`, `nullptr` |
| **KC6** | web | `CardCovers` is `inDetail && !overlay` over its four cases |
| **KC7** *(new)* | web | `CardCentreDy(S, CardScale(true, D))` within 0.001 px of: `S = 240`: 0 for `"151"` and `"156"`, 0.550318 for `"157"`, 57.692013 for `"313"`, 70.087218 for `"399"`, 85.2 for `"600"`; `S = 480`: 0 for `"156"`, 1.100637 for `"157"`, 140.174436 for `"399"`, 170.4 for `"600"`. 0 for `"150"` and for any invalid `D`. The tolerance is stated because the function computes in float32; the two differ by under 1e-5 px at every value above |
| **KW2** | `check_web_wiring.py` | `screen-diameter-mm` and `kiosk-follow` each read exactly once, inside `#ifdef BLIPSCOPE_WEB_RENDERER`, through `CardScale` / `FollowAllowed`. The `Update` early return and `IsRadarView` go through `CardCovers`. The card idle close, the zoom idle and the Follow leave go through `CardIdleMs`, `ZoomIdleMs` and `LeaveFollowDue`. The demo open contains no `usageStore.` and no `ClaimTappedAircraft(`, and the reveal-then-claim test excludes a demo card. `render.js`'s `frame()` and `step()` both call `drainEvents`. *(Re-frozen:)* `kioskRaisedTouched` is written only inside the fence; the kiosk leave of a touched device-raised face assigns `followAutoUntilMs` and never assigns `screen` itself; and `HandleFollowTransition`'s expiry branch is byte-identical to main's |

**Plants** (`test/host/sabotage_kiosk_card.py`, in copies; the real files' sha256 compared before and after):

| plant | the edit | red, exactly |
|---|---|---|
| **S-KC-device** | `CardScale` ignores `kWeb` | KC1 |
| **S-KC-coerce** | the diameter is parsed with `strtol` and its trailing junk ignored | KC2 |
| **S-KC-idle** | `CardIdleMs` ignores the kiosk | KC3 |
| **S-KC-case** | `FollowAllowed` compares case-insensitively | KC4 |
| **S-KC-clamp** | `DemoArgsValid` clamps seconds to 1..600 instead of refusing | KC5 |
| **S-KC-covers** | `CardCovers` ignores the overlay | KC6 |
| **S-KC-centre** *(new)* | `CardCentreDy` drops its "not positive: 0" rule | KC1, KC7 (a device's `s = 1` then gives `-0.02 S`, not 0) |
| **S-KC-raised** *(new)* | `LeaveFollowDue` ignores the touch on a device-raised face | KC3 |
| **S-KW2-claim** (in `check_web_wiring.py`) | the demo open calls `ClaimTappedAircraft` | KW2 |
| **S-KW2-fence** (in `check_web_wiring.py`) | the `screen-diameter-mm` read is moved outside the fence | KW2 |
| **S-KW2-return** *(new, in `check_web_wiring.py`)* | the kiosk leave of a touched device-raised face assigns `screen = followAutoReturnTo` itself | KW2 |

### On the images

| row | predicted |
|---|---|
| **KI6** *(re-frozen: five regions)* | `blipscope-s3-128` built at main and at the branch, the same tree otherwise: every differing byte lies in one of CLAUDE.md's five rebuild regions, counted per region, with nothing left over. The regions: `esp_app_desc_t.app_elf_sha256` (32 B at 0xB0), the image checksum byte, the trailing SHA-256 (32 B), and WiFiManager's two `__DATE__`/`__TIME__` strings (the about page's date and `Software Info:`). The three hash regions differ in all 65 bytes; the two strings differ in 0 or more bytes, set by the two builds' clock times, so their count is printed, not predicted. The accounting finds both strings itself and reads BLIND, not a pass, if it cannot; its control is one byte planted outside the regions, which must read outside. That is the strong form of "device behaviour unchanged". Why five and not three: v19's step 4 on 2026-11-11 stopped on 12 bytes in those two strings that a three-region read could not place |
| **KI4, extended** | `check-web-only.sh`'s `STRINGS` gains `screen-diameter-mm` and `kiosk-follow`. Every device image in CI: none of the four strings, anchor present. Every SKU's wasm, `--expect`: all four present. `--selftest` as today |

### On the wasm (`tools/web-renderer/kiosk_card.mjs`, every model, real clock, read from the frames and one state export)

The harness reads the drawn list as AF3 does. It also reads a web-only `web_state()` export: the screen, whether a card is open,
the zoom index and top, whether a session Follow is running, and whether the frame drew the sweep. It opens cards on contacts the
synthetic scene is serving at the time.

| row | setup | predicted | CONTROL, which must differ |
|---|---|---|---|
| **KW-1a** *(re-frozen: the offset)* | kiosk on, `D = "600"`, a 30 s demo card | the next drawn frame has exactly one `LAYER_BEGIN` at `(S/2, S/2 + 0.355 S, 0.25)`, within 0.001 px: `(120, 205.2)` on s3-128 and `(240, 410.4)` on s3-21, and `s` exactly 0.25. Then one `LAYER_END` after it. The card's callsign is inside the layer, at least one other contact's label is outside it, and `web_state()` reports the sweep drawn. Over the window from 2 s to 22 s after the open, **at least 3** `/blips` requests reach the hook | kiosk on, no `D`: no layer op, no sweep, and **0** `/blips` requests in that window |
| **KW-1b** *(re-frozen: the offset)* | as KW-1a, `kiosk-follow` unset | a press at the glyph's **drawn** position (`(cx, cy) + s * (p - c0)`: `y = 219.7` on s3-128, `439.4` on s3-21) starts a session Follow | a press at the glyph's **card-coordinate** position (`y = 178` / `356`, which maps to the card's top, `y = 11.2` / `22.4`) closes the card, and no Follow starts |
| **KW-2** | five instances at once, about 72 s | kiosk on: a touch-opened card is open at 59 s after the opening tap and closed by 61 s. A zoomed-in session Follow is held at 59 s after the last touch, and by 61 s it has left Follow with the zoom home. An untouched 70 s demo card is open at 69 s (so **still open at 61 s**) and closed by 71 s | kiosk off: the card is still open, and Follow and the zoom still held, at 61 s |
| **KW-3** | kiosk on, `kiosk-follow "false"` | over 30 s the card draws no `follow` text, and a swipe down on the card closes it with no Follow | `kiosk-follow` unset: `follow` drawn, and the swipe down starts Follow |
| **KW-4a** | kiosk on, `logbook "true"`, `drainEvents` into a Node `EventTarget` | `demo(hex, 5)` returns true, fires exactly one `cardopen` whose `detail` is exactly `{"by":"demo"}`, is open at 4.5 s and closed by 5.5 s, and no frame of it draws `CLAIMED`. The refusals listed above each return false and fire nothing. A touch open fires exactly one `{"by":"touch"}`, and a glyph tap fires exactly one `followstart` `{"by":"touch"}`. Compared as whole values, never as substrings | the instrument can see a claim: after the demo, a touch open of the **same** contact draws `CLAIMED`. Had the demo claimed the type, the contact's `claimFired` would already be set and that touch could not |
| **KW-4b** | `check_site_rules.mjs` on the built set | clean, as today | its `--selftest`, as today |

**The existing web checks are unchanged:** `smoke.mjs`, `carousel.mjs`, `airports_remote.mjs` and `check_wx_off.mjs`, as predicted
for main.

## Not covered, said so

- **The configured-target surfacing under `kiosk-follow "false"`** is graded by KC4 and KW2's wiring, not on the wasm. The device
  raises that face on a takeoff or landing transition, and the synthetic scene's cruising traffic makes none in a bounded run.
- **The touched device-raised face (Q-K2a)** is graded by KC3 and KW2 for the same reason: the wasm cannot raise that face.
- **How the overlay looks** (does a 150 mm card read at a venue's distance?) is a glass question. No harness answers it, and it
  needs a screen and a person.

## Rulings (reviewer, 2026-11-11)

- **Q-K1a, placement (~04:10Z): not centred.** The card's centre sits `(1 - s) * S/2 - 0.02 * S` below the screen's centre, and
  centred if that is not positive (K1 above). The screen's centre is uncovered only for `D >= 313`; the venue screens are 15.7 in
  (~399 mm) and 23.6 in (~599 mm), and a smaller screen keeps the formula (~04:39Z).
- **Q-K3a (~04:10Z): yes,** `kiosk-follow` is read only with the kiosk on.
- **Q-K4a (~04:10Z): yes,** demo opens fire `cardopen`, told apart by `by`.
- **Q-K4b (~04:10Z): yes,** demo seconds 1 to 600.
- **Q-K2a (~04:10Z): yes,** the device-raised Follow face is exempt, **and once a visitor touches it, the 60 s rule runs from
  that touch** (K2 above).
- **KI6 names all five rebuild regions (~04:39Z).**

**Outside the set, at any row:** any other row red, a plant that turns no row red, an anchor that does not match exactly once, a
BLIND scan or BLIND accounting, or KI6 with a byte outside the five regions. I stop and report, and nothing is improvised.

## CI prediction for this PR (docs only)

**32 = 29 success / 3 skipped / 0 failed**, from `pull_request` events only (device-ids, firmware, no-traffic-captures), the
same as the first freeze, which read exactly that at `4d0da6765abb`. Main's workflows have changed once since: #633 (`e2c34daf82a1`)
added two steps to device-ids' existing job, and a step is not a job, so the job set is unchanged.
