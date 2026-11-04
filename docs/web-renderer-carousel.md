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
