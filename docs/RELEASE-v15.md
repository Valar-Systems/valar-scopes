# v15 — the release list

**Cut v15 only when items 1–5 below are all on `main` AND the print cards are done.** "On main"
means `git ls-tree origin/main` shows the change, not that a PR is open or green (see CLAUDE.md,
*a green signal is about process*). Nothing else rides v15: an item not on this list waits for v16.

Order is priority, highest first.

| # | item | spec | status |
|---|---|---|---|
| 1 | Touch-wedge reboot cap + "touch unavailable" | [v15-touch-wedge-cap.md](v15-touch-wedge-cap.md) | spec accepted; not built |
| 2 | Follow flag in the usage report | this file, §2 | not built |
| 3 | ntfy removal | not yet written in the repo | not built |
| 4 | `nmi` → `NM` | not yet written in the repo | not built |
| 5 | "Use my location" on the config page | this file, §5 | spec; not built |
| — | Print cards | PR #340 | done — merged `e853f0a` |

## 1. Touch-wedge reboot cap

After 3 consecutive wedge-triggered reboots with zero touch events, stop rebooting. Show a
persistent strip with the device ID and support@valarsystems.com, and report boot reason
`SW_TOUCHWD`. Full spec, including the NVS counter and its three resets:
[v15-touch-wedge-cap.md](v15-touch-wedge-cap.md).

## 2. Follow flag: the usage report always says "not configured"

**The bug.** `UsageStore::SetFollowEnabled()` (`include/UsageStore.h:67`) has **no callers**, so
`followEnabled` stays `false` and every usage report sends `0` in field 7. Measured: 0 of 1,493
reports in 30 days had it set, including on units where Follow is configured. The flag is
correct by construction (a boolean, never the target); it is simply never written.

**The fix.** Call `SetFollowEnabled()` wherever `followTarget` changes: when it is **set**,
**cleared**, and **loaded at boot** (`src/AircraftManager.cpp` around 895–911, where
`followTarget = want`). The value passed is `!followTarget.isEmpty()`, never the string.

**Test (host).** A configured follow target puts `1` in field 7 of `usage::Format()`, and an empty
one puts `0`. The test drives the real call sites' path rather than calling the setter directly,
so **removing the setter call makes it fail**. That is the regression this bug is.

**Dashboard.** Label the Usage page's Follow column **"unreliable before v15"** until the fleet
is on 15. Every report from firmware below 15 carries `0` whatever the device's configuration.

## 3. ntfy removal

Spec not yet in the repo. Needs one before it can be built.

## 4. `nmi` → `NM`

Spec not yet in the repo. `include/DisplayUnits.h` already calls out the trap to avoid: a missed
site renders statute miles under a nautical label, and the stored unit string must survive a
downgrade.

## 5. "Use my location" on the config page

**The ask.** One button beside Latitude/Longitude that fills both from the browser's location.

**A direct `navigator.geolocation` call cannot work here, and that was measured, not assumed.**
The config page is served over plain HTTP at `http://<device>.local`, and geolocation is only
available in a *secure context* (HTTPS, or `localhost`). Measured 2026-09-24 in desktop Chrome
(headless, fresh profile), one page served two ways:

| origin | `isSecureContext` | `getCurrentPosition` |
|---|---|---|
| `http://blipscope-test.local` (how customers reach the device) | `false` | fails **at once**, code 1, *"Only secure origins are allowed"*; no prompt |
| `http://localhost` (control: same page, secure) | `true` | reaches the permission layer (code 3, timeout: headless has no location) |

The control is what makes the first row mean something: the same page on a secure origin gets
past the gate, so the refusal is the origin, not the rig. Firefox and Safari were **not**
measured; both document the same secure-context rule, and the design below does not depend on
which browsers refuse. Headless Edge produced no reading at all and is not claimed either way.
The customer-visible consequence of shipping the naive button: it would do nothing, instantly,
with a "permission denied" for a prompt the customer never saw.

**What the button does instead: the enrolment pattern, reused.** Enrolment already opens an HTTPS
page from this HTTP page and gets a value back (`window.open` → `/blipscope/enroll` →
`window.opener.postMessage`). Location does the same:

1. **"Use my location"** sits next to the Latitude field. It is added by the shared shell JS
   (`CONFIG_SHELL_JS`, beside `bpSay`), so every edition whose form has a Latitude field gets it
   from one implementation, not eight.
2. Tapping it opens `https://scopes.valarsystems.com/blipscope/locate?o=<this page's origin>` in a
   popup, **from inside the click handler** (outside a user gesture, the popup is blocked).
