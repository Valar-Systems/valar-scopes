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

## 4. `skus.yml` and releases

- **Rows:**
  - `weatherscope-s3-128`: **build-only, no slug, publishes nothing**;
  - `weatherscope-bench-s3-128`: build-only.
- **No customer-facing release, no store listing,** and no change to `version.txt`'s meaning.
- **No board is flashed except `907069326E64`.**
- **No deploys.** No Worker change happens in Stage 3.

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

**Outside every step's predictions: stop and decide.**

## Changes after review (blipscope-5c, 2026-10-20)

The review is on #449. Each finding below was checked against the artifact before the plan changed.
1. **Where the identity lives:** ESP-IDF's `.rodata_custom_desc`, at file offset 0x120.
   - It replaces the two candidates.
   - Checked: `project_name` reads `arduino-lib-builder` in a built image, and all 5 esp32s3 linker variants place the slot after `.rodata_desc`.
2. **There was no check point:** `rebootOnUpdate(true)` reboots straight after the write. The device side now sets it to false, then checks, then restarts or reverts.
   - Checked: `OtaUpdater.cpp:400`, `HTTPUpdate.cpp:396-397`.
3. **The Worker can't tell editions apart** (`X-Blip-Model` is `variant::SLUG`, `CloudFeed.cpp:51`).
   - Recorded in 1.3.
   - Out of scope for Stage 3, which makes no proxy calls.
   - Listed in section 7 as a contract before the proxy path.
4. **`ALLOWED_MENTIONS`:** this plan names `firmware.yml`. `scripts/skus.py` lists it in the same PR.
   - Checked: the reader sweep failed naming the doc, then passed.

**The predictions in 3.2 are unchanged.** They never depended on where the identity lives.
