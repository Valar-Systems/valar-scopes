# CI builds the join-diag bench env (predictions first)

**Asked by the reviewer (reviewer chat, pasted by Daniel, 2026-10-23),** as #473's follow-up: "build
blipscope-s3-128-joindiag in CI, the way the other bench envs are built, so JoinDiagScreen.h is compile-checked."

**Why.** `include/JoinDiagScreen.h` compiles only under `-DBLIPSCOPE_JOIN_DIAG`, which only
`[env:blipscope-s3-128-joindiag]` sets, and that env is not in `skus.yml`. So no CI run compiles it. #473 changed its
BSSID print, and that change could be checked only by a local build.

**The change.** One row in `skus.yml`, under the bring-up and harness envs, the way `animtest-s3-128` and
`weatherscope-bench-s3-128` are listed: `{ env: blipscope-s3-128-joindiag, status: build-only }`. No slug, so no asset,
no fleet and no veto.

**What could make the new leg red, checked before the change:**
- **The launch gate's plant site.** A build-only env that does not compile `src/ConfigMigration.cpp` needs its own
  `plant_target()` case, or the gate reads "control blind". The animtest lesson.
  `blipscope-s3-128-joindiag` is the full radar build plus one flag, so it compiles `ConfigMigration.cpp` and the
  default site applies. No new case.
- **The photo-mode role.** `check_photo_mode.mjs --role blipscope-s3-128-joindiag` answers `none`.
- **The bench-hook scan** runs on slug-ful legs only, so it does not run on this one.

| | check | predicted |
|---|---|---|
| **J1** | the matrix's own validators, locally, with the row added | `skus.py --validate` and its selftest pass; `check_release_envs.mjs` and its selftest pass; `check_photo_mode.mjs` passes; `skus.py --check-readers` passes |
| **J2** | CI on this PR | a new leg, `build (blipscope-s3-128-joindiag)`, which builds, and its launch gate is **not** "UNTRUSTWORTHY". Every check passes: **0 failures** |
| **J3** | the leg's artifact, from a local build of the same env and commit | the ELF holds the join-diag `[jdiag]` strings, so `JoinDiagScreen.h` is compiled in |
| **J4** | the rest of the matrix | every other leg unchanged: the matrix has exactly one leg more |

**Not done, optional:** a `[jdiag]` marker in `check-no-bench-hooks.sh`, so shipping images are scanned for the
join-diag code, with an `--expect` positive control on this leg.

**Outside the set:** stop and report.
