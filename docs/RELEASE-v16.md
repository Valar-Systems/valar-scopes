# v16 release plan

**Confirmed 2026-10-07 (review): cut v16 when #376 merges; ntfy and `nmi` → `NM` go to v17.**

## The rule

- **v16 is cut when both required items are on `main` and verified**: zoom and the touch-wedge
  reboot cap.
- **"On main" means `git ls-tree origin/main` shows the change**, not that a PR is open or green
  (CLAUDE.md, *a green signal is about process*).
- **ntfy removal and `nmi` → `NM` ride along only if they are done by then.** Otherwise they move to
  v17 unchanged.
- **The coarse-location and README `/blips` privacy rows say where they stand either way.**
- **Gate added at review: an overnight soak of the SHIPPING image** (real constants, no bench
  hooks) on the bench board, graded against predictions frozen before the flash. Passed
  2026-10-08 (S1–S5 clean; graded on #376).
- **Promote is approved by Daniel**, as with v15: the cut stops at the `promote` job and reports.

## Items

| # | item | spec | v16? | status |
|---|---|---|---|---|
| 1 | Swipe-to-zoom on the radar, plus a ZOOM tag | PR #374 | **required** | **merged `b803aef`** (Z1–Z5 and sabotages a–d graded, glass done; the shipped 10-min idle return is pinned by a test and the bench override cannot compile into a shipping env) |
| 2 | Touch-wedge reboot cap + "touch unavailable" | [v15-touch-wedge-cap.md](v15-touch-wedge-cap.md) | **required** | **merged `34305d1`** (#376: W1–W5 graded, sabotages a–c plus host sabotages red then undone, glass done incl. an iPhone scan of Connect with the strip up; overnight shipping-image soak S1–S5 clean) |
| 3 | ntfy removal | not yet written | **v17** (decided at review) | not started. Needs scope: radar only, or every edition that uses ntfy (Missileer, Quakescope, Quillscope, Reelscope, Claudescope, Speedscope)? And what happens to a saved `ntfy-topic`? |
| 4 | `nmi` → `NM` | not yet written | **v17** (decided at review) | not started |
| 5 | Coarse location on the wire | PR #372 | **already shipped in v15**, not a v16 item | the `v15` tag (`4df698f`) contains `include/WireLocation.h`, and its `AircraftManager.cpp` sends every location at 2 dp (6 `wireloc::Center` call sites) |
| 6 | README `/blips` privacy line | this PR | v16 item; done here | the README says what the feed request carries: the centre at 2 dp (~1.1 km), never the stored 4-dp value; a tapped aircraft's position the same way; nothing from a local receiver; the "Use my location" position goes only to the device |

Proposed in the session, **not on this list unless added**: config-page checkboxes that save
themselves, and pre-scanning Wi-Fi networks for the setup portal.

## Known cosmetic issues (accepted)

- **The ZOOM tag can sit on top of an airport label** in the top-left corner (seen at 10 mi with
  `0G15`, 2026-10-07). The tag is drawn last, so ZOOM itself always reads; part of the airport code
  can be hidden. Accepted as-is.

## Print cards

- **Zoom line, pending v16:** *"Swipe up on the radar to zoom in; swipe down to zoom out."* It is
  recorded in [CARDS-README.md](CARDS-README.md).
- **The Canva edit happens when v16 is promoted, not before.** The 50 units ship on v15, whose
  firmware has no zoom.

## Promoted

**v16 was promoted 2026-10-08T19:13:55Z** (Daniel approved). `releases/latest` is `v16`
(`prerelease: false`), and `latest/download/version.txt` serves `16`. The gates: overnight
shipping-image soak S1-S5 clean (#376); `verify-release.sh v16` 8/1/1, as pre-registered; fresh-boot
acceptance 6/7. **Step 4 FAILED, and it is a real defect**: a newly claimed aircraft may be missing from
Collection until the page is refreshed, sometimes more than once. It is a known issue in v15 and v16,
fixed in v17 (save immediately on a claim). No rollback; no data is lost. Detail and log timeline are in
the release notes.

## Rollout graded (2026-10-09 11:43Z)

Graded against the predictions frozen before promote (sha256 prefix `aef3b0607dc5`, checked before
reading). T is the promote, 2026-10-08T19:13:55Z. The figures are the Worker's request and boot rows,
read with the dashboard's own query. **Every always-on device moved to 16 on the first request after
its first boot after T, as predicted. Nothing fell outside the set.**

| device | predicted | last fw 15 | first fw 16 | the boot before it |
|---|---|---|---|---|
| `336f` | ~10:01Z | 09:59:04Z | 10:01:19Z | 10:01:19Z `SW`, the quiet reboot |
| `ada1` | ~11:01Z | 10:59:42Z | 11:01:19Z | 11:01:19Z `SW`, the quiet reboot |
| `04f8` (COM6) | ~11:01Z, "unless something else reboots it sooner" | 02:23:16Z | 02:25:24Z | 02:25:24Z `SW`, **not** the quiet reboot |

- **`04f8` moved 8.6 h early, through the prediction's own exception.** Its first boot after T was a
  `SW` reboot at 02:25:24Z, and its first fw-16 request was that boot's check-in. Its normal quiet
  reboot followed at 11:00:18Z. **The cause of the 02:25 reboot is unknown and is not guessed.** It
  falls between two unexplained `USB` resets of the bench board `e5cd`, at 02:24:09Z and 02:26:32Z.
  **Daniel confirmed on the glass that COM6 runs v16: swipe-up zoom works.**
- **`22e6` has made no request since T**, so it did not reconnect, and R2 (the touch-wedge cap on a
  real wedged chip) is **not graded**.
- **Did not move** (no request since T): `22e6` (last 2026-09-29 08:54Z, fw 14), `2aee` (2026-10-02
  01:03Z, fw 14), `870b` (2026-09-22 23:11Z, fw 11), `2afa` (2026-09-14 21:20Z, fw 11), `a566`
  (2026-09-13 19:51Z, fw 11), `5bec` (2026-09-11 02:59Z, fw 11).
- `e5cd` (COM18) is the bench board, not a rollout data point.

## 50-unit bench run sheet: v16 factory assets

**The run flashes v16's CI-built factory set**, published by the release workflow from `74866aa`,
never a local build (RELEASING.md: *CI is the only place a factory image for customers is built*).

valar-flasher's bench mode resolves `latest`, which is now v16, and refuses a manifest whose app
region is not the release's own `firmware-<slug>.bin` (valar-flasher PR #4). These are the files it
must write, and their sha256 digests. **The manifest's own sha256 fields, GitHub's asset digests,
and a local hash of the downloaded manifest agree on every row** (read 2026-10-08):

| file | offset | size | sha256 |
|---|---|---|---|
| `flash-manifest-s3-128.json` (`v: 1`, `fw_version: 16`, `commit: 74866aa...`) | -- | 1,333 | `a379e0c44358c361802cbbf7734d03917dcd8b604dc8876fbbc1fa1ff4130c0f` |
| `firmware-s3-128.factory.bin` (the whole image) | `0x0` | 2,056,256 | `d61710da5a1f79c072d029777e0aed38057026f2c0c114a62d77bd7c9a343f95` |
| `bootloader-s3-128.bin` | `0x0` | 19,984 | `b41be55ae9a52aeeb21645c51b86c14027f84c2c91bf67bee6aa0e1b15d18e8b` |
| `partitions-s3-128.bin` | `0x8000` | 3,072 | `28f336fe57a9991a5f39db735295a978e0a6aee5710f5e7107ac44bdd812d29b` |
| `boot_app0-s3-128.bin` | `0x1e000` | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| `firmware-s3-128.bin` (the app) | `0x20000` | 1,925,184 | `3c3eb3f5bca7784a9e8d976c383761930fa04884164738e704ecc4f6c2ab0aec` |
| `factory-scan-s3-128.json` (the scan record) | -- | 1,405 | `98a194d26a356e2b795dfc5c55bf2e48f19a59ffbe391b2b6167a3af9205abbf` |

**NVS is preserved** (`0x9000`, size `0x15000`): flash mode never writes the region holding the
device key, settings and logbook.

**Before board 1:**
1. If not already done for v15, update the installed flasher (`C:\Github\valar-flasher`) to main,
   and remove any `release_tag` pin from `products.local.json`, so it resolves `latest` (v16).
2. **Flash ONE board and confirm it** before the run continues:
   - the `[build] env=blipscope-s3-128 fw=v16` banner;
   - `[ota] channel=s3-128 current=16 latest=16`;
   - Worker requests at **200 as FW 16** (the dashboard device page, or the fleet query).
3. RELEASING.md's Board #1 gates still apply as written.
4. The scratch prerelease `factory-manifest-scratch-2026-09-24` is deleted only after step 2
   passes. It is not a release, and must never be promoted.

**The v16 app image is already proven on hardware.** The same `firmware-s3-128.bin` (sha256 above)
ran COM18's fresh-boot acceptance, and the same source at `f2d9fbc` ran the overnight soak.
