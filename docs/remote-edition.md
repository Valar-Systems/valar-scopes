# Remote edition -- plan, rulings and records

The Remote edition (`-DFEATURE_REMOTE`, working name "Remote") turns a 1.28" round board into a
desk control for Valar window and curtain openers on the local network: Glasscalibur (window)
and the Ropener (curtain). Control and status only -- no radar, no clock face, no data feed, no
ntfy, no MQTT. User-facing documentation is `docs/remote.md` (lands with the feature PR); this
file is the engineering record: what was established, what was ruled, what was predicted, and
what happened.

Privacy rule for this file and every record it points at: no LAN address, and no hostname's
unit suffix. A unit is written `glasscalibur-XXXXXX`, never its real suffix.

## 1. What the devices expose (research, 2026-10-30)

Established from the RELEASED images (Glasscalibur v2.1.0, Ropener v2.9.0), not from the YAML:
both carry `ESPHome version 2026.6.5`. Neither product repo pins ESPHome (their release CI runs a
bare `pip install esphome`), so the next release of either will be built on whatever is current.

| | Glasscalibur | Ropener |
|---|---|---|
| mDNS | `_esphomelib._tcp`, TXT `project_name=valar-systems.glasscalibur`; host `glasscalibur-XXXXXX` | TXT `project_name=valar-systems.ropener`; host `ropener-XXXXXX` |
| HTTP | ESPHome `web_server` v3, port 80, no auth | same |
| cover entity | `Glasscalibur` | `Ropener` (`device_class: curtain`) |
| State text sensor | IDLE / OPENING / CLOSING / TAMPERED | IDLE / OPENING / CLOSING / HOMING |
| tamper | `Tamper Detected` binary sensor (latched) | none |
| obstruction | no entity; `StallGuard Trips` / `Vibration Stall Trips` counters, published every 30 s | none |

The 2026.6.5 web server (source at that tag, `components/web_server/web_server.cpp`):

- `GET /events` is Server-Sent Events. On connect: a `ping` carrying the page config, the
  sorting groups, then one `state` event per entity with full detail; afterwards a `state` event
  on every change and a `ping` every 10 s. Every state carries `name_id` (`cover/Glasscalibur`)
  and the legacy `id` (`cover-glasscalibur`); the source says `id` changes format in 2026.8.0.
- Cover state: `{"name_id":"cover/Glasscalibur","value":0.5,"state":"OPEN","current_operation":"IDLE","position":0.5}`.
- Commands: `POST /cover/<name>/open`, `/close`, `/stop`, `/set?position=<0..1>`.
- URLs are matched by entity NAME. The object-id form the Glasscalibur wiki documents
  (`/cover/glasscalibur/open`) still works on 2026.6.5 but logs "Object ID URLs will be removed
  in 2026.7.0". The remote therefore takes the cover's name from the event stream and uses
  name URLs, falling back to the object-id form only for firmware whose events carry no `name_id`.

Device-side findings (security, a 15-minute reboot, back-off reported as IDLE, calibration not
aborted by a cover STOP, no obstruction entity, unpinned ESPHome) went to Daniel as a separate
list for the product repos. This edition does not edit those repos and does not work around them.

## 2. Rulings

