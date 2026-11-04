# The web renderer's carousel: Radar and List

**Status:** the design and its predictions, frozen before the code. The web renderer itself is
[docs/web-renderer.md](web-renderer.md).

## The request (Daniel, 2026-11-04)

> leave the Connect screen out (it's the device's Wi-Fi/setup screen and means nothing on a website). Do it with a
> web-build-only compile flag, so device builds stay byte-identical (prove it). Also tell me what the Stats screen shows
> in the web build. If it's device internals (heap, uptime, Wi-Fi, fetch counters), leave it out the same way.

## What the Stats screen shows, read from `DrawStats` (`src/AircraftManager.cpp`)

| block | what it is | on a website |
|---|---|---|
| `STATS`, *n* aircraft; High, Fast and Near with their callsigns | a summary of the current sky | meaningful, but the site's own list can show it |
| TODAY: contacts, peak, busiest hour and its chart | counted since local midnight **on the device** | counts only since the page loaded, so it is misleading |
| AIRCRAFT OF THE DAY, LIFELIST, LEADERBOARD | logbook and leaderboard | off in the web build, never drawn |
| FEED: the source, the data age, STALE; poll interval and fail count | device health | device internals |
| Tilt (2.1 in only) | the IMU | device internals |
| Wi-Fi network, or NOT CONNECTED; `<name>.local` | the device's network | device internals |

**So Stats is left out as well.** The web build's carousel is **Radar and List**.

## The design

- **One rule, in `AircraftManager.h`:** `InCarousel(Screen)` and `CarouselCount()`, both `constexpr`.
  - **On a device:** the rule is today's, `s != Screen::Follow`, so the count is 4.
  - **Under `BLIPSCOPE_WEB_RENDERER`** (set only by `tools/web-renderer/build.sh`, never by a PlatformIO env), it is
    `Radar || List`, so the count is 2.
- **Its three readers in `AircraftManager.cpp`, each line edited in place** (no line added or removed, so no line
  number moves):
  - `AdvanceScreen`'s skip, which was "Follow";
  - the dots: their count (was `SCREEN_COUNT - 1`) and their skip (was "Follow");
  - the boot landing on Connect when no location is set (it now also requires that Connect is in the carousel).
- **Why the dots are in it.** A rule in `AdvanceScreen` alone would leave the web build showing four dots for two
  screens. That is a promise the device does not keep, the reason the dots already skip Follow.
- **Nothing else in the web build reaches Stats or Connect:**
  - Follow returns to the screen it was raised from, which in the web build is Radar or List;
  - the serial screen-stepper is `TOUCH_WEDGE_BENCH` only.

## Predictions

| | check | predicted |
|---|---|---|
| **WC1** | device images: `blipscope-s3-128`, `blipscope-s3-146`, `blipscope-pro-s3-21`, `blipscope-pro-s3-175-amoled`, `blipscope-s3-128-joindiag`, each built at this branch and again with main's two files | the same size, differing **only** in the rebuild regions (`app_elf_sha256`, WiFiManager's `__TIME__`, the checksum and trailing SHA-256); **zero payload bytes** |
| **WC2** | the web build, s3-128 and s3-21, driven headless from Radar | six swipes left visit List, Radar, List, Radar, List, Radar, and six swipes right do the same. **No frame** draws `STATS`, `CONNECT`, `SET LOCATION` or `http://`. Every frame draws **exactly 2** indicator dots |
| **WC3** | the web build with no location configured | lands on Radar, not Connect: no `CONNECT`, `SET LOCATION` or `http://` in its frames |
| **WC4** | the web smoke test (R3-R6), both models | unchanged, all as before |
| **WC5** | CI on the PR | its total predicted from the last finished run's job list, before the push |
| **WC6** | the release | see below |

**WC6, the tag.**
- **Why v18 cannot carry it:** a release's web set is built from **that tag's own firmware source**
  (`web-renderer.yml`, the release checkout). This change is in `src/`, so v18's published web set does not have it.
- **What does:** the first release tag cut after this merges (expected **v19**) gets it on publish, through the
  `release: published` trigger, with no further step.
- Re-publishing v18's web set from later source would break "v18's web set is v18's firmware". That is not proposed.

**Outside the set:** stop and report.

**Added before any run: WC2's control.**
- **The check:** `tools/web-renderer/carousel.mjs` drives the swipes and reads each frame's own text and dots.
- **The control:** it is also run against a web build of **main's unchanged** `AircraftManager`, with the same tools
  and the same flag. There it must **fail**: Stats or Connect reached, and 4 dots.
- **Why it is needed:** a check that passes on both builds proves nothing about the change.

## Results (2026-11-04, local; WC4 and WC5 read from the PR's CI)

| | read | verdict |
|---|---|---|
| **WC1** | the five device envs, each built at this branch and again with main's `AircraftManager.h` and `.cpp` (the second builds recompiled both `AircraftManager.cpp` and `main.cpp`, so the comparison is not vacuous). Each pair is the **same size** (1,929,856 / 1,942,912 / 1,925,824 / 1,888,464 / 1,920,944 B) and differs **only** in `app_elf_sha256` (32 B) and the checksum and trailing SHA-256 (32-33 B). **Zero payload bytes.** WiFiManager was not recompiled, so even its `__TIME__` matches | **as predicted** |
| **WC2** | `carousel.mjs`, s3-128 and s3-21: starts on Radar; six swipes left read List, Radar, List, Radar, List, Radar, and six right the same; no frame draws Stats or Connect; every frame read shows **2** dots | **as predicted** (both models) |
| **WC3** | no location configured: the face is Radar (its own "SET YOUR LOCATION" note), not Connect | **as predicted** (both models) |
| **WC2's control** | the same check on web builds of **main's** `AircraftManager`, both models: left reads List, Stats, Connect, Radar, List, Stats; **4** dots; with no location it lands on **Connect**. 4 checks fail, exit 1 | **fails, as it must** |

**One instrument stall, recorded:**
- **What happened:** the first control run (s3-128, inside a shell loop piped to `tail`) sat idle for 20 min with 1 s of CPU, and was stopped.
- **What followed:** the same build then completed twice, once under a progress-printing copy and once under the unmodified check with a time limit, in under 4 min each. The verdicts are above.
- **The alternative, and what excludes it:** a hang in the firmware under main's code. A deterministic one would recur, and the same module completed twice.
- **The cause is not identified,** and the rig (pipe, terminal or timer) is not excluded.

**WC5's count, predicted before the push** from the last finished run's job list (#524's head, `ef3a38d0058e`: 40 checks, 36 success, 4 skipped):
- **This PR runs the same four workflows,** `web-renderer` among them because its paths include `src/**`.
- **This push adds commits to an existing branch.** Its diff matches no branch-agnostic push filter, so no push run is expected.
- **The prediction:** **40** checks, **36 success** and **4 skipped**, 0 failed.
- **WC4** is read from the `web-renderer` job's smoke step (R3-R6, both models), and the new carousel step must pass too.

The local smoke run (WC4) was not made: the bench PC's disk directive of 2026-11-04 ~20:30Z holds new runs. CI makes
it off this machine.
