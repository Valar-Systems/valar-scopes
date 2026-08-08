# Releasing Blipscope firmware

Blipscope ships several hardware SKUs from one codebase (see `include/variants/`). All SKUs
are versioned and released **together** from a single commit, and each device self-updates to
**its own** binary over OTA. This doc is the release checklist.

## How OTA works

- A device checks `releases/latest/download/version.txt` — a single integer, the latest
  firmware version, shared by all SKUs.
- If that integer is greater than the device's compiled `FW_VERSION`, the device downloads
  **`firmware-<slug>.bin`**, where `<slug>` is its own `variant::SLUG` (e.g. `s3-146`). Each
  SKU only ever downloads its own image.
- The flow lives in [src/OtaUpdater.cpp](src/OtaUpdater.cpp); `FW_VERSION` is in
  [src/OtaUpdater.h](src/OtaUpdater.h).

## Cutting a release

1. **Bump the version:** edit `FW_VERSION` in `src/OtaUpdater.h` (one number, all SKUs).
2. Commit + merge to `main`.
3. **Create a GitHub Release** with a tag (e.g. `v5`). Publishing it triggers
   [.github/workflows/firmware.yml](.github/workflows/firmware.yml), which:
   - builds every SKU in the matrix,
   - attaches each as `firmware-<slug>.bin`,
   - attaches a `version.txt` containing `FW_VERSION`.
4. Devices pick up the update on their next daily check (or reboot).

> Don't hand-upload assets — the workflow names them so they match what devices request.

## Bench OTA testing — the pinned pre-release

The download half of the OTA path is dead code on a normal boot (`latest <= current`), so it
can only be exercised deliberately. `-DOTA_RELEASE_BASE` exists for this: point a bench build
at a **pre-release** tag whose `version.txt` is higher than the build's `FW_VERSION`.

Pre-releases never resolve through `releases/latest/download`, so the fleet cannot see the tag.
Verified 2026-08-07 while a preflight tag was live: `latest/download/version.txt` read `4`
throughout while the pinned tag read `6`. See `[env:blipscope-s3-128-otatest]` /
`[env:blipscope-s3-128-otafault]`.

> **Publishing ANY release — including a pre-release — triggers the build workflow, and it
> OVERWRITES hand-uploaded assets.** This is not obvious and it silently invalidates a test.
> Measured: a preflight tag created at 20:10 with a hand-built `version.txt` of `6` had every
> asset replaced by CI between 20:14 and 20:15, `version.txt` becoming `5` (the tag commit's
> `FW_VERSION`) and `firmware-s3-128.bin` becoming a CI-built v5. A test run before that window
> updated correctly; one after it read `latest=5` and did nothing, which looks exactly like a
> broken OTA path rather than a moved goalpost.
>
> So for a bench pre-release: create the tag, **wait for the workflow to finish**, then
> `gh release upload <tag> version.txt firmware-<slug>.bin --clobber`. Re-uploading does not
> re-trigger CI, so the assets stay put. Confirm by fetching the pinned `version.txt` over HTTP
> immediately before the run — the device's answer is only as good as what the URL served.

### The standing harness tag: `ota-preflight-v6`

Kept deliberately rather than deleted — it is the harness every future OTA test runs against,
and a pre-release carries no fleet exposure. **It is not a release and must never be treated as
one.**

Its assets are a **mixed matrix**: a full ten-SKU set of CI-built binaries at whatever
`FW_VERSION` the tag's commit carried, plus whichever single SKU was hand-clobbered for the last
test. So the `version.txt` sitting there right now is only meaningful next to the specific binary
someone uploaded beside it.

**Before each use: re-clobber `version.txt` and the one `firmware-<slug>.bin` you are testing,
then re-read the URL over HTTP.** Never infer the tag's state from the last time it was used, and
never promote it to a full release — doing so would publish a stale mixed matrix to the fleet.

## Board #1: the OTA rehearsal that gates a flash run

**Nothing else flashes until this passes.** One board, one OTA, from a virgin slot.

It exists because of a near-miss on 2026-08-08. The cloud feed lived in a
`blipscope-s3-128-prodburn` env; boards were to be flashed from it, but CI builds
`blipscope-s3-128`, so `firmware-s3-128.bin` — the asset those boards download on their
first update — had the feed compiled out. All 50 units would have quietly stopped using
Blipscope Cloud, one update in. Three things hid it, and the rehearsal is built around
all three:

