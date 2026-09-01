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
