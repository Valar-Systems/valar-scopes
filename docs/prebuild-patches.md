# The pre-build library patches in release images: the audit, and making a missed patch fail

Reviewer rulings, 2026-10-21:
1. **First:** report whether every release image carried its patches. That covers (a) each v18 and v17 build leg's line from each patch script, (b) any image shipped with a patch skipped, and (c) a cross-check that doesn't rely on the logs.
2. A patch that can't apply fails the build, in CI and locally. Predictions are frozen first, with a sabotage.

The three patches are `[common]`'s `pre:` scripts (`platformio.ini:82-86`). Each targets one file under `.pio/libdeps/<env>/`, and each prints a skip line and carries on when that file isn't there:

| script | library file | what it changes | skip line |
|---|---|---|---|
| `patch_async_buff.py` | ESPAsyncWebServer `WebResponseImpl.h` | guards `ASYNC_RESPONCE_BUFF_SIZE`, so the `-D…=1024` override takes effect | `… not found yet; skipping` |
| `patch_wm_copy.py` | WiFiManager `wm_strings_en.h` | three customer-facing strings in the setup portal | `… not found; skipping` |
| `patch_lgfx_rgb_intr.py` | LovyanGFX `Bus_RGB.cpp` | makes the LCD_CAM interrupt shared. The script's own comment says a missed patch means a dead-display OTA image | `… not found yet; skipping` |

## 1a, 1b: the release logs (read 2026-10-21; findings, not predictions)

Read from the release runs' logs: v18 run 39464041512 at `672eab7`, and v17 run 38627256599 at `523cd73`. Every patch line in each, grouped by leg:

| leg | `patch_async_buff` | `patch_wm_copy` | `patch_lgfx_rgb_intr` |
|---|---|---|---|
| 14 of 15, in both releases, including the only shipped one, `blipscope-s3-128` (slug `s3-128`) | `guarded ASYNC_RESPONCE_BUFF_SIZE` | `3 applied, 0 already patched` | `made the LCD_CAM interrupt shared` |
| `animtest-s3-128`, in both | `not found yet; skipping` | `not found; skipping` | `made the LCD_CAM interrupt shared` |

- **The 14 legs:** every patch applied in each leg's first build. On a fresh runner, PlatformIO installed `lib_deps` before the `pre:` scripts ran.
- **No `FATAL`, and no `WARNING -- pattern NOT FOUND`**, in either run.
- **`animtest-s3-128`'s two skips leave nothing unpatched.** Its `lib_deps` is `lovyan03/LovyanGFX @ 1.2.21` only, so ESPAsyncWebServer and WiFiManager are not in that build at all. It is also a build-only harness row that publishes nothing.

**1b:** no image shipped in v18 or v17 had a patch skipped.

## 1c: the cross-check that doesn't rely on the logs (predictions frozen before either is run)

**What I read before freezing this,** about the instrument and not the result. The v18 asset `firmware-s3-128.bin` (1,928,192 B, sha256 `5a5f662b…`) embeds **37 framework source paths** beginning `/home/runner/.platformio/packages/framework-arduinoespressif32…` (`__FILE__` in framework macros). It embeds no project path. So **a plain Windows build cannot be identical outside the stamps, patched or not.** Its paths are `C:\Users\DanielFrenkel\.platformio\…`, a different length, which shifts the image. The comparison as ruled could not fail in the way that matters, so C2 equalises the path length first. C1 is a direct content check that needs no build at all.

### C1: the patched strings are in the v18 image

**The read:** the v18 asset, searched for each of `patch_wm_copy`'s three replacements and each of the three originals it replaces.
- patched: "Connecting your Blipscope to the network.", `<dt>SDK</dt>`, "Set up Blipscope"
- originals: "Trying to connect ESP to network.", `<dt>ESP-SDK/IDF</dt>`, "Config ESP"

**The anchor:** `[build] env=` must be found first, or the read is BLIND.

**The control, so that absence means something:** the pristine library's `wm_strings_en.h` (WiFiManager 2.0.17 as downloaded, unpatched) must contain all three originals. Otherwise the originals aren't what an unpatched image would carry.

| outcome | meaning |
|---|---|
| **3 patched, 0 originals** (predicted) | `patch_wm_copy` reached the shipped image |
| 0 patched, 3 originals | the shipped image is unpatched: stop and report |
| anything else, or the anchor or control fails | the instrument or the model is wrong: stop |

C1 covers only `patch_wm_copy`. The other two patches change code, not strings, so C2 is their check. For s3-128 the RGB patch has no effect anyway, because its panel is SPI.

### C2: a warm local build of `672eab7` against the v18 asset, the stamp regions listed first

