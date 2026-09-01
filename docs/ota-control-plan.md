# OTA Control — Launch Gate

*Written 2026-09-01. Not yet run.*

**Status of the claim under test:** unknown, not broken. OTA has never been validly
observed on a device running shipping firmware.

**Why the last attempt proved nothing.** The COM16 control watched a device running stock
firmware, whose OTA check interval is the 24-hour default, over a window of a few minutes.
The 5-minute interval was a build flag present only on COM119. So "no update seen" was the
expected observation whether OTA worked perfectly or was dead in the water. Pass and fail
states were identical. That is not a weak test; it is not a test.

**What this plan fixes.** Every phase below has a distinguishable failure, and every
observation is made on an image identical to what ships.

---

## The property being tested

> A device running shipped firmware, left alone with no cable and no human touching it,
> will discover, download, verify, apply and boot a new image within one check interval,
> and will refuse an image that fails verification.

That is six separate claims. A test that only watches for "did it update" collapses them
and cannot tell you which one failed.

---

## Invalidation conditions

Any of these makes the run meaningless. Stop and restart rather than reasoning around them.

1. **Any device under test is running a non-shipping build.** No debug flags, no shortened
   intervals, no canary tag. COM119 is currently on a canary tag and must be reflashed from
   the release artifact before it can participate. If the interval is shortened to make the
   test convenient, the test no longer covers the firmware you sell.
2. **Any human intervention inside the window** — power cycle, reset, button press, USB
   connection. The claim is about unattended behaviour. Touching the device tests something
   else.
3. **Version read over the serial console rather than from the device's own report or its
   screen.** A customer has neither a cable nor a terminal. If the only way to know the
   device updated is to plug into it, you have not tested what customers experience.
4. **Any other unit powered on during the window.** `download_count` is fleet-wide; a
   second device fetching makes the counter uninterpretable.
5. **Devices flashed from a local build rather than the release artifact.** Flash from the
   same artifact the OTA channel serves, or you are comparing two different things.

---

## Phase 0 — instrument the observable (before any device is touched)

**Corrected premise.** Devices check GitHub Releases directly. The Worker is never on the
update path, so the update *check* cannot be observed server-side at all. An earlier draft
of this plan assumed a Worker endpoint; that was wrong, and the phase below replaces it.

What can be observed, tonight, with no firmware change:

**Instrument A — the feed request (Worker).** Every feed request already carries
`X-Blip-Device` and `X-Blip-FW`. Log device id, firmware version and timestamp on each one.
The Worker then knows what firmware every device is running, over time. **An OTA success is
that value changing with nobody touching the device.** That is the observation the whole
plan turns on.

**Instrument B — the release asset (GitHub).** The Releases API exposes `download_count`
per asset. Record it immediately before the window opens and immediately after it closes.

Together they give three distinguishable outcomes:

| `download_count` | reported FW | conclusion |
|---|---|---|
| increments | changes | success, end to end |
| increments | unchanged | downloaded, then failed to verify or apply |
| unchanged | unchanged | never fetched — the timer or discovery is dead |

**What is not observable, and why it was dropped.** "Checked in and there was nothing to
install" cannot be seen while the device talks to GitHub directly. An earlier version of
this plan made that distinction the foundation of the test. It cannot be performed without
a firmware change, so it is out. Do not substitute a weaker proxy for it and call it
covered.

**Verify both instruments can produce a signal before trusting a null reading:**

- Issue one feed request from a known device and confirm the row appears with both headers
  populated. A log that has never been seen producing a row is not evidence.
- Fetch the release asset once by hand and confirm `download_count` moves. Record that
  fetch — it is part of your baseline, and it is also the control proving the counter works.

**Keep the window clean.** `download_count` is fleet-wide, not per-device. No other unit may
be running or fetching during the window, or the counter is uninterpretable.

### Status as of 2026-09-01: A built and undeployed, B built and FAILING its control

**Instrument B's control does not pass.** After a hand fetch of `version.txt`, the counter
did not move across six reads over sixty seconds; re-checked six minutes later after three
hand fetches in total, still 87. The fetches themselves succeed (`HTTP 200`, 2 bytes, two
redirects). So `download_count` is lagging far longer than an hour, not counting these
fetches, or dead.

