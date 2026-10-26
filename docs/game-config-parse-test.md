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
