# CI runners: every job names its image

**The rule:** no job runs on `ubuntu-latest`. Every `runs-on` names its image, today `ubuntu-24.04`.

**Why:** GitHub moves `ubuntu-latest` to a new image under every workflow at once.
- In November 2026 it began resolving to `ubuntu-26.04` (g++ 15).
- There, `test/host/test_wx_crop.cpp`'s replacement `operator delete` fails `-Werror=mismatched-new-delete`, and `test/host/run.sh` stops at its WxCrop block. Every later block goes unrun.
- `host-tests` is main's required check, so every PR whose job drew a 26.04 runner failed at random (raised by blipscope-5c in 2ff8c3d; seen on #514).
- **A result on a floating label describes an image nobody chose.**

**The guard:** `scripts/check_runners.py`, run as its own job in the firmware workflow.
- **What it does:** it fails when any line of `.github/workflows/*.yml` names `ubuntu-latest` in its code. A comment is not code, so prose about the label passes; a step's name is code, so it is worded without the label.
- **Its `--selftest`:** it plants the label into a copy of the workflows twice, once alone and once with a comment after it. Each copy must fail, naming exactly the planted line, and the real tree must pass.
- **At the pin:** 27 `runs-on` lines in 9 workflows (26 pinned, plus the guard's own job), 0 naming `ubuntu-latest`.

**Since the one-count follow-up** (frozen in docs/weatherscope-stage3.md A12.77):
- **Two legs:** the host tests run as `host-tests-leg`, on `ubuntu-24.04` and on `ubuntu-26.04` (`fail-fast: false`).
  - **A job still named `host-tests`** needs them, runs `if: always()`, and succeeds only when both legs succeed.
  - So main's required context keeps its name, and both legs are required through it, with no branch-protection change.
  - The leg's `runs-on` is `${{ matrix.os }}`, so the guard counts 28 `runs-on` lines: the matrix line, plus the
    aggregator's own `ubuntu-24.04`.
- **`test_wx_crop.cpp`** reaches `malloc` and `free` through `volatile` function pointers, so no compiler can pair a
  `new` with a `free`. There is no pragma, no flag and no compiler-version test.
- **`run.sh` keeps going:** each block runs in its own subshell, and a failed block, a compile failure included, ends that
  block only. At the end, `FAILED BLOCKS:` lists every one, and the exit is 2 if any failed as the rig, else 1, else 0.

**The record:** docs/weatherscope-stage3.md A12.77 (the ruling and the plan), A12.78 (the count, 26) and A12.79 (the move seen, and Q68).
