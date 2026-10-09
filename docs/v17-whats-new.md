# "What's new" after an update (v17)

**Status: spec, not built.** Includes Daniel's amendment of 2026-10-09: no timeout; the tag stays
until it is acknowledged. Line refs at `7bc5f43`. "AM" = `src/AircraftManager.cpp`.

## Customer view

After an over-the-air update that has news, three things tell the owner, and they share **one**
acknowledgment:
- **Device:** a small **"Updated to v17 · What's new"** tag on the Radar. Tap it for a short summary
  (2-4 lines) and a QR code to the full changes page.
- **Config page:** a banner, **"Your Blipscope updated to v17 on <date> — What's new"**, with a link
  to the changes page and a dismiss button.
- **Changes page:** plain language, one section per version, at
  `scopes.valarsystems.com/blipscope/changes`.

Tapping the tag **or** dismissing the banner acknowledges the update. Either one clears both, for
good, until the next update with news.

## What exists today (facts this spec builds on)

- **Version:** `FW_VERSION` (`src/OtaUpdater.h:18`). An OTA only happens when `version.txt` is
  newer, and the update check runs only at boot (`src/main.cpp:604-606`).
- **The OTA leaves a record that survives the update.** The OLD firmware writes `from`/`to` into
  NVS namespace `ota-mem` just before downloading (`NoteOtaAttempt`, `OtaUpdater.cpp:49-67`). The
  NEW firmware reads it after the reboot, and `TakeOtaMemReport` clears it when it reports
  (`OtaUpdater.cpp:466-508`).
- **Factory reset clears namespaces by name**, never the whole partition (`include/FactoryReset.h:17`,
  `src/FactoryReset.cpp:88-110`).
- **Radar chrome:**
  - the ZOOM tag and the ring label share the top edge (`DrawZoomOverlay`, AM:4887-4910);
  - the screen-indicator dots sit at `y = SCREEN_SIZE - 16` (`DrawScreenIndicator`, AM:4319-4322);
  - the touch-unavailable strip is drawn last, at the top (AM:3366; `StripTopY`).
- **The config page** already builds a dismissible-style block from device-substituted values
  (`window.BP_ENROLLED` etc., `ConfigurationWebServer.cpp:306`) and posts small actions with the
  `X-Blipscope` header (`/location`, :287).
- **The Worker** serves pages under `PAGE_PREFIX` (`/${EDITION}`, `proxy/src/index.ts:106`), e.g.
  `/blipscope/locate` (:242). Static pages are embedded from `proxy/pages/*.html` by
  `embed-pages.mjs`, which has a `--check` mode.
- **The release workflow** builds every SKU in `build` (`needs: skus`, `firmware.yml:227-229`).
  `version` publishes `version.txt` only when every slug-ful leg left a publish receipt
  (`firmware.yml:445-462`).

## The first-version problem, and how it is solved

The rule "no last-seen version = factory-fresh, stay silent" is right for a new unit, and **wrong for
every v16 device in the field**. v16 never wrote a last-seen key, so a 16 -> 17 OTA would read as
factory-fresh, and **the first release with this feature would announce itself to nobody.**

So "is this an upgrade?" is answered by evidence, in this order:
1. **`whatsnew/seen` exists:** the previous version is that number. Normal from v17 on.
2. **No `seen`, but `ota-mem` says `to == FW_VERSION`:** this boot is the first after an OTA from
   `from`. That is an upgrade from `from`. This is how every v16 -> v17 OTA is recognised. It is read
   at boot, before the first check-in's `TakeOtaMemReport` clears the record.
3. **Neither:** factory-fresh. Set `seen = FW_VERSION` silently and show nothing.

**Accepted limitation:** a used unit updated over USB with NVS kept (the bench `app0` flash, or a web
flasher in "keep settings" mode) has no OTA record, so it falls into case 3 and stays silent. The
changes page still lists everything. Stated, not hidden.

