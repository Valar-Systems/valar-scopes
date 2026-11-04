# The web renderer: the radar firmware as the website's renderer

## The request (Daniel, 2026-11-04)

> the WebAssembly demo looks right. Let's make it the website's renderer. Productionise the web build: release
> artifacts per model per firmware release (1.28" now; 2.1" built but not published until I announce it) ... replace
> the demo's fixed scene with the firmware's real data path, shimmed to fetch a same-origin URL that returns the
> device's /blips response shape ... a smooth-text mode with a self-hosted font file shipped in the artifact ...
> Stub anything device-only ... Device builds stay byte-identical. Predictions first.

**Where it starts:** the spike, `tools/wasm-demo/` (`dc81ceb`).
- The radar's own translation units, unmodified, compiled with Emscripten and replayed as vector shapes.
- A synthetic scene compiled into the wasm.
- Shown on 2026-11-04 in Chrome and in a claude.ai preview.

## The design, frozen before the code

**The directory** becomes `tools/web-renderer/`. The scene leaves the wasm: the release build has no data of its own.

**The data path. The firmware's own, end to end:**
- `RunFetchTask` and `RunEnrichTask` run as before, on cooperative fibers.
- The web `HttpRequestManager` turns each request into a real `fetch()` against the page's origin. The task's fiber
  yields frame by frame until the response arrives (or 15 s pass: status 0, as a dead socket reads on the device).
- The firmware's parsers read the body: `CloudFeed::ParseBlips`, `ParseEnrich`.
- **The URL shape is the device's.**
  - `GET <apiBase>/api/v1/blipscope/blips?lat=<2 dp>&lon=<2 dp>&r=<km>&limit=40`, built by the firmware itself
    (`wireloc::Center`, `BLIPS_LIMIT`).
  - `<apiBase>` is a page option, default `""`: the same origin.
  - The shim fetches only URLs that begin with the configured cloud base, re-rooted onto `<apiBase>`. Anything else is
    answered locally with 404, and **no request leaves for it**.
