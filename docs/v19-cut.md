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
| 5 | **O11 on C**, on E2:08 | ruled; A12.172 | **held:** C2 stopped in window 2, and the defect it exposed in C's head read is with the owner (see "Step 5 so far") |
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

**Window 2 (2026-11-11, 18:38:00Z to 19:03Z), opened on the owner's own words.** The cycle went through a YKUSH switchable
USB hub. Its tool is Yepkit's `ykushcmd` v1.4.0: installer `ykushcmd-installer-64bit.exe` from
`github.com/Yepkit/ykush/releases/download/v1.4.0/`, sha256 `e40cf7a66ae838fd228622a5bea8077484be50c8ac7da48c9db4837d3b4e3d3c`,
signed by YEPKIT LDA; the installed `ykushcmd.exe` is sha256 `d32ff453df1e9356d961ed885e35ec70110a10f85502208f50c599e07bc64d56`.
The hub's ports were mapped from Windows' USB location paths without switching any, and confirmed against the printed port numbers.
- **Before the cut, as frozen:** F and "20" uploaded at 18:38:02Z and read back by API digest and by download; `releases/latest`
  v18; the reservation held; E2:08 answering on image A, still on the hub's port 3, with no boot or `ota` row after 18:10:43Z.
- **C1-PC, as predicted.** The switch: port 3 (E2:08) OFF 18:39:12Z, ON 18:39:17Z, each confirmed by the tool's own state read.
  The board answered ping 14 s after power-on. Two Worker reads 33 s apart agreed: exactly one boot, `POWERON`, and exactly one
  `ota` row, `refused-foreign` 19->20 `reset=POWERON`, both at 18:39:44Z. The anchors were all returned, and no port was opened.
- **C2 stopped off its prediction.** The sabotage image S (the parse shifted by one byte) was flashed, accounted against the
  checked S (five regions only), and B was uploaded and read back. The graded boot, read in one trace open, refused B as
  **`refused truncated: requested=s3-128 image=-`**, not the predicted `refused no-record`. S's first boot after its flash,
  against F, read `refused-truncated` too (Worker, 18:48:12Z). "Truncated" was not in C2's frozen set, so the window stopped
  and was restored. C3 did not run.
- **The restore, as frozen.** The prerelease's originals went back at 18:52:57Z, read back by digest and by download, and matched
  the reviewer's own baseline. B's download count read 0, a lagging counter, recorded and not a stop. `releases/latest` read v18
  at 18:53:03Z and 19:00:24Z. E2:08 went back to C by Q112 (a): C4-0 and C4-1 as predicted (four regions, `0x0`, `0x8000`,
  `0x1E000`, `0x20000`, none in NVS), and the written image was byte-identical to `5048a7223d63`. C4-2: `running=app0
  @0x020000`, Wi-Fi from saved credentials with no portal, and an authenticated config check-in. The reservation was released
  at 19:02:41Z.

**What C2's stop found (read-only, from images saved in the window).**
- **S differs from A by one byte outside the hash regions:** the immediate of `ParseRecord`'s first argument in
  `MaybeUpdateFirmware`, `head + 0x120` -> `head + 0x121`. It is the only differing instruction there, and every function's name
  and size is identical. In `Decide` the truncated test comes before the parse, so the plant never ran: on both of S's boots
  the head read returned fewer than 352 bytes, possibly none.
- **Excluded by that measurement:** the plant changing the decision or the read; a wrong image or asset; the outcome token.
  **Not excluded at the time:** an intermittent short read that fell on both of S's boots, or something else about those boots.

**Step 1's read of the mechanism (read-only, at C `3a322758eadc`, framework-arduinoespressif32 3.3.9):**
- `ReadHead` (`include/OtaInstall.h:69-78`) stops on the first read that returns 0.
- That read is `StreamSource::read`, which is `Stream::readBytes` (`src/OtaUpdater.cpp:189-192`). Through the virtual call it
  becomes `NetworkClient::readBytes`, which stops at once when the client's `read()` returns a negative.