## State: NVS namespace `whatsnew`

| key | meaning |
|---|---|
| `seen` | the last `FW_VERSION` that booted; rewritten at every boot |
| `tag` | the version whose news is **unacknowledged**; `0` = nothing pending |
| `when` | epoch of the update, for the banner's date; stamped once the clock is sane (`CLOCK_SANE_EPOCH`), possibly after boot |

**Factory reset clears `whatsnew`** (added to the clear list). A new owner must not inherit the
previous owner's pending tag. After the reset the unit is factory-fresh (case 3).

## Rules

1. **Raise:** on an upgrade (above) where **this image's entry is `notify: yes`**, set `tag =
   FW_VERSION` and `when` = now, or when the clock becomes sane.
2. **One tag, the latest.** A newer `notify: yes` release **replaces** a pending tag. The changes
   page covers everything missed in between.
   - A `notify: no` release leaves a pending older tag in place, still showing its own version: that
     news is still unread. (The image carries every entry's summary, so it can show it.)
3. **Persist:** the tag survives reboots (it is NVS) and has **no timeout.**
4. **Acknowledge, by either path, and both clear `tag` (one acknowledgment, stored once):**
   - **tap the tag on the device** (it also opens the summary);
   - **dismiss the banner on the config page** (`POST /whats-new/ack`, `X-Blipscope`).
   - **This is the only way a touch-unavailable unit can clear it,** and the spec says so on
     purpose. A unit whose touch is down shows the tag until someone dismisses the banner.
5. **Never a modal.** The tag never blocks the radar, never takes the screen, and never needs a tap
   for the device to work. **It yields** (is not drawn) while any of these is up:
   - the emergency or military flash (`visualAlertActive`);
   - the overhead card (v17);
   - setup (Connect, `tookScreenForSetup`);
   - the touch-unavailable strip (`StripShown`);
   - the reset menu or a detail card.

   It shows on the **Radar only**: bottom centre, just above the screen-indicator dots, sized to the
   chord at that row.
6. **The summary** (opened by a tap on the tag):
   - **Page 1:** "What's new in v17" and the entry's 2-4 summary lines.
   - **Tap:** page 2, the QR for `https://scopes.valarsystems.com/blipscope/changes#v17` and the
     short text URL.
   - **Tap again**, or **60 s with no touch**, closes it.
   - It uses the detail card's palette and dark background, so it is night-safe.
   - The overhead card does not show while it is open, the same rule as for a detail card. An
     emergency flash still draws over it.

## Config page banner

- The radar page's processor substitutes `%WN_TAG%` (0 = none) and `%WN_WHEN%` (epoch).
- The banner renders only when `WN_TAG > 0`.
- **The date is formatted by the browser** (`toLocaleDateString`), so it is right in the owner's own
  zone whatever the device's clock setting (#379).
- **Dismiss** posts `/whats-new/ack`, then removes the banner. The device clears `tag`, so the Radar
  tag is gone on the next frame.

## Changes page and its source

**Source of truth:** `docs/CHANGES-customer.md`, one section per FW version, newest first:

```
## v17
notify: yes
summary:
- Double-tap to zoom in
...
(page text, plain language)
```

- **Summary:** at most 4 lines, at most 24 characters each, checked by the generator.
- **Generators:** two outputs from that one file, each with a `--check` mode run in CI (the
  `embed-pages` pattern, so an edit to the source cannot ship with stale output):
  - `include/generated/WhatsNew.inc`: every entry's `notify` and summary, compiled into the image;
  - `proxy/pages/changes.html`: the page, embedded and served at `/blipscope/changes`.
- **The page must be live before the QR points at it.** `smoke-prod.sh` gains a check that takes the
  changes URL from the firmware source (input from the other side of the contract) and requires the
  live page to contain the current `FW_VERSION`'s section. It runs as part of promote. A QR to a page
  without the version's section would be the enrolment-404 failure again.