- **No device credential exists in the web build:**
  - the web config's `cloud-key` is a placeholder;
  - the shim sends **no headers at all** (the firmware's `X-Blip-*` set is dropped), so a website request carries
    nothing of a device's.
- **`/enrich/<hex>`** goes the same way, to `<apiBase>/api/v1/blipscope/enrich/<hex>` with the firmware's query.
  - The Worker may serve it or answer 404.
  - On 404 the contact settles without type or operator (the firmware's own outcome), so labels show callsign,
    altitude and speed.

**`/config` and `/airports` stay on baked defaults. They are never fetched.** The shim answers both locally with 404:
- the firmware keeps its compiled-in cadence and gates;
- the radar draws airports from its baked majors table (`Airports.h`).
- **Why:** `/config` carries fleet gates (`minFw`, OTA) that mean nothing on a website, and one fewer Worker route
  is one fewer thing to keep in step.
- Either can be routed later by adding one prefix to the shim's allow-list.

**The settings come from the page.**
- `mount(canvas, {lat, lon, radius, unit, …})` fills the web config map before `Initialise()`.
- Every other key stays empty, so the firmware's own default applies, as on a factory-fresh device. The exceptions
  are the stubs below.

**Smooth text uses a font shipped with the release.**
- `proxy/fonts/mono.woff2`: JetBrains Mono, SIL OFL 1.1, the Latin subset the leaderboard page already self-hosts.
  It is copied at build time from that one file, never duplicated in the tree.
- It goes out as `blipscope-web-mono-v<N>.woff2` with `proxy/fonts/LICENSE-FONTS.txt` beside it.
- `render.js` loads it with `FontFace` from a URL the page gives. **No Google Fonts and no third-party request.**
- The device font (glcd 6 × 8, drawn from LovyanGFX's own bitmap) stays the default.

**Stubbed: device-only, and what each does in the web build.**

| | the web build |
|---|---|
| photos | never fetched; the card says "No photo available" |
| the logbook | **off by default** (`logbook` false in the web config). If a page turns it on, its storage is memory for the page's life |
| the leaderboard, ntfy alerts, usage and boot reports | every `Post()` is answered locally; nothing is sent |
| OTA | the four `OtaUpdater.cpp` entry points answer "nothing to report" |
| Wi-Fi, its setup screens | not compiled (they live in `main.cpp`). The Connect screen draws the shim's address, `192.0.2.10` (RFC 5737), and its QR. **It cannot be hidden without a firmware change**, so it is reported here and not changed |
| MQTT | never connects |
| the touch chip | always healthy |
| backlight dimming | reported to the page; `render.js` applies it only if asked |
| the 2.1 in board's buzzer and IMU | silent; no reading |

**The release, per firmware release.** A separate workflow, `web-renderer.yml`.
- **It cannot block the firmware's.** It is a different workflow, with no job in the firmware workflow's graph, so a
  red web build never touches `version.txt` or any device-facing asset.
- **On push and PR:** both SKUs build against the tree, and the Node smoke test runs against a fixture `/blips`.
- **On `release: published`, and by `workflow_dispatch` with a tag:**
  - it builds from that tag's `src/` and `include/`, with this tree's `tools/web-renderer/`;
  - it uploads the s3-128 set to that release;
  - **s3-21 builds too, as a workflow artifact only,** until Daniel announces it.
- **The assets, versioned by `FW_VERSION`:**
  - `blipscope-web-s3-128-v<N>.wasm`
  - `blipscope-web-s3-128-v<N>.js` (the loader)
  - `blipscope-web-render-v<N>.js`
  - `blipscope-web-mono-v<N>.woff2`
  - `blipscope-web-fonts-LICENSE.txt`
  - `blipscope-web-v<N>.sha256`
- **The first set goes on `v18`** (the current release, built from `v18`'s own source). That source was checked
  before this was written: every firmware unit the build needs compiles against the shims.

**When it goes out.** After this PR merges, under the standing rule: the reviewer approves (Daniel pastes it), then
I merge, then dispatch for `v18`. The tag is reported then. Daniel can say otherwise.

## Predictions

| | check | predicted |
|---|---|---|
| **R1** | device builds | `git diff origin/main -- src include lib platformio.ini partitions* boards` is empty. A `blipscope-s3-128` build on this branch against one on `origin/main`: the same size, and **every** differing byte in the five rebuild regions (`app_elf_sha256` at 0xB0, the image checksum, the trailing SHA-256, the two WiFiManager `__TIME__` strings). **Zero payload bytes.** The firmware workflow's file is unchanged |
| **R2** | the web build | compiles every listed firmware unit unmodified, for s3-128 and s3-21, at this branch and at `v18` |
| **R3** | the data path, in the Node smoke test against a fixture `/blips` | **Requests:** `/blips` with the device's query (lat and lon at 2 dp, r, `limit=40`) and **no header**. **Draws:** the fixture's contacts, with labels. **Cadence:** about **12 `/blips` a minute** at the active rate (5 s). **Not fetched:** `/config` and `/airports` 0, photos 0, `Post` 0. Every request is under the configured base |
| **R4** | `/enrich` answered 404 by the fixture | read from the code: a non-2xx is transient (`retryCooldownMs = 5000`), and three failed answers settle a contact as unknown (`enrichAttempts`). So **each contact is asked at most 3 times**, at the firmware's pace (one lookup every 5 s, nearest first), then settled. Labels draw without a type line |
| **R5** | `/enrich` answered by the fixture | type lines appear on enriched contacts |
| **R6** | a request outside the base (a planted test URL) | answered locally 404, **0** network requests |
| **R7** | the browser, with smooth text on | **Network:** every request same-origin; the font from the shipped file; **0 third-party requests** (read off the network log). **Font:** `document.fonts` holds the face, loaded from that file |
| **R8** | the release workflow on this branch (push) | both SKUs build and smoke green. The release-asset step is skipped (not a release) |
| **R9** | the `v18` dispatch, after the merge | exactly the six s3-128 assets above on `v18`, their sha256 matching the manifest. s3-21 as a workflow artifact, **not** on the release. `v18`'s firmware assets unchanged (names, sizes and download counts read before and after) |
| **R10** | a device on `v18` | unaffected: `releases/latest/download/version.txt` reads the same before and after the upload |

**Outside the set:** stop and report. A web build that cannot compile `v18`'s source unmodified is outside the set,
not a reason to change the source.
