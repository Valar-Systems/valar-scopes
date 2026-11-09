# The web build's kiosk and the venue's airfield

**Read this first.** app.valarscopes.com runs the WebAssembly web build full screen and always on, on large round Android
screens. Two web-build changes were **ruled 2026-11-08 ~20:01Z, predictions first**:
- **A `kiosk` key**, which keeps `/blips` at the active cadence with no touch, day and night. The weekly-reboot check becomes a no-op
  in the web build, and a string scan shows `kiosk` in no device image.
- **An `airports-remote` key**, which lets the web build fetch `/airports` from `apiBase` so the venue's own field is drawn. It is off
  by default.

Devices and the public website are unchanged by construction. This page freezes the design and the predictions before any code.

## Kiosk

**Today** (`CurrentPollIntervalMs()`, `src/AircraftManager.cpp`): active if touched within `idleAfterMs`, else night by the solar
clock if the clock is valid, else idle. The web build never fetches `/config`, so those are 5 s, 60 s and 15 s, and a page load
counts as a touch. Separately, the weekly-reboot check logs and calls `ESP.restart()`, which the web build ignores, on every night-time frame
once uptime passes 7 days and no one has touched it for 10 minutes. An always-on screen meets every one of those conditions.

**The design:**
- **A new header, `include/PollCadence.h`,** holds both rules as pure functions, beside a compile-time `kWeb` that is true only
  under `BLIPSCOPE_WEB_RENDERER` (the shape of `include/WxLayerSwitch.h`):
  - `IntervalMs(...)` is today's rule, unchanged, plus: when the kiosk is on, the active rate, always;
  - `KioskOn(stored)` is `kWeb && stored == "true"`, so a device build cannot turn it on;
  - `PreventiveRebootDue(...)` is today's condition, unchanged, and `false` whenever `kWeb`.
- **`AircraftManager`** calls the three. It reads the `kiosk` key only inside `#ifdef BLIPSCOPE_WEB_RENDERER`, so the literal
  `kiosk` exists only in the web build.

| row | where | predicted |
|---|---|---|
| **KI1** | host, device build | `IntervalMs` over the grid (touched or not, night or day, clock valid or not, kiosk on or off) equals today's rule exactly, kiosk ignored. `KioskOn("true")` is false |
| **KI2** | host, web build | kiosk on: the active rate in every case. Kiosk off: KI1's rule. `KioskOn` is true for exactly `"true"`, and false for `""`, `"false"`, `"1"` and `"TRUE"` |
| **KI3** | host, both builds | `PreventiveRebootDue` in the device build equals today's condition over its grid, and in the web build is never true |
| **KW1** | host, `test/host/check_web_wiring.py` | `CurrentPollIntervalMs` returns through `pollcadence::IntervalMs`, the weekly block tests `pollcadence::PreventiveRebootDue`, and the one read of `kiosk` sits inside `#ifdef BLIPSCOPE_WEB_RENDERER` through `KioskOn` |
| **KI4** | images, `scripts/check-web-only.sh` | `kiosk` and `airports-remote` appear in no shipping device image (anchor `[build] env=`; no anchor is BLIND). Positive control: both appear in the web build's wasm. Its `--selftest`: a planted string fails, and an anchor-less file is BLIND |

**Baseline, read before freezing:** `kiosk` and `airports-remote` are in no source file on main. Three device images from the bench
tree (`blipscope-s3-128-wxkey`, `-wxclear` and `-crashbench`) hold 0 of each, with the anchor present.

