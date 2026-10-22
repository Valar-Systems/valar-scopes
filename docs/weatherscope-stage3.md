# Weatherscope Stage 3: from the bench harness to a full edition (plan)

**Status: a plan for review. No code is written until it is approved.** Ruled by Daniel on 2026-10-20: Weatherscope moves from the Stage 1 bench harness to a full edition. Stage 2's 2.2 (the past-hour loop) and 2.3 (the screen logic) fold into this plan.

**Approved 2026-10-21 (Daniel) at `35903dd`, and merged as `69e81c1` (#449).** The rulings given with the approval are in Addendum 4.

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
- **Ruled 2026-10-21 (Daniel; Addendum 4, A4.2): one exception to "no board is flashed except `907069326E64`", for O11 only.** O11 runs on E2:08, operated by blipscope-5c. Every other Stage 3 step keeps the rule.

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
- **Ruled 2026-10-21 (Daniel; Addendum 4, A4.1): accepted.** A refused update shows nothing on screen. Its fleet reporting is later work.

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
- **Ruled 2026-10-21 (Daniel; Addendum 4, A4.2):** the board is E2:08, operated by blipscope-5c. B is hosted as a GitHub prerelease asset clearly named as a bench test, and deleted after O11.
- **Amended 2026-10-21 (reviewer ruling; A4.2's amendment):** B's hosting has changed. B is uploaded directly as an asset on the existing bench prerelease `ota-preflight-v6`, never through a tag push or the release workflow. The asset is removed after O11, and the release is kept.

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

## Addendum 4, 2026-10-21: Daniel's rulings with the approval (recorded after the merge)

The plan was approved at `35903dd` and merged as `69e81c1`. These rulings came with the approval. They are recorded here, in a follow-up PR, so that nothing was pushed to #449 after it was approved at that SHA.

### A4.1 What a refused update shows (A2.1)

**A refused update shows nothing on screen, as proposed.** The refusal is carried in the OTA record (`refused-<reason>`) and on serial.

**Added to the later fleet work:** `refused-<reason>` **must be reported in fleet telemetry,** so that a mislabelled publish gets noticed.
- **What covers part of this:** A3.1's reader (`reconcile-fleet.py`, the devices behind `version.txt` broken down by last OTA result), which blipscope-5c will write. It reaches radar devices on the cloud feed.
- **The gap that stays:** A3.2's blind spot remains. Radar devices off the cloud feed, and every non-radar edition, Weatherscope included, send no OTA record. Closing it is part of the same later work. It is not a Stage 3 change.

### A4.2 O11: the board, image B, and what is frozen before it runs (A2.4)

- **The board: E2:08, operated by blipscope-5c.** This is a **one-test exception** to section 4's "no board is flashed except `907069326E64`" rule, for O11 only.
- **Image B:** a **GitHub prerelease asset, clearly named as a bench test,** deleted after O11.
- **Timing:** the release candidate's timing is coordinated between this session and blipscope-5c.
- **Frozen in O11's own addendum, before it runs:**
  - how A's difference from R's candidate, the URL override alone, is bounded (A2.4);
  - **what `firmware.yml`'s release-event jobs (`version`, `promote`, `ship-alert`) do on a PRERELEASE publish.** O11 needs it established that they build or upload nothing that reaches `releases/latest`. blipscope-5c raised this, and it is checked together before any prerelease is created.

#### A4.2's amendment, 2026-10-21: where image B is hosted (reviewer ruling)

**Why it changed.** blipscope-5c's read-only analysis of the release jobs found two things:
- **A new prerelease runs the full release pipeline.** The build legs upload `firmware-<slug>.bin` to it, and `version` uploads `version.txt`.
- **`promote` then waits on Daniel's required review.** One approval would make the bench release `latest` for the whole fleet.

The analysis also found that the firmware decides the asset names, so B must be `firmware-s3-128.bin`. blipscope-5c therefore withdrew its suggestion of a distinct asset name, which Addendum 5's A5.3 on #453 still lists as a recommendation.

**The text below was pasted into blipscope-5c's session by Daniel on 2026-10-21, headed "Reviewer ruling on O11 hosting".**
- **Settled by Daniel (2026-10-21): the four-bullet text was written by the Weatherscope chat, not the reviewer chat, and was pasted by Daniel.**
- The reviewer's own ruling is quoted word for word in the continuation below, and that is the ruling.
- Daniel's summary in this session says the same as the four bullets in substance.

*Corrected 2026-10-21, on the reviewer's note:* `cb32317` labelled this text "the ruling, verbatim" and attributed it to the reviewer. blipscope-5c had relayed it under that heading without its provenance being checked.

> "Reviewer ruling on O11 hosting: accepted, replacing "a new bench-named prerelease, deleted after".
> - Host image B as an asset on the existing bench prerelease ota-preflight-v6, uploaded directly (gh release upload), never through a tag push or the release workflow.
> - Before upload and after O11: confirm ota-preflight-v6 is still marked prerelease and that releases/latest (and version.txt) are unchanged. Record both checks.
> - After O11, remove image B's asset (keep the release for reuse).
> - Never approve a promote step for anything connected to O11."

**It replaces A4.2's "Image B" line above.** The board, the timing and the two items frozen in O11's own addendum are unchanged.

**For O11's own addendum, frozen before O11 runs:**
- **The checks, as actual reads, recorded before the upload and after O11.** blipscope-5c proposed these three:
  - `gh release view ota-preflight-v6 --json isPrerelease`;
  - `gh api repos/Valar-Systems/valar-scopes/releases/latest -q .tag_name`;
  - `releases/latest/download/version.txt`, read twice a few minutes apart, so the value is proven static.
- **An open question, not a change to the ruling:** what `ota-preflight-v6`'s own `version.txt` is left at once B's asset is removed.
  - If it is left at B's version (R+1), any bench unit pinned to that prerelease (such as `blipscope-s3-128-otatest`) would see a newer version with no image behind it, and show "Update failed" on every check.
  - The two answers are: restore it to its pre-O11 value, or leave it and record it.
  - blipscope-5c is bringing this to the reviewer. `RELEASING.md`'s rule of re-clobbering and re-reading before each use protects the next user either way.
  - **Settled 2026-10-21 by the reviewer's condition 1 (below):**
    - both original files are re-uploaded and verified by hash;
    - the values restored are **the ones snapshotted at upload time**; "6" was only the 2026-10-21 reading;
    - re-uploading both originals also puts back the older v6 `firmware-s3-128.bin` that removing B takes away.

#### A4.2's amendment, continued: the reviewer's ruling on O11 hosting (2026-10-21)

**The reviewer's ruling, word for word, as pasted from the reviewer chat into this session:**

```text
Reviewer ruling on O11 hosting (Plan B: ota-preflight-v6), 2026-10-21

Approved, as Daniel accepted. The write-up is sound: a direct asset upload fires no "published", so nothing runs and no promote can appear, and shipping images only read releases/latest, which is v18. Conditions, all before or around the run:

1. Snapshot first. Before any clobber, download ota-preflight-v6's current version.txt and firmware-s3-128.bin, and record their sha256 and download counts.
   After O11:
   - read B's download_count;
   - delete B;
   - re-upload both originals and verify them by hash.
   That settles your open item: the harness goes back exactly as it was, version.txt "6" included.

2. Close two of your "unsure" items before the upload:
   - confirm that no unit other than E2:08 runs blipscope-s3-128-otatest;
   - read how valar-flasher picks a release, and confirm it can't offer anything from ota-preflight-v6.

3. Add a CI check that refuses OTA_RELEASE_BASE (or any OTA URL override) in every skus.yml env, with a sabotage that turns it red. It closes the one silent path to the fleet you found. It's cheap, and it must be merged before O11's upload.

4. Run the before and after checks exactly as listed in 4.3:
   - isPrerelease is true;
   - latest's tag is unchanged;
   - latest's version.txt is unchanged, read twice a few minutes apart.
   Any difference is a stop.

5. Record O11's time window and E2:08's rows in it (the fw R+1 KV row and the ota points) as bench-only, so reconcile-fleet.py and any fleet grade exclude them.

6. As you wrote: never approve a promote connected to O11. If one appears, O11 stops.

O11's own predictions are frozen in its addendum before the run, and it waits its turn on E2:08 behind the counter's healthy leg and the Part B weather-on soak.
```

**Who does what.** Conditions 2, 3 and 5 are blipscope-5c's. Condition 3 is its own PR, with predictions first, and is **merged before O11's upload**. Conditions 1, 4 and 6 belong to O11's run and its addendum.

**blipscope-5c's two notes on #454 are both answered by condition 1:**
- **The values restored are the ones snapshotted at upload time.** For reference only, the 2026-10-21 reading was: `ota-preflight-v6` a prerelease, with `version.txt` 6; `releases/latest` v18, with `version.txt` 18.
- **Removing B's asset also removes the older v6 `firmware-s3-128.bin`,** since they share a name. Re-uploading both originals puts it back.

### A4.3 Stage 3 code may start

"No Stage 3 code until #449 is approved at its new head" is met. Step 3.1 starts with its predictions frozen first (section 8).

## Addendum 5, 2026-10-21: step 3.1, moving the bench harness (frozen before its code)

**Ruled 2026-10-21 (Daniel):** step 3.1 goes now, with its predictions frozen first, without waiting for measurement 4. Measurement 4 has since completed (`docs/wxc1-v2.md` A7.7).

**The base:** main once #452 is merged. Its tree is `991ac88`'s, and this branch starts from `991ac88`. **The tree equality is checked** (`git diff 991ac88 <merge>` empty) before the base builds are believed.

### A5.1 The complete change list

1. **The move:** `src/weatherscope/stage1_main.cpp` → `src/weatherscope/bench/stage1_main.cpp`, by `git mv`, **byte-identical** (a 100 % rename).
   - Its includes stay bare names (`"WxPalette.h"` and the rest), so the bench env gains an include path to `src/weatherscope`. The shared headers stay where they are, for the product.
   - Its header comment still names the old env. That line changes in 3.2, when the product env takes the name, so the file stays byte-identical here.
2. **`platformio.ini`:** `[env:weatherscope-s3-128]` becomes `[env:weatherscope-bench-s3-128]`.
   - `build_src_filter = -<*> +<weatherscope/bench/>`.
   - The include path for `src/weatherscope` is added to its `build_flags`, and its comment is updated.
   - `extends`, `embed_files` and `lib_deps` are unchanged.
   - `[filters] editions_off` is unchanged: it drops `weatherscope/`, which still covers `bench/`.
3. **`skus.yml`:** the row is renamed `weatherscope-bench-s3-128`, still build-only.
4. **`scripts/check-adsbdb-out.sh`:** the `weatherscope-*` case is narrowed to `weatherscope-bench-*`. Its plant path, in the case and in `PLANT_FILES`, moves to `src/weatherscope/bench/stage1_main.cpp`.
5. **`tools/weatherscope/s1/`:** Stage 1's stamp accountant (`stampdiff.py`, committed unchanged) and the S1 driver, both committed before S1 runs.
6. **`RELEASING.md`, and A5.3 here:** the two release rulings relayed on 2026-10-21, beside "Planned, not yet in any release".

**Nothing else changes:** no other env's configuration, and no source any other env compiles.

### A5.2 Predicted

| check | how | predicted |
|---|---|---|
| **S1** | **What:** the 14 other envs in `skus.yml`, each built clean at the base and at 3.1's head.<br>**Where:** in **one directory**, so no path compiled into an image differs.<br>**How:** each pair compared with `stampdiff.py`. | **Stamps only, in 14/14.** That means `app_elf_sha256`, the checksum and trailing SHA-256, and WiFiManager's two time strings where WiFiManager is linked. **Any size change or UNEXPLAINED byte: stop.** |
| **S1's sabotage** | **The edit:** at the head, `if (millis() == 0xFFFFFFFFul) Serial.println("S1 SABOTAGE");` at the top of `ConfigurationWebServer::Initialise()`, outside every edition guard. It is runtime-dependent, so the linker keeps it.<br>**The check:** the edit is asserted applied, then reverted after. | **Two-sided, keyed to the build itself:**<br>• a non-stamp difference in **every env whose build compiled `ConfigurationWebServer.cpp`** (its object file exists). Predicted: 13 of the 14, every one except `animtest-s3-128`;<br>• **stamps only in every env that didn't.** Predicted: `animtest-s3-128` alone. |
| **the bench image** | `weatherscope-bench-s3-128` at the head against `weatherscope-s3-128` at the base, in the same directory | **Stamps only, with no time strings**, since the bench links no WiFiManager: `app_elf_sha256`, the checksum and the trailing SHA-256. |
| **C1, on the board** | Flash the head's bench image on `907069326E64` (standing OK; the port found by serial). Then run `capture_stage1.py`. | **Exit 0, as in Stage 2:**<br>• 240/240 rows;<br>• B9 `344383b4…`, equal to the board's `fb_sha256`;<br>• its standing timing checks repeating. |
| **C2, on the board** | the same capture's `WX,sprite_sha256` | **`a88c3a99fa5c1bb4d6d2bb77d081b22b8da6e752200ae79cf67acec90c335ed3`**: `palette_ref.py`'s sprite for the final 55 % palette, 115,200 bytes, run before this addendum |
| **L1-L6, on the board** | `legend_check.py` | **Exit 0:** L1, L2, L4, L5 and L6 all hold. The script defines no L3. |
| **CI** | the matrix, from `skus.yml` | **The bench leg** builds under its new name. Its adsbdb gate reads PASS, with the control seen in `src/weatherscope/bench/stage1_main.cpp`.<br>**The discriminator:** if the narrowed case failed to match the renamed env, the gate would plant at the default site, which the bench doesn't compile, and read `UNTRUSTWORTHY (control blind)`.<br>**Every other leg:** as on main. |

**The order:**
1. this addendum;
2. the code commit, then the S1 tooling commit;
3. S1's base builds, then its head builds, then the sabotage builds;
4. the bench image;
5. the flash and the board checks.

**Outside every prediction: stop and decide.**

**Disk:** C: has 26 GB free. Each env's build directory is deleted once its image is copied out, so at most one env's build is on disk at a time.

### A5.3 Two release rulings, relayed by blipscope-5c from the reviewer chat (2026-10-21)

- **The OTA identity check's release:** "The release that adds it needs Daniel's explicit OK at cut time, with those two consequences named in plain words." The two consequences are A1.4's:
  - no OTA downgrade past that release;
  - edition and board identities become permanent.

  This is also written into `RELEASING.md`'s "Planned, not yet in any release" note, where the person cutting R reads it.
- **O11:** "no prerelease gets created, as a bench test or otherwise, until you've brought me what each release job does on a prerelease publish and Daniel has OK'd it."
  - blipscope-5c's read-only analysis is with the reviewer. A prerelease publish runs the full release pipeline. `promote` pauses on Daniel's required review, and **one approval would make the bench release `latest` for the whole fleet.**
  - blipscope-5c recommends three things:
    - a structural guard, so that `promote` never runs for `bench-*` tags;
    - rejecting the promote deployment, as a second layer;
    - a distinct asset name for B.
  - Daniel decides. **No prerelease exists until then.**

### A5.4 S1 stopped: the reading, and a diagnosis registered before it runs (2026-10-21; local until ruled)

**The reading** (`tools/weatherscope/s1/s1.py compare`). All three rounds built every env. The sabotage was planted, then removed, with the file restored byte-identical and the clone clean.

| row | predicted | measured |
|---|---|---|
| **S1** | stamps only, 14/14 | **11/14: OFF-PREDICTION, STOP.** These three are not stamps-only, at equal sizes:<br>• `blipscope-s3-128`: 143,061 unexplained bytes;<br>• `blipscope-pro-s3-21`: 21,858;<br>• `missileer-photo`: 6, in two 3-byte regions that read as rodata pointers (`0x3C…`). |
| **S1's sabotage** | two-sided, with the non-compiling set exactly `animtest-s3-128` | **14/14 as predicted.** The 13 envs that compiled `ConfigurationWebServer.cpp` show a non-stamp difference, and `animtest-s3-128` is stamps only. **The instrument sees.** |
| **the bench image** | stamps only, no time strings | **OFF-PREDICTION: the size differs** (466,528 → 466,544 B). The cause is read off the images: `[common]` compiles `-DBUILD_ENV=\"$PIOENV\"` into every image (`platformio.ini:100`). So the base image holds `weatherscope-s3-128` and the head holds `weatherscope-bench-s3-128`. **Renaming the env must change the image, and A5.2's prediction missed it.** |

**Not run:** the board checks (C1, C2, L1-L6). They follow S1 in A5.2's order. CI's own prediction for #453 is unaffected.

**Three explanations fit the three envs, and the saved images can't separate them:**
- **H-A, 3.1 changed them.** No changed env name or commit id appears in them (searched for), but nothing yet excludes it.
- **H-B, a first-build effect in the fresh clone.** The base round was the clone's FIRST build of every env. It installed and patched each env's libraries (`pre:` patch scripts) on the way, and the head round reused them. `blipscope-s3-128` and `blipscope-pro-s3-21` were the first two envs built.
- **H-C, these envs don't build reproducibly** in this setup at all.

**D1, the control** (to run once this is committed):
- **What:** rebuild `blipscope-s3-128`, `blipscope-pro-s3-21` and `missileer-photo` at the **base** (`991ac88`), in the same clone, with `s1.py build` (label `base-rebuild`).
- **The comparisons:** (i) the rebuild against the base round's image, which is the same source; and (ii) the head round's image against the rebuild.

| per env | (i) base-rebuild vs base | (ii) head vs base-rebuild | means |
|---|---|---|---|
| **H-A** | stamps only | NOT stamps only | 3.1 changed the image: S1's miss is real |
| **H-B** | NOT stamps only | stamps only | the base round's images were first-build artifacts: S1's miss is the instrument's |
| **H-C** | NOT stamps only | NOT stamps only | the env doesn't build reproducibly: S1 can't judge it as designed |
| anything else | | | the instrument is unreliable at this scale: stop and decide |

**D2, the bench's one difference:**
- **What:** in the clone at the head, the bench env's header alone is temporarily renamed back to `[env:weatherscope-s3-128]` (asserted applied, restored, and the clone asserted clean). It is built and compared with the base round's bench image.
- **Predicted: stamps only, with no time strings.** That would make the env name the image's only difference.
- **Anything else:** stop.

**The fix is not chosen here.** It waits for the ruling, after D1 and D2 are read.

### A5.5 D1 and D2, read as registered; the cause; a proposed fix (2026-10-21; local until ruled)

**D1, per env, graded on A5.4's table:**

| env | (i) base-rebuild vs base | (ii) head vs base-rebuild | outcome |
|---|---|---|---|
| `blipscope-pro-s3-21` | NOT stamps only (21,858) | stamps only | **H-B, a first-build effect** |
| `missileer-photo` | NOT stamps only (6) | stamps only | **H-B, a first-build effect** |
| `blipscope-s3-128` | stamps only | NOT stamps only (143,061) | **H-A on the table.** But see below: the table's set was incomplete |

**Supporting evidence for H-B (read after D1; blipscope-5c named the discriminator):**
- In the base round, each of these envs' build **installed** its libraries during the run.
- Every later round printed `patch_wm_copy: 0 applied, 3 already patched` and the LovyanGFX patch line.
- The `pre:` library patches skip silently when their target isn't installed yet. So the base round compiled unpatched libraries, and the head and rebuild rounds compiled patched ones.

**`blipscope-s3-128`: the cause was read off the images, and it is not 3.1.**
- **Compiled sources:** the same 213 project translation units in every round, none of them from Weatherscope.
- **Library state:** identical in the head and rebuild rounds.
- **The difference:** at the head, `"custom.15."` becomes `"custom.&amp;"`. A standalone `"15"` string is gone, and the rodata after it shifts.
- **Why it's gone:** each image carries two build-time strings.
  - The head build's second one is **`09:15:15`**. It ends in `15`, so the linker tail-merged the standalone `"15"` literal into it.
  - Base (`09:00:40`/`09:00:54`), base-rebuild (`09:44:49`/`09:45:02`) and sabotage (`09:28:48`/`09:29:02`) have no time ending in `15`, and each keeps its standalone `"15"`.

  **The head image is the only one with a time ending in `15`, and the only one without the standalone `"15"`.**

**So D1's H-A label is wrong for this env.** The table read "(i) stamps only, (ii) not" as "3.1 changed the image". It did not name a build whose own clock changes its layout. That is the outside-the-set branch this file warns about. D1 stands as read and is not re-graded; this cause is recorded beside it.
- **Not yet proven by a controlled rebuild.** The proposed fix's calibration tests it.

**D2, as registered:**
- **The verdict:** stamps only, at equal size (466,528 B).
- **One clause missed:** D2, and A5.2's bench row, predicted **no time strings**. The bench carries one 4-byte compile-time string (`…Info:… 09:14:30` → `09:50:14`). The claim that the bench links nothing with a time string was wrong.
- **What still holds:** under its old name, the head's bench equals the base's bench except for stamps. **The env name is the image's only real change.**

**What all this says about S1.** The rows the instrument got wrong were wrong for reasons of build setup, not because of the change:
- **First builds compile unpatched libraries,** and that left 2 false failures.
- **Build-time strings feed the linker's tail-merging,** and that left 1 false failure, which recurs whenever a compile time happens to end in a string that exists elsewhere.

The sabotage row (14/14) shows the instrument sees a real change. **3.1's own change is not shown to alter any of the 14 envs.**

**Proposed fix, for the ruling. Nothing is re-run until then.** S1 v2 adds two things to A5.2's procedure:
1. **A warm-up build of every env in the clone before the base round,** with its image discarded, so every measured build compiles patched libraries.
2. **Every measured build runs with `SOURCE_DATE_EPOCH` fixed to one value.** GCC then fixes `__DATE__`/`__TIME__`, which removes both the stamp strings and the tail-merge effect.

**S1 v2's new frozen predictions:**
- **C-S1v2, the calibration, run first:** two builds of `blipscope-s3-128` at the base, minutes apart, both with `SOURCE_DATE_EPOCH` set, are **byte-identical** (0 differing bytes). If not, `SOURCE_DATE_EPOCH` didn't reach the compiler or something else is time-dependent: stop.
- **S1:** base against head is **byte-identical in 14/14.** That is stricter than "stamps only".
- **The bench:** byte-identical to the base's bench when the head's bench is built under the old name (D2's method). Under the new name, every difference is downstream of the env-name string.
- **S1's sabotage:** unchanged: a difference in the 13 envs that compile `ConfigurationWebServer.cpp`, and **0 differing bytes** in `animtest-s3-128`.

**Not proposed, raised for Daniel:** the patch scripts' "not found yet; skipping" path lets a build succeed **unpatched** (blipscope-5c's note). Whether that should be fatal in CI is a separate decision.

### A5.6 Ruled 2026-10-21: S1 v2, its stamp list and its predictions (frozen before any measured build)

**The reviewer's ruling (the single reviewer from this date; the Weatherscope chat is retired):**
- S1 v2 is accepted, with one correction: predict base against head identical **outside an explicit list of regions that can still differ between commits**. Each region is listed with the reason it can differ, before any measured build. Not "blindly byte-identical".
- The first run's misses stay on record (A5.4, A5.5).
- The branch is rebased onto `bf7ece6` and the run repeated.
- **The patch-skip question is ruled separately:** a patch that can't apply will fail the build. blipscope-5c is doing that work.

**The base and the head.**
- **Base:** main at `bf7ece6`.
- **Head:** this branch once S1 v2's tooling is committed. The rebase changed no line of this branch's change (the same numstat and the same added and removed lines as before it).

**The list: regions that can still differ between base and head.**

| region | where | why it can differ | allowed? |
|---|---|---|---|
| **R1, `app_elf_sha256`** | image offset `0xB0`, 32 B | a hash of the whole ELF, including debug sections that never reach the image, so it can change when no image byte does | **yes** |
| **R2, the image checksum and the appended SHA-256** | the last 33 B | computed over the image, R1 included, so they change whenever R1 does | **yes** |

**Checked and NOT on the list** (a difference in any of these is a stop):
- **`BUILD_ENV`:** the 14 other envs keep their names, so their `$PIOENV` strings can't differ. The bench's name does change, which is why the bench is compared under its old name.
- **A commit id:** none is embedded.
  - No build script writes one: `check_config_form_prebuild.py`'s only subprocess runs a checker, and the patch scripts and `fix_boot_app0_offset.py` call no git.
  - No build flag carries one.
  - Run 1's images contained neither commit id (searched).
- **`__DATE__`/`__TIME__` strings** (WiFiManager's two, and the bench's one): fixed by `SOURCE_DATE_EPOCH`. If they differ, it didn't reach the compiler, and the calibration catches that first.
- **The other `esp_app_desc_t` fields** (project name, version, build date and time): fixed in the prebuilt framework.

**The procedure.** It runs in the same clone, every measured build with `SOURCE_DATE_EPOCH=1760000000` passed in the build's environment:
1. **Warm-up:** one build of every env at the base, and of `weatherscope-bench-s3-128` at the head, with the images discarded. Every measured build then compiles installed, patched libraries.
2. **Calibration**, then the base, head and sabotage rounds.
3. **The bench under its old name**, by the D2 method, now committed in the tooling.
4. **The board checks**, as frozen in A5.2.

**Predicted:**

| check | predicted |
|---|---|
| **C-S1v2, the calibration, first** | two builds of `blipscope-s3-128` at the base, minutes apart: **0 differing bytes, R1 and R2 included** (the same commit gives the same ELF).<br>• A time string differs: `SOURCE_DATE_EPOCH` didn't reach the compiler. Stop.<br>• Anything else differs: something else in the build is time-dependent. Stop. |
| **S1** | the 14 other envs, base against head: **identical outside R1 and R2, in 14/14.** Whether R1 and R2 differ is reported, not graded. |
| **S1's sabotage** | a difference outside R1 and R2 in every env whose sabotage build compiled `ConfigurationWebServer.cpp`, predicted to be the 13 envs other than `animtest-s3-128`. **`animtest-s3-128` at 0 differing bytes**, as ruled. |
| **the bench** | the head's bench built under its old name, against the base's bench: **identical outside R1 and R2.** |
| **C1, C2, L1-L6, on the board** | as frozen in A5.2 |

**Outside every prediction: stop and decide.** The first run's records stay as they are.

### A5.7 Step 3.1's results: S1 v2, the board and CI, every row as predicted (2026-10-21)

**S1 v2**, run as A5.6 froze it.
- **The build:** in one clone, with `SOURCE_DATE_EPOCH=1760000000` on every measured build. The base was `bf7ece6` and the head `0de94ad`.
- **The rounds:** every warm-up and measured build succeeded. The sabotage and the bench rename were each restored byte-identical, and the clone was clean after each round.
- **The grade:** `s1.py compare2` printed `S1 v2 rows: ALL AS PREDICTED`.

| row | predicted | measured |
|---|---|---|
| **C-S1v2, the calibration** | `blipscope-s3-128` built twice at the base, minutes apart: 0 bytes | **0 differing bytes** (the builds finished 10:40:49 and 10:43:41) |
| **S1** | identical outside R1/R2 in 14/14 | **14/14 completely byte-identical.** R1 and R2 are unchanged too, with 0 differing bytes in every env. **3.1 changes nothing in any other product's image.** |
| **S1's sabotage** | outside R1/R2 in the 13 envs that compile `ConfigurationWebServer.cpp`; `animtest-s3-128` at 0 bytes | **13/13 changed** (size differs), and **`animtest-s3-128` is at 0 bytes.** The non-compiling set is exactly `['animtest-s3-128']`. |
| **the bench, under its old name** | identical outside R1/R2 | **0 bytes outside R1/R2.** R1 and R2 differ (65 bytes in all), as listed: moving the file changes the ELF's debug data. |

**On the board**, `907069326E64`:
- **The flash:** the head's bench image, with the port resolved from the serial (COM6). esptool read the MAC as `90:70:69:32:6e:64`, and the board's banner reads `env=weatherscope-bench-s3-128`.
- **An instrument slip before this:** a first capture attempt passed the serial number as the port name. The open failed, and no byte reached the board.

| check | predicted | measured |
|---|---|---|
| **C1** (`capture_stage1.py`) | exit 0: 240/240 rows, B9 `344383b4…` = `fb_sha256`, the standing timing checks repeating | **exit 0.**<br>• 240/240 rows good; B9 **IDENTICAL**, 0 of 57,600 pixels differ, consistent with the board's `fb_sha256`.<br>• B3 153,608, B4 0 and B8 25,768 as predicted.<br>• B5-B7, the standing misses, repeat at +0.0 %, −0.1 % and +2.7 %. |
| **C2** | `WX,sprite_sha256` = `a88c3a99fa5c…` | **`a88c3a99fa5c1bb4d6d2bb77d081b22b8da6e752200ae79cf67acec90c335ed3`**, exact |
| **L1, L2, L4, L5, L6** (`legend_check.py`) | exit 0 | **exit 0:**<br>• L4 240/240;<br>• L2 exactly the ruled mapping;<br>• L1 21/21 swatches exact;<br>• L5 dry, with 0 stray pixels;<br>• L6 farthest pixel 109.0 px (≤ 112), as in #448. |

**CI on `0de94ad`:** 33 checks, 30 successes and 3 release-only skips.
- The bench leg's adsbdb gate reads `weatherscope-bench-s3-128  CONTROL found … PASS`.
- **"found" is the discriminator:** had the narrowed case not matched the renamed env, the control would have been planted in a file the bench doesn't compile.

**Step 3.1 is complete.** The bench harness is `weatherscope-bench-s3-128`, its file moved byte-identical, and no other product's image changed by a single byte.

**On record from the first run (A5.4, A5.5):**
- S1 v1's three false failures came from the build setup: first-build library patching, and a build-time string tail-merge.
- The bench prediction missed the compiled-in env name.
- D2's "no time strings" clause missed.

### A5.8 Correction (2026-10-21): S1 v1's "first-build library patching" explanation was wrong

**What A5.5 and A5.7 said.** Two of S1 v1's false failures (`blipscope-pro-s3-21` and `missileer-photo`) came from first-build library patching: the base round compiled unpatched libraries.

**What the logs show, read in full** during the review of #457, which depends on exactly this ordering:
- In each of the three envs' first builds in the fresh clone, every library was installed first (`Library Manager: Installing`, log lines 3-25).
- Then **all three patches applied**: `guarded ASYNC_RESPONCE_BUFF_SIZE`, `patch_wm_copy: 3 applied, 0 already patched` and `made the LCD_CAM interrupt shared` (lines 40-42).
- Compilation started only after that (line 75 onward).

**So the base round compiled patched libraries, and A5.5's mechanism is false.** It was drawn from a read of those logs that my own command cut off at 330 characters, which never showed lines 40-42. That is CLAUDE.md's "never filter the output of a command you are testing", applied to a diagnosis.

**The cause, re-read from the saved images:**

| env | what the images show | cause |
|---|---|---|
| `blipscope-pro-s3-21` | the base build's second time string is `09:02:01`, and that image has **one standalone `"01"` string fewer** than the rebuild and the head (1 against 2) | the **same build-time-string tail merge** as `blipscope-s3-128` |
| `blipscope-s3-128` | `"15"` merged into the head build's `09:15:15` (A5.5) | unchanged |
| `missileer-photo` | 6 bytes in two rodata-pointer regions; no short digit string changes count | **not explained** |

**What stands:**
- **D1's graded outcomes**, as read (H-B for `pro-s3-21` and `missileer-photo`: the base round's images were the odd ones out). Only the mechanism recorded beside them changes.
- **S1 v2's results (A5.7).** S1 v2 added warm-up builds AND fixed `SOURCE_DATE_EPOCH`. On the corrected cause, fixing the time strings is what removed the false failures. The warm-ups were harmless, but S1 v2's pass can't say which of the two mattered.
- **#457 and the reviewer's ruling 2** ("a patch that can't apply fails the build"). They stand on their own: the skip path was a hazard whether or not it fired here.

## Addendum 6, 2026-10-21: step 3.2, the edition scaffolding (frozen before its code)

**The reviewer's ruling (2026-10-21):** "Step 3.2: go ahead, with predictions frozen before any code, as the plan says. Include S1 for every shared file it touches, and show in CI that the product env's adsbdb gate finds its control at the default site."

**The base:** main at `75d62c4`. If main moves before the merge, S1 is measured against the PR's actual merge parent, and that parent is recorded.

### A6.1 What 3.2 builds, and what it leaves to 3.3

**The edition's own code** lives in `src/weatherscope/`, outside `bench/`, and is compiled only by the product env:
- **`WeatherscopeManager`:** the shared `Initialise()`/`Update()`/`Draw()` surface. **3.2 draws only a splash, then a status face**: the device name, Wi-Fi state, the config-page URL and the UTC time. **No weather screen is drawn in 3.2**; screens and their logic are 3.3.
- **The three feed interfaces** (`WxFeed`, `ObsFeed`, `AlertFeed`) **and their stored implementations:**
  - `StoredWxFeed` serves the KDFW crop already in the repo (`test/fixtures/wx/kdfw-60mi.wxc`), embedded.
  - `StoredObsFeed` serves one NWS observation for KDFW. It is fetched once by this session at implementation time, never by the device, and trimmed to the fields the device reads (present weather and time). **No coordinates are kept.**
  - `StoredAlertFeed` serves the event, headline and expiry of the 2026-10-20 sample in `docs/weatherscope-screens.md` §3. The warning polygon waits for the screen that draws it (3.3).
- **One factory file** chooses the implementations (D1).

**Shared files.** Each change is a new `#elif` branch, a name added to an edition list, or `&& !defined(FEATURE_WEATHERSCOPE)` appended to a guard. The counts were taken at `75d62c4`:

| file | change |
|---|---|
| `platformio.ini` | the product env `[env:weatherscope-s3-128]`, on the s3-128 board, with `${common.build_flags}`, the s3-128 variant, `-DFEATURE_WEATHERSCOPE`, `-DFW_OTA_PREFIX=\"weatherscope-\"`, PSRAM and `ARDUINO_USB_MODE=1`. Its filter is `+<*> ${filters.radar_off} ${filters.editions_off} ${filters.hwtest_off} ${filters.gametest_off} ${filters.anim_off} +<weatherscope/> -<weatherscope/bench/>`. No other env changes. |
| `src/main.cpp` | **2** `#elif` branches (include `:63`, instance `:111`), and the guard term on **6** radar-only guards (`:665`, `:680`, `:801`, `:831`, `:848`, `:865`) |
| `src/ConfigurationWebServer.cpp` | **4** `#elif` branches (page `:2334`, load `:3040`, template `:3395`, save `:3859`), and the guard term on **9** radar-default guards (`:18`, `:415`, `:572`, `:2612`, `:3064`, `:3081`, `:3574`, `:3964`, `:4075`). **Not** on the no-geography list at `:3920`: Weatherscope needs a location. |
| `include/BuildIdentity.h` | 2 (its edition `#ifdef` and `#elif`, as Speedscope's at `:67` and `:99`) |
| `include/DeviceIdentity.h` | 1 (`DEVICE_PRODUCT_NAME` = "Weatherscope") |
| `include/FactoryReset.h` | 1 (on the "persist no logbook" list at `:84`) |
| `src/SoakHarness.h` | 1 (on the edition list at `:25`) |
| `scripts/check_release_envs.mjs` | `FEATURE_WEATHERSCOPE` added to `EDITION_FLAGS` |
| `skus.yml` | the product row `{ env: weatherscope-s3-128, status: build-only }`, with no slug |
| docs | `CLAUDE.md`'s edition section (authorized, A1.5); an edition line in `README.md`; `RELEASING.md`'s edition OTA channel section, saying there is no slug yet |

`FW_VERSION` doesn't change, and nothing is released.

**Graded later, in 3.3, because 3.2 has no frame path:** the PSRAM, frame-time and loop-rate budgets of section 6.

### A6.2 Predicted

| check | how | predicted |
|---|---|---|
| **S1, every other env** | S1 v2's method (A5.6): warm-ups, `SOURCE_DATE_EPOCH`, and the R1/R2 list. All **15** other envs, the bench included, base against head. | **identical outside R1 and R2, in 15/15** |
| **S1, every compiled shared file** (the ruling) | **The covering sabotage:** one per shared file that the other envs compile (`main.cpp`, `ConfigurationWebServer.cpp`, `BuildIdentity.h`, `DeviceIdentity.h`, `FactoryReset.h`, `SoakHarness.h`). Each is an UNGATED change to a value that file already contributes to every image, built in `blipscope-s3-128`. The site in each file is named in the tooling commit, before any measured build.<br>**Plus the full-matrix sabotage from 3.1** (`ConfigurationWebServer::Initialise`). | **The per-file sabotages:** `blipscope-s3-128` differs outside R1/R2 in **6/6**.<br>**The full-matrix sabotage:** every env that compiles the file differs, and **`animtest-s3-128` and `weatherscope-bench-s3-128` are at 0 bytes.**<br>**If a file has no ungated runtime site,** it is reported as such, and its S1 coverage rests on the full-image comparison alone. |
| **S1, the shared files no build reads** (`skus.yml`, `check_release_envs.mjs`, docs) | their own checks | `skus.py --selftest/--validate/--check-readers` and `check_release_envs` exit 0 |
| **S2, a guard term left off** (A2.3) | grep the product image for the radar-only strings **`local-url`, `radius-unit`, `data-source`, `/api/v1/blipscope/blips`** | **0 hits.**<br>**The positive control:** all four are present in `blipscope-s3-128`'s image at the base. They read 3, 3, 4 and 1 in S1 v2's `base2` image, and **0** in Claudescope's, Speedscope's and Missileer's, so they are radar-only in fact, not only by guard.<br>**The anchor:** a control build of the product with one planted, reachable copy of `local-url` must be found.<br>**The sabotage:** the guard term removed from the guard around the radar form makes S2 find its strings, and name them. |
| **D1** | `grep` of `src/`, and a host test | exactly **one** file names the `Stored*` implementations, besides their own definitions. The host test builds all three interfaces against fakes. |
| **D2** | grep the product image, **case-sensitive, lowercase**, as hostnames are written, for `weather.gov`, `noaa`, `ncep`, `mrms`, `amazonaws` | **0 hits.**<br>**The control:** a control build with one planted, reachable hostname string must be found.<br>*Recorded now so it isn't discovered later:* the bench image carries one uppercase `MRMS`, a text string, and no host fragment. |
| **CI** | the matrix on the PR head | **The product leg builds.**<br>**Its adsbdb gate:** `CONTROL found` at the **default site, `ConfigMigration.cpp`** (3.1 narrowed the bench case), and PASS.<br>**The OTA URL check (#455):** **PASS** (the edition has an OTA client on the default base).<br>**Every other leg:** as on main. |
| **Budgets** (graded now) | the product build's `pio` size summary | **flash 1.60-1.85 MB; static RAM 55-70 KB** (section 6) |
| **The board** | flash `weatherscope-s3-128` on `907069326E64` (standing OK; port from the serial) | **The banner:** `[build] env=weatherscope-s3-128`.<br>**The mDNS name:** `Weatherscope-XXXXXX`.<br>**Wi-Fi:**<br>• it joins with the stored credentials;<br>• *if none survive the flash,* the portal appears, and that is a stop to ask Daniel, not a build fault.<br>**`GET http://<name>.local/`:** **200**, with the Weatherscope form, and **none** of S2's strings in the body. |

**Order:**
1. This addendum.
2. The code commit.
3. The tooling commit, with the per-file sabotage sites named.
4. S1.
5. S2 and D2.
6. D1.
7. CI.
8. The budgets.
9. The board.

**Outside every prediction: stop and decide.**

### A6.3 Corrections and choices made before the code (2026-10-21; for the reviewer)

**A correction: the alert sample A6.1 names doesn't exist as one record.**
- **What A6.1 says:** `StoredAlertFeed` serves "the event, headline and expiry of the 2026-10-20 sample in `docs/weatherscope-screens.md` §3".
- **What is there:** read at #425's head `5b0e819` (the doc isn't on main), §3 is a **summary** of which fields about 10 alerts of each type carried, sampled on 2026-10-20. It holds no single alert with an event, a headline and an expiry. Section 2 of this plan carries the same reference.
- **The replacement, which stays as close to the intent as the API allows:** one alert, fetched once by this session at implementation time from `api.weather.gov/alerts`, the same way A6.1 already has this session fetch the observation. The selection rule is fixed here, before any alert is seen:
  - the types in 3d's order: Tornado, Snow Squall, Severe Thunderstorm, Flash Flood Warning;
  - alerts **sent on 2026-10-20 (UTC)**, the sampled day;
  - **the first type with any such alert, and within it the earliest `sent`.**
  - If none of the four types has one, stop and ask.
- **What is kept:** `event`, `headline`, `expires`, and the alert's `sent` for the record. **Not kept:** the polygon, `areaDesc`, zone and SAME codes, ids, URLs and sender addresses. So nothing it stores can hit D2's lowercase hosts.
- **What depends on it:** nothing graded in A6.2. No 3.2 screen draws an alert. If the reviewer prefers another source, the stored record is swapped. Only the product image changes, and D2 and the budgets are re-read.

**Choices the frozen text left open, written down so they aren't discovered later:**
- **The env's flags:** `${s3128_hw.build_flags}`, the URL-free board section every s3-128 bench env composes from. That is A6.1's list (`${common.build_flags}`, the s3-128 variant, PSRAM, `ARDUINO_USB_MODE=1`) **plus `ARDUINO_USB_CDC_ON_BOOT=1`**. Claudescope's env and every s3-128 env carry that flag, and without it the banner goes to UART0 instead of the USB port. Then `-DFEATURE_WEATHERSCOPE` and `-DFW_OTA_PREFIX=\"weatherscope-\"`.
- **The partition table:** `partitions-s3-16mb-bignvs.csv`, the s3-128 SKU's table and the one `907069326E64` carries now. So its NVS, and any stored Wi-Fi credentials, stay where they are. `-DBLIPSCOPE_EXPECT_BIG_NVS` is **not** set: its only reader is `Logbook.cpp`, which the product doesn't compile.
- **The crop:** `board_build.embed_files = test/fixtures/wx/kdfw-60mi.wxc`, as section 2 says, read through the same `_binary_…` symbols the bench uses.
- **The stored observation and alert** are compiled constants, generated from the fetched JSON by this session. The device parses no JSON in 3.2; the live parsers come with the proxy path.
- **The observation:** `KDFW`'s `/observations/latest` at fetch time, keeping `presentWeather` (its raw strings) and `timestamp`. Station coordinates and every other field are dropped.
- **The interfaces in 3.2:** `WxFeed::Latest()`, `ObsFeed::Latest()`, `AlertFeed::Active()`. The past hour's history is added to `WxFeed` in 3.3, with the 15 frames.
- **The config page:** location (latitude, longitude) and display (auto-dim, brightness), like Claudescope's location and display blocks. No ntfy and no alert toggles until there are alerts to send.

### A6.4 STOPPED: the budgets read below their bands (2026-10-21; local until ruled)

**How it was seen, ahead of the frozen order.** The code had to be compiled before it could be committed. That compile of the tree that became `4df1065` printed PlatformIO's size summary, so the budgets row (step 8) was read **before** S1. Nothing was changed after it: `4df1065` is that tree.

| | read | frozen band (A6.2) | |
|---|---|---|---|
| flash | **1,518,635 B** | 1.60-1.85 MB | **below**, in decimal or binary units |
| static RAM | **54,600 B** | 55-70 KB | **below**, in decimal or binary units |

**The verdict:** a miss on both, recorded on the frozen bands and **not re-graded**. Per the standing rule, no measured row (S1, S2, D2, D1, CI, the board) runs until the reviewer rules. The tooling commit, which measures nothing, may be prepared locally.

**Kept apart from the verdict: what the bands assumed, and candidate causes. None of them is tested.**
- **What the bands assumed:** section 6 built them from Claudescope (1,575,195 B flash, 54,992 B RAM) and Speedscope, both on the **s3-146**, plus about 20 KB of Weatherscope code and up to 150 KB of stored data. So they assumed Weatherscope would be **at least Claudescope's size**.
- **(a) Less code than the basis.** Step 3.2's edition has no feed client, no HTTP or JSON parsing and no ntfy, all of which Claudescope carries. The RAM lands 392 B under Claudescope, which this fits.
- **(b) A different board.** The basis envs are s3-146 builds (an SPD2010 QSPI panel and its touch driver). This env is the s3-128 (GC9A01, CST816), so the variant's driver code differs.
- **What would separate (a) from (b),** proposed and not run: build `claudescope-s3-146`'s source with the s3-128 variant flags, and compare its size with both.

**Asked of the reviewer:**
1. Is the miss recorded as above, with its cause left open, like M4g?
2. May the remaining rows (S1, S2/D2, D1, CI, the board) proceed as frozen? Their predictions don't depend on the budgets.
3. Should 3.3's flash and RAM bands be re-derived from this build, `4df1065`, rather than from the s3-146 envs? Any new bands would be frozen in 3.3's addendum, before its code.

### A6.5 Ruled 2026-10-21: A6.3 and A6.4

These are the reviewer's rulings, relayed by blipscope-5c "verbatim as pasted to me":

> 1. A6.4: recorded as a miss, cause left open, as with M4g. Both sizes are under their bands; the model (Claudescope on s3-146 as a floor) was wrong.
> 2. The other checks (S1, S2/D2, D1, CI, the board) go ahead as frozen. None depends on the sizes.
> 3. 3.3's size ranges are re-derived from 4df1065's measured sizes, with the basis for each addition stated, and frozen in 3.3's addendum before any 3.3 code.
> 4. A6.3 is accepted. The selection rule was written before fetching, and one real alert is fine. The USB-serial flag and the large-NVS partition table are accepted too.
> 5. Push the four local commits now.

**Done:**
- the four commits were pushed (`b348623`);
- **the budgets row reads as a miss** (A6.4), with its cause open.

**The base moves.** Main is now `882d49e` (#450, which changes radar firmware). So, as A6 provides, S1 is measured against the PR's actual merge parent. The branch is rebased before S1, and the parent is recorded with S1's result.

### A6.6 S1's base, and what a later move of main means (2026-10-21, written before S1 runs)

- **The base:** main at `6bb61df` (#450 and #458), merged into this branch rather than rebased, so `4df1065` stays the commit the rulings name. S1's base is `6bb61df`, and its head is this branch's tip after the merge.
- **If #457 merges before this PR does,** main moves again and the merge parent changes. **S1 then re-runs in full against the new parent:** the calibration, base, head, both sabotages and the per-file rounds.
  - blipscope-5c reports that #457 applies the same patches to the same bytes, so no image should change. That is a claim about code this PR doesn't touch, so it is re-measured, not assumed.
  - **Predicted for that re-run:** the same verdicts as the first run.
- **If main moves for any other reason before the merge,** the same rule applies.

### A6.7 S1's first run stopped, ungraded; the re-run's base is `b7d7233` (2026-10-21, written before the re-run)

- **The first run** (base `6bb61df`, head `1818261`) was stopped after its `head` round and partway through `sabotage`, when #457 merged (main `b7d7233`, 23:35:22Z). **No row of it was graded,** and none will be. A6.6 already said a move of main means a full re-run.
- **The new base:** main `b7d7233`. Since `6bb61df` it carries #460 (proxy only) and #457 (the pre-build patch scripts). Neither changes `src/`, `include/` or `platformio.ini`. It was **merged in** (`8946254`), not rebased.
- **The re-run:** every round again, in a fresh output directory, with A6.2's predictions **unchanged**, as A6.6 says.
- **New with #457:** each build's log prints one line per pre-build patch. Those lines are read and reported as the evidence of which patches ran in each env. A FATAL is a failed round, and the run stops.

### A6.8 Step 3.2's results: every measured row as predicted, apart from the ruled budgets miss (2026-10-21)

**The pair measured.**
- **Base:** main `b7d7233`.
- **Head:** `eb90e9a`, this branch with main merged in (A6.7).
- **Method:** one clone, `SOURCE_DATE_EPOCH=1760000000`, R1/R2 as in A5.6.
- **The tooling:** `tools/weatherscope/s32/s32.py` (committed in `b348623`, before any measured build), run round by round from a separate clone, so the tool never sat on the tree being checked out.

| row (A6.2) | predicted | measured |
|---|---|---|
| **S1**, the calibration | 0 differing bytes | **0** |
| **S1**, 15 other envs | identical outside R1/R2, 15/15 | **15/15.**<br>• R1/R2 differ by 64-65 B in the 13 envs that compile the edited shared files: their debug info moves, and so does the ELF hash.<br>• `animtest-s3-128` and `weatherscope-bench-s3-128` compile none of those files, and read **0 bytes total** |
| **S1**, the full-matrix sabotage | a change wherever `ConfigurationWebServer.cpp` compiles; `animtest` and the bench at 0 bytes | **15/15.**<br>• All 13 compiling envs changed size.<br>• The non-compiling set is exactly `['animtest-s3-128', 'weatherscope-bench-s3-128']`, both at 0 bytes |
| **S1**, the per-file sabotages | 6/6, or a file reported as having no site | **5/5 differ outside R1/R2:**<br>• `main.cpp` 5 B;<br>• `ConfigurationWebServer.cpp` 7 B;<br>• `BuildIdentity.h` 3 B;<br>• `DeviceIdentity.h` 5 B;<br>• `FactoryReset.h` 1,198,906 B (the changed literal moved the layout).<br>**`SoakHarness.h` reported as having no ungated site.** `s32.py sites` checked all three conditions, with a control |
| **S1**, files no build reads | all exit 0 | `skus.py --selftest`, `--validate`, `--check-readers` and `check_release_envs`: **all exit 0** |
| **S2** | 0 hits; the positive control, the anchor and the sabotage as written | **0 hits in the product.**<br>• Positive control: present in the radar (3/4/5/1), absent from Claudescope, Speedscope and Missileer.<br>• Anchor: the plant found (`local-url` ×1).<br>• Sabotage: finds and names `local-url` ×1, `radius-unit` ×3, `data-source` ×4 |
| **D2** | 0 hits; the anchor found | **0 hits;** the anchor found `weather.gov` ×1. The bench image reads 0 lowercase and one `MRMS`, as recorded beforehand |
| **D1** | exactly one naming file; the host test with fakes; both sabotages red on their target | **`WxFeedFactory.cpp` alone.**<br>• The sabotage reports `WeatherscopeManager.cpp` as a second namer.<br>• The host test passes 7/7, and its sabotage fails exactly "the alerts: both served, in order" |
| **CI** (#462 at `eb90e9a`) | the product leg builds; the adsbdb CONTROL found at `ConfigMigration.cpp`, PASS; the OTA URL check PASS; every other leg as on main | **34 checks: 31 success, 3 skipped** (`promote`, `ship-alert`, `version`, which run only on releases).<br>• Product leg: `CONTROL found`, `PASS`. The site is the default, `src/ConfigMigration.cpp`, found by running the gate's own `plant_target` (the bench resolves to its own file as the control).<br>• `check_ota_url.py`: selftest, wiring and the image all PASS |
| **Budgets** | flash 1.60-1.85 MB, RAM 55-70 KB | **the miss of A6.4,** ruled in A6.5. The head's sizes are **unchanged** from `4df1065`'s: flash 1,518,635 B, RAM 54,600 B |

**The pre-build patch lines (#457), read from all 31 base and head logs:**
- **The two LovyanGFX-only envs** print "not a dependency" for ESPAsyncWebServer and WiFiManager, and the LCD_CAM patch already in place.
- **The other 29,** the product included, print all three patches in place.
- **No FATAL** anywhere.

**A correction to A6.2's text, not a regrade.**
- **The error:** its quoted positive-control counts, "3, 3, 4 and 1 in S1 v2's `base2` image", were wrong when written.
- **The evidence:** that image, rescanned today, reads **3, 4, 5 and 1**, the same as this base. The source counts are identical at `bf7ece6` and `b7d7233`.
- **The alternative excluded:** a change in the image or the source between then and now. The rescan of the same file rules it out.
- **The most likely cause:** I counted lines containing each string rather than occurrences. This is not re-established.
- **What stands:** the graded criterion is presence, and it holds.

**The board, `907069326E64`.** Its port was resolved from the serial (COM6). It ran the head's product image.

| check | predicted | measured |
|---|---|---|
| the flash | (a precondition) | `pio run -t upload` exit 0. esptool read MAC `90:70:69:32:6e:64`. `boot_app0` was written to 0x1E000, the big-NVS table's otadata (A6.3) |
| the banner | `[build] env=weatherscope-s3-128` | **`[build] env=weatherscope-s3-128 fw=v18 features=no-cloud+weatherscope`** |
| the mDNS name | `Weatherscope-XXXXXX` | **`hostname=Weatherscope-326E64`**; the config server announces `http://Weatherscope-326E64.local` |
| Wi-Fi | joins with the stored credentials; the portal would be a stop | **joined with the stored credentials** (fast join, 5.8 s); no portal |
| `GET http://<name>.local/` | 200, the Weatherscope form, none of S2's strings | **200 by the mDNS name**, measured after the reset described below. <br>• The title is "Configure Weatherscope".<br>• The named controls are exactly `latitude`, `longitude`, `autodim`, `brightness`, plus the `cfg-form` marker.<br>• S2's four strings appear **0** times.<br>• No placeholder is left unsubstituted.<br>• The build id reads `weatherscope-s3-128 · v18 · no-cloud+weatherscope` |

**Seen on the board, not graded:**
- `[wx] feeds: crop 4033 B Ok 174x208 frame=1716328842 | obs valid=1 t=1792619700 present=0 | alerts=1 first=Tornado Warning`. So the factory path answers on the device.
- `[ota] channel=weatherscope-s3-128 current=18 latest=18`. The device read `version.txt` and fetched nothing: **O6's first half, observed, not forced.** The 404 half waits for the next real version bump, as section 3 says.

**An instrument event, recorded and not graded.**
- **What happened:** the first `GET` timed out. ARP for the board was `Incomplete` while the router answered, so the board was off the network. This came after the single `bench-trace.ps1` run had ended normally at 02:43:39Z (not killed), its verdict clean.
- **Two explanations:**
  - **(a)** the trace's close dropped DTR/RTS in an order the USB-Serial-JTAG reads as "reset into download mode". That is the effect already known from a *killed* trace on E2:08.
  - **(b)** the app left the network on its own after 02:43:39Z.
- **The recovery:** `esptool read_mac` (no writes, then a hard reset) brought the app back at 02:47:19Z, and the page then read as predicted.
- **The test against (b):** at 02:52:54Z, untouched, the board still answered (TCP up, `GET /` 200). That fits (a) and argues against (b) recurring on a timer. It does **not** exclude a one-off (b).
- **What would settle it, proposed and not run:** open and close a trace with the board's network watched, and see whether it drops.
