# Factory reset — the two tiers, and what each one destroys

Two destructive operations, deliberately separate, reachable from three places.
This file is the enumeration: what each tier clears, what survives, and why.

## The tiers

| | Reset Wi-Fi | Factory Reset |
|---|---|---|
| network credentials | **cleared** | **cleared** |
| pinned AP (`wifi-fast`) | **cleared** | **cleared** |
| location, radius, display prefs | kept | **cleared** |
| spotting logbook | kept | **cleared** |
| leaderboard opt-in + display name | kept | **cleared** |
| factory cloud key (`cloud-key-fac`) | kept | **kept** — see below |
| leaderboard identity | unchanged | **unchanged — cannot be cleared** |
| reboots into | AP setup mode | AP setup mode |

## Every namespace, by tier

Cleared **by namespace**, never `nvs_flash_erase()`. A partition-wide erase would
take the Wi-Fi driver's own storage and the factory cloud key with it, and the
customer cannot recover either.

### Reset Wi-Fi

| namespace | contents |
|---|---|
| *(WiFiManager's own store)* | SSID + passphrase, via `wm.resetSettings()`. Not a Preferences namespace. |
| `wifi-fast` | `ch`, `bssid` — the pinned AP hint. Cleared because it belongs to the network just forgotten; leaving it makes the next boot chase a node it has no credentials for. |

### Factory Reset

Everything above, plus:

| namespace | contents |
|---|---|
| `config` | every settings-form key: `latitude`, `longitude`, `radius`, `radius-unit`, `data-source`, `local-url`, `opensky-id`, `opensky-secret`, `cloud-url`, `cloud-key`, `lb-enabled`, `lb-name`, `lb-standing`, `ntfy-topic`, `mqtt*`, `brightness`, `autodim`, `scanline`, `trail`, `fade`, `altcolor`, `highlight`, `triangle`, `infotext`, `airports*`, `watchlist`, `tz-offset`, `night-clock`, and every edition's keys. **Except `cloud-key-fac`.** |
| `logbook` | `types`, `operators`, `countries`, `airports`, `contacts`, `rec-high`, `rec-fast`, `rec-near` |

The logbook namespace is **the compiled edition's own** — `logbook` on radar,
`eam-log` / `fi-log` / `sp-log` on those editions, none on the rest. A radar
build does not clear `eam-log`: reaching into a namespace belonging to firmware
that is not running is the "took something with it" failure this whole design
avoids. A board reflashed across editions keeps the other edition's namespace,
which is the safe direction to be wrong in.

## What survives, and why

**`cloud-key-fac`** — the factory-provisioned cloud identity, written once by
the enrollment route, never rendered and never writable from the settings form.
That design is what makes "clear the Access key box and save" a safe repair
rather than an unrecoverable act, and it has the same consequence here: a
customer who factory-resets to fix a problem must not lose the one value they
cannot type back in.

It is preserved by reading it, clearing the namespace, and writing it back
**inside the same open handle** — reopening to restore would leave a window in
which a reboot mid-reset loses it permanently.

## What a factory reset CANNOT clear

**The leaderboard identity.** `DeviceIdentity::LeaderboardId()` is
`SHA-256(MAC ‖ salt)`, computed at boot and cached in RAM. It is **not stored**,
so there is nothing in NVS to erase, and a factory-reset device that opts back in
re-claims its own server row — inheriting the previous score, claimed types and
callsign.

The firmware says so on every factory reset rather than letting the log imply a
clean slate:

```
[reset] NOTE leaderboard id is MAC-derived and unchanged; the server row is not
        released by this reset
```

This matters for **resale** and it is a privacy question, not only a scoring one.
Releasing the row needs a server endpoint that does not exist yet. What it would
have to do:

- delete `lb:dev:<id>` — the device row (score, claimed types, badges)
- release `lb:name:<name>` → `<id>` — the callsign claim, or the name is held forever
- decide `lb:firsttype:<TYPE>` → `<id>` — the first-finder credits. **Deleting
  them frees the credit for someone else to claim; keeping them orphans a record
  to a device that no longer exists.** That is a product ruling, not an
  implementation detail, and it should be made before the endpoint is written.

Authentication is the open question: the device proves identity today only by
sending `X-Blip-Device`, which is the id itself — so an endpoint keyed on that
alone lets anyone who learns an id delete that row. It needs the device key.

### Before you go looking for `reset-leaderboard.mjs` — it is gone on purpose

There was a `scripts/reset-leaderboard.mjs`. It was a **one-shot migration tool**
for the KV move and was **deleted after use, by policy** — delete-after-use, not
an oversight, and not a thing to reconstruct.

**A device-release endpoint has never existed.** That is the parked design
question above, not a regression and not something that was lost with the
script. Recorded here so a future session finds this paragraph instead of
"rediscovering the gap" and re-litigating it.

**Releasing identity is a separate operation from factory reset** and is
deliberately not wired to it. A board changing hands needs its cloud identity and
its leaderboard row released together; doing half of it — clearing the cloud key
while the MAC-derived id stays — leaves a device that cannot reach the cloud
while its old score still sits on the public board.

## The three entry points

1. **Config web page.** Both tiers, visually separated, factory reset below a
   rule and in danger styling. The button is disabled until `RESET` is typed
   exactly (uppercase; a case-insensitive compare would let `reset` through, and
   this is the one control where easier is the wrong direction). The
   confirmation panel carries the `logbook.json?download=1` export link with
   "save your logbook first" — inside the panel, so it is shown at the moment it
   is worth anything. The device **re-checks the typed word**: the page's gate
   stops a mis-click, the server's stops a replayed or bookmarked POST.

2. **On device: Stats → `[ Reset ]` → two options → confirm.** Discrete taps
   throughout. **No press-and-hold anywhere** — the CST816D may report no change
   interrupt under a static contact, so a held finger can register as nothing at
   all, and a destructive control the panel may fail to see is one that looks
   broken to the customer who needs it most. Cancel is the largest target on both
   screens and sits lowest; the confirm target is smaller and higher. Backing out
   is the easy gesture; destroying data takes aim, twice.

   This replaced a 2 s hold. The hold existed to answer #165 (a cloth dragged
   over the panel triggered a two-tap reset) and it answered it well — a cloth
   cannot sustain contact for two seconds. The menu answers it differently: the
   cloth now reaches a screen with two options and a large Cancel, and a wipe
   needs a specific small target hit twice.