- a non-cloud image does not error, it just never contacts the proxy;
- the `-otatest` env extended the same non-cloud base, so the old rehearsal would have
  passed while proving nothing about the shipping image;
- the version number still increments, so "the OTA worked" was true and useless.

`scripts/check_release_envs.mjs` now fails CI on that config mistake. This rehearsal
covers what a static check cannot: that the binary a real device actually pulled and
booted is a working cloud radar.

### The three assertions

A pass needs **all three**. The version bump alone is explicitly not enough — trusting
it is what let this through.

1. **It updated.** `FW_VERSION` on the serial banner is the new number.
2. **It came back a CLOUD image.** The board reaches the proxy after the update. This is
   the assertion that catches a wrong-image OTA, and it is decisive precisely because a
   non-cloud build makes *no* request at all — absence is the failure signal, so there
   is nothing to misread.
3. **It is actually serving aircraft.** The radar draws blips from the cloud, not a
   cached last-good picture and not an empty screen.

### Running it

```sh
# Board #1 only. Erase first -- a virgin slot is the point: an OTA onto a board already
# carrying the new image proves nothing.
pio run -e blipscope-s3-128 -t erase
pio run -e blipscope-s3-128-otatest -t upload -t monitor
```

The flash env is `-otatest`, which now inherits the cloud flags from
`blipscope-s3-128`; that inheritance is the fix, and it is what makes assertions 2 and 3
meaningful at all. See the pinned pre-release section above for preparing the tag.

After the device takes the update and reboots, ask the proxy whether it came back:

```sh
# Substitute the board's device id. The window must START after the update landed.
npx wrangler analytics-engine sql --env production <<'SQL'
SELECT blob6 AS fw, blob1 AS route, SUM(double4) AS requests, MAX(timestamp) AS last_seen
FROM blipscope_proxy
WHERE timestamp > NOW() - INTERVAL '15' MINUTE AND blob5 = '<device-id>'
GROUP BY fw, route ORDER BY last_seen DESC
SQL
```

- **PASS** — rows with `fw` = the NEW version and `route` = `/api/v1/blipscope/blips`,
  with `last_seen` advancing when you re-run it.
- **FAIL** — no rows at all, or rows only at the OLD `fw`. That is the wrong-image OTA:
  the board is alive and updated but is no longer a cloud device. Stop the flash run.

Then on the board itself: the radar shows aircraft, and the serial `[health]` line
reports the cloud source with a non-zero fetch count. A board that updated but sits
empty with the stale tag amber is a **fail** even if the query returned rows.

## Adding a new SKU to releases

A new SKU needs three entries that stay in sync:

1. `variant::SLUG` in its `include/variants/<sku>.h`
2. an `[env:*]` in `platformio.ini`, named `<product>-<board>` (e.g. `blipscope-s3-146`, `quakescope-s3-146`)
3. its `{ env, slug }` row in the `matrix.include` of `.github/workflows/firmware.yml` — `slug`
   MUST equal `FW_OTA_PREFIX` + `variant::SLUG` (it names the OTA asset devices download)

Add all three, and the next release automatically builds and publishes that SKU's binary.

## Missileer builds — a separate OTA channel

The `missileer-*` envs build a different **product** (Missileer, the EAM monitor, `-DFEATURE_EAM`; see
[CLAUDE.md](CLAUDE.md)) from the same boards. So a Missileer device never pulls a radar image for its
board, the Missileer envs set `-DFW_OTA_PREFIX="eam-"`, and their CI slug is prefixed to match:

| env | slug (CI + OTA asset) |
| --- | --- |
| `missileer-s3-146` | `eam-s3-146` → `firmware-eam-s3-146.bin` |

These ride the **same** `version.txt` gate (one `FW_VERSION` bump releases radar and Missileer together),
and a device only ever downloads its own `<prefix><slug>` binary. Releasing is otherwise identical
— the matrix rows are already in [.github/workflows/firmware.yml](.github/workflows/firmware.yml).

## Orbitscope builds — another separate OTA channel

The `orbitscope-*` envs build a third **product** (Orbitscope, the Space edition, `-DFEATURE_SPACE`; see
[CLAUDE.md](CLAUDE.md)) from the same boards. Same arrangement as Missileer: `-DFW_OTA_PREFIX="space-"`,
and a CI slug prefixed to match so an Orbitscope device never pulls a radar or Missileer image:

