# Weatherscope Stage 3: from the bench harness to a full edition (plan)

**Status: a plan for review. No code is written until it is approved.** Ruled by Daniel on 2026-10-20: Weatherscope moves from the Stage 1 bench harness to a full edition. Stage 2's 2.2 (the past-hour loop) and 2.3 (the screen logic) fold into this plan.

**Reviewers:**
- Daniel's reviewer chat, which rules;
- blipscope-5c, focusing on **OTA separation** and **the shared code Blipscope relies on**.

**The rules carry over from Stages 1 and 2:**
- predictions are frozen before each step's code, as a dated addendum here;
- stop on anything off-prediction, and don't re-grade;
- every check is proven able to fail by a sabotage;
- the only board flashed is `907069326E64` (standing OK);
- no deploys, and no customer-facing release.

Facts below cite `origin/main` at `788ad44` unless a commit is named.

## 1. The edition pattern

Weatherscope becomes an edition the way Claudescope (`FEATURE_CLAUDESCOPE`) and Speedscope (`FEATURE_SPEED`) are: the same shared infrastructure, with the radar/aircraft code and every sibling edition left out of the build.

**What it reuses, unchanged:**

| piece | where | compiled into every edition today? |
|---|---|---|
| Wi-Fi provisioning | WiFiManager (`platformio.ini:127`), `main.cpp:455-490`, `include/WiFiManagerHelpers.h` | yes |
| Config page and mDNS | `ConfigurationWebServer.cpp`: the shared shell CSS/JS (:100, :154), `MDNS.begin(DeviceIdentity::Name())` (:2583) | yes, with a per-edition form branch |
| Location | NVS `config` / `latitude`,`longitude`; `POST /location` (:3449-3474); "Use my location" in the shared JS (:266) | yes |
| Time | `configTime(0, 0, "pool.ntp.org")` (`main.cpp:593`) | yes |
| OTA | `MaybeUpdateFirmware` (`main.cpp:612`); `OtaUpdater.cpp` | yes (all but PHOTO_MODE) |
| Support / enroll | the popup to `…/blipscope/enroll` (:384); the `%ENROLLED%` substitution and the `/enroll-key` route (:3964-4036) | **radar only today** (see 1.3) |

### 1.1 Every shared file this touches, and how small each change is

