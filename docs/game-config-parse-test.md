# ParseGameConfig gets a host test that reads the served config (predictions first)

**Why.** #482 regenerated `test/fixtures/game-config.json` from version 1 to version 2. Everything that moved was in
the `config` block: the weights became integer `ppm`, and `autoDecodeS` and `maxClockSyncAgeS` were added.
- **No test reads that block.** `test_game_format.cpp` reads the header's constants and strings.
- **`ParseGameConfig`** (`src/eam/EamModels.cpp`), the device's parser for that block, has **no host test**.
- **So the gap was invisible:** the v1 config this repo carried until 2026-10-25 is one that parser refuses, and nothing
  said so.

**The ruling (reviewer chat, pasted by Daniel, 2026-10-26), verbatim:** "a ParseGameConfig host test, as its own PR
after #482, predictions first.
- Move GameConfig, ReadU32 and ParseGameConfig into their own files with no Arduino.h (they use no String). A pure
  move; EamModels.h includes the new header. Predict that every env builds, with a size band for the missileer envs.
- Take ArduinoJson from .pio/libdeps (the 7.4.3 copy the firmware builds with), not a vendored copy: version parity
  by construction, and the same pattern as the Wi-Fi QR test. The -I goes on this test's binary only, with the reason
  written next to it, as FIXTURE_INCLUDES does. Missing is BLIND (exit 2). Confirm CI's host-tests build installs it.
- Rows: the v2 fixture's config parses, every field equal to the JSON (weights in served order, both tiers,
  ackCutoffS 60, autoDecodeS 300, maxClockSyncAgeS 3600, windowUs 2,000,000, bucketUs 200,000, epoch 1). The v1
  config block, copied from git history into its own test input, is refused.
- Sabotages, predicted red: the autoDecodeS read removed (the v2 row); a missing ppm read as 0 (the v1 row)."

## The change

1. **A pure move.** `struct GameConfig` (with its comment), `ReadU32` and `ParseGameConfig` go from `EamModels.h/.cpp`
   to new **`src/eam/GameConfig.h` / `GameConfig.cpp`**, which include ArduinoJson and `../game/Derive.h`, and **no
   `Arduino.h`**. `EamModels.h` includes `GameConfig.h`. No other line of `EamModels` changes.
2. **`test/host/test_game_config.cpp`**, built from `src/eam/GameConfig.cpp` and that file alone.
3. **`test/host/run.sh`, a new last block.**
   - **The include:** `JSON_INCLUDES` is `.pio/libdeps/*/ArduinoJson/src`, the copy PlatformIO installed from
     `platformio.ini`'s pin, and it goes on this binary only, with the reason written beside it.
   - **Missing is BLIND:** exit 2.
   - **Parity:** the block passes the test the pin it parses from `platformio.ini`, and the test refuses (exit 2) when
     `ARDUINOJSON_VERSION` differs. That makes "parity by construction" a check, not a comment.
   - **Why last:** a BLIND there leaves no earlier block unrun.
4. **`test/fixtures/game-config-v1.config.json`:** the v1 `config` block, copied from git history
   (`303d57d:test/fixtures/game-config.json`) with its provenance. It is frozen and is never regenerated.
   `test/fixtures/README.md` says so.

**CI already installs ArduinoJson, so no workflow change.** The `host-tests` job runs `pio run -e blipscope-s3-128`
before the suite. That env's `lib_deps` (from `[common]`) include `bblanchon/ArduinoJson @ 7.4.3`, the only ArduinoJson
pin in `platformio.ini`, so a build leaves `.pio/libdeps/blipscope-s3-128/ArduinoJson`. Locally,
`pio pkg install -e blipscope-s3-128` produced exactly that directory, and its `version.hpp` reads `"7.4.3"`.

**Which envs can this touch?** The three that compile `src/eam`: `missileer-s3-146`, `missileer-s3-128` and
`missileer-photo`.
- They are the only envs whose resolved `build_src_filter` (from `pio project config`) contains `+<eam/>`.
- They are also the only envs that define `FEATURE_EAM`, the one gate on `src/main.cpp`'s only EAM include.
- The other 40 envs compile no file this changes.

## Predictions

