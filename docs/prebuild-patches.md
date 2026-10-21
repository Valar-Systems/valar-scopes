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

## 2. Results: local (2026-10-21, before CI)

Graded on `2aac2e3`. Driver `run_fix_checks.py` (scratchpad); every `pio run`'s whole output was kept in its own log.

| check | read | verdict |
|---|---|---|
| **F1**, fresh tree (no `.pio`) | exit 0; `guarded ASYNC_RESPONCE_BUFF_SIZE`, `3 applied, 0 already patched`, `made the LCD_CAM interrupt shared` | as predicted |
| **F2**, warm | exit 0; `already guarded`, `0 applied, 3 already patched`, `already shared` | as predicted |
| **F3**, `animtest-s3-128` | exit 0; `ESPAsyncWebServer is not a dependency of animtest-s3-128`, `WiFiManager is not a dependency of animtest-s3-128`; LovyanGFX applied | as predicted |
| **S-F1a**, `WebResponseImpl.h` removed | exit 1, `[patch_async_buff] FATAL …`; `firmware.bin` unchanged (`f7654560…`); the rebuild did not re-create the file | as predicted |
| **S-F1b**, `wm_strings_en.h` removed | exit 1, `patch_wm_copy: FATAL …`; `firmware.bin` unchanged; not re-created | as predicted |
| **S-F1c**, `Bus_RGB.cpp` removed | exit 1, `[patch_lgfx_rgb_intr] FATAL …`; `firmware.bin` unchanged; not re-created | as predicted |
| **S-F2**, a replacement string drifted | exit 1, `patch_wm_copy: FATAL -- pattern NOT FOUND …` (was a WARNING in a green build); `firmware.bin` unchanged | as predicted |

After each sabotage the file was restored from its saved copy and the rebuild was green. Every plant asserted that it took (the removed file was gone; the drift anchor appeared exactly once and the file changed). **F4 is CI on this push.**

## 2. Results: CI, F4 (run 39668873359 on `4a8c171`): OFF-PREDICTION on the count. Recorded, not re-graded

CI was counted with `per_page=100` against `total_count`: 33/33, 30 success, 3 release-only skipped, 0 failing. Every leg's patch lines:

| legs | `patch_async_buff` | `patch_wm_copy` | `patch_lgfx_rgb_intr` |
|---|---|---|---|
| **13** build legs (`blipscope-pro-s3-175-amoled`, `blipscope-pro-s3-21`, `blipscope-s3-128`, `blipscope-s3-146`, `claudescope-s3-146`, `missileer-photo`, `missileer-s3-128`, `missileer-s3-146`, `orbitscope-s3-146`, `quakescope-s3-146`, `quillscope-s3-146`, `reelscope-s3-146`, `speedscope-s3-146`), plus `host-tests` | `guarded …` | `3 applied, 0 already patched` | `made … shared` |
| `animtest-s3-128` | `not a dependency of animtest-s3-128` | `not a dependency of animtest-s3-128` | `made … shared` |
| **`weatherscope-s3-128`** | `not a dependency of weatherscope-s3-128` | `not a dependency of weatherscope-s3-128` | `made … shared` |

**0** `skipping` lines and **0** `FATAL` lines anywhere in the run.

**The frozen F4 said** 14 legs would print F1's three lines and `animtest-s3-128` alone would print F3's. **The miss:** the Weatherscope bench env also declares LovyanGFX as its only library, as `platformio.ini`'s own comment on it says ("LovyanGFX only"). So it prints the explicit not-a-dependency lines too. It isn't a skip of a needed patch: the libraries aren't in that build. The prediction undercounted the envs without those libraries.

Nothing is changed for this. Whether F4 may be read as satisfied in substance (no skip, no FATAL, every env that depends on a library patched it) is for the reviewer to rule.

## Rulings of 2026-10-21, and what they set in motion

- **C1:** stays outside its predicted set on the record. **Ruled:** C1 shows `patch_wm_copy` reached the v18 image on the two linked pairs. The third pair is uninformative.
- **C2 (iii):** stays inconclusive until the separator below runs.
- **F4:** a miss on the count, recorded and not re-graded. The substance holds: no skip, no FATAL, and every env that uses a patched library patched it.
- **The tightening:** accepted, as below.
- **The audit's answer, for the record:** v17 and v18 shipped with all three patches applied.