| file | change |
|---|---|
| `platformio.ini` | (a) a new product env `weatherscope-s3-128`, shaped like `claudescope-s3-146` but on the s3-128 board. Its flags: `${common.build_flags}`, the s3-128 variant, `-DFEATURE_WEATHERSCOPE`, `-DFW_OTA_PREFIX=\"weatherscope-\"`, PSRAM, `ARDUINO_USB_MODE=1`. Its `build_src_filter` follows Claudescope's (`platformio.ini:923`): `+<*> ${filters.radar_off} ${filters.editions_off} ${filters.hwtest_off} ${filters.gametest_off} ${filters.anim_off} +<weatherscope/> -<weatherscope/bench/>`. A later `+<weatherscope/>` re-adds what `editions_off` drops, exactly as `+<claudescope/>` does. (b) The bench harness keeps working as its own env, **`weatherscope-bench-s3-128`**, building only `src/weatherscope/bench/`. (c) `editions_off` already drops `weatherscope/` from every other env, and that stays |
| `src/main.cpp` | one `#elif defined(FEATURE_WEATHERSCOPE)` in the include chain (:51-70) and the instance chain (:99-123); `&& !defined(FEATURE_WEATHERSCOPE)` appended to the **six** radar-only guards (:665, :680, :801, :831, :848, :865). There is no PPI sweep (static rings, like Seismic) |
| `src/ConfigurationWebServer.cpp` | four `#elif defined(FEATURE_WEATHERSCOPE)` branches (page :2242-, load :3023-, template :3368-, save :3843-, beside Claudescope's); `&& !defined(FEATURE_WEATHERSCOPE)` on the radar-default guards. Nine were found (:18, :415, :572, :2612, :3064, :3081, :3574, :3964, :4075), and they are **recounted at 3.2's base before editing**, with the count frozen in its addendum. It is **not** added to the "no-geography" list at :3920: Weatherscope needs a location, so the missing-location nag must stay |
| `include/BuildIdentity.h`, `include/DeviceIdentity.h`, `src/SoakHarness.h` | Weatherscope's entry in each edition list (feature name, wordmark, `DEVICE_PRODUCT_NAME` = "Weatherscope", so the mDNS name is `Weatherscope-XXXXXX`) |
| `skus.yml` | the product row `{ env: weatherscope-s3-128, status: build-only }` (no slug), and the bench row renamed `weatherscope-bench-s3-128` (build-only). The CI matrix follows from `skus.py --matrix-json` (`firmware.yml:255-317`), with no workflow edit |
| `scripts/check_release_envs.mjs` | `FEATURE_WEATHERSCOPE` added to `EDITION_FLAGS` (:34-37). Otherwise a future slug-ful Weatherscope env would be read as a radar SKU |
| `scripts/skus.py` | `docs/weatherscope-stage3.md` added to `ALLOWED_MENTIONS`, since this plan names `firmware.yml`. Checked: `skus.py --check-readers` failed naming the doc before, and passes after |
| `scripts/check-adsbdb-out.sh` | the `weatherscope-*` case in `plant_target()` (:359-365) narrowed to **`weatherscope-bench-*`**. The product env links `ConfigMigration.cpp`, so it falls through to the default plant site. Without this, the product leg would plant into a file it doesn't compile and read `UNTRUSTWORTHY (control blind)` |
| `src/weatherscope/` | the edition code, and `bench/stage1_main.cpp` (moved, unchanged in content) |
| docs | the edition section in `CLAUDE.md` (**this needs Daniel's authorization**), plus `README.md` and `RELEASING.md` |

### 1.2 The shared-code invariant, the check that protects Blipscope

Every shared edit in 1.1 is either a new `#elif` branch or `&& !defined(FEATURE_WEATHERSCOPE)` appended to a guard. **No other env's preprocessed source changes.**

**S1, predicted:** every other env in `skus.yml` builds an image **byte-identical** to its build at the base commit, **except the five known stamp regions** (CLAUDE.md, "two builds of the same source are NOT byte-identical"):
- `app_elf_sha256` (32 B);
- the image checksum (1 B) and the trailing SHA-256 (32 B);
- the two WiFiManager `__DATE__`/`__TIME__` strings.

It is measured by building each env at the base and at the head and diffing each pair with the stamp accountant used in Stage 1. **Any byte outside those regions: stop.** This rests on builds alone; nothing but Weatherscope's board is ever flashed.

**S1's sabotage:** a shared edit made **outside any guard**, one ungated constant string added to `ConfigurationWebServer.cpp`. Every env that compiles the file then changes. **S1 must report a non-stamp difference in every one of them.**

A guard term left **off** (a missing `&& !defined(FEATURE_WEATHERSCOPE)`) would not show up in S1 at all. It changes only Weatherscope's own build, by pulling radar code into it. That failure is caught by 3.2's own budgets and the D2 gate, not by S1, and the plan doesn't claim otherwise.
- **Added 2026-10-21 (Addendum 2, A2.3):** S2 now checks this case directly, with a grep of the Weatherscope image for radar-only strings.

The OTA identity change (section 3) deliberately changes **every** edition's image. So it is a separate step, with its own predictions, outside S1.

### 1.3 Support and enroll

- **Today:** the enroll popup renders only when `%ENROLLED%` is substituted, which only the radar processor does (:310-313), and `/enroll-key` is radar-only (:3964). Making it Weatherscope's means widening those two guards. That is still a gated, preprocessor-only change, so S1 covers it.
- **Device side only.** Stage 3 makes **no Worker change and no deploy**. The end-to-end enrolment is verified when the proxy data path lands.
- **A finding from blipscope-5c's review (checked): the Worker accepts the device, but can't tell the editions apart.**
  - The model header is `X-Blip-Model: variant::SLUG` (`CloudFeed.cpp:51`).
  - So a Weatherscope s3-128 would read as a **Blipscope s3-128** in the Worker's per-model config (`proxy/src/config.ts:65`) and its fleet telemetry.
  - **Stage 3 makes no proxy calls** (every feed is stored, section 2), so the confusion can't arise yet.
  - **Before any Weatherscope request reaches the Worker,** its model must carry the edition. The OTA identity string (`weatherscope-s3-128`) is the natural value. How the Worker reads it is blipscope-5c's side, so that contract is listed in section 7.

## 2. Data: stored now, live later, one module apart

**There are no live network calls to NOAA or NWS from the device, ever.** Live data will come through the proxy (blipscope-5c's shared pipeline). Until then the edition draws from stored data behind three small interfaces:

| interface | Stage 3 implementation | later |
|---|---|---|
| `WxFeed`: the latest WXC1 crop and the past hour's history | `StoredWxFeed`: SeamlessHSR crops embedded with `board_build.embed_files`, made by the shared encoder (`crop.ts`) from the KDFW day (2024-05-21) | `ProxyWxFeed`, through blipscope-5c's endpoint |
| `ObsFeed`: the nearest station's latest present weather ("here now", 1c) | `StoredObsFeed`: stored NWS observation JSON samples | through the proxy |
| `AlertFeed`: active warnings (polygon, event, expiry, fields) | `StoredAlertFeed`: stored NWS alert JSON (the 2026-10-20 sample in `docs/weatherscope-screens.md` §3) | through the proxy |

**Predicted, and checked:**
- **D1, swap in one module:** each interface's implementation is chosen in exactly one factory file. A host test builds the screen logic (section 5) against **fake** feeds, so nothing else knows which implementation is in use.
- **D2, no NOAA/NWS hosts in the image:** an artifact gate greps the built image for `weather.gov`, `noaa`, `ncep`, `mrms` and `amazonaws`, and must find 0 hits. **Its control** plants one hostname string into a compiled file and must find it, so a clean reading means the scanner could see. This is the adsbdb gate's discipline.

## 3. OTA separation (the critical part)

**What protects editions from each other today** (facts, `origin/main`):
- **The device fetches by name only.** It pulls `…/releases/latest/download/firmware-<FW_OTA_PREFIX><variant::SLUG>.bin` (`OtaUpdater.cpp:104-111`) when the single, shared `version.txt` integer is above `FW_VERSION` (:357).
- **Nothing checks the image itself.** There's no edition, board, slug, signature or `esp_app_desc` check before flashing. `httpUpdate.update()` (:450) writes whatever that URL returns.
- **CI publishes by name only.** It uploads `firmware-<slug>.bin` for slug-ful legs only (`firmware.yml:446-468`), and `version.txt` advances only after `check-publish-receipts`. The flash manifest (`scripts/flash_manifest.py:160-174`) records the slug, but no device reads it.
- **No script checks** that a slug equals `FW_OTA_PREFIX + variant::SLUG` (RELEASING.md:340 states it as a rule).
- **The Worker is never on the update path** (`proxy/src/fleet.ts:6-10`).

**So a single mislabel**, such as Weatherscope's image uploaded as `firmware-s3-128.bin` or a manifest slug typed wrong, would be fetched and **accepted** by every Blipscope s3-128 device. The requirement is that **a Weatherscope image can never be offered to or accepted by a Blipscope or any other edition, and vice versa.** That needs a check on both sides.

### 3.1 The design (its details are for review)

- **An identity in every image:** `<FW_OTA_PREFIX><variant::SLUG>`, for example `weatherscope-s3-128` or `s3-128`, in **ESP-IDF's custom app descriptor**: a small constant record placed in `.rodata_custom_desc` (blipscope-5c's review; checked):
  - **`esp_app_desc_t` can't carry it.** Its `project_name` is fixed by the prebuilt framework: a built image reads `arduino-lib-builder` at file offset 0x50, after the descriptor's magic `0xabcd5432` at 0x20.
  - **The slot is everywhere we build.** All 5 esp32s3 `sections.ld` variants in `framework-arduinoespressif32-libs` place `.rodata_custom_desc` second, right after `.rodata_desc`. `esp_app_desc_t` is 256 bytes, so the record sits at **file offset 0x120** in every image.
  - **No scanning is needed.** CI and the device read a fixed offset. The record carries its own magic and length, so a missing or foreign record is refused rather than misread.
- **The accepting side:** `OtaIdentity::Accept(running, candidate)`, a pure function, accepts only an exact match.
  - **Today there's no moment to check.** `OtaUpdater` sets `httpUpdate.rebootOnUpdate(true)` (`OtaUpdater.cpp:400`), and `HTTPUpdate` restarts right after `Update.end()` (`HTTPUpdate.cpp:396-397`), which has already switched the boot partition. **So the device side sets `rebootOnUpdate(false)`.**
  - **On `HTTP_UPDATE_OK`,** it reads the identity at 0x120 of the partition just written.
    - On a match, it restarts as today.
    - On a mismatch, it sets the boot partition back to the running one (`esp_ota_set_boot_partition`), logs the refusal with both identities, and keeps running the current image.
  - **Withdrawn 2026-10-21 (Addendum 1):** the three lines above leave a window. A power loss between `Update.end()` and the flip back boots the foreign image. The check now reads the identity from the stream **before any write**; see A1.2.
- **The offering side:** a CI step, `check-ota-identity`, runs **before upload** on every slug-ful leg. The identity inside `firmware.bin` must equal the name it is about to be published under (`firmware-<slug>.bin`) and the flash manifest's slug. If not, there's no upload and no receipt, so `version.txt` cannot advance.

### 3.2 Predicted

| check | predicted |
|---|---|
| **O1** (host): the accept matrix | over every identity derived from `skus.yml` and the env flags, plus Weatherscope's, `Accept` is true exactly on the diagonal and false everywhere else. Every prefix + slug is unique |
| **O2** (host): identity from real images | the identity read from each built image's bytes equals its env's `<FW_OTA_PREFIX><variant::SLUG>`, for every env in `skus.yml` |
| **O3** (CI): the offering side | on a normal run, `check-ota-identity` passes on every slug-ful leg, and the publish behaves exactly as today |
| **O4: the sabotage, a mislabelled manifest, refused on BOTH sides** | Weatherscope's image presented as `firmware-s3-128.bin`, with its manifest slug edited to `s3-128`: **(i)** `check-ota-identity` refuses it, naming both identities, and nothing is uploaded; **(ii)** the device side refuses it (`Accept("s3-128", identity read from the Weatherscope image)` is false, from the real image bytes, on the host); **(iii) and vice versa:** a Blipscope `s3-128` image offered to a Weatherscope device (`Accept("weatherscope-s3-128", …)`) is false |
| **O5: the slug rule enforced** | the same CI step checks that each slug-ful row's slug equals its env's `FW_OTA_PREFIX + variant::SLUG`. Its sabotage, a slug row edited to another edition's prefix, is refused |

**The order, which is a release decision, not mine:**
- The accepting side changes **every** edition's firmware, the Blipscope fleet's OTA path included. So it reaches devices only through a normal release, which is Daniel's call, with blipscope-5c's release discipline. **It must be on the fleet before Weatherscope ever gets a slug.**
- **Until then Weatherscope is build-only with no slug, so CI never publishes a Weatherscope image** (`skus.py` rules, :75-90; the receipts). Nothing can be offered, and a Weatherscope device asking for `firmware-weatherscope-s3-128.bin` gets a 404 and flashes nothing.
- **O6, observed, not forced:** while `version.txt` equals the edition's `FW_VERSION`, the device fetches nothing. The 404 path is **predicted for the next real version bump and observed then**, on the board's serial. No version is bumped to test it.
- **Added 2026-10-21 (Addendum 2, A2.1):** what the screen shows, and how often, for a failure, a refusal and an accepted image.

## 4. `skus.yml` and releases

- **Rows:**
  - `weatherscope-s3-128`: **build-only, no slug, publishes nothing**;
  - `weatherscope-bench-s3-128`: build-only.
- **No customer-facing release, no store listing,** and no change to `version.txt`'s meaning.
- **No board is flashed except `907069326E64`.**
- **No deploys.** No Worker change happens in Stage 3.
- **Added 2026-10-21 (Addendum 2, A2.2):** once Weatherscope has a slug, a failing Weatherscope leg withholds `version.txt` for **every** SKU. The slug decision is made knowing that.

## 5. Screens and screen logic (2.2 and 2.3 fold in here)

Every behaviour follows `docs/weatherscope-screens.md`'s rulings.

**5.1 The past-hour loop (was 2.2).**
- **The frames:** 15 SeamlessHSR frames, every other one (about 4 min apart), covering the hour to 2024-05-21 22:00Z at KDFW. They're stored crops from the shared encoder, held in flash.
- **How it plays:** a tap opens it (2a), there's no +30 (2b), it plays once and ends (2d).
- **Predicted:**
  - **PSRAM:** the frame path is unchanged from the bench (sprite 115,200 B + grid 36,192 B; 153,608 B measured). The crops stay in flash, so the loop adds **≤ 8 KB** of PSRAM.
  - **Time per loop frame:** decode + render + push. The bench measured 51.3 ms; the edition is predicted at **≤ 70 ms**, against the **≤ 100 ms bar**, giving a loop rate of **≥ 14 fps** against the **≥ 8 fps bar**.
  - **Pacing:** the interval between frames, at the chosen playback rate, has max − min ≤ 10 ms.
  - **Per-frame code hashes:** each of the 15 frames' per-pixel code dump equals a host reference (`render_ref.py` on the same crop).

**5.2 The screen logic, as pure functions with host tests (was 2.3).**
- the priority order (1a: warning, then approaching or now, then dry);
- **the 15 mi ring at 15 dBZ** (code ≥ 1, rain and snow alike, ruled);
- **"here now" from the NWS observation's present weather** (1c, changed), not the radar's centre pixel;
- whether a warning covers the location (1d: polygon containment);
- which warnings count, and their order (3a, 3d: Tornado, Snow Squall, Severe Thunderstorm, Flash Flood);
- when a warning ends (3c: `expires` or a Cancel);
- the bottom lines (3b, 3e), with storm motion converted to distance, bearing, direction of travel and mph;
- **imperial units** (6a);
- town labels (5: Census Gazetteer incorporated places, a stored subset for the KDFW fixture area);
- the legend (screen 7, final; #448).

**Each function's addendum freezes** a truth table of named cases, with a count, and at least one sabotage that must fail exactly its own case.

**5.3 On the board.** Four stored scenarios drive the priority order end to end: dry, approaching (echo inside 15 mi), here now (the observation reports precipitation) and a warning covering the location. **Predicted:** each scenario shows its ruled screen, and the per-pixel dump of the frame layer equals the host reference.

## 6. Budgets, from what Stages 1 and 2 measured

| | basis (measured) | predicted for the edition |
|---|---|---|
| Flash (app partition 6,553,600 B) | Claudescope 1,575,195 B and Speedscope 1,587,771 B (s3-146, `788ad44`); radar s3-128 1,927,783 B; bench harness 466,119 B | **1.60-1.85 MB**: the edition shell, plus about 20 KB of Weatherscope code, plus ≤ 150 KB of stored crops and JSON |
| Static RAM | Claudescope 54,992 B; Speedscope 55,200 B; radar s3-128 73,952 B | **55-70 KB** |
| PSRAM | frame path 153,608 B (Stages 1 and 2) | **150-170 KB** for the frame path and the loop |
| Frame time | 54.8 ms per frame (decode 2.87 + indices 3.58 + render 22.6 + push 25.8) | **≤ 70 ms** against the ≤ 100 ms bar |
| Loop rate | 19.5 fps (bench) | **≥ 14 fps** against the ≥ 8 fps bar |

**B5-B7 stay misses on their Stage 1 bands.** The measured costs are what these predictions come from (ruled in Stage 2).

## 7. Out of scope until later

- **The live pipeline:** blipscope-5c's shared server pipeline. The edition uses stored crops behind `WxFeed`.
- **Alerts and observations through the proxy:** stored JSON behind `AlertFeed` and `ObsFeed`.
- **WXC1 v2's rain/snow type field:** shared work with blipscope-5c (`docs/wxc1-v2.md`, to come). Stage 3 draws rain only, since the wire carries no type yet.
- **The first-boot legend:** parked until the screens are built (ruled).
- **Worker changes, deploys, and end-to-end enrolment.**
- **The edition-qualified model header:** what Weatherscope sends as `X-Blip-Model`, and how the Worker reads it. This is a contract with blipscope-5c, and it's needed before the proxy data path, not in Stage 3.
- **Any release, slug, store listing or customer device.**

## 8. Order of work; each step frozen before its code

1. **3.0** This plan, then review.
2. **3.1** Move the bench harness (`weatherscope-bench-s3-128`, `src/weatherscope/bench/`; narrow the adsbdb gate case). Predicted: S1 holds for every other env, and the bench checks re-run unchanged (C1, C2, the legend's L1-L6).
3. **3.2** The edition scaffolding (the product env, `main.cpp`, the config page, the identity headers, the stored feeds, the D2 hostname gate). Predicted: S1, D1, D2, and the budgets in section 6; it boots on `907069326E64`, joins Wi-Fi and serves its config page.
4. **3.3** The screens and the logic (section 5).
5. **3.4** OTA identity (section 3): host O1, O2 and O4; CI O3 and O5. Its release is Daniel's call, and it must come before any Weatherscope slug.
   - Added 2026-10-21 (Addendum 1): host O7, O8 and O10, and bench O9 on `907069326E64`.
   - Added 2026-10-21 (Addendum 2): O6's screen predictions (A2.1), and O11, the real download path end to end, on the release candidate **before the release that adds the check is published** (A2.4). S2 belongs to 3.2 (A2.3).

**Outside every step's predictions: stop and decide.**

## Changes after review (blipscope-5c, 2026-10-20)

The review is on #449. Each finding below was checked against the artifact before the plan changed.
1. **Where the identity lives:** ESP-IDF's `.rodata_custom_desc`, at file offset 0x120.
   - It replaces the two candidates.
   - Checked: `project_name` reads `arduino-lib-builder` in a built image, and all 5 esp32s3 linker variants place the slot after `.rodata_desc`.
2. **There was no check point:** `rebootOnUpdate(true)` reboots straight after the write. The device side now sets it to false, then checks, then restarts or reverts.
   - Checked: `OtaUpdater.cpp:400`, `HTTPUpdate.cpp:396-397`.
   - **Superseded 2026-10-21 by Addendum 1:** check before any write, never after `Update.end()`.
3. **The Worker can't tell editions apart** (`X-Blip-Model` is `variant::SLUG`, `CloudFeed.cpp:51`).
   - Recorded in 1.3.
   - Out of scope for Stage 3, which makes no proxy calls.
   - Listed in section 7 as a contract before the proxy path.
4. **`ALLOWED_MENTIONS`:** this plan names `firmware.yml`. `scripts/skus.py` lists it in the same PR.
   - Checked: the reader sweep failed naming the doc, then passed.

**The predictions in 3.2 are unchanged.** They never depended on where the identity lives.

## Addendum 1, 2026-10-21: the #449 review (frozen before any Stage 3 code)

blipscope-5c reviewed #449 at `012f984`: approve after two changes, both frozen here. Daniel's answers from the same message are at the end.

**The device-side design in 3.1 is withdrawn.** It set `rebootOnUpdate(false)`, checked on `HTTP_UPDATE_OK`, then set the boot partition back. That leaves a window: `Update.end()` has already called `esp_ota_set_boot_partition`, so a power loss between it and the flip back boots the foreign image. **The check now happens before anything is written.** The offering side (`check-ota-identity`) and O1-O6 are unchanged.

### A1.1 Where flash is first touched

Read in the installed framework (arduino-esp32 3.3.9, `libraries/Update/src/Updater.cpp`):
- **`Update.begin()` touches no flash.** It picks the next OTA partition and allocates a 4096-byte buffer (:184-300).
- **The first erase and write are in `_writeBuffer()`,** once 4096 bytes are buffered (erase :666, write :674).
- **The boot partition changes only in `end()`.** `_verifyEnd()` writes the 16 held-back bytes, then calls `esp_ota_set_boot_partition` (:730-735).
- **"Abort in the first write callback" isn't safe here.** The first progress callback, `(0, size)` at :655, fires just before the first erase. It isn't handed the bytes, and nothing after it checks for an abort:
  - `abort()` runs `_reset()` (:104-131), which frees the buffer and zeroes the size.
  - `_writeBuffer()` then carries on to the erase at :666. By a read of the code (not a run), the tail-sector test is still true with the size zeroed, so the slot's first sector is erased anyway.
  - **So the check can't live in the Updater's callbacks.**

### A1.2 The design: read the identity from the stream, decide, then write

- **`OtaUpdater` opens the image download itself** and stops using `httpUpdate.update()` for it. It keeps:
  - the same `WiFiClientSecure` with the pinned roots (`OTA_ROOT_CAS`);
  - `HTTPClient` with `HTTPC_FORCE_FOLLOW_REDIRECTS` (`latest/download` redirects to GitHub's CDN);
  - the request bus, held for the whole download.
- **It requires HTTP 200 and a `Content-Length` larger than the head.**
  - It then reads exactly `kHeadBytes` into RAM: 0x120 plus the record's fixed size.
  - A `static_assert` holds the record to at most 64 bytes, so the head is at most 352 bytes.
- **The decision is a pure function, `ota::Install(source, flash, running)`,** header-only and compiled on the host. In order:
  1. Read the head. A short read is `truncated`.
  2. Parse the record. A wrong magic is `no-record`; an unknown record version or length is `bad-record`.
  3. Call `OtaIdentity::Accept(running, candidate)`. False is `foreign`.
  4. **Only then** call `flash.begin(size)`, `flash.write(head)`, write the rest of the stream, and call `flash.end()`.
- **A refusal makes no flash call at all.**
  - The inactive slot is never erased or written, and the boot partition is never touched. The connection is closed after the head.
  - It logs `[ota] refused <reason>: running=<id> image=<id or ->`.
  - It records `res=refused-<reason>` in the OTA-mem record, overwriting the pre-armed `incomplete`. Without that, the next check-in would report a refusal as a power loss mid-flash (the `incomplete` finalisation in `OtaUpdater.cpp`).
  - It releases the bus and keeps running.
- **A non-200 or a broken stream is still a failure (`fail-<n>`), not a refusal.**
  - A refusal is never written as a failure, and a failure never as a refusal.
  - The mapping from today's `HTTP_UE_*` codes to the new open's conditions is frozen at 3.4, before its code: a condition that existed before keeps its `fail-<n>` token.
- **The real `flash` is a thin adapter over `Update`** (`begin`, `write`, `writeStream`, `end`). `ESP.restart()` runs only after `end()` succeeds.
- **The Worker needs no change for the new token.** `proxy/src/metrics.ts:292` keeps up to 24 `[\w.-]` characters and checks no fixed set, so `refused-foreign` arrives as its own category, apart from `fail-<n>` and `incomplete`. Stage 3 deploys nothing.

### A1.3 Predicted (added to 3.2; O1-O6 unchanged)

| check | predicted |
|---|---|
| **O7** (host): a refused image never touches flash | **The fake:** `ota::Install` runs against a fake flash that records every call in order and keeps a **history** of the boot partition. Its `end()` switches the boot to the target slot, as `Update.end()` does.<br>**Five refused inputs, each with its predicted reason:**<br>• the real Weatherscope image head offered to `s3-128` (`foreign`; this is O4 (ii) through the new path)<br>• a pre-check image head, from the `s3-128` build of the base commit (`no-record`)<br>• a record with a wrong magic (`no-record`)<br>• a record with an unknown version (`bad-record`)<br>• a stream that ends before `kHeadBytes` (`truncated`)<br>**For each:** the call log is **empty**, the boot history holds **only its initial value**, and the target slot's bytes are unchanged.<br>**CONTROL:** the board's own identity is accepted. The log is `begin, write…, end`, the boot history switches exactly once, and the target equals the image. Without this control, a fake that ignores every call would pass. |
| **O8** (host): the sabotage, the check moved after `Update.end()` | **The sabotage:** `sabotage_ota_install.py` moves step 3 after step 4. That is the withdrawn design, flip-back on a mismatch included.<br>**Predicted:** O7 turns red on all five refused inputs, naming `begin` as the first forbidden call and showing the switch in the boot history.<br>**The assertion is on the history, not the final state.** The withdrawn design reverts the boot partition, so a final-state assertion passes it. The sabotage run prints both, final state PASS and history FAIL, which is why the history is the assertion.<br>**A second sabotage** moves the check after the first `write`. O7 turns red on the target bytes while the boot history stays clean, which proves the target assertion works on its own. |
| **O9** (bench, `907069326E64`): the real adapter | **Why it is needed:** the host fake models our interface, not the Updater. A real adapter that wrote before the decision would pass O7 (the named alternative), and only the board can exclude it.<br>**The source:** a bench-only source (`WX_OTA_BENCH`, in the bench env only) feeds `ota::Install` an image head over USB serial.<br>**Refused** (a Blipscope `s3-128` head): the `esp_ota_get_boot_partition()` label and the sha256 of the inactive slot's first 64 KB are identical before and after.<br>**CONTROL, so the instrument is shown to see a write:** the board's own head, with the stream cut at 8 KB, is accepted, writes, then fails. The inactive slot's sha256 changes and the boot label does not.<br>**Where the bench code is:** `WX_OTA_BENCH`'s marker string is in the bench ELF and in no shipping env's ELF (grep). |
| **O10** (host): the downgrade consequence, asserted, not just stated | **Every** identity in the O1 matrix refuses the pre-check `s3-128` head from O7 as `no-record`. No pre-check image in that matrix has the record's magic at 0x120. |

- **3.4 runs O7-O10 alongside O1-O5.** O8's two sabotages must turn O7 red before O7's green counts.
- **O6 stays as it is.** At the next real version bump, the 404 is a failure (`fail-<n>`), observed on serial, not forced.

### A1.4 The consequence (review item 2), recorded here and in `RELEASING.md`

- **No downgrade past the release that adds the check.**
  - Every image built before that release has no record, so a device running the check refuses all of them over OTA (`no-record`; O10 asserts it).
  - OTA never offers a lower `version.txt` anyway. What this really closes is a rollback by re-publishing an old binary under a higher `version.txt`: from that release on, every device refuses it.
- **A rollback is a new build.** Old behaviour comes back as a new build from source that carries the record. The offering side's `check-ota-identity` refuses to publish anything else.
- **USB flashing is unaffected.** The check is on the OTA path only, so a single device can always be recovered by cable (esptool or the web flasher).
- **Identities become permanent.** From that release, a device accepts only its own `<FW_OTA_PREFIX><variant::SLUG>`, in a record version it knows.
  - A device is stranded on OTA if any of these change, because it then refuses every later image:
    - an edition prefix or a slug is renamed;
    - the record's magic or layout changes incompatibly.
  - Any such change needs its own ruling, plus a transitional release that accepts both and is already on the fleet. Until then, `Accept` is an exact match.
- **A mislabelled publish costs a head read, not a brick.** On each check, each device reads at most 352 bytes, closes the connection, records `refused-foreign` and keeps running. No device updates until a correctly labelled image is published.
- **Added 2026-10-21 (Addendum 3, A3.2):** some devices never report a refusal anywhere: radar devices off the cloud feed, and every non-radar edition.
- **The order is unchanged.** The accepting side reaches the fleet only through a release Daniel rules on, with blipscope-5c's release discipline, and before any Weatherscope slug exists.

### A1.5 Daniel's answers (2026-10-21)

- **The edition section in `CLAUDE.md`: authorized.** The shared-file table in 1.1 named it as needing authorization. It lands with the Stage 3 code, not before.
- **`docs/wxc1-v2.md`: go.** It opens as its own docs-only draft PR and is not part of this plan.
- **No Stage 3 code until #449 is approved at its new head.**

## Addendum 2, 2026-10-21: blipscope-5c's re-read at `94d863b` (frozen before any Stage 3 code)

blipscope-5c re-read the plan at `94d863b` (#449):
- **A1.1** was checked against the installed Updater (3.3.9) and holds as written.
- **Point 1, the revert's bench proof,** is settled by the redesign.
- **Points 2-4 were still open.** Each is taken below, so that "settled" refers to something in the plan.
- **One new point** is taken too. Two of its parts are Daniel's call.

| point | decision |
|---|---|
| 1. The revert needs a bench proof | **Settled by the redesign.** No revert is left; O9 proves the stronger property. |
| 2. What the screen shows | **Taken: A2.1.** The refusal case is a proposal, because it is what customers see. |
| 3. The release-train coupling | **Taken: A2.2,** plus one line in section 4. Nothing changes for Stage 3. |
| 4. A guard term left off | **Taken: A2.3,** as check S2 in 3.2. |
| New: the real path never runs end to end | **Taken: A2.4,** as check O11. **The board and where image B is hosted are Daniel's call.** |

### A2.1 What the screen shows, and how often (point 2)

**When the check runs.** At every boot, before the screens start (`main.cpp:612`), and on the daily reboot-then-check, at most once per 24 h (`main.cpp:790`, `REBOOT_MIN_INTERVAL_S`). It gets past the version check only while `version.txt` > `FW_VERSION`.

**What the screen shows today:**
- `drawProgress(0)` runs right after `NoteOtaAttempt`, before the download (`OtaUpdater.cpp:393-394`);
- `onProgress` draws the bar (:405);
- a failure draws "Update failed" for 2 s (:455-456).

A1.2 replaces `httpUpdate.update()`, so all three are rewritten. **What the new path shows:**

| case | record | screen | how often |
|---|---|---|---|
| a 404, a non-200 or a broken stream | `fail-<n>`, as today | "Update failed" for 2 s, as today | every boot and every daily check while `version.txt` > `FW_VERSION`. **Unchanged from today** |
| **a refusal** | `refused-<reason>` | **nothing.** No bar, no status: the screen stays on whatever it showed before the check, and the boot carries on | the same cadence. After a mislabelled publish, that is every device, every boot and every daily check |
| an accepted image | `incomplete`, finalised as `ok` on the next boot, as today | the bar from 0 to 100 %, then the restart, as today | once |

**Why a refusal draws nothing (proposed; Daniel can overrule).**
- The customer can't act on it.
- It repeats until a correctly labelled image is published.
- A1.4 says a mislabelled publish "costs a head read, not a brick". An "Update failed" on every boot across the fleet would also make it cost a visible fault.
- The record and the serial line still carry the refusal.

**One consequence:** the first draw of the bar moves to after acceptance, so a refused head never flashes a 0 % bar.

**How to observe it.** Absence can't be seen on a panel (CLAUDE.md, "know which instrument can see the failure you have"). So every OTA screen draw logs one serial line: `[ota] screen: progress <pct>` on the first and last draw, and `[ota] screen: status <text>`.

**Predicted on serial:**
- a refusal: **0** screen lines;
- a failure: exactly **one** `status Update failed`;
- an accepted image: a `progress 0` and a `progress 100`.

The refusal and accepted cases are observed in O11; the failure case in O6, at the next real version bump.
- **Added 2026-10-21 (Addendum 3, A3.1):** with nothing drawn, the refusal's record needs a reader. That reader is `reconcile-fleet.py`.

### A2.2 The release-train coupling (point 3)

**Weatherscope shares `FW_VERSION` and the single `version.txt`.**
- **What a slug turns on:** once Weatherscope has one, its leg is slug-ful, and `check-publish-receipts` withholds `version.txt` for **every** SKU when any slug-ful leg fails (CLAUDE.md, "a gate that refuses to certify is working; check the coupling").
- **The consequence:** a broken Weatherscope build would stop OTA for the whole Blipscope fleet.
- **Stage 3 changes nothing here:** Weatherscope stays build-only and has no slug.
- **The slug ruling has to be made knowing this.** An alternative to weigh then is per-prefix version files, which would be another `OtaUpdater` change.

### A2.3 S2: a guard term left off (point 4), a 3.2 check

**What it catches:** a missing `&& !defined(FEATURE_WEATHERSCOPE)` pulls radar code into Weatherscope's image only. S1 can't see that, and the budgets and D2 catch it only indirectly. S2 checks it directly, on the same pattern as D2 and the adsbdb gate:
- **The check:** a grep of the Weatherscope product image (`firmware.bin` and the ELF) for radar-only strings. **Predicted: 0 hits.**
- **The list:** frozen in 3.2's addendum, before its code. It is drawn from the code behind the radar-default guards in `ConfigurationWebServer.cpp` (form field names) and the cloud blips path (`/api/v1/blipscope/blips`).
- **Positive control, from the other side:** the same grep finds **every** listed string in the `blipscope-s3-128` image. A string the radar image doesn't contain proves nothing by being absent.
- **Anchor control:** an anchor build of the Weatherscope env, with one reachable string planted, must be found. So 0 hits means the scanner could see.
- **Sabotage:** remove one `&& !defined(FEATURE_WEATHERSCOPE)` from one of the guards. S2 must find that guard's strings and name them.
- **Whether S2 becomes a CI step** is decided at 3.2's freeze.

### A2.4 O11: the real download path end to end, before it can strand anyone (new point)

**Why.** blipscope-5c's point, and it is correct:
- **What is covered:** O7 runs a fake source into a fake flash, and O9 runs the real adapter fed over serial.
- **What isn't:** nothing runs the self-opened download on a board against a correctly labelled image, through `HTTPClient`, the redirect to the CDN, the pinned roots, `Content-Length`, the head read and `writeStream`.
- **Why a defect there would go unseen:** an off-by-N head read refuses everything as `no-record`, which looks exactly like "nothing to update".

**Taken, with one change to when it runs.** It runs **before the release that adds the check is published**, not before the release after it.
- Once a device runs that release, call it R, a broken path refuses every later image, and only a cable fixes it.
- **So O11 runs on R's own release candidate.**

**The builds:**
- **A:** R's candidate, with the check and the record.
- **B:** the same source and identity, with `FW_VERSION` + 1. B is never published to `releases/latest`.

| case | predicted |
|---|---|
| **accepted** | A finds B over the real HTTPS path: `version.txt`, then the image, through the redirect and the pinned roots. It accepts B, writes it, restarts and boots B. B's `[build]` banner and version show on serial, and serial has a `progress 0` and a `progress 100` (A2.1). |
| **refused** | The same path, serving another edition's image for the same board under B's name. Serial shows `refused-foreign` and **0** screen lines. After a power cycle, A is still running and the boot label is unchanged. |
| **sabotage** | A bench build of A whose head read is off by one byte refuses the good B as `no-record` or `bad-record`. The accepted case rules out exactly this defect, so it must be visible as one. |

- **Amended 2026-10-21 (Addendum 3):**
  - The refused case's image is built from R's candidate with another edition's env (A3.4).
  - The sabotage predicts exactly `no-record` (A3.3).

**Open, for Daniel's ruling:**
- **The board.**
  - Option 1: `907069326E64`, the board I have a standing OK to flash, running A with a Blipscope `s3-128` identity.
  - Option 2: E2:08, run by blipscope-5c. It proposed this because it is the Blipscope fleet's path at stake. Section 4 says no board but `907069326E64` is flashed in Stage 3, so option 2 is also a ruling on that line.
- **Where B is hosted.**
  - A GitHub prerelease asset is invisible to `releases/latest`, but it is release machinery.
  - Or another host Daniel names.
  - Either way, A needs a bench-only override of the `version.txt` and image URLs. Hosting B on GitHub keeps the production host and redirect path.
  - The override's marker string must be absent from every shipping ELF (the O9 grep).
  - How A's difference from R's candidate, the override alone, is bounded is frozen with O11's addendum before it runs.

## Addendum 3, 2026-10-21: blipscope-5c's re-read at `9e9c6a9` (frozen before any Stage 3 code)

blipscope-5c re-read Addendum 2 and found points 2-4 and O11 taken correctly. It confirmed the line numbers (`drawProgress(0)` at :394, `drawStatus` at :455) and O11's timing. **Three small items remain, and all three are taken.**

### A3.1 The refusal's record gets a reader (item 1)

**Why.** With nothing on screen (A2.1), `refused-<reason>` is the **only** fleet-level sign of a mislabelled publish, or of a broken accept path at R+1. Today nothing routinely reads it, which is the shape CLAUDE.md calls "an instrument that fires correctly into a void". Checked:
- **Where the result is written:** only as an Analytics Engine point (`proxy/src/metrics.ts:304`: index `ota`, with the result and the device as blobs).
- **What reads it:** nothing routinely. `scripts/reconcile-fleet.py` reads only the KV `fw:` rows, so its Instrument A prints the firmware spread and the change count. It shows neither the result nor "devices behind `version.txt`".

**The reader.** On every run, `reconcile-fleet.py` prints the count of devices behind the published `version.txt`, broken down by each device's last OTA result:
- `refused-*`, `fail-*`, `incomplete` and `ok`;
- a bucket for devices with no `ota` point at all.

**How it works:**
- **The data:** the "behind" count comes from the `fw:` rows the script already reads; each device's last result comes from the `ota` points.
- **It's a script change only:** no Worker change and no deploy.
- **The analytics read** uses the token file's `analytics-read=` line, as `usage-stats.ts` does, never a user-level token.
- **Why "behind":** it catches every cause, a refusal included. A defect at R+1 shows as devices stuck behind, sitting in a `refused-no-record` bucket.

**Predicted, as part of 3.4, and in place before R ships:**
- **R1 (host).** The breakdown is a pure function, run over fixture rows:
  - each device lands in exactly one bucket, so the buckets always sum to the behind count;
  - a fixture with `refused-foreign` rows prints a `refused-foreign` bucket.
  - **Sabotage:** a breakdown that drops tokens it doesn't recognise loses the refused rows, so the sum check turns red.
- **R2 (live, read-only).** One real run prints the line, and its buckets sum to the behind count computed from the `fw:` rows alone.

**Who writes it** is agreed with blipscope-5c, since it is the Blipscope fleet's tool.

### A3.2 Who never reports a refusal (item 1's gap), added to A1.4's consequences

**The OTA record is sent only on the radar's cloud-feed requests.** `AircraftManager.cpp:2919` and `:2964` set it, and `CloudFeed.cpp:70` sends it. `AircraftManager` is radar-only: every other edition's build drops it. So two groups never report a refusal, and for them it shows only on serial:
- **radar devices on a local feed or on OpenSky**, which make no cloud-feed requests;
- **every non-radar edition:** Missileer, Orbitscope, Quakescope, Quillscope, Reelscope, Claudescope and Speedscope, plus Weatherscope, which makes no proxy calls in Stage 3.

**This is a known blind spot,** stated so that A3.1's reader isn't mistaken for full coverage. Whether those editions should report too is a question for when they gain a proxy path, not for Stage 3.

### A3.3 O11's sabotage predicts one value (item 2)

**Predicted: exactly `no-record`.** The record's magic is parsed first (A1.2, step 2), and a one-byte shift moves it.
- **A `bad-record` would mean** the shift landed on another valid magic. That is its own reason to stop.
- **Made structural:** the magic's four bytes are pairwise distinct, held by a `static_assert`. So a one-byte shift in either direction can never re-form it.

### A3.4 O11's foreign image carries a valid record (item 3)

**How it's built:** from **R's candidate source**, with another edition's env for the same board; for example `weatherscope-s3-128` when A is `s3-128`. So it has the magic and a well-formed record naming the other edition.
- **Predicted: exactly `refused-foreign`.**
- **Why it matters:** a pre-check image, or one from another source, would be refused as `no-record`. That would test O10, not `foreign`.

### A3.5 For Daniel's O11 ruling

blipscope-5c is content with either board. If it is E2:08, O11 waits until #450's SC6 and the Part B soak are off that board.