| | check | predicted |
|---|---|---|
| **M1** | the move, read as text | the moved lines are **identical**, line endings normalised: the struct with its comment, and `ReadU32` plus `ParseGameConfig`. `EamModels.h/.cpp` lose exactly those lines, and `EamModels.h` gains one `#include "GameConfig.h"`. Neither new file includes `Arduino.h` |
| **M2** | the three envs that compile `src/eam`, built on `main` and on this branch | all build. **`firmware.bin` within 256 B of `main`'s**; PlatformIO's "RAM: used" **unchanged** |
| **M3** | the other 40 envs | they compile nothing this changes. That is an argument from the resolved config, **not a local build**. CI's 17-row matrix builds 14 of them, plus all three above |
| **T1** | the v2 fixture's `config` through `ParseGameConfig` | **true**, with `valid` set. **Every field equals the JSON**, read separately by the test, and also the values named here: weights in served order nam 830000 / fdm 120000 / execution 50000 (`n_weights` 3); tiers normal 900000 [300, 900) / snap 100000 [90, 150) (`n_tiers` 2); `ackCutoffS` 60, `autoDecodeS` 300, `maxClockSyncAgeS` 3600, `windowUs` 2000000, `bucketUs` 200000, `epoch` 1, `hold` false |
| **T2** | the v1 `config` block | **refused**: false, with `valid` false. **CONTROL:** the input is the v1 shape (version 1, three weights each with a `weight` and no `ppm`), so an empty or wrong file cannot pass this row by being refused |
| **T3** | the v1 input's provenance | its `config` **equals** the `config` in `git show 303d57d:test/fixtures/game-config.json` |
| **T4** | ArduinoJson parity | the test prints **ArduinoJson 7.4.3**, equal to the pin. **CONTROL:** the binary run by hand with pin `7.4.2` **exits 2** |
| **T5** | BLIND | with `.pio/libdeps/*/ArduinoJson` renamed away, the block prints **BLIND** and `run.sh` **exits 2**; renamed back afterwards |
| **S1** | sabotage: the `autoDecodeS` read removed | **T1 red**, on exactly its two `autoDecodeS` checks (against the JSON and against 300). T2 green |
| **S2** | sabotage: a missing `ppm` read as 0, in **both** reads (weights and tiers) | **T2 red** (v1 now parses), on its two checks. T1 green |
| **S2′** | a probe, **not a sabotage expected red**: only the weights' `ppm` read made lenient | **T2 stays green**, because v1's `tOffset` has no `ppm` either and still refuses it. This states a limit before it is seen: the v1 row cannot tell which of the two reads regressed |
| **H1** | the whole suite: WSL `Ubuntu-24.04` g++ 13.3 (pre-push evidence) and MinGW 5.1 (the fast loop) | **ALL HOST TESTS PASSED**, and every earlier block reports the same as on `main` |
| **C1** | CI on this PR | **0 failures**. `host-tests` runs the new block, and it reads ArduinoJson 7.4.3 rather than BLIND |

Every plant is restored **by bytes**, from a copy, with a sha256 check.

**Outside the set:** stop and report.

## Results (local). TWO ROWS ARE OFF-PREDICTION, and measuring stopped there

| | read | verdict |
|---|---|---|
| **M1** | read as text against `origin/main`: `EamModels.h` lost 20 lines and gained only the include; `EamModels.cpp` lost 70 lines; all of them appear verbatim in `GameConfig.h` / `GameConfig.cpp`; neither new file includes `Arduino.h` | **as predicted** |
| **M2** | **all three build, and RAM is unchanged** (56008 / 90648 / 90608 B). **`firmware.bin` is +576 B** (`missileer-s3-146`), **+592 B** (`-s3-128`) and **+672 B** (`-photo`), against a band of 256 B | **OFF-PREDICTION** (see below) |
| **M3** | an argument, not a build; nothing ran for it | unchanged |
| **T1, T2** | MinGW 5.1 fast loop, and inside the WSL g++ 13.3 suite: **53 checks, 0 failures** | as predicted |
| **T3** | the `config` in `game-config-v1.config.json` **equals** the `config` in `git show 303d57dc3284:test/fixtures/game-config.json` | as predicted |
| **T4** | prints `ArduinoJson 7.4.3 (platformio.ini pins 7.4.3)`. **Control:** pin `7.4.2` gives exit **2** (an empty pin also exits 2) | as predicted |
| **H1, WSL** | **HOST TESTS FAILED, exit 1.** The one FAIL is the `== the same TU, for the board ==` block, which says "no xtensa toolchain found": `run.sh` looks only under `$HOME/.platformio`, and WSL's `$HOME` has none. **Control: `main`'s tree (`wt-gcfix`, tree `09335a4747c8`), run in the same WSL, fails identically.** Every result line is the same, apart from this branch's added `53 checks, 0 failures` | **OFF-PREDICTION, in the rig.** It fails on `main` too |
| **T5, S1, S2, S2′, H1 MinGW, C1** | not run: measuring stopped at the two misses above | **UNRUN** |