- `NetworkClientSecure::read` returns -1 whenever nothing is buffered yet, and the TLS socket is non-blocking. So "nothing has
  arrived yet" and "the stream ended" reach `ReadHead` as the same 0, immediately, and a body that is merely late is refused as
  truncated.
- `writeRest`, which reads the rest of an accepted image from the same source, already waits: 300 empty reads, 100 ms apart. The
  head read does not. The path is new in #552 (`84409651`); v18 updated through `httpUpdate`.
- **What it means for v19:** an update from v19 to anything later can be refused as `refused-truncated`. Nothing is drawn, and
  the board retries at its next check, so it delays an update and cannot brick a board. Updates from v18 to v19 use v18's old
  path and are unaffected. How often it fires on real networks is unmeasured.
- **Held for the owner's decision, as a release question:** C3, C2's re-run, steps 6 and 7, and any fix.

**Step 2 of that decision: C's head-read rate, registered before the window (the owner, 19:39:40Z: "measure C's rate
first").** A new measurement, not a re-run of C2.
- **Why through image A.** C checks `releases/latest` for updates (`OTA_RELEASE_BASE`'s default, `src/OtaUpdater.cpp:105-106`),
  which says 18, so C makes no update attempt and writes no `ota` row. Only a bench build pins the bench prerelease.
  - **The premise that makes A's rate C's:** window 1's X2 found A and C with identical function (name, size) sets, 8,770 each:
    two strings moved and no code. X3 and X4 showed the pin is the only difference the scanners see. A's `ReadHead`, its
    `StreamSource` and the TLS client under them are C's code.
- **The window, in order:**
  1. Reserve E2:08.
  2. Flash A with bench-flash `-AppOnly` (app0 is already selected). Five-region accounting against the checked A
     (`f57ab3556b37`): 0 bytes outside.
  3. F (`42c8124fb01e`) and `version.txt` "20" (`f5ca38f748a1`) on `ota-preflight-v6`, read back by API digest and by download.
  4. The re-reads:
     - E2:08 is on Hub_#0009 port 3, and the port reads ON;
     - its config page reads env `blipscope-s3-128-otatest`;
     - the anchors, read twice at least 60 s apart: boot rows at 18:58:08Z and 19:00:50Z (USB), no `ota` row after 18:50Z,
       plus A's own boot after the flash, read and named before the first cut.
  5. 20 cycles of port 3 only, each logged with its off and on times in UTC, about 80 s apart so each boot's check-in lands
     before the next cut. No serial port is opened at any point.
  6. The grade (below).
  7. The restore: the originals back by sha256, read back against the reviewer's 18:36:15Z baseline (`b12e184d4570`,
     `e7f6c011776e`); `releases/latest` untouched; E2:08 back to C by bench-flash `-AppOnly`, accounted against
     `5048a7223d63` and graded over the network only (ping, the config page's env `blipscope-s3-128`, a boot row with no
     `ota` row), so no trace and no download mode; the reservation released.
- **Predicted, and what each result means:**

  | reading over the 20 cycles | means |
  |---|---|
  | exactly 20 `POWERON` boot rows, each with one `ota` row at the same second, every one `refused-foreign` | **0 of 20 truncated:** C's head read is reliable at this rate |
  | the same 20 pairs, **1 or more** `refused-truncated` (and the rest `refused-foreign`) | C ships the defect. The count goes to the owner |
  | a boot row with no `ota` row; an `ota` result other than those two; a boot count other than 20 or a reason other than `POWERON`; no ping within 180 s of power-on; a missing anchor or a failed query | **stop,** restore, report |
  | anything else | the instrument is unreliable at this scale: stop, restore, decide |

- **What the count can say.** 0 of 20 bounds the per-check refusal rate below about 14% at 95% (3/20, the rule of three). It
  is not proof of absence. It is the BEFORE half of the fix's own test.

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
