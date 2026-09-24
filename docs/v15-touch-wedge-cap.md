# v15: cap the touch-wedge reboot, and say "touch unavailable" instead

**Status: spec only, no code.** Ahead of `nmi → NM` in v15. Firmware item; it also needs one
dashboard classification and no device-Worker change (the boot-reason parser already accepts
the token, see §5).

## 1. What happens today

The touch-wedge **last rung** (`src/AircraftManager.cpp:1359–1365` on main; `:1333` in the v11
image the fleet runs) reboots when `TouchWatchdog::RebootRecommended()` has held for the 90 s
outage bound **and** there has been no touch activity for `REBOOT_IDLE_MS` = 10 min, measured
from `lastTouchActivityMs`. That variable is set to `millis()` at initialisation and advanced only
by real touch activity. It exists only where `variant::TOUCH_WATCHDOG` is set (the Kit S3,
`s3-128`).

**On a board whose touch controller never answers, that condition is true on every boot**,
~600 s after start. Nothing caps it. Measured on one fleet unit (s3-128, fw 11; its id is on the fleet dashboard, not in this public
repo), 30 days of telemetry:

- 36 boots: 31 `SW`, 5 `POWERON`. 30 of 35 intervals are **615–616 s**; the others are three
  short cycles around someone power-cycling it and two long offline gaps.
- Network healthy throughout: 676 request points, **all HTTP 200** (`/blips`, `/config`,
  `/enrich`, `/airports`), traffic until ~590 s into every cycle, and each reboot lands ~30 s after
  the first usage report (sent at 600 s uptime). That rules out the Wi-Fi-down watchdog (needs
  600 s disconnected), the portal (180 s, no traffic) and `netwatch` (24 h cap, needs failures).
- 31 usage reports, **zero** card opens, screen switches and logbook claims, while every other
  reporting device shows some.

What the customer sees today: the radar works and aircraft appear, taps and swipes do nothing,
and every ~10 min the screen goes dark for ~10 s, shows "Connecting to Wi-Fi…", and comes back.
Forever. Nothing on screen says what is wrong or who to contact.

Not yet confirmed: the wedge itself. Only the device's serial output (`[touch-wd]`, and the
`[health] touch-wd wedges=… rebootRec=…` line) can show it. A bench capture or an RMA settles it.
Nobody touching the unit in those 31 ten-minute windows would give the same counters.

## 2. The change

After **N consecutive wedge-triggered reboots with zero touch events**, stop rebooting and enter a
**touch unavailable** state that the customer can see.

**N = 3.** The rung exists because a reboot was historically the one recovery a stuck chip always
answered, so the first reboot is worth taking, and a second covers a chip that needed a cold-ish
start. A chip that is still wedged after the third fresh boot, with the driver re-initialised and
the ~450 ms reset done each time, is not going to answer the fourth: the fleet unit above went through 30.
Three costs the customer at most ~30 min of periodic blackouts before the device settles, where
today the cost is unbounded. A larger N only lengthens the blackout period for a unit that cannot
recover this way, and a smaller one gives up on the case the rung was written for.

### The counter: NVS, and what resets it

- **Storage:** NVS, its own namespace `touch-wd` (not `ota-boot`: a dropped telemetry read must
  never be able to clear a reboot cap, the same reason OTA keeps its namespaces apart), key `run`,
  `u8`.
- **Increment:** in the rung, **immediately before** `ESP.restart()`. Stamp first, then reboot,
  like `DeferRebootWithCause`: power lost between the two leaves one reboot uncounted (harmless),
  never a counted reboot that didn't happen.
- **Reset to 0, in any of three ways:**
  1. **a real touch event** (the path that advances `lastTouchActivityMs`): the controller is
     alive, so the chain is broken;
  2. **a power cycle**: `esp_reset_reason() == ESP_RST_POWERON` at boot. Unplugging is what a
     customer tries first, and it must give the device a fresh start;
  3. **an hour without the wedge**: a boot that has run 60 min with `RebootRecommended()` never
     holding. Without this, a healthy unit that glitches once, recovers after one reboot, and
     then sits untouched overnight would carry `run=1` into the next glitch weeks later and creep
     toward the cap across unrelated, recovered episodes. The fleet unit never got past ~90 s healthy.
- **Wear:** write only when the value changes. At most N+1 writes per chain.

### Touch unavailable state (`run >= N` at boot, or reached during a boot)

- **Do not reboot for touch** in this state. The soft and hard recovery rungs
  (`TouchWatchdog`'s driver re-inits, which don't reboot) keep running, so a chip that comes back
  on its own is picked up, and the first real touch clears the state (reset 1).
- **On screen:** a persistent strip on every screen, which the customer can't dismiss because
  touch is what's broken, reading:

  ```
  Touch unavailable — contact support@valarsystems.com
  Device ID: <16-hex id>
  ```

  **It must show the device ID and the support address**, so a customer can self-report with the
  one identifier that finds the unit. The ID is the same string the config page shows under
  "Device ID:". The radar keeps running underneath: the device is still a working display, and
  switching it off would turn a partial failure into a total one.
- The strip uses no touch affordance and no text a customer is asked to tap.

## 3. Boot reason: `SW_TOUCHWD`

Record a new deferred cause, `REBOOT_CAUSE_TOUCH_WEDGE` (next free value after
`REBOOT_CAUSE_NET_WEDGE = 2` in `src/OtaUpdater.h`), before the rung's `ESP.restart()`, through
the same stamp-then-reboot mechanism. The next boot's reported reason becomes **`SW_TOUCHWD`**,
exactly as a network-wedge reboot becomes `SW_NETWD` (`OtaUpdater.cpp:66` and `:487`).

## 4. Tests (host, before any bench time)

The decision is a pure policy function, like `include/NetWatchPolicy.h`: inputs are `run`, the
reset reason, whether a touch occurred, and how long this boot has run wedge-free; the output is
`Reboot` / `EnterUnavailable` / `None`, plus the new `run`. Host tests:

- `run` 0→1→2→3 across three wedged boots, and the third reboot request becomes `EnterUnavailable`;
- each of the three resets returns `run` to 0, and each has a control where the condition is
  absent and `run` is kept;
- a `POWERON` boot at `run = 3` starts clean;
- an `SW` boot at `run = 3` stays unavailable (the cap survives a soft reset);
- the on-screen string contains the device id **and** `support@valarsystems.com`, with a control
  that it is not empty.

## 5. Telemetry and dashboard

- **Device Worker: no change.** `recordBoot` accepts reset-reason tokens up to 16 characters
  (`SW_TOUCHWD` is 10).
- **Dashboard:** classify `SW_TOUCHWD` as its own class in the triage item "most recent boot
  reason is not power-on / software reset", so a unit in this loop, or one that just entered
  touch unavailable, shows up by name instead of as a plain `SW`.
- The touch unavailable state itself sends nothing new. The device health header (v15 PR 2) is
  where a lifetime counter could go later; this item doesn't add one.

## 6. Incoming inspection

Touch is checked on every unit today: §6's "touch registers a tap" and §7's touch-and-hold Wi-Fi
reset. Neither catches this failure, because it only appears after **10 minutes untouched** and an
inspection is shorter. §6 of `INCOMING-INSPECTION.md` gains a one-line 11-minute soak for it.