**M2: why it grew, read from the artifacts already built, and not re-measured.**
- **The setup:** `main`'s images were built in `wt-gcfix` (tree `09335a4747c8`, the same as `main`'s). This branch's
  were built in `wt-gcparse`.
- **The named alternative, excluded:** the different build directories, carried in as `__FILE__` strings. Neither image
  contains either directory name (0 hits each).
- **The cause, from `nm -S` on the two `missileer-s3-128` ELFs:** a net **+597 B** over 16 symbols.
  - GCC's IPA-SRA clones (`...$isra$0`) of ArduinoJson internals are local to their translation unit. With
    `ParseGameConfig` in a second TU that includes ArduinoJson, each one now exists **twice**:
    `ObjectData::getMember` +157, `CollectionData::createIterator` +38, `MemoryPoolList::getSlot` +31,
    `VariantData::getExtension` +22.
  - `VariantData::asFloat<float>` gets an out-of-line copy: **+243**.
  - The `operator|<float>` clone: +26.
  - Inlining moved inside the two parsers: `ParseGameConfig` +44, `ParseOpenSkyStates` +36.
  - The other 8 symbols differ by 1 to 4 bytes each, and net to 0. The 8 above account for all +597.
- **The fault was in the prediction, not the move.** The 256 B band assumed the linker would de-duplicate the template
  code the two files share. It does for COMDAT copies, but not for per-TU clones. No line of code changed meaning.

**H1's control needed a second attempt.** The first one ran `git rev-parse` inside WSL, which cannot follow a
worktree's Windows-path `.git` link, and the `&&` chain stopped before the suite started. That attempt measured
nothing. It was run again without the git step, with the tree hash taken from Windows git.

## Re-registered by ruling (reviewer chat, pasted by Daniel, 2026-10-26), before any further measurement

The ruling, as pasted:
- **M2:** "re-registered from the measured cause, the miss kept ... each missileer image grows over main's CI build at
  4b0bd07 by the measured +576 / +592 / +672 B, +- 16 B ... Name which size CI reports before you read it."
- **H1 (WSL):** "counted, with the board block recorded as a rig gap that main shares (your control). The miss stays
  recorded. Don't set up a board compiler in Ubuntu-24.04 for this PR: the board block's evidence comes from the MinGW
  full suite and CI. H1's MinGW half stays as predicted: ALL HOST TESTS PASSED."
- **What may run next:** T5, S1, S2, S2′ and the MinGW full suite; then the PR, and C1.

**Which size CI reports, named from `firmware.yml` before any CI log was read.**
- **The quantity:** the missileer legs are build-only (no `slug`), so CI uploads **no image** for them and records no
  `firmware.bin` size. What it reports is PlatformIO's line **`Flash: [...] (used N bytes from 6553600 bytes)`**, in
  the matrix leg's step **`Build <env>`**, the plain `pio run -e <env>`.
- **What not to read:** the later `adsbdb launch gate` step rebuilds with a plant, so its size lines are not this
  quantity and are not read.
- **Main's side:** push run `40258192095` on `4b0bd07`. **This PR's side:** the `pull_request` run on its head.

That quantity is not the one the ruling's numbers measured. +576 / +592 / +672 are `firmware.bin` deltas. The same
local builds' `Flash: used` deltas were **+568 / +592 / +664**, because the image adds alignment, a header and a
checksum on top. So:

| | check | predicted |
|---|---|---|
| **M2′** | per missileer env: the `Flash: used` N in this PR's `Build <env>` step, minus N in main's run `40258192095` | **within +-16 B of the ruling's +576 / +592 / +672**, and specifically **+568 / +592 / +664** (the local `Flash: used` deltas; the toolchain is pinned) |

**Outside the set:** a `Build <env>` step with no `Flash:` line is no measurement: stop. A delta outside the band:
stop.
