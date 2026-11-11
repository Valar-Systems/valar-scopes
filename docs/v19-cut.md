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
| 2 | **the candidate:** `FW_VERSION` 18 -> 19, merged by the rule. Its squash, called **C** below, is the commit that gets tagged | ruled; `RELEASING.md` 1-2 | **done:** #631, C = `3a322758eadc`, 2026-11-11 01:15:48Z |
| 3 | **the pre-tag reads on C:** `check-tag-workflow.sh`, and "is this cut R?" | `RELEASING.md` 3 and its R gate | **done** ~01:17Z, as predicted: the cut is R |
| 4 | **the fresh-boot acceptance on E2:08, on C's image** | `RELEASING.md` 0 | **passed**, accepted ~05:04Z, on image `5048a7223d63` (see "Step 4's result") |
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

**Step 2's first CI run, at `51fa0932`, was refused by `customer-entry`, and that is the gate working:** it refuses a
`FW_VERSION` with no `## v<N>` section in `docs/CHANGES-customer.md`, and `build` waits on it. A fourth step my plan did not
name. The proposed v19 entry is in this PR for Daniel to approve, as the file says: Code proposes, and Daniel approves in the
bump PR. The gate passes locally on full history.

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

**Step 4's result (2026-11-11).** Two readings were off before the acceptance ran. Each stopped the chain and was ruled.
- **P2, the image preflight (04:33Z):** a rebuild of `blipscope-s3-128` at C (`6335f52408f2`) was predicted identical to the
  image built at C at 01:23Z, or different only in the three hash regions `cmpimg.py` knows. It differed in 12 more bytes, all in
  WiFiManager's two `__DATE__ " " __TIME__` strings, the about page's date and `Software Info:`. My prediction used
  `cmpimg.py`'s three regions, not CLAUDE.md's five. Ruled ~04:39Z: the cause is accepted, and step 4 runs.
- **P4, the flash (04:48Z):** `bench-flash.ps1`, run under Windows PowerShell 5.1, rebuilt before writing, so E2:08 received
  `5048a7223d63`, not `6335f52408f2`. The two differ only in those two strings (8 bytes). Ruled ~04:48Z: run the acceptance on
  `5048a7223d63`, and record which image ran. **The image that ran is `5048a7223d63`.** By the rule ruled the same morning, an
  image that differs from another only in the five rebuild regions is the same build.
- **The acceptance:** every one of the script's 10 checks passed. Accepted ~05:04Z. Its `--grade-boots` step first read
  UNTRUSTWORTHY: my shell had no `BLIPSCOPE_TOKEN_FILE`, so it could not read the Worker. Re-run in the same fixed window with it
  set, it passed. That was accepted as a rig failure.
- **The key question above, and a correction to how it was to be answered.** The `[reset]` line is **not** evidence.
  `cloud-key-fac` lives inside the `config` namespace, and `ClearNamespace` writes it back within the same handle, so the line
  prints `preserved=cloud-key-fac` whenever `config` is cleared, whether or not a key exists. The evidence is the Worker: a
  keyless boot never reaches it. A POWERON boot row for E2:08 after the 04:50:07Z reset is what showed the key survived. It did,
  and no re-enrolment was needed.
- **E2:08 ended on `5048a7223d63`,** C's image, set up again by Daniel.

**Step 5, O11:** A12.172's rows C0-C3 exactly as frozen there. Image B is uploaded directly as an asset on `ota-preflight-v6`,
never through a tag or the workflow, with the snapshot and restore reads around it. E2:08 is restored by Q112 (a) inside the window.
A bench image ends no window.

**Step 5 so far (2026-11-11).** X1-X4, C0 and C1's first-boot refusal (`refused foreign: requested=s3-128 image=s3-21`) read as
frozen. C1's power-cycle row stopped twice: each time the trace's open reset the chip (`rst:0x15`), so the boot it read was the
trace's, not the power-on's. The ruling (~16:52Z) was to grade the power-on boot at the Worker, with nothing open on the port.
- **The re-read before freezing it stopped (~17:05Z) at two differences, both my misses.** The bench prerelease held its originals,
  which I had restored at 06:02:56Z under O11's "stop, restore, report" without reporting it. E2:08 was up on bench image A rather
  than in download mode: a USB boot reached the Worker at 06:00:05Z, before the report that said otherwise was written.
