# The web renderer at 960 and 1280 px: the headless checks (registration; predictions before the code)

**Commissioned** by the owner's big-screens item (c), 2026-11-12 16:57Z, relayed by the reviewer: "Whoever owns
tools/web-renderer adds 960 and 1280 to its headless checks." This session wrote tools/web-renderer (#518, #523, #531,
#629), so the item is this session's. The pictures (a) and the touch proposal (b) are another session's. **This page
is committed before any code is written or any build is run.**

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

## The change

| file | what |
|---|---|
| `include/variants/s3_21.h` | `SCREEN_SIZE` is `BLIPSCOPE_WEB_SCREEN_SIZE` when that macro is set, and 480 as today otherwise. The macro is refused outside the web build (`#error` unless `BLIPSCOPE_WEB_RENDERER`) and outside the set {960, 1280} (`static_assert`) |
| `tools/web-renderer/build.sh` | two models, `web-960` and `web-1280`: `BLIPSCOPE_VARIANT_S3_21` plus `-DBLIPSCOPE_WEB_SCREEN_SIZE=<N>` |
| `tools/web-renderer/smoke.mjs` | **R0**: the model draws at the size it was built for. A `web-<N>` model expects N. Any other model expects the number its variant header states (`include/variants/<name with - as _>.h`, the one `constexpr int SCREEN_SIZE = <digits>;` line, from `SRC_ROOT` when set), read from that header, never retyped. `--expect-size=<N>` overrides it, for R0's control only. `--r0-only` runs R0 alone |
| `.github/workflows/web-renderer.yml` | a `BIG: 'web-960 web-1280'` list and two steps, **on pushes and PRs only** (`if: steps.src.outputs.tag == ''`): every headless check on the two models, then R0's control. `BUILT` and `PUBLISHED` don't change |

So `web-960` and `web-1280` are the Pro 2.1's web model with one thing changed, its size. A check that reads
differently there than on `s3-21` can be put down to the size.

**Never published.** A release run builds from its tag and skips both new steps, so it is the same run as today: the
same models, checks and staged set. The two models appear only in a push or PR run's workflow artifact.

**Why one branch in `s3_21.h`, and not a patched copy of it inside `build.sh`.** A copy would touch no device file. But
it is invisible from the header where the size is defined, and it depends on an anchor that can stop matching.
The branch says what it is, at the place the size lives, and the guards refuse it anywhere else. The cost is that a
device header changes, so the device image is shown unchanged (D1).

## Predictions

**Local, before the push (this tree, one path for both builds):**

| row | what | predicted | stop if |
|---|---|---|---|
| **D1** | `blipscope-pro-s3-21` (the one env that selects `s3_21.h`; `platformio.ini`) built at main `71156714ba44` and again with the change, in this same worktree; the two `firmware.bin` compared by the five-region comparator | **PLACED**: every differing byte inside the five rebuild regions (app ELF sha, checksum byte, trailing SHA-256, WiFiManager's two `__TIME__` strings) | OUTSIDE (a byte outside the five), or IDENTICAL (the second build did not rebuild) |
| **G1** | `g++ -fsyntax-only` on `s3_21.h` with `-DBLIPSCOPE_WEB_SCREEN_SIZE=960` and no `BLIPSCOPE_WEB_RENDERER` | refused, by the `#error` | it compiles |
| **G2** | the same with `-DBLIPSCOPE_WEB_RENDERER -DBLIPSCOPE_WEB_SCREEN_SIZE=720` | refused, by the `static_assert` | it compiles |
| **G3** | the same with `-DBLIPSCOPE_WEB_RENDERER -DBLIPSCOPE_WEB_SCREEN_SIZE=960`, then `=1280`, then with neither macro | all three compile (the control: G1 and G2 fail for their guard, not for the header) | any of the three fails |

The other device envs select another variant header in `Variant.h`, and don't read `s3_21.h`. That is stated from
`Variant.h`, not measured.

**CI, at the code commit (no local Emscripten; CI's web-renderer build is the instrument):**

| row | what | predicted |
|---|---|---|
| **B1** | the two models build | `built web-960 v19 ...` and `built web-1280 v19 ...` (`build.sh`'s last line) |
| **R0** | each of the four models | `ok   R0`: s3-128 240, s3-21 480, web-960 960, web-1280 1280 |
| **R0-ctl** | `smoke.mjs web-dist/s3-21 --r0-only --expect-size=960` | `FAIL R0`, exit 1; the step reads it as the control holding |
| **S1** | smoke R3-R7 on web-960 and on web-1280 | `WEB RENDERER SMOKE AS PREDICTED`, both |
| **S2** | carousel WC2 and WC3 on both | every row `ok`; WC2's dots read `2` |
| **S3** | AF3 on both | `AF3: as predicted`, both |
| **S4** | KI4's `--expect` on both wasms | exit 0, both |
| **S5** | every existing row on s3-128 and s3-21 | unchanged from main's run, with R0 added |
| **T1** | the build job's time | about 11 min on main (run 43167854623) becomes **17-24 min**. Per big model: build ~45 s, smoke ~200 s, carousel ~11 s, AF3 ~14 s |
| **J1** | the job set | firmware 30 (27 success / 3 skipped), web-renderer 2 (build success / publish skipped), device-ids 1, no-traffic-captures 1: **34 = 30 / 4 / 0** |

**Outside the set, pre-registered:**
- **A check red at 960 or 1280 only, with `s3-21` green on the same row.** That is a big-screen finding, the thing the
  checks exist to catch. It is reported as a finding with the row's own output. The check is never changed to clear it,
  and the PR stops there for a ruling.
- **An exit 2 from any check** means the instrument failed. Stop and decide.
- **Any other reading not in these tables** means the instrument is unreliable at this size. Stop and decide.