**2026-10-30, reviewer.** Build against the devices as shipped, with per-device credential fields
ready (device-side auth is a product fix that comes before the remote's release). Obstruction:
Glasscalibur's trip counters, nothing for the Ropener, no log-line matching. Tamper is
acknowledged on the remote only; the remote never silences the window's buzzer. STOP may be
resent up to 3 times in 1.5 s while the device still reports moving; open and close are never
retried. Touch: a hold starts any motion (default 1000 ms for windows, 600 ms for curtains);
single tap is a per-device opt-in with a one-line warning; STOP stays on touch-down. A command
log (time, gesture, device name; no addresses) on serial and in a short ring the config page
shows. The quiet hour runs on local time, derived as the radar derives it. The native ESPHome
API with per-unit encryption keys is the secure route later; it is noted in `docs/remote.md`, not
built. The scaffold lands first as its own small PR, predictions first.

**2026-10-30, addendum.** The remote ships on the 1.28" board (s3-128). One env,
`remote-s3-128`; no env for the 2.1" or 1.46" panels. Layout stays in `Layout.h`'s terms so a
bigger panel can follow. The 240 x 240 face is shown as host renders before any board run. The
s3-128 stack is reused as it stands (TouchPoll, the CST816 handling and TouchWatchdog's re-arm,
Layout, Wi-Fi setup, the config page shell, NVS, OTA, SolarDim). The test board is one of
Daniel's stock 1.28" units, named by its USB serial -- not E2:08 and not the Weatherscope board.

## 3. The scaffold PR

### What it changes

Shared files, each change mechanical:

- `platformio.ini`: `-<remote/>` joins `[filters] editions_off`; new `[env:remote-s3-128]`
  (the s3-128 board flags and big-NVS table, `-DFEATURE_REMOTE`, `-DFW_OTA_PREFIX=\"remote-\"`,
  `+<remote/>`), modelled on `[env:weatherscope-s3-128]`.
- `skus.yml`: one `build-only` row, no slug -- nothing publishes, nothing can withhold the fleet's
  `version.txt`.
- `scripts/check_release_envs.mjs`: `FEATURE_REMOTE` joins `EDITION_FLAGS`, so the new row is
  skipped as an edition rather than failed as a radar SKU with no cloud feed.
- `src/main.cpp`: the include and the `appManager` declaration; `&& !defined(FEATURE_REMOTE)`
  appended to the six radar-only guard lists; and a Remote-only hold on the quiet hour's
  reboot-arming site (`appManager.HoldsUpdate()`, which the scaffold's manager answers `false`).
  That hold is the only update path this edition has: the on-demand check is radar/cloud-only.
- `src/ConfigurationWebServer.cpp`: the same guard lists; Remote joins the "no geography, do not
  nag about location" list; a Remote branch of the page, its load, processor and save. The page
  holds location, a UTC offset, brightness and auto-dim. The device list arrives with the feature PR.
- `include/BuildIdentity.h` (feature `remote`, wordmark `REMOTE`), `include/DeviceIdentity.h`
  (network name `Remote-XXXXXX`), `include/FactoryReset.h` (no logbook), `src/SoakHarness.h`
  (radar-only harness refuses this edition too).
- `CLAUDE.md`: a short FEATURE_REMOTE section.

Edition-local: `src/remote/RemoteManager.{h,cpp}` -- a splash that says no devices are added yet
and where to add them, brightness with SolarDim auto-dim, and the touch poll feeding
`TouchWatchdog::OnPoll` exactly as the radar does. (Today the radar is the only caller of
`OnPoll`; no other edition re-arms the s3-128's CST816.)

### The quiet hour on local time

Derived as the radar derives it, from the same keys, so `main.cpp`'s `QuietHourOffsetSec()` reads
them unchanged: an explicit `tz-offset` wins; an empty field means auto, and auto is the ABSENCE
of the key (the radar's v11 rule, so a 0 nobody typed is never stored); auto is the nominal zone
from `longitude`. Unlike the radar, location is optional here. With neither location nor offset
set, the quiet hour falls to 03:00 UTC, and the page's placeholder says "Auto - set your location"
exactly as the radar's does.

### Method

All builds in one worktree, one path, so `__FILE__`-bearing library code cannot differ by path.

- **P** = the commit adding this section (predictions), on `origin/main` d256ff0.
- **I** = P + the `platformio.ini` and `skus.yml` changes only.
- **B** = every one of the 43 existing envs built at I from clean.
- **S** = the same 43 envs rebuilt INCREMENTALLY at the scaffold head, with no clean in between.

Why I comes before B: PlatformIO wipes `.pio/build` when `platformio.ini` changes, and a full
rebuild re-stamps WiFiManager's `__DATE__`/`__TIME__` (CLAUDE.md's 75-byte accounting). An ini
change between B and S would make byte identity unreachable by construction, so it is measured
on its own, as P0. None of the C++ files the scaffold edits contains a `__LINE__`, `__FILE__`,
`assert` or `log_x` site (checked by grep before writing this), so line shifts cannot move bytes.

### Predictions (frozen before any build)

**P0 -- the ini change is inert for existing envs.** PlatformIO's own `ProjectConfig`, fully
expanded, dumped at P and at I. The only differences: the new `env:remote-s3-128` section;
`[filters] editions_off` gains `-<remote/>`; and the `build_src_filter` of each env that expands
`editions_off` gains that same token. No other option of any existing env changes.

**P1 -- the baseline builds.** All 43 existing envs build from clean at I. If one does not, it
is recorded with its first error line and not fixed here; for that env P2 reads "fails at S
with the same first error line".

**P2 -- every existing image is unchanged.** For each env that builds at B, its
`firmware.bin` at S is byte-identical to B's (`cmp`, zero differing bytes).

P2 is exactly the observation a rebuild that never happened would also produce, so three
controls are run with it:

- **C2a -- the rebuild happened.** Every env whose B log compiled `src/main.cpp.o` compiles it
  again at S. The set is taken from B's logs (measured, not assumed); the prediction is that the
  S set equals the B set.
- **C2b -- the filter keeps `src/remote/` out.** No existing env's S log compiles anything under
  `src/remote/`. (Byte identity alone cannot show this: unreferenced code is garbage-collected.)
- **C2c -- the comparison can fail.** One character added to a radar-reachable string literal in
  `main.cpp`, `blipscope-s3-128` rebuilt: `cmp` reports at least one differing byte. Restored from
  a byte copy, sha256 checked, rebuilt: identical to B again.

**P3 -- the new env.** `remote-s3-128` builds. Its image contains `[remote] init` and
`<title>Configure Remote</title>`, and contains neither `<title>Configure Speedscope</title>` nor
`<title>Configure Blipscope</title>`. Anchor control: the same scan finds
`<title>Configure Speedscope</title>` in `speedscope-s3-146`'s B image, so the scanner can see a
page title in these images at all.

**P4 -- the checkers.**

| check | predicted |
|---|---|
| `python scripts/skus.py --validate` | `18 leg(s), 1 shipping (s3-128), 17 build-only.` |
| `python scripts/check-config-form.py` | exit 0, `10 page(s)`, one of them `Configure Remote` |
| `node scripts/check_release_envs.mjs` (and `--selftest`) | exit 0 (and exit 0) |
| `node scripts/check_photo_mode.mjs` | exit 0 |
| `python scripts/skus.py --check-readers` | `15 reviewed mention(s)`, unchanged |

Two of these registrations must be shown to be load-bearing, by relaxing each and watching the
check go red (then restoring):

- **C4a.** A second `<form>` planted in the Remote page: `check-config-form.py` exits 1 and names
  `Configure Remote`.
- **C4b.** `FEATURE_REMOTE` removed from `EDITION_FLAGS`: `check_release_envs.mjs` exits
  non-zero and names `remote-s3-128`.

**P5 -- CI on the PR.** Every job green, including a `remote-s3-128` build leg whose adsbdb
launch gate reads PASS (the gate's default plant site, `src/ConfigMigration.cpp`, is compiled into
this env). A CI tally is read only when every check run has completed, against `total_count`.

**P6 -- on a board (deferred until Daniel names one).** Flashed `remote-s3-128`, the boot banner
reads `env=remote-s3-128`, and the splash says no devices are added yet. With `longitude`
-121.3 saved and `tz-offset` left blank, the quiet-hour boot line reads
`tz-offset=unset (-28800 s)`. With `tz-offset` -7 it reads `(-25200 s)`.

**Outside the set.** Any result not listed above -- an existing image that differs, a byte
count other than zero, a checker count other than the one stated, a P0 difference anywhere else,
a control that does not fire -- means the instrument or the change is not what this section
says. Work stops, the result is recorded as it is, and nothing is fixed until the reviewer rules.

### Results

(Appended after the runs; predictions above are not edited.)

**P0 -- OFF-PREDICTION (2026-10-30). Work on the measurement chain stopped here for a ruling.**

Dumps: at P (751cc53) and at I (98c82b2), PlatformIO `ProjectConfig` with interpolation
expanded (no `${` left in either; sha256 of the two dumps begin `56220866b643` and
`30be43b98bae`). Observed differences:

| difference | predicted? |
|---|---|
| new section `env:remote-s3-128` | yes |
| `[filters] editions_off` gains `-<remote/>` | yes |
| `build_src_filter` of 29 existing envs gains exactly that token, nothing else in those envs | yes |
| **`[common] build_src_filter` gains exactly that token** (inserted once, nothing else) | **no** |
| any other option of any existing env | none observed |

The 14 existing envs with no difference are the ones whose filter does not expand
`editions_off`: the three animtest envs, gametest-s3-128, hwtest-s3-146, the eight probe envs, and
weatherscope-bench-s3-128.

What the prediction got wrong: it enumerated `[filters]` and the env sections and left out
`[common]`, which also expands `editions_off` (it is the radar's default source set, line
"build_src_filter = +<*> ${filters.editions_off} ..."). The predicted SET was incomplete; the
diff tool classified the row by section and flagged it, as written. No build has been run.
Not continued: B, S, and every prediction after P0, until the reviewer rules.

**Ruling (reviewer, 2026-11-04 ~04:45Z).** P0 is re-registered with the miss kept: its predicted
set gains "`[common] build_src_filter` gains the same `-<remote/>` token". Continue with P1 and P2
as frozen; P2's byte-identical images are the real proof that the change is inert. (Same ruling:
both bench boards logged 0 touches over 7 unattended hours on 10-31, and a hold to start motion
stays the default anyway, as a motor-safety choice.)

## 4. Feature step F1 -- the safety policy (`src/remote/CoverPolicy.h`), pure and host-tested

Written before the header exists. The policy is where every motion rule lives, once: the manager
feeds it touch edges and the link's snapshot of the device on screen, and it says what each edge
means. It needs no chip, clock, socket or screen, so every rule is graded on a host
(`test/host/test_remote_policy.cpp`) before it reaches the glass. C++11, like every host-tested header.

### The rules

- **Only a touch commands.** Intents come out of the touch handlers (`OnPress`, `OnHeld`,
  `OnRelease`) and the idle tick (`OnIdle`, which only ends a stroke already in progress). A change
  in the device's state, a reconnect or a boot produces nothing.
- **STOP on touch-down.** If the device on screen is MOVING, the press edge itself is STOP, and the
  rest of that stroke is consumed (no hold, no tap, no swipe, no ack). MOVING is any of: the
  device reports OPENING/CLOSING (from its last snapshot, even if it is now offline -- a STOP is
  attempted when it was last seen moving); its position changed in the last `SETTLE_MS` = 1500 ms
  (Glasscalibur's stall back-off moves while reporting IDLE); or this policy emitted OPEN/CLOSE in
  the last `EXPECT_MS` = 2000 ms (so the first touch after a command is always STOP, even before
  the device has reported the motion).
- **Motion under the finger.** If a held, not-yet-fired stroke sees the device start MOVING, that
  frame is STOP and the stroke is consumed. A stroke that has already FIRED its own OPEN/CLOSE is
  consumed and never stops the motion it started.
- **A deliberate gesture starts motion**, per device: `Hold` (the default; 1000 ms for a window,
  600 ms for a curtain, clamped to 400..3000), `DoubleTap` (opt-in) or `SingleTap` (opt-in, with a
  warning on the page). The measured CST816 constants come from `include/LongPressPolicy.h`, one
  source: the arc appears at 250 ms (above the recorded tap p99 of 183 ms); a release while holding
  waits `REJOIN_MS` = 150 ms for a dropout to rejoin within `REJOIN_PX` = 20 px, progress kept;
  40 px inside the first 250 ms is a swipe, drift up to 80 px after it is still the hold.
- **Where.** Split layout: the upper half is OPEN, the lower half CLOSE, and a 10 px band either
  side of the centre line is neither. Face-button layout: the whole face is one target, resolved at
  the fire to the cycle's next step -- fully closed (<= 5 permille) opens, fully open (>= 995)
  closes, stopped part-way reverses the last direction, and with no known direction it heads for the
  farther end (below 500 opens, otherwise closes).
- **When motion may start.** Only while the device is LIVE (a full state dump since the last
  connect), not MOVING, with no alarm awaiting acknowledgement -- and the device must have been LIVE
  continuously since the press, so a hold that spans a reconnect never fires.
- **Alarm takeover.** While an alarm awaits acknowledgement, a release that did not travel 40 px
  (any duration) acknowledges it; nothing else is offered. STOP still comes first.
- **Swipes** (horizontal, 40 px) move between devices in any connection state; vertical does nothing.
- **STOP resend.** After a STOP, while the device still reports MOVING, STOP is resent every 500 ms,
  at most 3 times -- the last at 1.5 s. A new STOP restarts the count. OPEN and CLOSE have no resend path.

### Predictions (frozen before the header is written)

The suite prints one line per named check and ends `N checks, M failures`. On the clean header,
built with run.sh's flags under BOTH MinGW g++ 5.1 and WSL Ubuntu-24.04 g++ 13.3: every check
passes, exit 0. Cases, by name:

- **A1** OnIdle every 52 ms through a scripted 60 s timeline (Unknown, Live idle, Live OPENING with
  the position changing, Live idle, Offline, Unknown, Live, an obstruction, a tamper) with no
  stroke: every return is None (control: at least 1000 calls were made).
- **A2** a hold begun Live; the device goes Offline at 400 ms and Live again at 600 ms; the finger
  stays down to 2000 ms: no OPEN or CLOSE.
- **A3** a hold begun while Unknown; the device goes Live at 300 ms; held to 2000 ms: no OPEN or CLOSE.
- **B1** device Live and OPENING: a press at each of 4 points (upper, lower, centre band, upper
  left) x 2 layouts x 3 gestures x alarm pending or not = 48 presses, each STOP on the press edge,
  cause TouchDownWhileMoving.
- **B2** each of those 48 strokes then held to 3000 ms and released: no further intent (48 intents
  in all, every one STOP).
- **B3** Live, IDLE, position changed 1000 ms before the press: STOP. 1600 ms before: not STOP.
- **B4** the policy emitted OPEN at t, device still IDLE: a press at t+1900 is STOP; at t+2100, not.
- **B5** Offline, last seen OPENING: a press is STOP.
- **B6** a split hold on the upper half at 500 ms; the device starts OPENING: that frame is STOP,
  cause MotionBeganUnderFinger, and the stroke yields nothing after it.
- **B7** a hold fires OPEN at 1000 ms; the device starts OPENING at 1100 ms, finger down to 1500 ms:
  no STOP from that stroke.
- **C1** window, split, Live idle, upper: OnHeld at 999 ms None, at 1000 ms OPEN (cause Hold), then
  nothing through the release. Lower: CLOSE. One intent each.
- **C2** curtain (600 ms): at 599 None, at 600 the intent.
- **C3** released at 990 ms with no rejoin, idle to 2000 ms: no intent.
- **C4** released at 500 ms, re-pressed 100 ms later within 20 px: OPEN at exactly 1000 ms from the
  ORIGINAL press.
- **C5** re-pressed 160 ms after the release: nothing at 1000 ms from the original; the new press is
  its own hold and fires 1000 ms after itself.
- **C6** after 250 ms, drift of 79 px: still fires. Drift of 80 px (leftwards): no fire, and its
  release is a swipe (Next).
- **C7** a press in the centre band held to 3000 ms: no intent.
- **C8** a hold where the device goes Offline at 600 ms: no fire. A hold where an alarm becomes
  pending at 600 ms: no fire.
- **C9** ClampHoldMs: (Window, 0 = unset) 1000; (Curtain, 0) 600; 100 -> 400; 5000 -> 3000; 1200 stays.
- **D1** Hold gesture: a tap on the upper half, then 1 s idle: nothing.
- **D2** SingleTap: a tap on the upper half is OPEN on the release edge (cause Tap); lower CLOSE;
  centre band nothing.
- **D3** DoubleTap: one tap then 1 s idle: nothing. Two taps, the second pressed 300 ms after the
  first release, same half, within 30 px: OPEN on the second release (cause DoubleTap). Second tap in
  the other half: nothing. Second press 450 ms after: nothing.
- **D4** SingleTap or DoubleTap while Offline, and while Unknown: nothing.
- **E1** face layout, Hold, held to the fire anywhere including the centre band: position 0 OPEN;
  1000 CLOSE; 400 after OPENING CLOSE; 400 after CLOSING OPEN; 300 with no known direction OPEN; 700 CLOSE.
- **E2** face layout: SingleTap's tap follows the same mapping; Hold's tap does nothing.
- **F1** a swipe left (60 px in 150 ms) is Next, right is Prev -- Live, Offline and Unknown alike.
- **F2** a vertical swipe: nothing.
- **G1** alarm pending, not moving: a tap is Ack; a 2000 ms press released in place is Ack on the
  release and never OPEN/CLOSE; a swipe is nothing.
- **G2** alarm pending AND moving: the press is STOP, its release is nothing (no Ack); a new tap is Ack.
- **H1** STOP at t0, still moving: resends at t0+500, +1000, +1500 and none after (3 in all).
- **H2** not moving at t0+500: no resend then or later.
- **H3** a new STOP at t0+700 restarts the count: 3 more resends after it.
- **I1** OPEN emitted at t, the device never moves, OnIdle for 10 s: no further OPEN or CLOSE.
- **J1** ControlsEnabled: Unknown false; Offline false; Live idle true; Live OPENING false; Live idle
  with an alarm pending false; Live idle with the position changed 1000 ms ago false.

### Sabotages (each planted alone in a COPY of the header; the real file is hashed before and after)

Each must fail EXACTLY its named set and every other check must pass:

| plant | what it breaks | predicted red set |
|---|---|---|
| SB1 | `OnPress` no longer turns MOVING into STOP | B1, B2, B3, B4, B5, G2 |
| SB2 | MOVING ignores the position-settle term | B3, J1 |
| SB3 | MOVING ignores the expect-motion window | B4 |
| SB4 | the hold fires at `held > holdMs` instead of `>=` | C1, C2, C4 |
| SB5 | "Live continuously since the press" dropped (Live at the fire is enough) | A2, A3 |
| SB6 | a tap fires in the Hold gesture too | D1, E2 |
| SB7 | an alarm awaiting acknowledgement no longer blocks motion | C8, G1, J1 |
| SB8 | STOP resend cap 3 -> 4 | H1, H3 |
| SB9 | the rejoin dropped (a release while holding always cancels) | C4 |

The unplanted copy runs first as the control (all checks pass). A plant whose anchor does not occur
exactly once, or whose copy is byte-identical to the original, is REFUSED and reported as the rig.
**Outside the set** -- a plant that fails a check not named, misses one that is named, or a clean
run that fails anything -- stops the step for a ruling, as in section 3.

### F1 results (2026-11-04)

Header sha256 begins `cdb76dcbb443`, test `b611683268c7`.

| prediction | observed | held? |
|---|---|---|
| clean suite, MinGW g++ 5.1.0 | `34 checks, 0 failures`, exit 0 | yes |
| clean suite, WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0` | `34 checks, 0 failures`, exit 0 | yes |
| SB1..SB9, MinGW | each failed exactly its frozen set; control clean; real header unchanged | yes, 9 of 9 |
| SB1..SB9, WSL g++ 13.3 | `SABOTAGE PASSED` | yes, 9 of 9 |
| `--anchors` (run.sh's pass) | `9 plant(s), 9 anchor(s), all present exactly once` | -- |

The full `test/host/run.sh`, with the new block, after installing the worktree's libraries (the first
run of each read BLIND at the Wi-Fi QR test for want of `.pio/libdeps`; the rig, not a result):

- the new block: `34 checks, 0 failures` under both compilers; the anchors pass lists the new driver
  (19 drivers checked).
- **One real failure, both compilers, and it is NOT F1's: `test_wifi_qr.cpp` fails
  `Remote-FFFFFF: version 3 (31 B)`.** The test sweeps every `DEVICE_PRODUCT_NAME` and pins the setup
  screen's Wi-Fi QR at exactly version 3 (the 148 px symbol). `WIFI:T:nopass;S:Remote-FFFFFF;;` is 31
  bytes, which is version 2; every other product name is 9+ characters and lands on version 3. The
  name comes from the SCAFFOLD's `DeviceIdentity.h` entry, so the scaffold PR's host-tests job would
  fail too. Recorded against the scaffold (section 3) and stopped there for a ruling: the fix is a
  choice of product network name.
- WSL also ends in its two known rigs, reported rather than folded in: `FAIL: no xtensa toolchain
  found` (the board half; MinGW's run has the toolchain) and `no python with Pillow` (R0/R0b, the
  last blocks).

## 5. Feature step F2 -- reading a device: the event stream, pure and host-tested

Written before the code. F2 turns the bytes of a device's `GET /events` stream into the snapshot F1's
policy reads (`coverpolicy::Device`), and builds the one URL a command goes to. No socket, task or clock
of its own: the link (a later step) feeds it bytes and times. Three pieces:

- `src/remote/SseStream.h` (pure): an incremental HTTP chunked-transfer decoder, then an SSE framer.
- `src/remote/EsphomeModel.{h,cpp}` (ArduinoJson only, the firmware's own copy): one device's model.
- `test/host/test_remote_stream.cpp`, built like `test_game_config.cpp`: against the ArduinoJson in
  `.pio/libdeps`, refusing (exit 2, the rig) a version other than platformio.ini's pin.

### What the wire is (ESPHome 2026.6.5, read from source, not observed)

From `components/web_server_idf/web_server_idf.cpp` and `components/web_server/web_server.cpp` at the tag
both products are built on:

- The stream is HTTP **chunked**: each event is one chunk whose size line is `%08x` (zero-padded hex), then
  `retry: N`, `id: N`, `event: <type>`, `data: <json>`, each ending CRLF, and a blank CRLF line.
- On connect: a `ping` carrying the page config (`title` = the friendly name), the `sorting_group`s, then
  one `state` event per entity with full detail (it carries `domain` and `name`); after that, a `state` on
  every change (no `domain`) and a `ping` (`{"uptime":N}`) every 10 s.
- **While the connect dump is still iterating, bare state updates are DROPPED, not queued**
  (`deferrable_send_state`: "allow all json details_all to go through before publishing bare state
  events"). So the dump's own cover entry can be stale, and the first bare state event is proof the dump
  has finished. That is F2's definition of LIVE.
- Each state carries `name_id` (`cover/Glasscalibur`) and the legacy `id` (`cover-glasscalibur`); the source
  says `id` takes the `name_id` form, and `name_id` goes, in 2026.8.0.

The fixtures under `test/fixtures/remote/` are TRANSCRIPTIONS of that source, the weaker form of a
contract check: they can catch this code disagreeing with my reading of ESPHome, not my reading being
wrong. A capture from a real unit replaces them on the bench (the hardware test plan).

Why a second chunked decoder: `ChunkedSocketStream` (src/HttpRequestManager.cpp) is a blocking `Stream`
adapter for one-shot fetches, file-local, with a fetch-length timeout; a stream that lives for hours and is
fed whatever the socket has needs an incremental one. What the first establishes, and this one must too:
the size line is hex with an optional `;extension`, a chunk's data is followed by CRLF, size 0 ends the
body. What it does on garbage, this one does differently and deliberately: a malformed size line BREAKS
the stream (the link drops and reconnects); it never skips ahead looking for the next plausible line.

### The model's rules

- **LIVE** = this connection has delivered the cover's entry AND at least one bare state event. Before
  that it is Unknown, though the dump's values still land in the snapshot (so a STOP decision uses them).
  A disconnect, or 25,000 ms with no event of any kind (pings come every 10 s), is Offline; the last
  reported operation is kept, so a device last seen moving still draws a STOP.
- **The cover** is the first `cover` entity seen; its name (from `name_id`, or the 2026.8 `id`) gives
  `/cover/<percent-encoded name>/<action>`, and with neither, the legacy object id (`cover-<x>` gives
  `/cover/<x>/<action>`). A SECOND, different cover makes the device unsupported: no command path, never Live.
- **Values are validated, never coerced.** A `position` outside 0..1, or a `current_operation` other than
  IDLE / OPENING / CLOSING, rejects that event whole (counted); the model keeps its previous values.
- **Motion**: OPENING/CLOSING sets the direction; any change of position, or a non-IDLE operation, stamps
  `lastMotionMs` (this is what makes Glasscalibur's back-off, IDLE while moving, visible to F1).
- **Obstruction (Glasscalibur only; ruled 2026-10-30)**: `StallGuard Trips` and `Vibration Stall Trips`.
  The first value after a connect is the baseline; an increase is an obstruction, pending until
  acknowledged; a decrease (the reset button) re-baselines silently. Re-baselined on every reconnect, so
  trips while the remote was not listening never fire.
- **Tamper (Glasscalibur only)**: `Tamper Detected` ON, or `State` = TAMPERED. Acknowledging hides it until
  it clears and comes back (a new episode is pending again). The remote never presses `Clear Tamper Alert`.
- **Only what it reads is parsed.** An event is JSON-parsed only if it names the cover, `State`,
  `Tamper Detected` or one of the two trip counters; the accelerometer, logs, pings and every other entity
  pass through unparsed. An SSE line longer than 3,072 bytes drops its whole event (counted); it is never
  truncated, because half a JSON object is a different object.

### Predictions (frozen before the code)

One line per named check, ending `N checks, M failures`; clean on MinGW g++ 5.1 and WSL g++ 13.3:

- **K1** the Glasscalibur fixture, chunked as the IDF server chunks it (`%08x` per event), decodes to the
  same bytes fed whole and fed split at every byte boundary of its first three events.
- **K2** a size line with an extension (`1A;x=y`) decodes; a size line that is not hex breaks the stream,
  and nothing after it is delivered.
- **K3** a size-0 chunk ends the stream.
- **W1** framing the same events fed whole and split at every byte boundary gives identical events.
- **W2** CRLF and LF line endings give identical events.
- **W3** a comment line (`:`) and the `retry`/`id` fields leave type and data untouched.
- **W4** a 3,073-byte `data:` line drops that event (counted once); the next event arrives intact.
- **W5** two `data:` lines join with a newline.
- **M1** the Glasscalibur connect dump and then one bare `State` event: Live; cover `Glasscalibur`, by
  name; position 500; Idle; no alarm; counters baselined (StallGuard 3, Vibration 1).
- **M2** the same dump with no bare event after it: still Unknown, but the snapshot already carries the
  dump's operation and position.
- **M3** the Ropener dump and a bare event: Live; cover `Ropener`; no alarm.
- **M4** bare OPENING at 0.52: Opening, direction Opening, motion stamped. IDLE at 0.50 then 0.47: Idle,
  stamped each time. IDLE at 0.47 again: not stamped.
- **M5** position 0.0 is 0, 1.0 is 1000, 0.4996 is 500; -0.1 and 1.2 are each rejected (unchanged, +1 reject each).
- **M6** `current_operation` "WOBBLING": rejected, unchanged, +1 reject.
- **M7** StallGuard 3 to 3: nothing; 3 to 4: pending; acknowledged: cleared; 4 to 4: nothing; 4 to 0:
  nothing, re-baselined; 0 to 1: pending. One Vibration Stall increment: pending.
- **M8** disconnected with StallGuard at 4, reconnected to a dump saying 6: nothing pending.
- **M9** Tamper Detected ON: pending; acknowledged while still ON: not pending; OFF then ON: pending again.
  `State` TAMPERED with Tamper Detected OFF: pending.
- **M10** a second, different cover: unsupported, no command path, not Live.
- **M11** command paths: by name `/cover/Glasscalibur/open`; legacy id only `/cover/glasscalibur/open`; a
  name with a space `/cover/Kitchen%20Window/stop`; the 2026.8 shape (`id` = `cover/Glasscalibur`, no
  `name_id`) `/cover/Glasscalibur/close`.
- **M12** accelerometer, log, `sorting_group` and uptime-ping events change nothing and are never
  JSON-parsed (the parse count does not move).
- **M13** Live, then no event for 24,999 ms: Live; 25,000 ms: Offline. A ping in between keeps it Live.
  Offline after an OPENING keeps Opening in the snapshot.

### Sabotages (copies of the headers, as in F1; each must fail exactly its set)

| plant | what it breaks | predicted red set |
|---|---|---|
| SX1 | a malformed size line is skipped instead of breaking the stream | K2 |
| SX2 | an oversize line is truncated to 3,072 bytes instead of dropping the event | W4 |
| SX3 | Live on the cover's dump entry, without the bare-event proof | M2 |
| SX4 | an out-of-range position is clamped instead of rejected | M5 |
| SX5 | an unknown `current_operation` is read as IDLE | M6 |
| SX6 | the trip counters are not re-baselined on a reconnect | M8 |
| SX7 | acknowledging tamper is not undone by a new episode | M9 |
| SX8 | the name is not percent-encoded | M11 |
| SX9 | the parse prefilter is removed | M12 |

**Outside the set** stops the step for a ruling, as before.

### F2 results (2026-11-04)

Against ArduinoJson 7.4.3 from `.pio/libdeps` (the pin). Header sha256 begins: SseStream.h `83333de08718`,
EsphomeModel.h `ec5066eaed2f`.

| prediction | observed | held? |
|---|---|---|
| clean suite, MinGW g++ 5.1.0 | `21 checks, 0 failures`, exit 0 | yes |
| clean suite, WSL `g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0` | `21 checks, 0 failures`, exit 0 | yes |
| SX1..SX9, MinGW | each failed exactly its frozen set; control clean; real headers unchanged | yes, 9 of 9 |
| SX1..SX9, WSL g++ 13.3 | `SABOTAGE PASSED` | yes, 9 of 9 |

Two corrections made in the TEST before its first clean run, both visible in the file: a helper named
`State` collided with `coverpolicy::State` (renamed `StateEvent`), and M8's first draft fed the original dump
(StallGuard 3) and then a 6, which is not the frozen scenario "a dump saying 6"; it now edits the fixture's
StallGuard entry to 6 and asserts the edit applied. And one hardening in the MODEL the predictions did not
cover: a cover name of 64+ characters makes the device unsupported instead of being truncated into a
different URL.

The full `test/host/run.sh`, both compilers: the new block `21 checks, 0 failures`, F1's still `34 checks,
0 failures`, the anchors pass lists both new drivers (20 drivers checked). The one real failure is still the
scaffold's (`Remote-FFFFFF: version 3 (31 B)`, awaiting the name ruling); WSL also ends in its two known
rigs (no xtensa toolchain; no Pillow).
