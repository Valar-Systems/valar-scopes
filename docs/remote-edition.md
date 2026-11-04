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

**P1 (2026-11-04).** B built at I (98c82b2) from an empty `.pio`: 42 of 43 existing envs built.
`probe-s3-128-screenstate` failed, first error `src/probe/ScreenStateProbe.cpp:111:18: error: too few
arguments to function 'void game::DrawDrill(...)'`. That env is already broken on main and is the
subject of open PR #399; it is not in skus.yml. Recorded and not fixed here, per P1's own fallback. At S
it fails with the same first error line, as that fallback predicts.

**P2 -- OFF-PREDICTION. Work on the scaffold's measurement stopped here for a ruling.**

S built at the scaffold head (c0b73b7) in the same worktree: 42 of 43 built (the same one failed).

| | predicted | observed |
|---|---|---|
| P2: S image byte-identical to B | 42 of 42 | **0 of 42** |
| C2a: the envs compiling `src/main.cpp.o`, B vs S | equal sets | equal: 28 and 28 |
| C2b: an existing env's S log compiling under `src/remote/` | none | none (0 of 43 logs) |
| C2c | -- | **not run** (see below) |

**Why: the method's premise was false, and the method section says so in the wrong words.** It said
"PlatformIO wipes `.pio/build` when `platformio.ini` changes", and put I before B to dodge that.
PlatformIO's `compute_project_checksum` (platformio/project/helpers.py) also hashes the project's
FILE STRUCTURE -- every `.c/.cc/.cpp/.h/.hpp/.s/.S` path under include/, src/ and lib/. Adding
`src/remote/` changed it, so `.pio/build` was wiped and S was a FULL rebuild (S compiled exactly as
many files as B: 165 and 165 for animtest-s3-128, 258 and 258 for blipscope-pro-s3-21), which
re-stamps every build-time string.

**Every differing byte was then accounted, and none of it is the change** (tool: CLAUDE.md's regions;
a byte outside them is "a different build"):

- **37 of 42** images differ ONLY in the named regions: `app_elf_sha256` (32 at 0xB0), the trailing
  image SHA (32), the checksum byte (1), WiFiManager's `aboutdate` stamp and the core's
  `Software Info:` stamp (the latter stores `__TIME__` and `__DATE__` as two strings, `20:43:24\0Nov  3 2026`).
- **5 of 42 differ beyond them, and each is the same mechanism, traced in the bytes: string tail-merging
  keyed on the timestamp's value.** The linker may store a short string as the tail of a longer one.
  - `blipscope-s3-128-densesky`, `-fetchtrace`, `-joindiag`, `blipscope-s3-146-cloud`: at B their
    `Software Info` time ended in `:15` (20:53:15, 20:54:15, 20:55:15, 21:05:15), so the standalone
    `"15"` was merged INTO it; at S it was not, so a `15\0` entry reappears in WiFiManager's string pool
    after `custom\0` and everything after it shifts (167,249 / 310,313 / 168,078 bytes, and
    fetchtrace's image grows 16 bytes across a padding boundary). Control: the clean
    `blipscope-s3-128` and `blipscope-s3-128-cloud` have `15\0` there in BOTH B and S, and no
    timestamp of theirs ends in 15.
  - `quakescope-s3-146`: 3 bytes, one literal-pool pointer. At S the `"20"` it points to is the tail of
    `22:00:20\0Nov  3 2026`; at B (stamp 21:20:24) it was a standalone `20\0`.

So a rebuild of the same source can move bytes OUTSIDE CLAUDE.md's five regions, whenever a timestamp's
last digits happen to match a short string: the 75-byte accounting assumes a stable layout, and the
layout depends on the clock. (A candidate for that CLAUDE.md corollary; not edited here.)

**C2c was not run.** As frozen it requires the restored rebuild to be identical to B, which a full
rebuild has already made impossible -- running it would produce a known failure about the rig.

Proposed for the ruling (not done): rebuild B at I **with the scaffold's three new files present**, so
I and the head share a file structure and S is genuinely incremental; then P2 can be measured as
frozen. The alternative is to accept the accounting above as the proof of inertness.

**A defect outside every prediction above: the network name `Remote` makes a version-2 setup QR.**
Found by the full host suite on the feature branch (section 4's results), and it belongs to THIS PR:
`test/host/test_wifi_qr.cpp` sweeps every `DEVICE_PRODUCT_NAME` in `include/DeviceIdentity.h` and pins
the setup screen's Wi-Fi QR at exactly version 3 (the 148 px symbol). `WIFI:T:nopass;S:Remote-FFFFFF;;`
is 31 bytes, which is version 2; every other product name is 9+ characters and lands on version 3. Both
compilers fail it identically. The scaffold's predictions never ran the host suite -- a gap in the set,
not only in the result -- and P5 (CI green) would have failed on the host-tests job. Not fixed: the fix
is a product network name, which is a ruling.