**Nothing may be concluded from the number 87 that is currently on `version.txt`.** It was
briefly read as proof that discovery and the timer are alive. That reading is withdrawn, and
it is withdrawn on its own evidence: if the `latest` alias is uncounted -- the leading
hypothesis, and the alias the device actually uses -- then none of those 87 came from a
device and they are development and CI traffic. The supporting argument was worse: the
`version.txt` 87 against `firmware-s3-128.bin` 3 was read as "the shape fleet traffic
makes, many checks and few installs". It is equally the shape DEVELOPER traffic makes --
the version endpoint gets hit constantly while working and the binary rarely. A ratio
consistent with two explanations discriminates between neither.

**So "the timer never fires, or discovery is dead" is NOT excluded.** It remains one of the
live outcomes in the Run 1 table below.

**The discriminating test, which is not "fetch the tag-pinned URL and see".** That has a
hole: if the real cause is counter lag or API caching rather than the alias, a pinned fetch
will not move promptly either, and the wrong conclusion -- "pinned is uncounted too" -- is
indistinguishable from the right one. Instead fetch BOTH paths with DIFFERENT counts in one
pass, then read once after an hour or more:

    3 fetches via  /releases/latest/download/version.txt
    7 fetches via  /releases/download/<tag>/version.txt

| delta after >= 1 h | conclusion |
|---|---|
| +7 | the `latest` alias is uncounted -- and it is the alias the device uses, so B can never see a device download |
| +10 | both paths count; what was observed was lag, and B is usable with a long enough settling time |
| 0 | the counter is dead, or lags far beyond any useful window |

### B's read, 2026-09-01T17:14Z: +6, which is in NO bucket. Timebox expired; B is DEGRADED.

The dual-path probe ran (`bench-logs/ota-dualpath-probe.txt`): baseline **89** at 14:33:51Z,
then 3 fetches via `latest` and 7 via the pinned tag. Read back at 17:06Z and again at
17:14:31Z: **95 both times.**

    delta = +6 over ~2h40m

**+6 matches none of the three predicted outcomes, and it is BELOW the pinned-only floor of
7.** Not one of the 3/7/0 rows can be claimed. The temptation is to call it "+7, so the
alias is uncounted" — that is rounding a measurement into the nearest hypothesis, and the
whole reason this test was designed with unequal counts was to make that impossible. If
fewer counts have landed than the *pinned* fetches alone should produce, then counting has
not settled, and "latest is uncounted" is **not** distinguishable from "still lagging."

**What IS now established: the counter is not dead.** The retracted reading was 87; this
probe's baseline was 89. Those +2 are the earlier hand fetches — the ones that "did not
move the counter" across 60 s and again at 6 minutes — arriving late. So the counter works
and its settling time is *hours*, which is what makes it useless inside a run window.

**The isolated follow-up probe is contaminated and cannot resolve this.**
`bench-logs/ota-latest-probe.txt` started at 17:06:11Z with baseline 95 — i.e. it opened
*while the dual-path probe was still draining*. Any late dual-path arrival now lands inside
its window and is attributed to its `latest` fetches, which is precisely the hypothesis it
exists to test. An anchor taken against a target that is still moving measures the movement,
not the target. **A clean re-test needs the previous probe fully settled first — no fetches
of any kind for several hours, a baseline read twice at an interval to prove it is static,
and only then the fetches.**

**Operative state for Run 1: the three-outcome table has degraded to a single signal.**
Instrument A alone is the gate, and A is verified and deployed. A null or ambiguous
`download_count` across Run 1 means **nothing** and must not be reported as "never fetched" —
that row of the table is unavailable until B's control passes, which it never has.

**B IS TIMEBOXED TO ONE HOUR AND MUST NOT BLOCK THE CLOCK.** Instrument A is the gate; B is
diagnostic assistance. If A shows a device's reported firmware changing with nobody having
touched it, OTA works end to end and that alone passes Run 1. B only tells you WHICH step
failed when it fails. If B is unresolved after an hour: deploy A, verify it produces a row,
and open Run 1 anyway with reduced diagnostics -- and record here that the three-outcome
table has degraded to a single signal, so that nobody later reads a null `download_count`
as meaningful when it means nothing.

### Instrument A is VERIFIED, including the branch Run 1 turns on (2026-09-01)

Deployed and confirmed live: `/healthz` reports `4d72a06`, and the confirmation loop read
`b16e859` on its first poll before flipping -- so it proved it could tell the two apart
rather than merely agreeing with the expected answer.

**The write-suppression is proven behaviourally.** COM4's first row read
`firstSeen == lastSeen` 52 minutes after boot: one KV write across roughly 600 feed requests
at a 5-second poll.

**And `changes[]` has now been seen executing, which it had not been.** An empty array from a
device that never updated is not evidence that the detector works -- it is a branch nobody
has watched run, sitting on the single observation this whole plan depends on. If Run 1 had
completed with `changes[]` empty, "the update never happened" and "the detector is broken"
would have been indistinguishable.