**The build:**
- **Where:** a fresh worktree at `672eab7`, env `blipscope-s3-128`, no `PLATFORMIO_BUILD_FLAGS`.
- **The core path:** `PLATFORMIO_CORE_DIR` is set to a directory junction **of exactly the length of `/home/runner/.platformio` (24 characters)**, `C:\hrr\runnr\.platformio`, pointing at the real core dir. The embedded framework paths then have CI's length, and can differ only in their prefix and their separators.
- **The date:** `SOURCE_DATE_EPOCH=1760979600` (2026-10-20T17:00:00Z), so `__DATE__` is CI's date and only the time digits can differ.
- **Warm:** build #1 installs and patches the libraries. **Build #2 is the one compared.** Its patch lines must read already-patched: `patch_wm_copy: 0 applied, 3 already patched`, `LCD_CAM interrupt already shared`, and nothing from `patch_async_buff`. Each patched file is also read on disk.
- **The reproducibility control:** `pio run -t clean`, then build #3, which must be byte-identical to #2, or differ only inside R1-R4. If it doesn't, local builds aren't reproducible and C2 is uninformative.

**The stamp regions, listed before any diff:**

| region | where | why it differs |
|---|---|---|
| R1 | `esp_app_desc_t.app_elf_sha256`, 32 B at offset 0xB0 | the ELF carries paths and stamps |
| R2 | the image checksum byte, at size − 33 | follows every other byte |
| R3 | the trailing SHA-256, the last 32 B | follows every other byte |
| R4 | each `__DATE__ " " __TIME__` string (20 B), located in the v18 asset by its pattern | build time |
| R5 | each of the 37 embedded `…/.platformio/packages/…` path strings, located in the v18 asset | prefix (`/home/runner` against `C:\hrr\runnr`) and separators only, at equal length |

**Outcomes:**

| outcome | meaning |
|---|---|
| **(i) the same size, and every differing byte inside R1-R5** (predicted) | identical outside the stamps: the v18 image was built patched |
| (ii) the same size, with bytes differing outside R1-R5 | **stop.** The bytes are listed. An unpatched release is one explanation; another input difference (a package, a flag) is the other. Neither is concluded from this alone |
| (iii) a different size | **stop.** The equal-length assumption failed (for example, PlatformIO resolved the junction to the real path) or a package differs. Inconclusive, and not evidence about the patches |
| (iv) the reproducibility control fails, or anything else | the instrument is unreliable at this scale: stop |

## 1c results

### C1: OUTSIDE THE PREDICTED SET (read 2026-10-21). Recorded, not re-graded

The anchor was found, and the control passed: the pristine WiFiManager 2.0.17 `wm_strings_en.h`, from the library's `v2.0.17` tag, contains all three originals.

| pair | patched in the v18 asset | original in the v18 asset |
|---|---|---|
| the portal's save message | **1** ("Connecting your Blipscope to the network.") | 0 |
| the browser tab title | **1** ("Set up Blipscope") | 0 |
| the info-page SDK row | **0** (`<dt>SDK</dt>`) | **0** (`<dt>ESP-SDK/IDF</dt>`) |

**The predicted outcome was "3 patched, 0 originals".** This is "anything else", so C1 stopped, and per its own rule the instrument or the model was wrong.

**The model was wrong for the third pair. The diagnosis, read after the stop, is kept apart from the verdict:**
- WiFiManager 2.0.17 defines `HTTP_INFO_aboutsdk` (`wm_strings_en.h:220`) and **never references it.**
  - The info page asks for `aboutidfver` (`WiFiManager.cpp:2063`), whose branch is commented out (:2295-2299).
  - The `aboutsdkver` branch uses a different string, `HTTP_INFO_sdkver` (:2301-2302).
- So the linker drops that string whether or not it was patched, which is why it is in the image in neither form. Its three neighbouring rows (`<dt>WiFiManager</dt>`, `<dt>Arduino</dt>`, `<dt>Build date</dt>`) are present.
- **For that pair the image can't distinguish patched from unpatched.** For the two pairs that are linked, the image carries the patched text and not the original.

Whether C1 may be read as "`patch_wm_copy` reached the v18 image" on the two linked pairs is for the reviewer to rule. It is not graded here.

## 2. The fix: a patch that can't apply fails the build (predictions frozen before any code)

**Ruling:** "a patch that can't apply fails the build, in CI and locally. Better still, install the library before patching instead of skipping. Freeze predictions first, with a sabotage: the fresh-runner path must go red, never build unpatched."

**What 1a measured, which shapes the design.** On a fresh runner, PlatformIO installs an env's `lib_deps` **before** the `pre:` scripts run. All 28 fresh legs whose env depends on the library printed "applied" on their first build. The only skips were `animtest-s3-128`'s, for libraries that aren't in its `lib_deps`.

So "not found" means one of two different things, and the scripts currently treat them the same:
1. **The library isn't a dependency of this env.** There is nothing to patch, and the build is correct.
2. **The library is a dependency, but the file is missing.** The install is broken, and building would ship unpatched code.

**The change, the same in all three scripts:**
- **The file is missing and its library is in this env's `lib_deps`** (read with `env.GetProjectOption("lib_deps")`): **FATAL** (`env.Exit(1)`), naming the file and the remedy (`pio pkg install -e <env>`). It is never built around.
- **The file is missing and the library isn't in `lib_deps`:** an explicit line, `<lib> is not a dependency of <env>; nothing to patch`. The build continues.
- **`patch_wm_copy`'s `WARNING -- pattern NOT FOUND`** becomes **FATAL**. A copy fix that can't apply is a patch that can't apply. The other two scripts already FATAL on anchor drift.
- **`patch_async_buff`'s already-guarded path** prints a line (`already guarded`) instead of nothing, so every path is visible in a log.

