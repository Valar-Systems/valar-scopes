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
| `reason=204 (4WAY_HANDSHAKE_TIMEOUT)` or `reason=15` | **(c)** auth negotiation — and the scan record decides which kind. **WPA3 or WPA2/WPA3 transition advertised ⇒ WPA3/PMF.** **WPA2-PSK only ⇒ weak-node handshake loss**, the failure already in the ledger as "30x reason=204" |
| `Associated with "<ssid>"` but no `CONNECTED IP=` | **(d)** DHCP, or the WPA3-associates-but-no-IP variant. Association succeeded, so auth is not the fault |
| no `[WiFi] DISCONNECTED` line at all, and no build banner | **(e) CAPTURE FAILURE, not a result.** Wrong port, or started after the board booted. Re-run |
| anything else | **(f) STOP.** Do not improvise a reading at the moment the result is visible |

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
