# The OTA URL check: no skus.yml image points OTA anywhere but `latest`

Ruled 2026-10-21 (reviewer, O11 hosting, condition 3): **a CI check that refuses `OTA_RELEASE_BASE`, or
any OTA URL override, in every `skus.yml` env, with a sabotage that turns it red. It must be merged
before O11's upload.** This document freezes the design and the predictions **before any code**.

## The silent path it closes

- Every shipping image finds updates at `releases/latest/download/version.txt` and
  `.../firmware-<prefix><slug>.bin` (`src/OtaUpdater.cpp:96-110`). `latest` never resolves to a
  prerelease, which is why a bench prerelease is invisible to the fleet.
- `-DOTA_RELEASE_BASE` replaces that base, at build time. Today only two bench envs set it
  (`blipscope-s3-128-soak` and `blipscope-s3-128-otatest`), and neither is a `skus.yml` row.
- **Nothing checks that this stays true.** A `skus.yml` env that gained the define would build a
  shipping image pinned to a bench tag, pass every gate, and publish. Its devices would then follow
  whatever was clobbered onto that tag, which is what O11 does deliberately.

## The design: check the image, not the config

The define is a statement of intent; the image is what ships (CLAUDE.md, "read the artifact, not the
config"). A config check would miss a define set in a variant header, in `src_build_flags`, or across an
`extends`. So the check reads the **built image** in every build leg.

**What it reads.** I looked at one image before freezing this: the s3-128 release image from `bb5042e`.
It holds exactly two release URLs, both built from the default base:
`https://github.com/Valar-Systems/valar-scopes/releases/latest/download/version.txt` and
`…/releases/latest/download/firmware-`. No other string mentions `valar-scopes/releases/`.

**`scripts/check_ota_url.py <image>`**, with the default base read from `src/OtaUpdater.cpp`'s own
`#define OTA_RELEASE_BASE "…"`, the other side of the contract, not restated:

| verdict | exit | when |
|---|---|---|
| **BLIND** | 2 | the image lacks the anchor every real image carries (`[build] env=`), or the default base can't be read from `OtaUpdater.cpp` |
| **FAIL** | 1 | any `/releases/download/` URL (a pinned tag); or an `https://…/version.txt` or `…/firmware-` URL not built from the default base (an override to any host); or exactly one of the two default URLs (inconsistent) |
| **PASS** | 0 | both default URLs present, and nothing that FAILs |
| **NO-OTA** | 0 | no OTA URL of any kind: the image has no OTA client, so it can't reach the fleet |

- `--require-ota` makes NO-OTA a FAIL. The workflow passes it on slug-ful rows, because a shipping
  image must find its updates at `latest`.
- `--selftest` plants each verdict into a fake image and requires the scanner to return it.
- `--check-wiring` reads `.github/workflows/firmware.yml` and requires the step to be in the `build`
  job, with no `if:` (it runs for every row), and before both `Attach to release` and
  `Publication receipt`. A shipping image that FAILs therefore leaves no receipt, and `version.txt`
  doesn't advance.

**Where it runs:** a new step in the `build` job, right after the adsbdb launch gate, on every matrix
row and every event (push, pull request, release).

## Predictions (frozen before any code)

Each check has a sabotage that must turn it red.

**OU1. The selftest (host).** `--selftest` returns each planted verdict: PASS; FAIL for a pinned
`releases/download/ota-preflight-v6/` URL; FAIL for a base on another host; FAIL for one default URL
without the other; NO-OTA; FAIL for NO-OTA under `--require-ota`; and BLIND for a file without the
anchor. *Sabotage:* a classifier that ignores `/releases/download/` must make the selftest fail.

**OU2. The real images (local, before CI).**
- The s3-128 release image from `bb5042e` (`3e887fdd…`): **PASS**, with and without `--require-ota`.
- The same env built with `PLATFORMIO_BUILD_FLAGS='-DOTA_RELEASE_BASE=\"https://github.com/Valar-Systems/valar-scopes/releases/download/ota-preflight-v6\"'`:
  **FAIL**, naming the pinned URL. This is the sabotage on a real image, not a fake one.
- The same env with the base pointed at another host (`https://example.invalid/ota`): **FAIL**.

**OU3. CI on this PR, every build leg.**

| env | predicted |
|---|---|
| `blipscope-s3-128` (slug-ful, `--require-ota`) | PASS |
| `blipscope-pro-s3-21`, `blipscope-s3-146`, `blipscope-pro-s3-175-amoled`, `missileer-s3-146`, `missileer-s3-128`, `orbitscope-s3-146`, `quakescope-s3-146`, `quillscope-s3-146`, `reelscope-s3-146`, `claudescope-s3-146`, `speedscope-s3-146` | PASS: each calls `MaybeUpdateFirmware` from `src/main.cpp` |
| `animtest-s3-128`, `weatherscope-s3-128` | NO-OTA: their `build_src_filter` excludes `OtaUpdater.cpp` |
| `missileer-photo` | **PASS or NO-OTA, whichever it reads, recorded.** It compiles `OtaUpdater.cpp`, but `PHOTO_MODE` skips the only call (`src/main.cpp:606-613`), and whether the linker keeps the URLs can't be read from source. Either result keeps the property, since neither is a FAIL. |

**0 FAIL and 0 BLIND across all legs.** Any FAIL or BLIND on this PR is outside the set.

**OU4. The wiring (host, and in CI).** `--check-wiring` passes on this branch. *Sabotages:* the step
moved after `Attach to release`; the step given `if: matrix.slug`. Each must make `--check-wiring` fail.

**OU5. The workflow end to end, a throwaway sabotage PR.** A draft PR, closed unmerged, adds
`-DOTA_RELEASE_BASE` (the `ota-preflight-v6` URL) to `[env:blipscope-s3-128]`. Predicted: that leg's
step FAILs naming the URL, and every other leg is unaffected. It is a pull-request run, so nothing is
uploaded either way.

**Outside the set:** stop and report. Code, tests and bars are not edited to make a reading come true.

## What it does not cover, stated

- **Images built outside CI.** A local build with the define, flashed by USB, is a bench act and is out
  of scope; only CI publishes.
- **A different shape of URL override.** For example, code that assembles the URL at run time from a
  config value. The check refuses any OTA URL string that isn't the default base. An image that builds
  its URL entirely from run-time data would read NO-OTA, and `--require-ota` turns that into a FAIL on
  shipping rows.

## Results: host and local (2026-10-21, before CI)

Graded on `dce27b5`.

| check | reading | verdict |
|---|---|---|
| OU1 `--selftest` | all 9 image cases and 4 wiring cases return their planted verdict; the default base is read from `OtaUpdater.cpp` | as predicted |
| S-OU1, the `/releases/download/` rule removed | the selftest fails (exit 1), on exactly the case only that rule sees ("the defaults plus a bare pinned base": got PASS) | as predicted |
| OU2, the release image (`3e887fdd…`, `bb5042e`) | **PASS**, with and without `--require-ota` | as predicted |
| OU2, the base pinned to `…/releases/download/ota-preflight-v6` (a real build) | **FAIL**, naming both pinned URLs | as predicted |
| OU2, the base on another host, `https://example.invalid/ota` (a real build) | **FAIL**, naming both off-base URLs | as predicted |
| OU4 `--check-wiring` on this branch | ok | as predicted |
| S-OU4a, the step moved after `Attach to release` | refused, naming the upload | as predicted |
| S-OU4b, the step given `if: matrix.slug` | refused, naming the `if:` | as predicted |

Every sabotage asserted its anchor (exactly once) and that the file changed, and each file was restored and verified against `dce27b5`.

**Read before OU5 runs (its prediction is unchanged).** Of the `skus.yml` envs, only `animtest-s3-128` interpolates `${env:blipscope-s3-128.build_flags}`. It has no OTA client, so OU5's define can't change its NO-OTA verdict. `weatherscope-s3-128` extends that env but replaces `build_flags`, and every other row has its own `build_flags`. So OU5 should change the s3-128 leg's verdict only, as frozen.