**Why no in-script install (the "better still"), for the reviewer to rule on.** PlatformIO already performs that install before the scripts run; measured above. An in-script `pio pkg install` would nest a second `pio` inside the build, for the one case where the install has already happened and a file then went missing. There, a FATAL with the remedy is the honest outcome. If the reviewer wants the install anyway, it's an addition on top of this, not a replacement.

| | check | predicted |
|---|---|---|
| **F1** | a fresh worktree (no `.pio`), `pio run -e blipscope-s3-128` | green; `guarded ASYNC_RESPONCE_BUFF_SIZE`, `3 applied, 0 already patched`, `made the LCD_CAM interrupt shared`; no `skipping`, no `not a dependency` |
| **F2** | the same tree, built again (warm) | green; `already guarded`, `0 applied, 3 already patched`, `LCD_CAM interrupt already shared` |
| **F3** | `pio run -e animtest-s3-128` | green; ESPAsyncWebServer and WiFiManager each `not a dependency of animtest-s3-128; nothing to patch`; LovyanGFX applied or already shared |
| **F4** | CI on the PR, all 15 legs | green. 14 legs print F1's three lines and `animtest-s3-128` prints F3's. No `skipping` anywhere |

**The sabotages:** the fresh-runner path must go red, never build unpatched. Each runs on a warm s3-128 tree with **one** target file removed, then rebuilds, simulating a dependent library absent at `pre:` time:

| | removed | predicted |
|---|---|---|
| **S-F1a** | `ESPAsyncWebServer/src/WebResponseImpl.h` | `[patch_async_buff] FATAL …`; the build fails; `firmware.bin` is unchanged (same sha256 and mtime as before the rebuild) |
| **S-F1b** | `WiFiManager/wm_strings_en.h` | `patch_wm_copy: FATAL …`; the build fails; `firmware.bin` unchanged |
| **S-F1c** | `LovyanGFX/src/lgfx/v1/platforms/esp32s3/Bus_RGB.cpp` | `[patch_lgfx_rgb_intr] FATAL …`; the build fails; `firmware.bin` unchanged |
| **S-F2** | none removed. One of `patch_wm_copy`'s find strings is altered in the header, and its replacement is absent | `patch_wm_copy: FATAL …` (today: a WARNING and a green build); the build fails |

**Outside the set:**
- **If a rebuild silently re-creates a removed file** (PlatformIO repairing the package), the removal isn't a valid simulation of absence. **Stop:** the sabotage needs redesigning, and nothing is concluded.
- **If F3 goes red,** the "is it a dependency?" read is wrong. Stop.

### C2: outcome (iii), the sizes differ. STOPPED and inconclusive, as frozen (read 2026-10-21)

**The build conditions held:**
- Build #1 applied all three patches. Build #2 read already-patched (`0 applied, 3 already patched`, `already shared`, and `patch_async_buff`'s silent already-guarded path).
- All three files were patched on disk.
- The local image embeds `C:/hrr/runnr/.platformio/…`: forward slashes, the same 24-character length as CI's prefix, and no `C:\Users\…` path.

| | |
|---|---|
| reproducibility control, #2 against #3 | **0 differing bytes.** Both are sha256 `e104e082…` |
| sizes | v18 asset **1,928,192 B** (`5a5f662b…`), local #2 **1,928,176 B**: 16 B shorter |
| verdict | **(iii): stop.** Not evidence about the patches either way |

**An error in my own instrument, recorded:** the frozen `SOURCE_DATE_EPOCH=1760979600` was described as 2026-10-20T17:00:00Z. It is **2025-10-20T17:00:00Z**. It changes only the `__DATE__` digits, at the same length, so it can't produce a size difference. It is still a defect in the setup.

**The diagnosis, read after the stop and kept apart from the verdict:**
- Every printable string of 6 or more characters matches between the two images once the core prefix is mapped, apart from the date and time strings.
- The other differing fragments are non-string bytes that look like shifted addresses. So the 16 B is in code or data layout, not strings.

**The two alternatives, neither excluded by this data:**
- **(a) an unpatched release.** For example, `ASYNC_RESPONCE_BUFF_SIZE` at 2,872 instead of 1,024 no longer fits an immediate and needs a literal-pool entry, so the code grows. That would contradict 1a's logs, which C2 was meant to stand without.
- **(b) a link-order or host-toolchain difference** between a Windows/NTFS build and a Linux/ext4 one. Object and archive ordering can move alignment padding by a few bytes.

**What would separate them,** proposed and not run: rebuild `672eab7` warm **on a GitHub Linux runner at CI's own paths**, in a throwaway PR job that uploads nothing, and compare it with the v18 asset, stamp regions listed first. CI-against-CI rebuilds were measured identical outside 75 stamp bytes (CLAUDE.md, v9), so that comparison can tell (a) from (b). It is for the reviewer to rule.
