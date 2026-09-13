# A board that joins at home and will not join an eero 6+

**PRE-REGISTERED 2026-09-12, BEFORE ANY CAPTURE RUNS.** Written first so the
number cannot be read backwards into whichever story fits it.

## The report

A Blipscope that joins its home network normally failed at another house. The
AP is an **eero 6+ (model R010001)**. The portal accepted the password — so the
customer's side of setup completed — and the device then sat on
`Connecting to Wi-Fi...` and never joined.

## What is being captured, and on what

**The plain v11 shipping image.** No bench overrides, no fault injection, no
firmware change of any kind. This is observation, and the datum already exists:
`WiFi.onEvent`'s `ARDUINO_EVENT_WIFI_STA_DISCONNECTED` case already prints

```
[WiFi] DISCONNECTED  reason=%d (%s)
```

at [src/main.cpp:353](../src/main.cpp#L353), with the reason NAME resolved by
`WiFi.disconnectReasonName()`. Nothing needs adding to read it.

**Duration: twelve minutes, minimum ten.** WiFiManager is configured
`setConnectRetries(5)` x `setConnectTimeout(15)`, so one join phase is ~75 s,
plus ~30 s of boot. The portal then holds 180 s before the device reboots and
retries. One full cycle is **~4.8 minutes**, measured over the air on 2026-09-10.
Anything shorter than two full cycles cannot distinguish *failed* from *still
trying*, which is the whole point.

## SUPERSEDED 2026-09-12 — the scan record moved on-device

The section below described borrowing a laptop's radio because the shipping
image cannot report `WiFi.encryptionType()` for a scanned entry. **That reasoning
was correct and is now moot**: a display-only diagnostic build can call it
directly, and the screen replaces the whole laptop rig. Kept because it records
why the stock image could not answer the question, which is still true of the
stock image.

## The second datum, and why it comes from a laptop

The reason code alone does not separate the two candidates behind a handshake
timeout. The **advertised auth mode** does.

**The shipping image cannot supply it.** It never logs `WiFi.encryptionType()`
for a scanned entry — the only scan-ish data it records is the channel and RSSI
of an AP it has already joined successfully, which by definition does not exist
in this failure. Adding that logging would mean a non-stock image, which this
capture explicitly excludes.

So the scan record comes from **the laptop's radio**, via
`netsh wlan show networks mode=bssid`, which reports `Authentication` per SSID
and channel/signal per BSSID. Different instrument, same beacons, same datum —
and it keeps the device image stock. `scripts/wifi-join-capture.ps1` takes both
halves in one run.

Its one blind spot is stated rather than assumed: a laptop that sees the SSID
only on 5 GHz tells you the board (2.4 GHz only) may not see it at all, which is
itself outcome (a).

## Registered outcomes

| observation | verdict |
|---|---|
| `reason=201 (NO_AP_FOUND)` | **(a)** the SSID is not visible to the radio on 2.4 GHz. Band steering or range — **not auth**. Cross-check the scan: if the laptop sees this SSID only on a 5 GHz channel, that is the answer |
| `reason=202 (AUTH_FAIL)` | **(b)** credentials. The portal accepting the password means it was *stored*, not that it was *right* |
| `reason=204 (4WAY_HANDSHAKE_TIMEOUT)` or `reason=15` | **(c)** auth negotiation. ~~The scan record decides which kind: **WPA3 or WPA2/WPA3 transition advertised ⇒ WPA3/PMF**~~ — **STRUCK 2026-09-12, see below**. Only the second half survives: **WPA2-PSK only ⇒ weak-node handshake loss**, the failure already in the ledger as "30x reason=204" |
| `Associated with "<ssid>"` but no `CONNECTED IP=` | **(d)** DHCP, or the WPA3-associates-but-no-IP variant. Association succeeded, so auth is not the fault |
| no `[WiFi] DISCONNECTED` line at all, and no build banner | **(e) CAPTURE FAILURE, not a result.** Wrong port, or started after the board booted. Re-run |
| **it joins normally** | **(f) NOT A CLEARANCE.** A finding about NON-DETERMINISM. The failure was reproducible enough to report, so a single success means the fault is intermittent {E} which is worse to chase, not better. Record it and run again |
| **nothing renders on the screen at all** | **(g) CAPTURE FAILURE, not a result.** The diagnostic did not run. Check the banner is on the glass before starting the clock |
| anything else | **(h) STOP.** Do not improvise a reading at the moment the result is visible |

## AMENDMENT 2026-09-12: the WPA3 branch is dead

**Evidence.** The owner's eero app reports **Encryption: Standard (WPA2)** on
**both** the 2.4 GHz and 5 GHz bands — screenshotted, on current firmware, which
exposes the setting per band. WPA3 and WPA2/WPA3 transition mode are therefore
**ruled out**, not deprioritised.

**The original prediction is left standing above, struck rather than deleted.**
A pre-registration quietly edited into agreement with its outcome is worth
nothing; the whole value is being able to see which branch was predicted and
which one died. Row (c) predicted two causes behind reason 204 and named the
scan record as the discriminator. One of those two is now gone before any
capture ran, which is the pre-registration doing its job at the cheapest
possible moment.

**What it leaves.** Row (c) collapses to its second half: a 204 now means
weak-node handshake loss, consistent with the existing "30x reason=204" ledger
entry. And the discriminating datum moves: with encryption eliminated, the
question is **whether the board can see the SSID on 2.4 GHz at all, and from
which nodes at what signal**.

That is why the diagnostic screen must list **every BSSID found for the SSID,
with its channel, band and RSSI** — not the best one. On a mesh, *which nodes the
board can hear and how loudly* is the finding.

## THE A/B, REPLACED: band steering, not encryption

There is no encryption setting left to flip, so the comparison changes. Both
runs use the same board, same image, same twelve-minute floor, nothing else
altered.

**Run A** — their network exactly as configured.

**Run B** — the owner sets, in the eero app:
`Settings -> Troubleshooting -> "My device won't connect" -> "My device is 2.4 GHz only" -> Pause 5 GHz and 6 GHz`,
then Run B starts **inside that 30-minute window**, unchanged in every other
respect. Restore the setting afterwards.

| A | B | verdict |
|---|---|---|
| fails | **joins** | **BAND STEERING.** The board is not being given the 2.4 GHz radio under normal conditions |
| fails | fails | **steering exonerated.** Node selection or handshake — which lines up with the existing 30x reason=204 |
| **joins** | — | **non-deterministic**, same as outcome (f). A finding, not a clearance. Do not read a single success as a fix |

**Record on paper or by photo before starting:** eero model, firmware version,
node count, and the Encryption value shown per band.

## THE PATHS WHERE NOTHING RENDERS — so a blank screen is never read as "no failure"

The diagnostic draws from WiFiManager's AP callback. That fires when the portal
opens, which is what a failed join produces — but it is NOT the only thing that
can happen, and on the paths below **the screen shows no diagnostic at all**.
Enumerated because a blank screen is the most ambiguous possible observation and
the instrument is going in a car.

| path | what is on the glass | is it a result? |
|---|---|---|
| **the first ~105 s of every boot** — `TryFastJoin()` then 5 x 15 s of attempts, all before the portal | the normal `Connecting to Wi-Fi...` splash | **NO. This is normal.** Do not read it as a hang. The elapsed timer only appears once the portal opens |
| **the board joins** (fast-join or autoConnect succeeds) | the normal radar/splash UI | **NO** — and see outcome (f): a success is a finding about non-determinism, not a clearance |
| **crash, panic or WDT before the portal** | splash, or a reboot | **CAPTURE FAILURE.** Check the serial banner and the cycle counter: a jump of more than 1 between photographs means boots happened that never reached the portal |
| **the portal fails to start** (AP init failure) | splash, indefinitely | **CAPTURE FAILURE**, and a rare one worth naming because it looks identical to a hang |

**How to tell them apart without a laptop: the cycle counter.** It is persisted
and increments once per boot. A photograph showing `cyc 4` after twelve minutes
is a device that has been through four cycles; `cyc 1` after twelve minutes is a
device that never rebooted, which is a different fault entirely.

**Registered as its own outcome:** a blank or splash-only screen for more than
~3 minutes is **(g) CAPTURE FAILURE**, not evidence of anything about the join.

## WHAT REFLASHING COM6 DESTROYED, recorded so nobody re-opens it later

COM6 was reflashed several times before this diagnostic existed. **Whatever NVS
state it held when it failed at the friend's house is gone.**

- **Fine for a physical cause.** An antenna link, a u.FL seat, an RF path fault
  survives any number of reflashes and is still testable today.
- **Fatal for a stateful one.** If the failure depended on stored state — a
  corrupted Wi-Fi record, a bad saved BSSID pin, an NVS entry in a state the code
  mishandles — that evidence no longer exists and **cannot be recovered**. The
  branch is not disproven; it is untestable.

If the bench A/B and the trip both come back clean, this is the branch that was
thrown away, and it would have to be caught on the next unit that fails, before
anyone reflashes it. Worth a line in the support notes: **a board that fails to
join in the field should be captured before it is reflashed.**

## A CLEAN SCAN DOES NOT CLEAR THIS BOARD — correction, 2026-09-12

The diagnostic's first run on COM6 returned six APs at -51 to -65 dBm, and that
was described in passing as showing "the radio and scan path are demonstrably
healthy on this board."

**The second half of that is wrong, and it is the precise inference
[INCOMING-INSPECTION.md](../INCOMING-INSPECTION.md) section 4 exists to forbid.**

Section 4's whole point is that near-identical RSSI across boards proves nothing,
because **RX was never the problem**. The chip-antenna defect fails on **TX** —
the handshake frames the device SENDS to the AP. Section 4's own rejected board
scanned, associated, and read -64 dBm. A clean scan is therefore consistent with
BOTH a healthy board AND the specific defect under investigation, so it
discriminates nothing.

Stated precisely, and this is all the scan supports:

- **What it establishes:** the diagnostic reports correctly — the scan path, the
  BSSID enumeration, the band classification and `WiFi.encryptionType()` all
  work. That is what makes the instrument worth carrying.
- **What it does NOT establish:** anything about COM6's TRANSMIT path, which is
  the open question.

**The hardware branch is not closed by a scan and must not be recorded as
closed.** Only the bench A/B below speaks to TX, by counting reason-204 retries
— a failure of handshake frames going OUT.

## BENCH A/B — PRE-REGISTERED 2026-09-12, BEFORE RUNNING

The suspect is on the bench, so this runs before any trip.

**Method.** COM6 against a known-good board, both at bench distance from
Daniel's AP, several associations each. **Count reason-204 retries.** Section 4's
acceptance criterion is zero, and its documented reject case failed this test at
bench distance *while still connecting* — which is exactly COM6's behaviour at the
friend's house.

**Before powering either board: visual on the ANT1/RF1 0-ohm link and the u.FL
seat, photographed.** If the link is on `SOLDER_TERMINATION` or the u.FL is not
fully clicked, the answer is in hand without running anything.

| COM6 | control | verdict |
|---|---|---|
| non-zero | zero | **HARDWARE.** Check the ANT1/RF1 0-ohm link and the u.FL seat. **No trip.** Also an INSPECTION FINDING: a board in this state reached a customer-facing test |
| zero | zero | COM6's RF path is clean at this range. **The antenna hypothesis is dead** and the trip is back on, with the network hypotheses |
| non-zero | non-zero | **STOP AND REPORT.** Either the control is not good either, or the AP is the common factor. Do not proceed to a trip on a broken baseline |

## What this capture will NOT establish

- **Whether pinning the station to WPA2-PSK fixes it.** That is the leading
  hypothesis for outcome (c)-WPA3 and it stays a hypothesis. A Wi-Fi stack change
  made on a guess is the kind that looks fine on the bench and strands units in
  the field, so no auth-mode, PMF, or protocol change is being made before the
  number arrives.
- **Whether it is specific to the eero 6+.** One AP, one house. A reason code
  identifies the failure mode, not its population.
- **Anything about the customer's password.** Outcome (b) would say credentials
  are wrong; it would not say what they are, and nothing here logs them.

## Result

*(to be filled in from the capture — left empty deliberately)*
