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

**What follows:**
- `host-tests` also runs on `ubuntu-26.04`, with both legs required through a job still named `host-tests`.
- `test_wx_crop.cpp` compiles under g++ 13, g++ 15 and MinGW.
- `run.sh` keeps going after a failed block and lists every failure at the end.

All three are frozen in docs/weatherscope-stage3.md A12.77, in the one-count follow-up.

**The record:** docs/weatherscope-stage3.md A12.77 (the ruling and the plan), A12.78 (the count, 26) and A12.79 (the move seen, and Q68).
