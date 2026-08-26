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

> ### ⛔ HARD GATE — the photo square library must be published BEFORE any release that raises `FW_VERSION` to `FULLBLEED_MIN_FW` (7) or above
>
> **Publishing such a release against an unpublished square library removes photographs
> from every card on every device in the fleet, by OTA, in one action.** It is the
> second member of the same family as the launch checklist in
> [ROADMAP.md](ROADMAP.md): not a recovery, a recall — except this one arrives *because*
> the update succeeded.
>
> **The mechanism.** [`squareSizeFor()`](proxy/src/photos.ts) reads `X-Blip-FW`. At FW ≥ 7
> with a known model it returns a panel size, and `resolvePhoto()` then reads
> **square-specific pointer keys** — and deliberately **returns null rather than falling
> back to the legacy 150×100 rectangle**, because a rectangle in a full-bleed disc is
> wrong wherever it lands. If the square pointers do not exist in KV, every lookup misses
> and every card shows "No photo available".
>
> **Why it will not announce itself.** Every part of this is behaving correctly. The
> firmware is right, the gate is right, the null-instead-of-rectangle choice is right, and
> "No photo available" is a *designed* state with a silhouette. There is no error, no 5xx,
> no log line, and nothing in CI to fail — the artifacts live in KV, which no build
> touches. The only signal is a customer saying the pictures went away.
>
> **This was live on 2026-08-14:** production held 233 manifest rows and **zero** square
> variants. The full-bleed work had shipped in firmware and the artifacts behind it had
> never been published. It was found by chance, from one bench photograph, four days
> after the framing work landed and hours before v7 was to be cut.
>
> **The gate, in order:**
>
> ```sh
> # 1. What would change? Must read the published manifest -- if it prints
> #    "could not read the published manifest", STOP: the count that follows is
> #    a comparison against nothing and is the same number in both worlds.
> cd proxy && npx tsx scripts/ingest-photos.ts --dry-run --env production
>
> # 2. Publish. Content-addressed, so unchanged rows are provable no-ops.
> npx tsx scripts/ingest-photos.ts --env production
>
> # 3. Prove a square pointer actually resolves, from outside the ingest.
> ./scripts/verify-release.sh <tag>        # includes the square-key probe
> ```
>
> Devices below FW 7 are unaffected at every step: they read the legacy pointer, exactly
> as before. So the ingest is safe to run at any time and there is never a reason to
> defer it past a release.

0. **Run THE FRESH-BOOT ACCEPTANCE on a real board** — `./scripts/fresh-boot-acceptance.sh <PORT>`.

   **Required from v8 on. It is step zero because it is the only check that
   exercises what a new owner actually meets**, and because everything it covers
   was found by a customer rather than by us.

   It is a guided procedure, not an automated test — the physical steps (tap the
   glass, pull the power) cannot be driven from a script, and it says so instead
   of pretending. What is automated is the evidence: one continuous serial
   capture, asserted at the end against lines the firmware already prints.

   The sequence, in order, because several assertions depend on the order:
   factory reset → join Wi-Fi and set a location → claim an aircraft → open
   Collection **within a minute** → toggle the logbook off (what you claimed must
   survive) → **pull the power** → reboot.

   The last one is the one that matters most: after a real power cut the book
   must come back non-empty. That is the difference between a collection and a
   session.

   Why it exists: on 2026-08-21 a device that was actively logging showed an
   empty Collection page telling the owner to turn on a logbook that was already
   on. `/logbook.json` is served from NVS, and NVS was not written for the first
   ten minutes of uptime — so a factory-fresh unit was invisible to its own page
   for exactly as long as a new owner would be staring at it.

1. **Bump the version:** edit `FW_VERSION` in `src/OtaUpdater.h` (one number, all SKUs).
   If this takes the number to 7 or above, the gate directly above applies.
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

> **When a check protects a property, verify the check's own environment has that
> property.** `-otatest` extended the non-cloud base, so the OTA safety net had a hole
> shaped exactly like the bug it existed to catch — it would have reported a clean pass
> while exercising an image that was not the one shipping. A test inherits its
> environment from somewhere, and that somewhere is rarely re-read once it works.
>
> The general form, worth applying to every gate in this file: **ask what the check
> would do if the defect were present.** If the answer is "pass", the check is
> decoration. `--selftest` on the CI checkers exists for the same reason — prove it can
> fail before trusting that it passed.

> **Rehearse from a board that is genuinely BEHIND the release under test.** A board
> flashed from the very release being rehearsed sits at `FW_VERSION == version.txt`.
> Equal is not greater, so the OTA never fires — and then every assertion below passes
> anyway: the banner reads the new number because it was *flashed* with it, the board is
> a cloud image because it was flashed as one, and it draws aircraft for the same reason.
> Three green ticks, and not one byte was downloaded. The rehearsal is structurally
> incapable of failing, which makes it worse than no rehearsal: it manufactures
> confidence.
>
> So pick a device at the PREVIOUS version — a bench board is ideal — and confirm before
> starting that `releases/latest/download/version.txt` is strictly greater than what the
> board reports. If they are equal, you are about to test nothing.
>
> This is the same defect as the `-otatest` one above wearing different clothes, and it
> is the sixth occurrence on this project of a check that could only ever pass. The
> pattern is now common enough to assume rather than discover: **when you build a check,
> the first thing to establish is what would make it fail.**

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