## C2b: the separator, a warm rebuild on a Linux runner (predictions frozen before it runs)

**The SOURCE_DATE_EPOCH correction:** C2 used `1760979600` and described it as 2026-10-20T17:00:00Z. It is **2025**-10-20T17:00:00Z. C2b uses **`1792515600`, which is 2026-10-20T17:00:00Z**, the day v18 was built. Both values were checked by converting them back.

**The job:**
- **Where it runs:** a scratch branch `scratch/c2b-linux-rebuild` carrying one workflow, `on: push` to that branch only. No pull request is opened and no release event fires, so `firmware.yml` does not run. The job has `permissions: contents: read` and **uploads nothing**: no artifact, no release asset. The branch is deleted after the read.
- **The runner:** `ubuntu-latest`, checked out at `/home/runner/work/valar-scopes/valar-scopes` like the release run, building `672eab7`, env `blipscope-s3-128`, with `pip install platformio` as in `firmware.yml`.
- **The builds:** build #1 (fresh), build #2 (warm), then `pio run -t clean` and build #3, all with the date fixed. The patch lines of #1 and #2 are printed.
- **The comparison:** the v18 asset is downloaded read-only (`gh release download v18 -p firmware-s3-128.bin`). C2's comparison then runs, the stamp regions listed from the asset first: R1 `app_elf_sha256`, R2 the checksum, R3 the trailing sha256, R4 the date-time strings, R5 the framework paths.

**Predicted:**
- **The patch lines:** #1 prints `guarded …`, `3 applied, 0 already patched` and `made … shared`. #2 prints the already-patched lines (`672eab7`'s scripts are the old ones, so `patch_async_buff` is silent).
- **The control:** #2 and #3 are byte-identical.
- **The comparison:** #2 and the v18 asset have the **same size (1,928,192 B)**, and **every differing byte lies in R1-R4**. R5 has **0** differing bytes, because the paths are identical on the same runner layout.

| outcome | meaning |
|---|---|
| **(i) the same size, every difference in R1-R4** (predicted) | identical outside the stamps: **C2 closes as "patched"** |
| (ii) the same size, with bytes outside R1-R4 | stop: list them |
| (iii) a different size | stop: inconclusive |
| (iv) the control fails, or a build fails, or anything else | the instrument: stop |

## The tightening (ruled 2026-10-21; predictions frozen before its code)

**The rule:**
- **A library counts as declared** when its name appears **anywhere** in a `lib_deps` entry, case-insensitive. That covers `owner/Name @ ver`, a bare `Name`, and git or zip URLs.
- **Also (the optional part, which is cheap):** a library counts as present when its folder exists under `.pio/libdeps/<env>/`, so a library pulled in by another one can't be skipped.

A wrong match can then only ever stop the build with a fix-it message, never skip a patch.

| | check | predicted |
|---|---|---|
| **T1** | F1-F3 rerun on the new code (fresh, warm, `animtest-s3-128`) | the same lines as before; `animtest`'s two "not a dependency" lines are unchanged, because neither library's name is in its `lib_deps` and neither folder exists |
| **T2** | `lib_declared` as a pure function, on the spec forms (a registry `owner/Name @ v`, a bare `Name`, `https://github.com/tzapu/WiFiManager.git#v2.0.17`, a zip URL, and an unrelated library) | declared for the first four; not declared for the unrelated one |
| **S-T1** | **a git-URL WiFiManager entry with its header removed**: `[env:blipscope-s3-128]`'s `lib_deps` given `https://github.com/tzapu/WiFiManager.git#v2.0.17` in place of the registry spec, built once, then `wm_strings_en.h` removed and rebuilt | **FATAL**, and the build fails. *Control, the hole it closes:* the **same** setup on the current rule (`4c3f131`) reads `WiFiManager is not a dependency …` and builds **green**, which is a silent skip |
| **S-T2** | **a fork installed under a different folder name**: on a warm tree, `.pio/libdeps/blipscope-s3-128/ESPAsyncWebServer` renamed to `ESPAsyncWebServer-fork`, then rebuilt | **FATAL** (the library is declared by its name in the spec, and the target path is missing); the build fails |

**Outside the set:**
- **If a rebuild re-installs the renamed or removed library** (PlatformIO repairing it), that sabotage isn't simulating what it claims: stop and redesign it.
- **If T1 moves `animtest` or `weatherscope-bench`,** the presence rule is reading something unexpected: stop.
