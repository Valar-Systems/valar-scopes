# The web renderer at 960, 1080 and 1280 px: the headless checks (registration; predictions before the code)

**Commissioned** by the owner's big-screens item (c), 2026-11-12 16:57Z, relayed by the reviewer: "Whoever owns
tools/web-renderer adds 960 and 1280 to its headless checks." At 17:06Z the owner added the 7" (1080 x 1080, round):
"Add it to the pictures and to the golden tests, beside 720, 960 and 1280." 720 is the P4 4"'s own model, from its own
variant. This page covers 960, 1080 and 1280.

This session wrote tools/web-renderer (#518, #523, #531, #629), so the item is this session's. The pictures (a) and the
touch proposal (b) are another session's.

**This page is committed before any code is written or any build is run.** The first version (7ee969fbfeed) was
accepted by the reviewer at 17:20Z with three changes, all made here: 1080 added; the big sizes run as parallel jobs;
and the note below on run-time size.

**These models are the interim.** The owner's build approach (17:03Z) makes the screen size a run-time value later. When
that lands, these three compile-time check models and the header branch below are retired. The checks then run at the
same sizes on one build.

## Today

The headless checks run once per built model, and each model draws at its variant's `SCREEN_SIZE`:

- `s3-128` at 240 px;
- `s3-21` at 480 px.

Those are the two names in `BUILT` (`.github/workflows/web-renderer.yml`). No check is written for one size. Each one
reads the size from the module (`_web_screen_size()`, which returns `variant::SCREEN_SIZE`, `web/web_gfx.cpp:71`):

- `smoke.mjs`: R3-R7;
- `carousel.mjs`: WC2's dots at `size - 16` and `size / 2 +- 6, 18`, and its swipes at 20 % and 80 % of the size. These
  are the firmware's own fixed offsets from `SCREEN_SIZE` (`DrawScreenIndicator`, `src/AircraftManager.cpp`);
- `airports_remote.mjs`: AF3;
- `scripts/check-web-only.sh --expect`: KI4.

On main (run 43167854623) the `build` job takes about 11 min. Per model: build ~43 s, smoke ~200 s, carousel ~11 s,
AF3 ~14 s.

## The change

| file | what |
|---|---|
| `include/variants/s3_21.h` | `SCREEN_SIZE` is `BLIPSCOPE_WEB_SCREEN_SIZE` when that macro is set, and 480 as today otherwise. The macro is refused outside the web build (`#error` unless `BLIPSCOPE_WEB_RENDERER`) and outside the set {960, 1080, 1280} (`static_assert`) |
| `tools/web-renderer/build.sh` | three models, `web-960`, `web-1080` and `web-1280`: `BLIPSCOPE_VARIANT_S3_21` plus `-DBLIPSCOPE_WEB_SCREEN_SIZE=<N>` |
| `tools/web-renderer/smoke.mjs` | **R0**: the model draws at the size it was built for. A `web-<N>` model expects N. Any other model expects the number its variant header states (`include/variants/<name with - as _>.h`, the one `constexpr int SCREEN_SIZE = <digits>;` line, from `SRC_ROOT` when set), read from that header, never retyped. `--expect-size=<N>` overrides it, for R0's control only. `--r0-only` runs R0 alone. `--r7-only` doesn't run R0 |
| `.github/workflows/web-renderer.yml` | **a new job, `big-sizes`**: a matrix over 960, 1080 and 1280, one leg per size, **on pushes and PRs only** (`if: github.event_name == 'push' \|\| github.event_name == 'pull_request'`). Each leg sets up its own toolchain from the same caches as `build`, builds its one model, and runs every headless check on it. It runs beside `build`, never after it. In `build`: one step, R0's control, on pushes and PRs only. `BUILT` and `PUBLISHED` don't change |

So each check model is the Pro 2.1's web model with one thing changed, its size. A check that reads differently there
than on `s3-21` can be put down to the size.

**Never published.** `publish` still `needs: build` only, and a release run skips `big-sizes` and R0's control. It
builds the same models and stages the same set as today. Its smoke step gains R0 on its two models, and nothing else
changes. The check models appear only in their legs' runs and are never uploaded.

**Why one branch in `s3_21.h`, and not a patched copy of it inside `build.sh`.** A copy would touch no device file. But
it is invisible from the header where the size is defined, and it depends on an anchor that can stop matching.
The branch says what it is, at the place the size lives, and the guards refuse it anywhere else. The cost is that a
device header changes, so the device image is shown unchanged (D1). Accepted by the reviewer, 17:20Z.

## Predictions

**Local, before the push (this tree, one path for both builds):**

| row | what | predicted | stop if |
|---|---|---|---|
| **D1** | `blipscope-pro-s3-21` (the one env that selects `s3_21.h`; `platformio.ini`) built at main `71156714ba44` and again with the change, in this same worktree; the two `firmware.bin` compared by the five-region comparator | **PLACED**: every differing byte inside the five rebuild regions (app ELF sha, checksum byte, trailing SHA-256, WiFiManager's two `__TIME__` strings) | OUTSIDE (a byte outside the five), or IDENTICAL (the second build did not rebuild) |
| **G1** | `g++ -fsyntax-only` on `s3_21.h` with `-DBLIPSCOPE_WEB_SCREEN_SIZE=960` and no `BLIPSCOPE_WEB_RENDERER` | refused, by the `#error` | it compiles |
| **G2** | the same with `-DBLIPSCOPE_WEB_RENDERER -DBLIPSCOPE_WEB_SCREEN_SIZE=720` | refused, by the `static_assert` | it compiles |
| **G3** | the same with `-DBLIPSCOPE_WEB_RENDERER` and each of `=960`, `=1080` and `=1280`, then with neither macro | all four compile (the control: G1 and G2 fail for their guard, not for the header) | any of the four fails |

The other device envs select another variant header in `Variant.h`, and don't read `s3_21.h`. That is stated from
`Variant.h`, not measured.

**CI, at the code commit (no local Emscripten; CI's web-renderer workflow is the instrument):**

| row | what | predicted |
|---|---|---|
| **B1** | each leg builds its model | `built web-960 v19 ...`, `built web-1080 v19 ...` and `built web-1280 v19 ...` (`build.sh`'s last line) |
| **R0** | the five models | `ok   R0`: s3-128 240, s3-21 480, web-960 960, web-1080 1080, web-1280 1280 |
| **R0-ctl** | `smoke.mjs web-dist/s3-21 --r0-only --expect-size=960`, in `build` | `FAIL R0`, exit 1; the step reads it as the control holding |
| **S1** | smoke R3-R7 in each leg | `WEB RENDERER SMOKE AS PREDICTED`, all three |
| **S2** | carousel WC2 and WC3 in each leg | every row `ok`; WC2's dots read `2` |
| **S3** | AF3 in each leg | `AF3: as predicted`, all three |
| **S4** | KI4's `--expect` on each leg's wasm | exit 0, all three |
| **S5** | every existing row on s3-128 and s3-21 | unchanged from main's run, with R0 added |
| **T1** | time | `build`: about 11 min, as on main (R0 and its control add seconds). Each `big-sizes` leg: **4-7 min** (toolchain from cache ~1.5 min, build ~45 s, smoke ~200 s, carousel ~11 s, AF3 ~14 s). The workflow's wall clock: **10-13 min**, set by `build`, as today |
| **J1** | the job set | firmware 30 (27 success / 3 skipped), web-renderer **5** (build, and three `big-sizes` legs success; publish skipped), device-ids 1, no-traffic-captures 1: **37 = 33 / 4 / 0** |

**After this merges, every later PR that touches `src/`, `include/`, `platformio.ini` or `tools/web-renderer/` predicts
web-renderer as 5 jobs, not 2.** A src change goes from 34 = 30/4/0 to 37 = 33/4/0. Release runs are unchanged at 2.

**Outside the set, pre-registered:**
- **A check red at 960, 1080 or 1280 only, with `s3-21` green on the same row.** That is a big-screen finding, the thing
  the checks exist to catch. It is reported as a finding with the row's own output. The check is never changed to clear
  it, and the PR stops there for a ruling.
- **A wall clock over 13 min with a `big-sizes` leg that started late** (its `started_at` well after `build`'s) means
  runners queued, not that the design is slow. It is reported with both start times.
- **An exit 2 from any check** means the instrument failed. Stop and decide.
- **Any other reading not in these tables** means the instrument is unreliable at this size. Stop and decide.

## Results

### Local, read before the push (2026-11-12)

| row | read | verdict |
|---|---|---|
| **D1** | `blipscope-pro-s3-21` at main: 17:19:31-17:23:01Z, 1,927,456 bytes, sha256 `55a70464cb7c...`. With the change: 17:26:05-17:26:54Z, 19 units recompiled (the ones that read the header), 1,927,456 bytes. The comparator: 65 differing bytes in 2 runs, app_elf_sha256 32 + checksum 1 + image_sha256 32, "PLACED", exit 0 | **as predicted.** No code byte moved. No `__TIME__` string differs, because the incremental build did not recompile WiFiManager; the five regions allow that |
| **G1** | g++ 13.3.0 (WSL): rc 1, `error: #error "BLIPSCOPE_WEB_SCREEN_SIZE is the web renderer's alone ..."` | as predicted |
| **G2** | rc 1, `error: static assertion failed: the big-size check models are 960, 1080 and 1280 px ...` | as predicted |
| **G3** | 960, 1080, 1280, and neither macro: rc 0 each | as predicted |

**R0's logic, rehearsed on stand-ins (not a registered row; the real reading is CI's).** Fake loaders reporting a fixed
size gave these results:
- s3-21 480 and s3-128 240 (both from their headers) and web-960, -1080 and -1280: `ok   R0`, exit 0;
- s3-21 with `--expect-size=960`: `FAIL R0`, exit 1;
- a `web-960` whose module reports 480 (a size define that did not take): `FAIL R0`, exit 1;
- an unknown model name: `INSTRUMENT`, exit 2.

So R0 can go red on both failures it exists to catch.