- **Observations, not graded:** six Worker `ota` rows from the C1 boots, all `refused-foreign` 19->20, one with `reset=POWERON`
  (05:57:34Z, the power cycle's own boot, which checked in before the trace's open reset it). Two later `POWERON` boots, at 17:35:26Z
  (a power cycle outside any frozen row) and 18:10:43Z (the board moved onto a switchable USB hub), each with no `ota` row, because
  the prerelease said 6.
- **Where the OTA result lands, from code at C:** the update check runs in `setup()` before the cloud source starts, so the refusal
  is already recorded when the first check-in is built. That check-in is the config fetch, which carries both `X-Blip-OTA-Mem` and
  `X-Blip-Boot`, and the Worker writes a `boot` point and an `ota` point for it. The boot reason is retried until a check-in is
  acknowledged; the OTA report is cleared on read and never retried.

**Step 5's power-cycle row, C1-PC (ruled ~18:20Z; frozen here, before the cut).**
- **Before the cut,** inside the announced O11 window: `ota-preflight-v6` holds F as `firmware-s3-128.bin` (`42c8124fb01e`) and
  `version.txt` = "20" (`f5ca38f748a1`), each read back by sha256; `releases/latest` is v18; the reservation stands; E2:08 answers
  on image A. **E2:08 has no boot or `ota` row after its 18:10:43Z `POWERON` boot.** The ruling named 17:35:26Z; the 18:10:43Z boot
  (the move onto the hub) came after it, so the later row is the anchor.
- **The cut:** E2:08's hub port off, then on, with both UTC times logged; or, if the hub is not used, the owner's hands, with the
  owner's stated time. The window starts at the off time, or at the first of three missed pings. Nothing opens the port until the
  rows are read.
- **Predicted:**
  - the board answers ping within about 60 s of power-on;
  - E2:08's Worker `boot` rows since the cut: exactly one, `POWERON`;
  - its `ota` rows since the cut: exactly one, `refused-foreign` 19->20, `reset=POWERON`, at the same second as that boot row.
- **The read:** 60 s after the board answers, then again until two reads 30 s apart agree.
- **Anchors:** the same queries still return the earlier rows (boots: `SW` 11:00:17Z, `POWERON` 17:35:26Z and 18:10:43Z; `ota`:
  the six from 05:51:03Z to 06:00:05Z), or the reading is UNTRUSTWORTHY.
- **Outside the set, stop and report:** a `POWERON` boot row with no `ota` row (the one-shot report was lost, or the instrument);
  any other result or version; more than one boot, or another reason; no boot row within 10 minutes; a failed query or a missing
  anchor; anything else. Nothing is improvised.
- **Then C2, as frozen.** The window ends after C2, or at any stop: the originals go back on `ota-preflight-v6` by sha256
  (`firmware-s3-128.bin` `b12e184d4570`, `version.txt` `e7f6c011776e`), read back, and `releases/latest` is never touched.

**Step 6:** Daniel's OK on `RELEASING.md`'s four consequences, quoted to him in its own words. A green CI run is not that OK.

**Step 7:**
- **The prerelease's run:** a receipt from every shipping SKU, `version.txt` = 19 on the prerelease, and the web set named `-v19`
  (`render.js`, `s3-128` js and wasm, the font, the licence, `.sha256`).
- **At the `promote` pause:** E2:08 is flashed from the prerelease's own `firmware-s3-128.bin`. Its About tab shows 19, and its
  serial shows `[ota] ... current=19 latest=18 (already up to date)`.
- **How that flash is done (ruled ~04:48Z, after step 4's P4):** the downloaded asset is written exactly, with **no build step**,
  because `bench-flash.ps1` rebuilds before it writes. The asset's sha256 is checked against the release before the write, the
  app partition is read back and compared after it, and those predictions are frozen before the flash. It is a flash, not an OTA.
- **The square-photo gate:** `scripts/verify-release.sh <tag>` passes, its square probe included.
- **After `promote` is approved:** `releases/latest/download/version.txt` reads 19, read twice a few minutes apart.

**Outside the set, at any step:** stop the chain there and report. Nothing is improvised on the spot, and a later step does not start.