## Board #1, second leg: the FIRST-RUN rehearsal that gates a flash run

**Also mandatory, and for the same reason as the OTA leg: nobody has ever deliberately
done it.** One board, one captive-portal provisioning, performed by a human.

Two bugs surfaced in one week (#164, #166) and *neither was found by testing*. #166 — the
config page never binds `:80` after portal setup, on every first-run device — was found
only because #164 fired, and #164 fired only because a multi-day soak ran long enough for
a random double-tap to reset the board's Wi-Fi by accident. See #171.

The reason it has no coverage is structural, and it will not fix itself:

> Every bench board, every soak and every CI build exercises the **saved-credentials**
> boot. The portal boot happens **once per board and then never again** — so the more a
> board gets used, the less it resembles a customer's first five minutes with it. A fifty
> unit run means fifty devices each taking that path exactly once, at someone's house,
> with no serial capture attached and nobody watching the log. It is simultaneously the
> boot most likely to fail and the one least likely to be observed failing.

### Running it

1. **Force the portal.** Use the boot **TOUCH & HOLD** to forget Wi-Fi.
   **Do NOT `-t erase` for this leg** — a full NVS wipe also destroys the logbook and the
   leaderboard claim, which is a different test and destroys the state this one needs.
2. **Provision exactly as a customer does.** Join the `Blipscope-XXXXXX` hotspot from a
   phone and submit credentials through the portal page. Not over USB, not by writing NVS.
3. **Read the device name OFF THE STATS SCREEN**, not off the flashing host. The name is
   MAC-suffixed and unguessable, and a customer who cannot read it from the glass cannot
   reach the config page at all — that is precisely why #166 presented as "the address is
   missing" rather than "the server is dead".
4. **Assert on the FIRST boot after setup, with NO power cycle.** The power cycle is what
   hides the bug: a reboot takes the saved-credentials path, which always worked.

### The three assertions

| | check | catches |
|---|---|---|
| 1 | `GET http://<name>.local/` returns 200 with the real config page | #166 |
| 2 | `GET http://<ip>/` returns 200 | proves it is the SERVER, not mDNS |
| 3 | boot log has `[web] config server listening`, and **not** `[web] ERROR: config server did NOT bind :80` | the direct signal |

Assertion 2 is not redundant. If only `.local` is checked, an mDNS failure and a dead
server look identical, and the two have completely different fixes.

**Not automated, deliberately.** Driving the hotspot join from CI needs a second radio and
real credentials, and the value here is a human doing what a customer does. The failure
mode being defended against is "nobody ever tried it", not "somebody tried it carelessly".
A checklist item a person signs off is the whole point.

> **Any boot path that runs once per device deserves an explicit test, precisely because
> normal use never repeats it.** On this product that is currently: first Wi-Fi
> provisioning (this leg), first OTA (the leg above), and the first leaderboard claim —
> which still has none.

## Adding a new SKU to releases

A new SKU needs three entries that stay in sync:

1. `variant::SLUG` in its `include/variants/<sku>.h`
2. an `[env:*]` in `platformio.ini`, named `<product>-<board>` (e.g. `blipscope-s3-146`, `quakescope-s3-146`)
3. its `{ env, slug }` row in the `matrix.include` of `.github/workflows/firmware.yml` — `slug`
   MUST equal `FW_OTA_PREFIX` + `variant::SLUG` (it names the OTA asset devices download)

Add all three, and the next release automatically builds and publishes that SKU's binary.

### A SKU that isn't ready yet: the compile-only row

A board still in bring-up should **not** get a `slug` — a slug is what names
`firmware-<slug>.bin`, and publishing that asset points an OTA channel at an image
nobody has flashed. Give it a row with the env alone:

```yaml
- { env: blipscope-pro-s3-175-amoled }
```

CI builds it and runs the adsbdb launch gate on it, then skips naming, uploading and
attaching. `check_release_envs.mjs` still asserts the env exists in
`platformio.ini`, and still does **not** demand a production cloud feed — a SKU with
no published asset has no fleet to strand. Add the `slug` when the pins are verified
and the board is brought up; that one edit promotes it to a published SKU and the
cloud-feed assertions switch on with it.

**Why this row kind exists at all.** The 1.75" AMOLED env had no CI row, stopped
compiling, and nobody knew. It was found by hand during a sweep that built all
eleven images rather than the ten CI knew about — and the break had been introduced
by the very change that sweep was checking. Leaving an env out of CI because it
isn't a product yet is how it becomes an env that no longer builds; the compile-only
row is the way to say "build this, don't ship it" instead.

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