Forced without an OTA, by bumping `FW_VERSION` and flashing over USB (safe: `OtaUpdater.cpp`
returns early on `latest <= FW_VERSION`, so a board on 9 against a release of 8 sits quiet --
observed: `[ota] channel=s3-128 current=9 latest=8`):

    8 -> 9   {"fw":"9", firstSeen 1788273635331, changes:[{from:"8",to:"9",at:1788277780930}]}
    9 -> 8   {"fw":"8", firstSeen 1788273635331, changes:[ ...8->9..., {from:"9",to:"8",at:1788277922570}]}

Both directions fire, entries APPEND rather than overwrite, and `firstSeen` survives both --
so a device that updates will still show when it was first seen.

**These two transitions are left in place deliberately, and Run 1 readers must subtract
them.** The bench board's own row (`fw:<COM4's device id>` -- the id is deliberately not
written here; the pre-commit hook refuses real ids, and it refused this paragraph's first
draft) carries two synthetic entries stamped 1788277780930 and
1788277922570 (2026-09-01). Deleting them would have tidied away the only evidence this
instrument has ever produced a row, which is precisely what this plan refuses to accept for
anything else. Any `changes[]` entry stamped inside a run window is real; these two are not.

**THE BASELINE IS TWO. Run 1 looks for a THIRD entry, not for a non-empty array.**
Written out because the absence of this line has already cost one false alarm: the
synthetic 8->9 transition was flagged as a possible OTA anomaly hours after it was
created, by the person who created it, because nothing recorded that it was
expected. A non-empty `changes[]` is now the NORMAL state of this row.

#### RUN 1 BASELINE: the change-entry COUNT is 2 (fleet-wide, 2026-09-01)

**Run 1's pass condition is a NEW entry, not a non-empty array.** Those are different
tests and only one of them works now: `changes[]` is already non-empty in the resting
state, so "did any transition appear" answers yes before Run 1 starts.

    fleet-wide changes[] entries, all synthetic, as of 2026-09-01 ...... 2

Run 1 passes on **3 or more**, or equivalently on an entry stamped inside the run window.

This paragraph is the second attempt at this note, and the first attempt's failure is the
reason for the number. The prose above -- "these two are left in place deliberately" -- was
already here, correct and unread, when a reconcile on 2026-09-01 hit the same row from a
different direction and reported the `8 -> 9 -> 8` pair as a firmware downgrade worth
investigating. **A note in a document is only read by someone who already opened the
document**, and the person who trips over a signal is by definition somewhere else.

So the count is also printed by `python scripts/reconcile-fleet.py`, which reads the fw:
table on every run and labels the figure as this baseline. That is the copy that will
actually be in front of whoever needs it; this one is the explanation of why.

### There is no upstream track source -- the device is the only one we will get

Established 2026-09-01 by enumerating adsb.lol's published OpenAPI: **18 endpoints,
none of them historical.** No trace, track, history or path route exists, and the
readsb/tar1090-style paths (`/api/0/trace/{hex}`, `/data/traces/<xx>/trace_full_<hex>.json`)
return 404 against the direct host. Every endpoint returns current state: one
position per aircraft. adsb.fi is not yet resolved either way and should not be
extrapolated from this.

So a real flown track can only come from **the device accumulating its own
observations during a follow session**, bounded by reception range. Written down
so nobody goes hunting for a source again. Note that the bend-through-the-aircraft
change in DrawRouteGlobe uses ONE current position and accumulates nothing -- it is
the cheap approximation, not the thing itself.

### An unenrolled board is undetectable in the field, by construction

`meta.dev` and `meta.fw` are only populated past authenticate (see the header of
`proxy/src/metrics.ts`: recording them earlier would let anyone write arbitrary device ids).
So Instrument A's placement is forced, and the consequence is stronger than "unenrolled
boards produce no row":

> **"No row" means unenrolled, or unpowered, or offline, and those three are
> indistinguishable -- permanently, not just during a run.**

This is not a check that can be added later. It is a PROCESS CHANGE, and it belongs to
manufacturing rather than to this plan's runs: **enrollment must be verified at the bench,
before the box closes, on every unit.** There is no subsequent opportunity to notice that a
unit was never enrolled -- a dead board and an unenrolled board look identical from here
forever.

What Instrument A *does* answer, for units that are enrolled: whether a running board ever
appears in the feed log at all, and what it is running when it does.

---

## Run 1 — does OTA work at all (~25 hours, two devices)

Two devices, both freshly flashed from the release artifact, both left alone. They run the
same window simultaneously, so the negative and the positive are observed under identical
conditions.

| | Device A (control) | Device B (subject) |
|---|---|---|
| Firmware | release artifact, version *N* | release artifact, version *N* |
| Update published for it | none | version *N+1* |
| Expected outcome | stays on *N* | downloads, verifies, applies, boots *N+1* |

**Version *N+1* must differ only in a marker**, not in behaviour — a bumped version
identifier is enough. The marker must be visible **two ways**: reported by the device to
the server, and displayed on screen. If only one of those changes, that is a finding, not
a pass.

**Start the window. Do not touch either device for 25 hours.**

### Reading the result

| Observation | Conclusion |
|---|---|
| `download_count` unchanged and neither device's reported FW moves | **The timer never fires, or discovery is dead.** Headline bug. Stop here — nothing downstream matters until it is fixed. |
| `download_count` +1, B reports *N+1*, A still reports *N* | **Pass.** The mechanism works end to end, and A proves the update was delivered by OTA rather than by something else in the room, and that targeting works. |
| `download_count` unchanged but B's FW moves | The update did not come from the release asset. Something else changed that device — find out what. |
| `download_count` increments, B still reports *N* | Transport works; verification or the apply step does not. |
| B applies but boots back on *N* | Rollback is firing. Something in *N+1* fails the boot check, or the partition logic is wrong. |
| B reports *N+1* to the server but the screen still shows *N* (or vice versa) | The reported version and the running image have separate sources. Broken looks exactly like normal here — treat as a failure. |
| A also updates | Targeting does not work. You cannot do staged rollouts, and a bad build reaches all fifty at once. |

**Record for each device:** device id, image hash before and after, every server log row with
timestamps, and a photograph of the screen showing the version.

---

## Run 2 — does it refuse a bad image (~25 hours, one device)

Run only after Run 1 passes. Use **one device you are willing to lose**, on shipping
firmware at version *N+1*.

Publish an artifact that fails verification — a corrupted payload, or one whose checksum or
signature does not match. This is the failure that matters most commercially: not "OTA
didn't update" but "OTA bricked fifty units."

**Expected:** `download_count` increments — the device fetched it — and the device stays on
*N+1* and keeps working. That pairing is the whole signal: fetched, then refused.

| Observation | Conclusion |
|---|---|
| `download_count` +1, device still reports *N+1*, still working | **Pass.** It fetched a corrupt image and refused to apply it. A bad artifact cannot brick a customer. |
| `download_count` unchanged | Inconclusive, not a pass. It never fetched, so verification was never exercised. Re-run. |
| Device reports the bad version, or behaves differently | **Launch blocker.** Verification is not running, or is not gating the apply. |
| Device stops reporting entirely | **Launch blocker.** It bricked or is stuck. Do not ship until this is understood. |

**Note what is missing:** with GitHub as the source there is no server-side record of the
*reason* a device refused an image. In the field, a unit that quietly refuses every update
looks identical to one that is up to date. If Run 1 and Run 2 both pass, that gap is worth
closing later — a refusal reported on the next feed request would do it — but it is not a
launch blocker on its own.

**Optional, only if Run 2 passes and you want the stronger guarantee:** publish an image
that verifies correctly but crashes on boot, and confirm the device rolls back to *N+1* on
its own. This is the closest thing to the real disaster scenario. It carries real risk to
the unit, so do it on the sacrificial device only, and only if you want that assurance
before selling.

---

## Coupled question worth answering in the same window

An unenrolled board may never be reachable by OTA at all. One unit in the field is already
unidentified and may never have enrolled.

While Run 1 is in flight, answer separately: **does an unenrolled board appear in the feed
log at all?** Instrument A gives you that for free — if a device is running and never
produces a row, it is invisible to you in every sense that matters. And:
**can a board leave the bench unenrolled without anything failing loudly?** If yes, that is
a fleet problem independent of OTA — units you cannot update, identify, support or revoke.

---

## What "OTA works" means when this is done

All of the following, or it does not:

- [ ] A device on shipping firmware fetches and applies an update with no cable and nobody touching it
- [ ] The Worker records each device's reported firmware version, and it is seen to change unattended
- [ ] A published update reaches the targeted device and only the targeted device
- [ ] The device boots the new image and both its screen and its report agree on the version
- [ ] An image that fails verification is fetched and then refused, and the device keeps working
- [ ] The device keeps reporting to the Worker after the refusal

Anything short of all six means you are shipping fifty units you may not be able to fix.