## Release gate

- **New job `customer-entry`:** reads `FW_VERSION` the way `version` already does
  (`firmware.yml:517-535`: exactly one declaration, numeric) and requires a `## v<N>` section with a
  valid `notify:` line and 1-4 summary lines.
- **`build` gains `needs: [skus, customer-entry]`.** If the entry is missing, no SKU builds, no
  receipt is written, and `version` refuses to publish `version.txt`. **The cut fails.** It runs on
  every push and PR too, so a FW bump without an entry is red long before a cut.
- **Code drafts the entry in the FW-bump PR.**
- **Wording approval, and a real constraint on it:** the summary is **compiled into the image at the
  cut**, so approving it at promote can only mean "accept, or re-cut".
  - **Recommendation:** Daniel approves the on-device summary in the FW-bump PR, **before** the cut.
  - The page text, served by the Worker, can still be edited and approved at promote.

## Telemetry (proposal, count only)

Two counters: **`whatsNewOpened`** (summary opened) and **`whatsNewQr`** (QR page shown). **Never
which version's text was read.**
- The approved v17 format change is 8 -> 10 (`doubleTapZooms`, `overheadCards`). **Proposal: make
  the one v17 change 8 -> 12** instead.
  - The Worker accepts 8 or 12 and drops anything else; 10 never shipped, so it is not accepted.
  - A Worker test sends one of each, plus 9-, 10-, 11- and 13-field controls that must be dropped.
  - The disclosures (README "Privacy & telemetry", `proxy/pages/support.html`) change in the same
    commit.
- **If the review keeps 8 -> 10,** these two wait for the next format change, and nothing is counted
  until then.

## Predictions to freeze before code (amended 2026-10-09)

- **W1:** after an **OTA 16 -> 17** on COM18, the tag appears on the Radar, and the config page shows
  the banner with the update's date.
- **W2:** after a **factory-fresh flash** (full erase), no tag and no banner, and `seen = 17`.
- **W3:** the tag **persists across reboots until it is tapped or dismissed on the config page**:
  present after a power cut and after the quiet reboot, with no time limit.
- **W4:** **dismissing the banner on the config page clears the device tag.**
- **W5:** a **`notify: no`** release raises no tag.
- **W6:** a **newer `notify: yes`** release replaces a pending tag. There is still one tag, now
  showing the newer version.
- **W7:** the cut **fails with no entry**: the `customer-entry` job is red and `build` does not run.
  - Proved in the script's selftest.
  - Proved in the workflow wiring with a throwaway **prerelease** tag (excluded from
    `releases/latest`, so no device can see it). This is the method CLAUDE.md prescribes for a gate
    whose blocking direction only a release event can exercise.

## Sabotage, each shown red and then undone

- **S1, drop the factory-fresh check (case 3 raises a tag):** W2 fails. A fresh flash shows
  "Updated to v17" (host test, then bench).
- **S2, make config-page dismissal not clear the device tag:** W4 fails. The host test of the
  acknowledgment, and the bench check of the Radar after the POST, both catch it.
- **S3, remove the release gate** (drop `customer-entry` from `build`'s `needs`): CI must go red.
  The gate script's `--check-wiring` mode reads `firmware.yml` and fails if `build` no longer depends
  on the gate. A gate that can be deleted without anything noticing protects nothing.

Host tests: the decision is a pure policy (`include/WhatsNewPolicy.h`: upgrade evidence,
raise/replace/keep, acknowledge), graded in `test/host/test_whats_new.cpp`.

## Card

Pending line for `docs/CARDS-README.md` (v17): *"After an update, tap 'What's new' on the radar to
see what changed."*

## Draft entries

See [CHANGES-customer.md](CHANGES-customer.md): a v16 and a v17 entry, written for Daniel to judge
the tone. Both are drafts, and the v17 entry describes features still being built.
