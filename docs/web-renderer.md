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
| the weather layer (#445) | **off**: `wx-layer` is never on in the web config (`check_wx_off.mjs`, run by `build.sh` with its selftest). Its fetch (`GetBytes`) answers as a dead socket, with no request. A page that turns it on stops at the shim's `setBuffer`, which aborts with a message: the shim has no pixels, and the layer's raw-buffer paths would corrupt memory. Weather on the website is a separate item (docs/wx-device.md, (B)) |
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

## Results (2026-11-04)

**S1, a defect found during the build, before any graded run, and fixed. Recorded because it was nearly invisible.**
- **How it showed:** the smoke test's R3 query check failed on the s3-21 build. It sent `lat=44.60` for 44.0582, and
  `12.50` for 12.05, while s3-128 sent `44.06`. Every fraction whose first digit is 0 had its two digits swapped.
- **The cause, by elimination:**
  - **Not the firmware:** the member read 44.058200 in memory.
  - **Not the formatter:** `wireloc::Center` and `%.2f` are correct on the frame loop, and in a fresh test fiber.
  - **Not the variant:** a rebuild **moved** the fault to the s3-128 build, with no source change.
  - **The cause:** `%.2f` was wrong only on the fetch task's own stack, where it gave "44.60" and "%.9f" gave
    "44.58200000". That stack came from `malloc`, which here returns **8-aligned** blocks; its base was 2402488,
    8 mod 16. The wasm C ABI requires 16.
  - Whether a task's stack landed 16-aligned depended on heap layout. That is why the fault moved with the binary.
- **The fix:** the fiber stacks come from `aligned_alloc(16, …)`, asserted at task creation. The same build then gives
  "44.06 | 44.058200000" inside the fetch task and sends `lat=44.06`.
- **Two instruments that did not see it:**
  - AddressSanitizer: under it the fiber scheduler never ran a task, so ASan is blind here.
  - `-O0`: the fault survived it, which ruled out the optimiser.
- **A second hazard found on the way:** `build.sh` rebuilt an object only when its `.cpp` was newer, so a shim header
  change could leave a stale object. It now rebuilds everything unless `INCREMENTAL=1`.
- **The spike (`dc81ceb`) carried the same latent fault.** Its scene ignored the query, so nothing showed.

| | read | verdict |
|---|---|---|
| **R1** | `git diff a27e00a HEAD -- src include lib platformio.ini` is empty. `blipscope-s3-128` at this branch's head against `a27e00a` (main): both 1,929,872 B. **70 differing bytes**, every one in the rebuild regions: `app_elf_sha256` @0xB0 (32), WiFiManager's two `__TIME__` strings (3 + 2), the checksum and trailing SHA-256 (33). **Zero payload bytes.** The firmware workflow's file is unchanged | **as predicted** |
| **R2** | every listed firmware unit compiles unmodified for s3-128 and s3-21, at this branch and at `v18` (`SRC_ROOT=`): four builds, 684 K / 680 K wasm | **as predicted** |
| **R3** | CI, both models, 70 s each: every request under `/api/v1/blipscope/`; the query `lat=44.06&lon=-121.32&r=40&limit=40`; **no header** (27 inits read, `credentials: "omit"`); `/blips` **14 in 70 s, 12.0 a minute**; 24 contacts with labels; `/config` and `/airports` answered locally (2), 0 from the loop | **as predicted** (s3-21 after S1's fix) |
| **R4** | `/enrich` 404: 9 hexes asked, **at most 3 times** each; no type line | **as predicted** |
| **R5** | type lines drawn: EC35 SR22 DH8D P28A E75L B739 B763 B38M C68A A320 C172 A20N | **as predicted** |
| **R6** | `https://elsewhere.invalid/...` and `/config` through the probe: 404 and 404, **0** requests | **as predicted** |
| **R7** | Chrome, the preview page served on 127.0.0.1: every resource from that one origin (`app.js`, `scene.js`, the manifest, the render module, the loader, the wasm, **`blipscope-web-mono-v18.woff2`**). `document.fonts`: `BlipscopeMono loaded`. With `?live=1`, the firmware's own `/blips` requests went to the same origin (2 in the first 8 s) | **as predicted**. Two "AdobeClean" faces in `document.fonts` come from a browser extension; no request for them is in the page's resource list |
| **R8** | CI on `a639199`: the `web-renderer` workflow green, both models built and smoked. One red elsewhere, the repo's workflow-concurrency convention (C1): the new workflow's PR block was not the standard one. **Fixed in the next commit**; the checker passes locally | **as predicted** for R8. C1's red recorded, fixed |
| **R9, R10** | after the merge (the `v18` dispatch) | **not yet** |