**Plants** (`test/host/sabotage_poll_cadence.py`, in copies; the real header's sha256 is compared before and after):

| plant | the edit | red, exactly |
|---|---|---|
| **S-KI-device** | `KioskOn` ignores `kWeb` | KI1 |
| **S-KI-ignored** | `IntervalMs` ignores the kiosk | KI2 |
| **S-KI-reboot** | `PreventiveRebootDue` ignores `kWeb` | KI3 |
| **S-KI-order** | night outranks a recent touch | KI1, KI2 |
| **S-KW1** (in `check_web_wiring.py`) | `CurrentPollIntervalMs` keeps its own copy of the rule | KW1 |

**Added before any test ran, 2026-11-08 ~20:50Z: KI5, and what it changes.** Today's code calls `isNightNow()`, a solar
calculation, only when the conditions before it hold: for the weekly reboot that means almost never, since uptime is under 7 days.
A helper that took `nightNow` as a plain `bool` would compute it on every device frame. So both helpers take the night test as a
function, called last.

| row | where | predicted |
|---|---|---|
| **KI5** | host, device build | the night function is called only where today's code calls it. In `IntervalMs`: never after a recent touch, never with an invalid clock, and once otherwise. In `PreventiveRebootDue`: never unless every earlier condition holds, and then once |

| plant | the edit | red, exactly |
|---|---|---|
| **S-KI-eager** | `PreventiveRebootDue` evaluates the night test first | KI5 |
| **S-KI-order**, amended | night outranks a recent touch, so the night test now runs before the touch is read | KI1, KI2 and **KI5** (the frozen set was KI1 and KI2, written before KI5 existed) |

## The venue's airfield

**Today:** the firmware asks for `/api/v1/blipscope/airports?lat&lon&r`, and a reply replaces the baked table of 329 airports.
The web shim answers it with a local 404, because only `/blips` and `/enrich/` are forwarded (`web_http.cpp`).

**The design:**
- **A pure header, `tools/web-renderer/web/web_route.h`,** with `Forwarded(path, airportsRemote)`. It returns true for `/blips` and
  `/enrich/` as today, and for `/airports` only when `airportsRemote` is true. `Route()` asks it, with the flag read from the web
  config's `airports-remote` key: exactly `"true"` enables it, and anything else or unset leaves it off.
- **The app serves `/airports`.** Its reply replaces the baked table, so it must list every airport it wants drawn, the big ones
  included. Codes are up to 4 characters.

| row | where | predicted |
|---|---|---|
| **AF1** | host | key off: `/airports` is not forwarded, `/blips` and `/enrich/` are, and nothing else is |
| **AF2** | host | key on: `/airports` is forwarded too, and nothing else is added |
| **AF3** | headless wasm, every model, `tools/web-renderer/airports_remote.mjs` | key on, with the page's fetch hook answering `/airports` with one EXAMPLE field `XFLD` at the configured location: the draw list's text holds `XFLD`. CONTROL, key off: no `/airports` request reaches the hook, and `XFLD` never appears |

| plant | the edit | red, exactly |
|---|---|---|
| **S-AF-always** | `/airports` forwarded whatever the key | AF1 |
| **S-AF-never** | `/airports` never forwarded | AF2 |

## Not covered, said so

- **The kiosk's end-to-end cadence on the wasm** is not graded. The harness runs on the real clock, and a page load counts as a touch,
  so idle and night are 10 minutes or more away. The rule is graded on the host (KI1-KI3) and its wiring by KW1.
- **The weekly-reboot no-op** is graded on the host (KI3), not on a 7-day wasm run.

**Outside the set:** any other row red, a plant that turns no row red, an anchor that doesn't match once, or a BLIND scan. I stop and
report, with nothing improvised on the spot.

## Results (2026-11-08, 20:51-21:13Z, local): as predicted on every row

| row or plant | where it ran | read |
|---|---|---|
| **KI1, KI2, KI3, KI5, AF1, AF2** | WSL g++ 13.3, `run.sh`'s C++11 flags | all green |
| **the seven plants** | `sabotage_web_kiosk.py` | each red in exactly its rows: S-KI-device KI1; S-KI-ignored KI2; S-KI-reboot KI3; S-KI-order KI1, KI2, KI5 (as amended); S-KI-eager KI5; S-AF-always AF1; S-AF-never AF2. The real headers unchanged |
| **KW1** | `check_web_wiring.py --selftest` | ok, and every plant refused, S-KW1 among them |
| **AF3** | a local s3-128 wasm (Emscripten 6.0.13, CI's version) | key on: `/airports` reached the hook once, and `XFLD` was drawn. CONTROL, key unset: 0 requests, `XFLD` never drawn |
| **KI4, device** | a local `blipscope-s3-128` build (1,932,064 B) | no web-only key, anchor present |
| **KI4, control** | the same wasm | both keys present, anchored on `/api/v1/blipscope/blips` |
| **the existing web checks** | the same wasm | `smoke.mjs` and `carousel.mjs` as predicted |
| **the full host suite** | WSL `run.sh` | exit 2 from exactly its two environment blocks ("the same TU, for the board" and "Weatherscope R0 and R0b"), `sabotage_wx_towns.py` SKIP, everything else green, this block included |

**One instrument fix, not a code fix.** The driver's first run stopped as the RIG (exit 2), on no row: S-KI-ignored's plant removed the only
use of `kiosk`, and `-Werror=unused-parameter` refused to compile it. The plant now keeps the parameter referenced with `(void)kiosk`,
as the S-AF plants already did. What it sabotages is unchanged.

**The build's warnings are older than this change.** Of the wasm build's six warnings, two are at `AircraftManager.cpp:4114`, which
is main's line 4108 moved by the six lines added above it, and the other four are in the variant header and the shim.

## CI prediction for the PR

**34 = 30 success / 4 skipped / 0 failed.** The PR touches `src/`, `include/`, `tools/web-renderer/**`, `test/host/`, `scripts/` and
two workflows, adding steps and no jobs. That is #582's and #609's shape: the docs-only base of 32 = 29/3/0, plus
`web-renderer`'s `build` and `publish` (skipped on a PR). It touches nothing under `proxy/`, so `workers.yml` does not run. The last
job-set change on main is `84409651`.
