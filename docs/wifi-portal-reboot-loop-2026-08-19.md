# FINDING: every unprovisioned board reboot-loops out of its setup portal

**Found:** 2026-08-19, on COM119, while running factory-reset acceptance.
**Severity: day-one, fleet-wide, first power-on.** All 50 pilot units.
**Status: fixed** — [`WiFiManagerHelpers.h`](../include/WiFiManagerHelpers.h), verified on hardware.

## Symptom

A board with no Wi-Fi credentials — **factory-fresh, or just factory-reset** —
starts its setup hotspot, drops it after 180 s, reboots, and repeats. Forever.

```
11:01:47  [WiFi] autoConnect() returned false (portal timed out / not connected)
11:01:47  [WiFi] no network and nobody at the portal -- restarting to retry saved credentials
11:01:49  [ota-slot] boot ...                                       <- reboot
11:01:49  [WiFi] credentials saved: portal will time out after 180 s   <- there are none
11:01:52  *wm:No wifi saved, skipping   x5
11:01:52  *wm:StartAP with SSID:  Blipscope-31D918
```

The firmware's own line says *credentials saved*; WiFiManager says *No wifi
saved* four lines later. Both are in the same boot.

A customer unboxing a unit sees the hotspot vanish from their phone's Wi-Fi list
every three minutes while they are still looking for it.
`setAPClientCheck(true)` suspends the timeout **once someone is connected**, so a
customer who joins quickly is unaffected — which is exactly why this survived:
it is invisible to anyone who already knows the SSID and connects straight away.

## Mechanism — undefined behaviour, not staleness

`wm.getWiFiIsSaved()` → `WiFi_hasAutoConnect()` → `WiFi_SSID(true)`, which in
WiFiManager 2.0.17 is:

```cpp
wifi_config_t conf;                          // NOT initialised
esp_wifi_get_config(WIFI_IF_STA, &conf);     // return value DISCARDED
return String(reinterpret_cast<const char*>(conf.sta.ssid));
```

Called before the WiFi driver is started, `esp_wifi_get_config()` returns
`ESP_ERR_WIFI_NOT_INIT` and **never writes `conf`**. The SSID is therefore read
out of uninitialised stack memory, and garbage is usually non-empty — so the
predicate answers "credentials exist" on a board that has none.

It is not a stale value. It is **undefined**, which is why it can differ between
boards and boots, and why it read `true` on COM119 on every boot observed.

### Why this is day-one and not a factory-reset regression

The failure condition is *"the driver is not started"*. That has nothing to do
with whether credentials were ever stored, so a **factory-fresh chip hits it
identically on first power-on**. Erasing credentials does not cause it; it only
makes it reachable a second time.

Factory reset did not introduce this. It makes it the *common* path rather than
a once-per-lifetime one, which is how it came to be found.

## Fix

Read the config ourselves — struct zeroed, return value checked, and the driver
brought up first so the answer is real rather than merely safe:

```cpp
WiFi.mode(WIFI_STA);            // esp_wifi_get_config() cannot answer before this
wifi_config_t conf{};           // zeroed
if (esp_wifi_get_config(WIFI_IF_STA, &conf) != ESP_OK) return false;
return conf.sta.ssid[0] != '\0';
```

`WiFi.mode()` is idempotent and `autoConnect()` sets the mode it wants moments
later, so bringing the driver up here costs nothing and is what makes the
question answerable at all.

**Failing to `false` matters.** If the config cannot be read we behave as
unprovisioned, which gives a rock-steady portal. That is the failure direction a
customer can recover from; the other one strands the device.

The 180 s timeout itself is **kept** for genuinely provisioned boards — it is
the self-heal for a unit that powered up before its router, and that behaviour
was never the problem.

## Verification, on hardware

| | before fix | after fix |
|---|---|---|
| unattended, credential-less, 255 s | **2 reboots**, portal restarted each time | **1 boot banner, 1 portal start, 0 reboots** |
| branch taken | `credentials saved: portal will time out after 180 s` | `no saved credentials: portal stays up indefinitely for first setup` |

Captures: [`bench-logs/com119-acceptance-wifi-reset.log`](../bench-logs/com119-acceptance-wifi-reset.log)
(broken), [`bench-logs/com119-predicate-fix-verify.log`](../bench-logs/com119-predicate-fix-verify.log) (fixed).

The broken capture is kept deliberately: it is the negative control, and it was
recorded before the fix existed rather than reconstructed after.

## Consequence for the 5-power-cycle failsafe

Recorded here because this finding is the reason the rule exists.

**The failsafe must count healthy UPTIME, never connectivity.** A board sitting
in its setup portal, unprovisioned, is *healthy* — it is doing exactly what it
should. If "healthy" were defined as "joined a network", then a device waiting
to be set up would never clear its boot counter, and any reboot loop of the kind
above would accumulate straight into a spurious factory reset — wiping a
customer's device because they had not configured it yet.

This specific loop would not have triggered it (each cycle ran 180 s, far past a
10 s clear threshold), but only by accident of the timeout being long. A shorter
loop would have.

So: the counter clears after N seconds of running, whatever the network is
doing. See [`factory-reset.md`](factory-reset.md).