3. The helper page says what it is about to do (*"Share this browser's location with your
   Blipscope? It goes to the settings page you came from and nowhere else."*) and has one button.
   It calls `getCurrentPosition` only when **that** button is tapped. That is deliberate: the
   browser remembers a granted permission per origin, so without a click on our own page, any
   website could open `/locate` and read a returning customer's location silently.
4. On success it posts `{type:'blipscope-location', lat, lon, acc}` to the opener, with
   `targetOrigin` set to the `o=` value, **only if** `o` is `http://<name>.local` or a private IPv4
   address (10/8, 172.16/12, 192.168/16 — the AP-mode page is `192.168.4.1`). Any other `o` is
   refused and nothing is posted. The browser then delivers the message only if the opener's
   *real* origin equals `o`, so a page that lies about its origin receives nothing. That is enforced
   by the browser, not by our code. Then it closes itself.
5. The config page's listener checks the VALUE, as enrolment does: `type` matches; `lat` and `lon`
   are finite and in range (±90, ±180). Otherwise it is ignored. Values are rounded to **4 decimal
   places** (~11 m, the precision `bpSay` already echoes; the radar needs nothing finer) and
   written into both boxes as a `"lat, lon"` string through the **existing** paste-splitter path, so
   one parser handles typed, pasted and located input.
6. **Nothing is saved.** The page says *"Filled from this browser: 44.0582, -121.3153. Press Save
   to keep it."* If `acc` is over 1,000 m (a desktop with no GPS locating by IP), it adds *"This is
   only accurate to about N km. Check it on a map before saving."*

**Privacy, by construction.** The helper page is static HTML. It makes no request, and is served
with `Content-Security-Policy: connect-src 'none'; form-action 'none'`, so the browser itself
refuses any attempt by it to send the position anywhere. Coordinates travel by `postMessage`,
never in a URL, so they never reach a Worker log or the analytics. The helper stores nothing.

**When it fails, the fields are never touched.** Each case is one sentence, shown on the helper
page (when it is open) and echoed on the config page via `bpSay`:

| case | what the customer sees |
|---|---|
| permission denied (code 1) | *"Location is turned off for this page, so nothing was filled in. You can find your numbers at gps-coordinates.org and paste them into either box."* (a link) |
| unavailable (code 2) | *"This browser couldn't work out where it is. Paste your numbers from gps-coordinates.org instead."* |
| timeout (15 s, code 3) | *"Finding your location took too long. Try again, or paste your numbers from gps-coordinates.org."* |
| popup blocked (`window.open` returns null) | *"Your browser blocked the location window. Allow pop-ups for this page, or paste your numbers from gps-coordinates.org."* |
| helper closed without answering | nothing; the fields stay as they were |

**On gps-coordinates.org "coming back with the values": it cannot.** It is a third-party site with
no way to return a value to our page. We cannot make it `postMessage`, and a link to it can only
hand the customer a page to copy from. So it is the **fallback** in every failure line above, and
the paste-into-either-box path it feeds already exists and is tested (`CoordParse`, whose header
records that gps-coordinates.org's 15-decimal output is why the fields are plain text).

**Tests.**

- **Host (`test/host/test_coord_parse.cpp`).** The input is taken from the other side of the
  contract, not typed by the test. The helper's formatter lives in `proxy/src/locatepage.ts` and
  is exported. A proxy test writes its output for a fixed set of positions (both hemispheres; the
  ±90/±180 bounds; values that round across `.99995`; `-0.00004`, which must not come out as
  `-0.0000`) to a checked-in fixture, `test/fixtures/locate-format.txt`. It fails if the file is
  stale (`--check`, like `embed-pages`). The host test reads that fixture and passes every line
  through `CoordParse::SplitPair`. Each line must parse, and must land within 0.00005 of the
  source position. Control: a line with lat 90.0001 is appended in-test and must be **rejected**,
  so the test proves it can fail.
- **Proxy (vitest).** `/blipscope/locate` returns 200 with the CSP header above. The page contains
  no `fetch(`, `XMLHttpRequest`, `sendBeacon` or `<form`. The origin rule accepts
  `http://blipscope.local` and `http://192.168.4.1`, and refuses `https://evil.example`,
  `http://blipscope.local.evil.example` and `http://8.8.8.8`. The four failure sentences are
  present.
- **Contract (`smoke-prod.sh`).** Extend the enrol-URL check, which already greps the URLs out of
  `ConfigurationWebServer.cpp`, to grep the locate URL the same way and fetch it from production,
  asserting 200 and the CSP header. A route typo then fails on the firmware's own string. That is
  the enrolment 404 lesson.
- **Sabotage, one per feature, each shown red then green.** Formatter emits 3 decimal places →
  the host round-trip fails. The origin regex drops its `$` anchor → the `.local.evil.example`
  case fails. The CSP header is removed → the vitest and the prod smoke both fail.

**Glass, once, before merge:** a phone on the LAN (iOS Safari and Android Chrome), permission
granted and then denied. Fill, echo, and "nothing saved until Save" are confirmed on the device
page. This is the one part no host or proxy test can see: whether the popup and its opener link
survive on a real mobile browser.
