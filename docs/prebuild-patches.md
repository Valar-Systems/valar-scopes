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
