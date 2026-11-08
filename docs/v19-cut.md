# Cutting v19: the order, and what each step must read

**Read this first.** v19 is **R**, the first release that carries the OTA identity check. Weather and the map ship together later,
so v19 ships without weather (#609). The cut was ruled 2026-11-08 ~19:07Z as five steps, each stopping the chain on an off reading.
This page freezes every step's predictions before the step runs.

**A correction before anything runs, and it is my miss.** I proposed those five steps without reading `RELEASING.md`'s procedure in
full. It has three more steps, all required, and the order below adds them in `RELEASING.md`'s words:
- **its step 0, the fresh-boot acceptance on a real board,** which needs Daniel's hands;
- **its step 3, the pre-tag check;**
- **its step 5, the bench flash from the prerelease before `promote` is approved.**

The order is for the reviewer to confirm.

## The order

| # | step | from | waits on |
|---|---|---|---|
| 1 | A3.1's reader merged | ruled ~19:07Z | **done:** #617, `c85fe77a7cda`, 2026-11-08 21:21:46Z |
| 2 | **the candidate:** `FW_VERSION` 18 -> 19, merged by the rule. Its squash, called **C** below, is the commit that gets tagged | ruled; `RELEASING.md` 1-2 | the PR's approval |
| 3 | **the pre-tag reads on C:** `check-tag-workflow.sh`, and "is this cut R?" | `RELEASING.md` 3 and its R gate | step 2 |
| 4 | **the fresh-boot acceptance on E2:08, on C's image** | `RELEASING.md` 0 | step 2, and **Daniel at the board** |
| 5 | **O11 on C**, on E2:08 | ruled; A12.172 | step 2, an announced window |
| 6 | **Daniel's OK, in plain words,** on the four consequences, asked in the reviewer chat | ruled; `RELEASING.md`'s R gate | steps 3-5 |
| 7 | **tag C, create the release as a prerelease, bench-flash from it, then approve `promote`** | ruled; `RELEASING.md` 4-5 | step 6 |
| 8 | **the week after:** A3.1's reader daily; the four boards behind 18 read again | ruled ~20:34Z | step 7 |

**Why the acceptance runs on C rather than first.** `RELEASING.md` calls it step 0 because it is the check closest to what a new
owner meets. Run on C, it grades the image that will ship. Run before the bump, it would grade a build no customer gets.

## Predicted, step by step

**Step 2, the candidate PR.**
- **The change:** `FW_VERSION` = 19 in `src/OtaUpdater.h`, and the two usage examples that name v18 files, in
  `tools/web-renderer/README.md` and `render.js`'s header comment, renamed to v19. Nothing else.
- **CI: 34 = 30 success / 4 skipped / 0 failed.** That is #629's finished shape: a `src/` change runs `web-renderer`'s `build` and its
  skipped `publish`. The last job-set change on main is `84409651`.

**Step 3, the pre-tag reads on C:**

| read | predicted |
|---|---|
| `bash scripts/check-tag-workflow.sh C` | `ok: promote needs version, runs in environment release ...` |
| `git ls-tree --name-only C -- include/OtaIdentity.h` | the path, so the check is in C |
| `releases/latest`'s `firmware-s3-128.bin` (v18), head extracted | `(record: no-record)`, so this cut **is R** |

**Step 4, the fresh-boot acceptance**, `scripts/fresh-boot-acceptance.sh`, guided, on E2:08 with C's `blipscope-s3-128` image.
- **Predicted:** every one of the script's own checks passes. It is authoritative, and its checks are not restated here.
- **What the reset clears:** `wifi`, `wifi-fast`, `config` and `logbook`. It preserves `cloud-key-fac`.
- **Not yet known, so said as a question:** whether E2:08's cloud key is in `config` or in `cloud-key-fac`. It was enrolled through the
  enrolment page. If the key is in `config`, step 2 of the acceptance includes re-enrolling the board. The activation gate is in watch
  mode, which allows that, and the key it mints is the same one. This is read from the board's own `[reset]` line before anything is
  concluded.
- **E2:08 ends on C's image,** set up again by Daniel.

**Step 5, O11:** A12.172's rows C0-C3 exactly as frozen there. Image B is uploaded directly as an asset on `ota-preflight-v6`,
never through a tag or the workflow, with the snapshot and restore reads around it. E2:08 is restored by Q112 (a) inside the window.
A bench image ends no window.

**Step 6:** Daniel's OK on `RELEASING.md`'s four consequences, quoted to him in its own words. A green CI run is not that OK.

**Step 7:**
- **The prerelease's run:** a receipt from every shipping SKU, `version.txt` = 19 on the prerelease, and the web set named `-v19`
  (`render.js`, `s3-128` js and wasm, the font, the licence, `.sha256`).
- **At the `promote` pause:** E2:08 is flashed from the prerelease's own `firmware-s3-128.bin`. Its About tab shows 19, and its
  serial shows `[ota] ... current=19 latest=18 (already up to date)`.
- **The square-photo gate:** `scripts/verify-release.sh <tag>` passes, its square probe included.
- **After `promote` is approved:** `releases/latest/download/version.txt` reads 19, read twice a few minutes apart.

**Outside the set, at any step:** stop the chain there and report. Nothing is improvised on the spot, and a later step does not start.