3. **Failsafe — not built yet.** Five power cycles in a row triggering a factory
   reset, counted in RTC memory and cleared after 10 s of healthy uptime. This is
   the only recovery path if touch is dead and Wi-Fi is unreachable, and there is
   no physical button on this hardware. **Scheduled, not dropped.**

   Until it lands, `BootTouchToForget` (touch-and-hold at power-on, Wi-Fi only)
   **stays** — it is presently the sole recovery path when touch works and the
   network does not. Its removal is gated on the failsafe being *proven on
   hardware*, not on the replacement being written.

## Logging

Every reset prints one line naming the tier and each namespace cleared:

```
[reset] tier=wifi cleared=wifi,wifi-fast
[reset] tier=factory cleared=wifi,wifi-fast,config,logbook preserved=cloud-key-fac
```

One line, not five interleaved prints — a destructive operation reported across
a scattered sequence is one nobody reads, and an unread report is the same as a
silent wipe. A namespace that could not be opened (an edition that never wrote
it, a device that never enrolled) is **left out of the list**, so the line
reports what was cleared rather than what was attempted.

## Where the work happens

All three entry points **request** a tier and return. `main.cpp` consumes it on
the loop task and calls `factoryreset::Perform()`. NVS writes from the async web
task would race the loop task's own writers — the logbook persists on a debounce
and the config form saves on POST. One place touches NVS for a reset.