| env | slug (CI + OTA asset) |
| --- | --- |
| `orbitscope-s3-146` | `space-s3-146` → `firmware-space-s3-146.bin` |

Same `version.txt` gate (one `FW_VERSION` bump releases radar, Missileer, and Orbitscope together).

## Quakescope builds — another separate OTA channel

The `quakescope-*` envs build a fourth **product** (Quakescope, the Seismic edition, `-DFEATURE_SEISMIC`; see
[CLAUDE.md](CLAUDE.md)) from the same boards. Same arrangement as Missileer/Orbitscope: `-DFW_OTA_PREFIX="seismic-"`,
and a CI slug prefixed to match so a Quakescope device never pulls a radar, Missileer, or Orbitscope image:

| env | slug (CI + OTA asset) |
| --- | --- |
| `quakescope-s3-146` | `seismic-s3-146` → `firmware-seismic-s3-146.bin` |

Same `version.txt` gate (one `FW_VERSION` bump releases radar, Missileer, Orbitscope, and Quakescope together).

## Quillscope builds — another separate OTA channel

The `quillscope-*` envs build a fifth **product** (Quillscope, the Birding edition, `-DFEATURE_BIRDING`; see
[CLAUDE.md](CLAUDE.md)) from the same boards. Same arrangement as the others: `-DFW_OTA_PREFIX="birding-"`,
and a CI slug prefixed to match so a Quillscope device never pulls another edition's image:

| env | slug (CI + OTA asset) |
| --- | --- |
| `quillscope-s3-146` | `birding-s3-146` → `firmware-birding-s3-146.bin` |

Same `version.txt` gate (one `FW_VERSION` bump releases every edition together).

## Reelscope builds — another separate OTA channel

The `reelscope-*` envs build a sixth **product** (Reelscope, the Fishing edition, `-DFEATURE_FISHING`; see
[CLAUDE.md](CLAUDE.md)) from the same boards. Same arrangement as the others: `-DFW_OTA_PREFIX="fishing-"`,
and a CI slug prefixed to match so a Reelscope device never pulls another edition's image:

| env | slug (CI + OTA asset) |
| --- | --- |
| `reelscope-s3-146` | `fishing-s3-146` → `firmware-fishing-s3-146.bin` |

Same `version.txt` gate (one `FW_VERSION` bump releases every edition together).

## Claudescope builds — another separate OTA channel

The `claudescope-*` envs build a seventh **product** (the Claudescope edition, `-DFEATURE_CLAUDESCOPE`; see
[CLAUDE.md](CLAUDE.md)) from the same boards — a live Claude usage-limit gauge. Same arrangement: `-DFW_OTA_PREFIX="claudescope-"`,
and a CI slug prefixed to match so a Claudescope device never pulls another edition's image:

| env | slug (CI + OTA asset) |
| --- | --- |
| `claudescope-s3-146` | `claudescope-s3-146` → `firmware-claudescope-s3-146.bin` |

Same `version.txt` gate (one `FW_VERSION` bump releases every edition together).

## Speedscope builds — another separate OTA channel

The `speedscope-*` envs build an eighth **product** (the Speedscope edition, `-DFEATURE_SPEED`; see
[CLAUDE.md](CLAUDE.md)) from the same boards — a desk speed-radar that ties into a MiniSpeedCam
(minispeedcam.com) over the LAN. Same arrangement as the others: `-DFW_OTA_PREFIX="speed-"`, and a CI
slug prefixed to match so a Speedscope device never pulls another edition's image:

| env | slug (CI + OTA asset) |
| --- | --- |
| `speedscope-s3-146` | `speed-s3-146` → `firmware-speed-s3-146.bin` |

Same `version.txt` gate (one `FW_VERSION` bump releases every edition together).

## Legacy note: the retired C3

The original ESP32-C3 Kit is retired — Blipscope is S3-only going forward. The workflow no
longer builds a `c3-128` slug, and the plain `firmware.bin` alias it used to publish for the
very first (pre-per-SKU-naming) devices is gone, so those C3 units no longer receive OTA
updates. The variant header (`include/variants/c3_128.h`) and the single-core guards it drove
stay in the tree, inert behind their capability flags, so the board can be revived by
re-adding its `[env:*]` and CI matrix row.
