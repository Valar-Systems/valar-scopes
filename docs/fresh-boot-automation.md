# The fresh-boot acceptance without the owner's hands: the plan (registration; nothing is run)

**Commissioned** by the owner's item 4 (2026-11-12, effective from v19's promote): v20's cut must not need his hands for
`RELEASING.md` step 0, the fresh-boot acceptance (`scripts/fresh-boot-acceptance.sh`). This page says how each hands step
is replaced, what can't be replaced, and the controls. **Nothing here has run.** The rehearsal's predictions are frozen
in an amendment before it runs.

**What the acceptance does today.** Six steps by the owner's hands, each graded by the script's own mode, with ten checks.
v19's run on C' passed 10 of 10 (`docs/v19-cut.md`, "Step 4' on C'").

## The owner's answers (2026-11-12 16:57Z, relayed by the reviewer at 17:01Z)

"**yes to (a), (b) and (c).** The glass reset and the phone's QR setup get the hands-on test whenever their code changes,
and at least every third release."

- **(a):** the bench Wi-Fi password is the owner's credential, which he provides. It lives in a local file outside any
  repo and is never printed. A presence check prints a boolean, never the value.
- **(b):** the claim is his one prompted tap.
- **(c):** `POST /factory-reset` stands in for the glass menu on automated runs.
- **The cadence rule** is the next section, and it is written into `RELEASING.md` step 0 as well.

**Confirmed** (the owner, 2026-11-13 01:17Z, relayed by the reviewer at 01:26:46Z): "yes to all three, with the
uncovered parts written into the record. I'll put the bench Wi-Fi password in the local file myself." The uncovered
parts are recorded in "What an automated run does not cover" below.

**The password file** (answer (a)). It is `%USERPROFILE%\.config\blipscope\bench-wifi`: the folder that already holds
the token file (CLAUDE.md, "no user-level CLOUDFLARE_API_TOKEN"), outside every git working tree.
- **Format:** two lines, `ssid=<the bench network's name>` and `password=<its password>`.
- **Who writes it:** the owner, himself.
- **How the driver uses it:** it reads the file and never prints, logs or commits either value. Its presence check
  prints `present` or `absent`, nothing else. With the file absent, or either line missing, it refuses the automated
  run, and step 2 falls back to his phone.
- **Windows** cannot check the file's permissions; the driver says so, as `tools/token_file.py` does.

## The hands-on cadence, and its counter

Answers (a) and (c) mean an automated run never exercises the glass reset menu or the phone's QR and captive-portal
setup. They get a hands-on run on the owner's cadence, and the cadence is a check the driver runs, not a rule anyone
has to remember.

- **The counter** is one line in `RELEASING.md` step 0, in a fixed form: `fresh-boot hands-on: last v19 (step 4',
  2026-11-12)`. The last hands-on run was v19's step 4', which passed 10 of 10 (`docs/v19-cut.md`). The driver parses
  the line and refuses when it is missing or appears twice.
- **Due by count:** the candidate's `FW_VERSION` minus the counter's version is 3 or more. After v19, that is v22 at the
  latest; v20 and v21 may run automated.
- **Due by code:** since the counter's release tag, any of these changed:
  - `DrawResetMenu`, `HandleResetMenuTap` or `CloseResetMenu` in `src/AircraftManager.cpp`, read function by function
    with `git log -L`, because the file is too big for a file-level diff to mean anything;
  - `src/FactoryReset.cpp`, `include/FactoryReset.h`, `include/SetupQr.h`, `include/QrRender.h`, or
    `include/WiFiManagerHelpers.h`;
  - the `tzapu/WiFiManager` line in `platformio.ini`.
- **When it is due,** the driver refuses the automated run and prints which condition holds. Step 0 then runs as on v19,
  with his hands, and that release's record commit updates the counter.

The cadence's controls, each shown failing once before the driver is trusted:

| control | the plant | must read |
|---|---|---|
| **K-due-count** | the counter set to v16, with a v19 candidate | refused, "due by count (19 - 16 = 3)" |
| **K-due-code** | a range in which `include/SetupQr.h` changed | refused, "due by code: include/SetupQr.h" |
| **K-due-func** | a range in which only `HandleResetMenuTap` changed inside `AircraftManager.cpp` | refused, "due by code: HandleResetMenuTap"; the control for the function-level read |
| **K-not-due** | the real counter (v19) and a range touching none of the triggers | allowed. This is the control that the refusals aren't unconditional |
| **K-counter** | the counter line removed, then doubled | refused both times, as INSTRUMENT, never allowed |

## The owner's three choices, as asked (kept as the record of the question)

| | the question | if yes | if no |
|---|---|---|---|
| **(a)** | Keep the bench Wi-Fi password in a local file outside any repo, never printed, for the PC to provision the device? It's a credential. | step 2 is automated | step 2 stays his phone (~3 min) |
| **(b)** | The claim: his one tap when the driver asks (recommended); a bench touch actuator (a purchase); or a product change that adds a claim path? | (i): ~1 min of his hands | (ii) and (iii) are out of this plan's scope |
| **(c)** | Accept the config page's `POST /factory-reset` in place of the glass menu, with the glass menu itself not covered? | step 1 is automated | step 1 stays his tap on the glass (~1 min) |

## Each step, and what replaces it

All the firmware facts are read from main at `c73aee2f1a5b`.

| step | today | replacement | what it no longer covers |
|---|---|---|---|
| **1. factory reset** | the glass menu: Connect, Reset, Factory Reset, confirm | from the PC on the LAN: `POST /factory-reset` with `confirm=RESET` and the `X-Blipscope` header. That route (`src/ConfigurationWebServer.cpp:4265-4277`) calls `RequestReset(factoryreset::Tier::Factory)`, the same reset the glass menu requests, which prints `[reset] tier=factory cleared=...` (`src/FactoryReset.cpp:109`) | the glass menu's rows and confirm screen (choice (c)) |
| **2. Wi-Fi** | his phone joins the device's setup network and uses the portal | one of the PC's Wi-Fi adapters (there are three) joins the device's open setup network `Blipscope-<6 hex>` and submits WiFiManager's save form at its portal address, with the bench network's name and password read from the local file (choice (a)). The PC's other interface keeps the LAN | the QR code, and the phone's captive-portal experience |
| **2b. location** | the config page, by hand | `POST /location` with `lat` and `lon` and the `X-Blipscope` header, then read back from `GET /` | the location form's UI |
| **3. claim** | tap a blip, open its card, claim | **his tap, prompted** (choice (b)(i)). The driver watches the capture for `[claim] <TYPE> claimed` and only then goes on | nothing. This step stays his |
| **4. Collection** | open Collection within about a minute of the claim | `--probe-claim` first, as today, then `GET /`. The check "Collection was opened after the claim" is graded on a `[GET] Handling request to config web server` line after the claim line (`scripts/fresh-boot-acceptance.sh`, the step-4 block), which that `GET` produces | someone looking at the Collection page |
| **5. power cut** | his cable | E2:08's hub port 3, off and on, both times logged, as v19 ran it with his OK | nothing (a hub cut is a real power cut, and the post-cut boot reads `POWERON`) |
| **6. logbook off** | the config page, by hand | `POST /save` with the page's own current values (parsed from `GET /`), except that the `logbook` box is left out, which `SaveToggle("logbook")` stores as "false". The whole form is posted, so no other value moves | the config form's UI |

**What it can't replace, said plainly:**
- **the claim.** The shipping image has no non-touch claim path: no `/claim` route, and its serial handlers are bench
  injections. Without his tap, an actuator or a product change, step 3 can't run;
- **the glass reset menu**, if (c) is yes;
- **the phone's QR scan and captive portal**, if (a) is yes;
- **anyone looking at the screen.** The acceptance doesn't grade the display today either. That is unchanged, but worth
  saying.

## What an automated run does not cover (the record; the owner's word, 2026-11-13 01:17Z)

With (a) and (c) yes, an automated step 0 passes without these parts ever running. **A green automated run says
nothing about them.** Each is covered only by the hands-on cadence above: at least every third release, and whenever
its code changes.

| uncovered part | what it is | what the automated run does instead | the cadence's trigger for it |
|---|---|---|---|
| **the glass reset menu** | the menu drawn on the panel (Connect, Reset, Factory Reset), its confirm screen, and the touch handling that opens, steps through and closes it (`DrawResetMenu`, `HandleResetMenuTap`, `CloseResetMenu` in `src/AircraftManager.cpp`) | `POST /factory-reset` from the PC. It reaches the same `RequestReset(factoryreset::Tier::Factory)` and the same `[reset] tier=factory cleared=` line, but it skips the menu's drawing and every tap | due by code: any of those three functions, `src/FactoryReset.cpp` or `include/FactoryReset.h` |
| **the phone's QR setup** | the setup QR drawn on the panel (`include/SetupQr.h`, `include/QrRender.h`), and a phone camera reading it and joining `Blipscope-<6 hex>` | a PC Wi-Fi adapter joins the open setup network by name. No QR is read | due by code: `include/SetupQr.h`, `include/QrRender.h` |
| **the phone's captive-portal experience** | the phone noticing the portal and opening it, and WiFiManager's pages as a phone renders them, up to the save | the PC posts WiFiManager's save form directly | due by code: `include/WiFiManagerHelpers.h`, or the `tzapu/WiFiManager` line in `platformio.ini` |
| the location form and the config form, as UI | the page's own forms, typed into by a person | `POST /location` and `POST /save` with the page's own current values, read back from `GET /` | none of its own: the forms' handlers are what is exercised, the UI isn't |
| someone looking at the Collection page and the screen | a human eye on the result | `GET /` after the claim, graded on the request line | none: the acceptance never graded the display, automated or not |

**The claim is NOT on this list.** It stays the owner's tap in every run (answer (b)), so it is covered every time.

## The driver

A script beside the acceptance (`scripts/fresh-boot-auto.sh`, a name to confirm) wraps the acceptance's existing split
modes. It doesn't replace them:
- `--capture-only` (the single-open capture);
- the HTTP steps above;
- the Wi-Fi join;
- the prompt for his tap;
- `--probe-claim`, `--grade-cut` (around the hub cut), `--grade-off`, `--grade-boots` and `--assert-only`.

The acceptance's ten checks and the step-1 execution line (#664, or the frozen requirement if #664 hasn't landed) grade
the run exactly as they did on v19. **The automation changes who acts, never what is graded.**

## Controls, each made to fail once before it is trusted

| control | what it shows | how it fails |
|---|---|---|
| **K-reset** | the reset route isn't a no-op | `POST /factory-reset` without `confirm=RESET` reads `[POST] factory reset REFUSED`, with no `[reset] tier=factory cleared=` line |
| **K-guard** | the driver's POSTs carry the header the guard needs | a POST without `X-Blipscope` reads 403 |
| **K-wifi** | provisioning really joins | in the rehearsal only, never in a graded run: a wrong password first, and the device stays in its portal; then the right one, and `[WiFi] CONNECTED` |
| **K-loc** | the location took | the value posted is read back from `GET /` |
| **S-noreset** | the step-1 checks can go red | a rehearsal run that skips step 1 must turn "step 1: a FACTORY reset happened" (with #664's line) red |
| **S-nooff** | the step-6 checks can go red | a rehearsal run that skips step 6 must turn "step 6: the logbook was turned off" red |
| **selftest** | the assertions themselves | the acceptance's own `--selftest` passes before every run |

## Size and order

- **This registration:** 0.5 d.
- **The driver:** 2 d, plus 0.25 d for the cadence check and its five controls.
- **One rehearsal window:** 1 d, with the hub, his one tap, and each control and sabotage once.
- **About 3.75 d elapsed.**
- **The owner's three choices are answered** (yes to all three, 16:57Z). Nothing waits on him before the rehearsal window
  except the bench Wi-Fi password, which he provides into the local file.
- **If it isn't ready by v20's cut,** step 0 runs by his hands as it did on v19 (~30 min). The plan never makes the cut
  wait.

## Security, unchanged and stated

`X-Blipscope` is a CSRF guard, not authentication. Anything on the LAN can already `POST /factory-reset` with the
confirmation word, and this plan doesn't change that. It only uses it.

## Amendment 1: the driver as built, the end state, and the rehearsal's predictions (frozen 2026-11-13 ~02:30Z, before any window is asked for)

### Who acts (ruled 2026-11-13 02:03:41Z)

A session never types the owner's real password into a form, and never changes a Wi-Fi profile on this PC. So answer
(a) now means **"his PC provisions it, on his command"**: step 2 is ONE command he runs, `scripts/fresh-boot-wifi-join.ps1`.
- It reads his file and finds exactly one `Blipscope-XXXXXX` network on the adapter he names.
- It adds a temporary open profile, joins, and posts `s=`/`p=` to the portal in-process.
- It always removes the profile.
- It prints neither the setup network's name (part of the board's MAC), his network nor his password.

`scripts/fresh-boot-auto.sh` drives everything else, and stops at his two commands and his one tap.

### The test's location (ruled 02:03:41Z)

**A PUBLIC airport reference point, committed in the driver:** Seattle-Tacoma, `47.45, -122.31` (stored by the board as
`47.4500` / `-122.3100`, which K-loc reads back). It guarantees traffic for his claim tap, and carries nothing about him.

### The end state (the plan above was silent on it; v19 was "set up again by Daniel", `docs/v19-cut.md`)

A factory reset clears `wifi, wifi-fast, config, logbook`, so EVERY setting goes back to its default, not only the
location. Accepted 02:0xZ:
- **R0 (the driver, read-only, before step 1).** `GET /`, turned into the config form's own body by
  `scripts/fresh-boot-form.py`. It is written ONLY to `%USERPROFILE%\.config\blipscope\e208-snapshot`, never printed.
  The NAMES of any masked secret fields go into the report BEFORE step 1, so he knows what he'd re-enter by hand.
- **R1 (his command, after the grades).** It posts the snapshot back to `/save`. The command names the board's address,
  so it is written to `e208-restore.cmd` beside the snapshot, and only that path is printed.
- **R2 (the driver, read-only).** `GET /` compared with the snapshot field by field, printing "N of M fields equal" and
  the NAMES that differ, never a value.
  - **All equal (masked secrets aside):** the snapshot and the command file are removed, and the window may close.
  - **Anything else:** the window is NOT closed, the snapshot is kept for him, and the report says where.
- **Not restorable, stated:**
  - masked secrets come back masked and the save skips them (R0 names them);
  - the logbook's collection (NVS is never read from the PC);
  - Wi-Fi comes back through step 2's own join, to the same network.
- **No value reaches any output.** `fresh-boot-form.py --selftest` F7 runs a planted sentinel (asserted planted) through
  every printing and error path and finds it in none, while the count and names lines still print.

### What was built (this PR)

| file | what | checked by |
|---|---|---|
| `scripts/fresh-boot-cadence.py` | the hands-on cadence; exit 0 allowed, 1 due, 2 instrument | `--selftest`: K-due-count, K-due-code, K-due-func, K-not-due, K-counter, as registered (in CI) |
| `scripts/fresh-boot-form.py` | `form#cfg`'s FormData: `body`, `compare`, `field-equals` | `--selftest` F1-F7 (in CI) |
| `scripts/fresh-boot-auto.sh` | the driver: preflight, R0, steps 1-6, the grades, R1/R2; `--controls`, `--sabotage skip-reset/skip-off`, `--dry-run` | `--selftest` W1-W2, P1-P2, locally. Its dry-run runs the real cadence check, which needs the release tags CI's shallow checkout lacks |
| `scripts/fresh-boot-wifi-join.ps1` | his step-2 command; `-Control WrongPassword` for K-wifi | parsed with no errors. **Never run by a session** |

### The rehearsal window: what it needs

- **E2:08**, reserved, on YKUSH port 3 (confirmed by the owner, 2026-11-11 ~18:31Z), running the candidate under test.
  Every switch is logged: port, label, UTC off and on, session, window.
- **His file** `%USERPROFILE%\.config\blipscope\bench-wifi`, present.
- **His hands, five times:**
  - K-wifi's wrong-password command;
  - step 2's command;
  - the claim tap, three times (the full run, S-noreset's run, and S-nooff's run);
  - R1's command after each run.
- **About 90 minutes:** three runs of about 25 minutes, plus the controls.

The rehearsal is three runs:
1. `--controls`: the full run, with K-guard, K-reset and K-wifi in place;
2. `--sabotage skip-reset`;
3. `--sabotage skip-off`.

### The rehearsal's predictions

| row | predicted | if not |
|---|---|---|
| **RH0** | preflight passes: cadence ALLOWED on the candidate, both selftests and the acceptance's `--selftest` pass, `bench-wifi: present`, E2:08 in the listing and reserved | stop before anything is touched (exit 2), and report which |
| **RH1** | R0: `GET /` by its mDNS name reads 200, and the snapshot is written. The masked-field names are a READING, reported before step 1, not predicted | if mDNS does not resolve from this shell, R0 refuses with nothing changed; the driver then takes the address another way, in a new amendment |
| **RH2** | K-guard 403; K-reset refused, with no reset; step 1 POST 200 and `[reset] tier=factory` within 90 s | stop: the board is in the state the report names |
| **RH3** | K-wifi: no `[WiFi] CONNECTED` for 90 s after the wrong password. Step 2: his command prints `save: HTTP 200` and `profile removed: yes`, and `[WiFi] CONNECTED` follows within 15 min | stop: the board is in its portal, for his phone |
| **RH4** | step 2b: POST 200 and K-loc equal | stop |
| **RH5** | the full run: the acceptance's ten checks all pass, as on v19 (`--assert-only`), and `--grade-boots` reads one POWERON | a red check is a FINDING about the automation's equivalence to the hands-on run; it is reported, not re-run |
| **RH6** | S-noreset: the step-1 check reads RED and the rest pass. S-nooff: the step-6 check reads RED and the rest pass | the sabotage did not show: the acceptance cannot see that step, so stop and report |
| **RH7** | R2 after each run: every field equal, masked secrets aside; the snapshot removed after the last | the window is NOT closed: E2:08 is left as it is for him, with the snapshot kept |
| escape | anything the rows above do not name | stop and report, with no reading, and E2:08 left as it is |

### Addendum (2026-11-13 ~06:20Z): his terminal, the pinned adapter, two slots

These change who presses what, and where. They do not change what is graded: RH1-RH7 and the escape row stand as
frozen, and RH0 gains one refusal (below).

- **He runs the driver in his own terminal** (the owner, 06:01Z). That means Git Bash, launched by its full path
  (`C:\Program Files\Git\bin\bash.exe`). It is never a bare `bash` from PowerShell, which can resolve to WSL's.
  - The driver reads his Enter from the terminal it runs in (stdin first).
  - It copies everything it prints to `bench-logs/fresh-boot-auto-<stamp>.log`, and the session grades from that file.
  - `--by` stays `blipscope-5c`, because the window and the reservation are the session's.
- **The PC adapter is pinned to "Wi-Fi"** (the owner, 06:06Z). His internet is on Ethernet, and "Wi-Fi 5" is a
  virtual copy of the same card.
  - Step 2's script no longer takes an adapter argument, and it matches the adapter by its exact name.
  - `scripts/fresh-boot-wlan.py` reads `netsh wlan show interfaces` in-process. It prints four facts only: whether
    "Wi-Fi" is listed, its state, its radios, and whether "Wi-Fi 5" is listed. netsh's own text carries a MAC and,
    when connected, a network name, and none of it is printed.
  - **RH0 now also refuses** unless "Wi-Fi" is listed and disconnected, with both radios on.
  - Step 2's script refuses at its own start unless "Wi-Fi" is listed and disconnected.
  - Read at 06:08:41Z: "Wi-Fi" was listed, disconnected, with both radios on; "Wi-Fi 5" was also listed.
- **R1 is a file he runs**, not a line he copies.
  - It is a batch file. In a batch file `%%` is one `%`, so `-w "%{http_code}"` survives.
  - The driver's W3 runs one against a closed port and requires `000` with an empty stderr.
  - Three broken shapes each read red: a single `%`, a missing data file, and both together.
- **Two slots** (the owner, relayed 06:04Z):
  - 18:00-19:30Z: run 1 (`--controls`), then p4.
  - 20:00-20:50Z: run 2 (`skip-reset`, ~20 min), then run 3 (`skip-off`, ~30 min).
  - Run 3 starts only if it can finish R2 by 20:50Z, so it must start by 20:20Z. Otherwise it waits for another slot,
    for time, not failure.
  - Run 2's R0 snapshots the board as p4 hands it back, so p4's restore is the check on that hand-back.
- **New controls, each shown red once before being trusted:**
  - The reader's F1 (no MAC, network name or BSSID reaches its output), under an in-memory plant that appends the
    address.
  - Step 2's J4 (with only "Wi-Fi 5" present, "Wi-Fi" reads as not listed), under a planted prefix match.
  - Both selftests run in `device-ids.yml`.
- **His file's line ends** (the reviewer and the cloud reviewer, ~06:20Z). His file has CRLF ends, so every reader of
  it was checked for a kept trailing CR.
  - **The driver** reads the file for presence only, never a value. Its old check (`grep -c '^password=.'`) was
    correct on this PC by accident: Git for Windows' grep 3.0 strips a CR at the line end by default.
    `printf 'password=\r\n' | grep -c '^password=.'` reads 0 here, while `grep -U` and WSL's Linux grep read 1, so
    the check was blind to an empty CRLF line on any Linux grep.
    - `wifi_file_ok` now strips a trailing CR (and only that) with `sed`, and turns grep's own handling off (`-U`).
    - P3 (an empty CRLF password line refuses) was GREEN on the old check here, for the reason above.
    - P5 calls the function directly on four synthetic files. It is green on the fix and RED on a plant with the strip
      removed (Linux grep behaviour).
  - **Step 2's script** is the only reader that uses the values. Its reader (`Get-Content`, which returns lines
    without their CRLF or LF) already stripped the CR, so J6-J8 were GREEN on their first run under both
    PowerShell 5.1 and 7.
    - J6 and J7 compare the parsed values with made-up ones by length and bytes, inside the test, printing nothing.
      J8: an empty CRLF password line is refused.
    - A planted reader that keeps the CR (`ReadAllText` split on LF) turns J6 and J8 RED.
    - The values go to the board in-process (`Invoke-WebRequest -Body`), unchanged. No other path passes them on.
- **The hand-back check between the two slots** (the reviewer, ~06:20Z). Run 1's snapshot is deleted at its R2, so
  run 2's R0 alone cannot show that the window in between left E2:08's settings untouched.
  - Run 1 takes `--handback keep`: at R0 it keeps one salted SHA-256 per field NAME, with no values, as
    `e208-handback.hash` in his config dir. The hash is never printed and never overwritten.
  - Runs 2 and 3 take `--handback check`: R0 must hash equal before anything is written, or the run STOPS before step
    1, naming the fields that differ.
  - `--drop-handback` removes the hash at the end of the second slot.
  - The preflight refuses `keep` when a hash already exists, and `check` when none does.
  - In `fresh-boot-form.py`, H1-H5 cover it: equal, a changed value, an added field, refusing to overwrite, and a
    missing file not reading as equal. F7 also requires that the sentinel value is absent from the saved hash file.

### Addendum 2 (2026-11-16 ~05:00Z): the first live runs, the fixes, and what the next sitting watches

Nothing above is edited. The rows above graded the 2026-11-16 03:43-04:32Z attempts as follows. Before that night,
no run had gone past the dry run's exit, so every board step was live for the first time.

- **RH0:** the first start (03:52:12Z) was REFUSED with `INSTRUMENT: git show failed`. `fresh-boot-cadence.py` takes
  the repo from the current folder, and his terminal was outside the tree. Nothing was touched. The second start,
  from inside the tree, passed.
- **RH1:** R0 read `000` at 03:54:55Z, and the "if not" column held: nothing was changed. `board-port.py --host`
  already returns `<name>.local`, and the driver appended `.local` again. After the fix, R0 read 200 at 04:06:33Z
  (57 fields, no masked secret).
- **RH2 and RH3 passed:** K-guard 403, K-reset refused, the reset seen at 04:06:51Z, K-wifi held for 90 s, and
  CONNECTED at 04:12:19Z.
- **RH4 stopped:** step 2b's POST read `000`. The firmware restarts once after provisioning ("provisioned via the
  portal -- restarting so the config server can bind :80"), and the driver had taken the CONNECTED before that
  restart. Read afterwards, K-loc could not have passed either: it read saved-location slot 0 (`loc0-*`, empty after a
  reset) and expected `47.4500`. `/location` writes `latitude`/`longitude`, as `CoordParse::Format`, which strips
  trailing zeros: `47.45`.
- **R1, run by hand from the snapshot:** the restore left the logbook ON ("57 of 58; differ: logbook"). `/save` turns
  an absent box OFF only when the post carries `cfg-toggles`, which the page's own JS appends, and the form tool built
  its bodies without it. Daniel unticked the logbook on his page, and E2:08 then read 57 of 57 against the kept hash
  (04:31:54Z and 04:32:02Z).

**The fixes**, each shown red before green, on `blipscope-5c/fresh-boot-auto` (local until pushed behind the guards):

| commit | fix | the red-first proof |
|---|---|---|
| `9fd4a13b499d` | R0's host gets `.local` only when board-port's name lacks it | old line 000, new 200, old 000, back to back against the board |
| `0f1620ace1ba` | `cfg-toggles` in every form body (compare ignores it; the hash never sees it) | CT1/CT2 red on the old code, F5/CT4 red on a half fix, 22 green |
| `0f1620ace1ba` | step 2 waits for the CONNECTED after the provisioning restart, then for the page | replayed on that night's capture: old picks line 150, new picks 195; with no restart it keeps 150 |
| `0f1620ace1ba` | an early stop removes `$WORK` (his pages) | old trap left 1 work dir, new 0 |
| `74949590eb02` | K-loc reads `latitude`/`longitude` and expects `47.45` / `-122.31` | on a page built from the firmware's form lines: old STOPS, fields alone STOP, fix PASSES |

**The next sitting's added predictions.** These are new rows. The rows above still stand, as does the escape row.

| row | predicted | if not |
|---|---|---|
| **RH1-host** | R0 `GET /` by the single-`.local` name reads 200 | stop, nothing changed |
| **RH3-restart** | step 2 takes the CONNECTED after the provisioning restart, and the page answers within 60 s | stop at step 2, naming which wait ran out |
| **RH4-loc** | step 2b POST 200, and K-loc equal on `latitude`/`longitude` | stop |
| **RH5-cut** | step 5 reads CLAIMED. The post-cut steps address the board by its PRE-cut IP: if the board takes a NEW DHCP lease after the cut, step 5 reads NOBACK and the run stops | NOBACK is an honest stop and a FINDING, never a pass and never re-run on the spot |
| **RH5-off** | step 6's POST carries `cfg-toggles`, and `--grade-off` reads the logbook OFF | a red is a FINDING (as RH5) |
| **RH7-clean** | R2 equal, with `cfg-toggles` ignored, and no `fba-*` work dir left in his config folder after any end, early or not | report the dir by name; never its contents |

**Live for the first time at the next sitting (watch these):** R0's `check` branch; the R0 body carrying
`cfg-toggles`; step 2's new waits; step 2b and K-loc; step 3's claim wait from the driver; steps 4-6 and the grades as
called from the driver; step 5's own block (the background grade, the sleep, the switch); step 6's POST; R1 from a
`cfg-toggles` snapshot; R2's compare; the EXIT trap's `$WORK` cleanup; both sabotage branches; `--drop-handback`.

**Launch** from inside the tree (`cd` into it first), until the cadence check stops taking the repo from the current
folder.

**Correction to the launch line above (2026-11-16 ~05:06Z):** the cwd defect is now structural. The driver runs the
cadence check from its own tree (`( cd "$ROOT" && ... )`), so a launch from any folder works. Red first: from his home
folder the old driver refused with INSTRUMENT (exit 2); the fix passed both from there and from inside the tree. A `cd`
first is still harmless.

### Addendum 3 (2026-11-17 ~00:50Z): the rehearsal graded, and fixes 1-4 registered before their code

Nothing above is edited.

**The rehearsal, 2026-11-16 19:25-20:07Z, on `2c555062482d`.** All three runs took `--handback check`, and the hand-back
hash read equal before each one.

- **Run 1 (`--controls`): 10 PASS.**
  - Nine rows passed at the run.
  - The boots row was read afterwards (20:10Z): ONE_POWERON, with one anchor row. The owner's shell had no analytics
    token during the run, so `--grade-boots` could not query then. The re-read used the kept step files, with its
    prediction frozen first.
  - **K-wifi was VOID in the driver.** The Enter came before the wrong-password save, so the 90 s window graded
    nothing. Read by hand from the capture instead: no `[WiFi] CONNECTED` in the 97 s after the save (19:31:49-19:33:26Z).
    The reviewer ruled that reading to stand.
  - R2: 57 of 57.
- **Run 2 (`--sabotage skip-reset`): INCOMPLETE.**
  - His real settings had the logbook off. Step 1 was skipped, so nothing turned it on, and the owner ticked it by hand
    (19:53:50Z, ruled).
  - The driver exported the board's address only in step 2's join branch. With no address, `--grade-cut` wrote
    `NOCLAIM` and returned at once. The driver then cut the power with no watcher, and step 6's GET read `000`
    mid-reboot. The run stopped there.
  - Restored by hand: 57 of 57.
  - A post-hoc `--assert-only` on its capture, predictions frozen first: both step-1 rows FAIL, as predicted.
- **Run 3 (`--sabotage skip-off`):** 8 PASS, plus the two predicted step-6 FAILs. Boots ONE_POWERON. R2: 57 of 57.

**RH6, corrected here, not edited above.** RH6 predicted that in S-noreset "the rest pass". Two rows cannot, because
both depend on the reboot that step 1 causes:
- **The build banner.** The capture attaches to a running board and ends at the cut, so it holds no boot. Run 2's
  capture has no `[build]` line.
- **The boot count's anchor.** The anchor is a boot of this device in the hour before the cut.

The re-run's predictions below carry both rows.

**The four fixes** (the reviewer's merge bar, 2026-11-16 20:08Z). Every row is a selftest, and the order is fixed:
1. The rows go in first, with seams that keep the OLD behaviour.
2. The selftests run, and must show exactly the RED set below.
3. The fixes land, and every row must then read GREEN.
4. Every selftest row that existed before must stay as it was.

| fix | row | what it checks | old code | fix |
|---|---|---|---|---|
| 1 | E1 | `wait_enter`: a typed line is ignored (its text never printed), and the next empty line returns, consuming both | RED: returns at the typed line | GREEN |
| 1 | E2 | a typed line, then end of input, is not an Enter: non-zero | RED: returns 0 | GREEN |
| 1 | E3 | control: a bare CRLF Enter returns 0 | GREEN | GREEN |
| 1 | K1 | `kwifi_grade`: a portal save line and no CONNECTED within the window -> PASS | GREEN | GREEN |
| 1 | K2 | a save line, then a CONNECTED appended inside the window -> RED | GREEN | GREEN |
| 1 | K3 | THE PLANT: an Enter with no save line -> VOID | RED: PASS | GREEN |
| 1 | K4 | run 1's shape: the Enter first, the save line appended 3 s later, no CONNECTED -> PASS, the window opened at the save | GREEN | GREEN |
| 1 | K5 | a save line BEFORE the prompt does not open the window -> VOID | RED: PASS | GREEN |
| 2 | T1 | the dry run with no analytics token (neither `CF_API_TOKEN` nor `BLIPSCOPE_TOKEN_FILE`) refuses: exit 2, `analytics token: absent` | RED: exit 0 | GREEN |
| 2 | T2 | with a sentinel in `CF_API_TOKEN`: exit 0, `analytics token: present`, and the sentinel nowhere in the output | RED: no presence line | GREEN |
| 2 | T3 | `fba-boot-rows.mjs --check-token`: a token file with a sentinel `analytics-read=` line reads present (exit 0, the sentinel not printed); a file without that line reads absent (exit 3) | RED: usage, exit 2 | GREEN |
| 3 | B1 | `board_addr`: a local listener answering 200, by name -> its IPv4 address | RED: the old branch used the name itself | GREEN |
| 3 | B2 | a closed port -> empty (the run refuses) | RED | GREEN |
| 3 | B3 | a listener answering 501 -> empty | RED | GREEN |
| 3 | G1 | `precut_guard`: no `FBA_DEVICE_IP` -> refuses, no cut | RED: old waits 5 s and cuts | GREEN |
| 3 | G2 | the watcher already exited (its no-address line) -> refuses | RED | GREEN |
| 3 | G3 | control: an address, and a live watcher that printed its marker -> passes | GREEN | GREEN |
| 3 | G4 | a live watcher that never prints its marker within the guard's wait -> refuses | RED | GREEN |
| 3 | G5 | the marker the guard waits for is the one `--grade-cut` prints (run whole, through its seams) | GREEN | GREEN |
| 3 | FO1 | `fresh-boot-form.py body --on logbook`, on a page with it unticked, posts `logbook=on`, and `cfg-toggles` still declares it | RED: no `--on`, logbook absent | GREEN |
| 3 | FO2 | `--on` a name that is not a checkbox refuses (exit 2) and writes nothing | RED: exit 0 | GREEN |
| 3 | FO3 | control: `--on logbook`, on a page with it ticked, posts it exactly once | GREEN | GREEN |
| 4 | A1 | `--probe-claim`: a claim and no address -> `verdict=NOIP` | RED: NOPROBE | GREEN |
| 4 | A2 | `--probe-claim`: no claim -> `verdict=NOCLAIM` | RED: NOPROBE | GREEN |
| 4 | A3 | `--assert-only` with a `NOIP` probe: step 4 says the address was unknown, and never "not the first fetch" | RED | GREEN |
| 4 | A4 | `--grade-cut`: a claim and no address -> `verdict=NOIP` | RED: NOCLAIM | GREEN |
| 4 | A5 | `--assert-only` with step 5 `NOIP`: says the address was unknown, and never "No claim in the log" | RED: the BLIND text | GREEN |
| 4 | A6 | control: `--grade-cut` with no claim -> `NOCLAIM` | GREEN | GREEN |

The RED set on the old code is every row whose "old code" cell is not GREEN. The controls are the rest. If the reds or
the greens differ in any row, that is OFF: stop, and report before any fix lands.

**Where each fix is wired.** These run only on a board:
- **Fix 1.** K-wifi (`--controls` only) waits up to 180 s after the Enter for the portal's `/wifisave` line, counted
  from the prompt, then watches 90 s from there.
  - A VOID K-wifi is reported as VOID and ends the run with exit 4 after R2: a control that tested nothing is never a
    pass.
  - R1's Enter goes through `wait_enter` too, so a command typed into the driver's window is ignored instead of being
    taken as the Enter (run 2, 2026-11-16).
- **Fix 2.** The preflight prints `analytics token: present` or `analytics token: absent`, and refuses on absent (exit
  2) before anything is touched. It asks the boot-row reader's own resolver, so presence means "the query will have a
  token", not "a variable is set".
- **Fix 3.** In `skip-reset`, after R0:
  - The address comes from a fetch of the board's own page by name, over IPv4 (curl's `remote_ip`, never printed). It
    is exported as `FBA_DEVICE_IP`, exactly as step 2's join branch does. If it cannot be read, the run refuses (exit
    3) with nothing changed.
  - The logbook goes to its default, ON (`LOGBOOK_DEFAULT_ON`, which is what a factory reset leaves), by a `/save` of
    the page's own values plus the box. It is read back (K-lb).
  - Before EVERY cut, `precut_guard` requires an address, and requires the watcher to be alive and to have printed its
    watching line. Otherwise the run stops with exit 3 and the power NOT cut.
- **Fix 4.** The acceptance only.

**At the sitting (2026-11-17 22:00Z): run 2's re-run, `--sabotage skip-reset --handback check`.**

| row | predicted | if not |
|---|---|---|
| **RR0** | preflight passes, including `analytics token: present`, and the hand-back hash reads equal | stop (exit 2), nothing touched |
| **RR1** | skip-reset: the address is read (IPv4, not printed), and the logbook set ON and read back (K-lb) | stop (exit 3). After a failed K-lb, the logbook may be on, and R1 restores it |
| **RR2** | step 2b POST 200 and K-loc equal. Step 3: his tap. Step 4: the probe CLAIMED, `first=yes` | stop, as RH4 and RH5 |
| **RR3** | `precut_guard` passes and the cut is made. Step 5 CLAIMED. Step 6: OFF and CLAIMED | a red is a FINDING, as RH5 |
| **RR4** | `--assert-only`: FAIL exactly the banner and both step-1 rows. PASS: step 3, both step-4 rows, step 5, both step-6 rows. Boots: UNTRUSTWORTHY with `anchor=0` if E2:08 did not boot in the hour before the cut (expected), or ONE_POWERON with `anchor>=1` if it did. The two are told apart by the same query's anchor count | any other boots verdict (WRONG, NONE, UNSTABLE, a failed query), or any other FAIL: stop and report, with no reading improvised |
| **RR5** | R1 by his file. R2 equal, `cfg-toggles` ignored. No `fba-*` work dir left | as RH7 |
| escape | anything the rows above do not name | stop and report, with E2:08 left as it is |

Two of these rest on run 2's own capture:
- **Step 1's second row FAILs.** A `/save` that turns the logbook on mid-session prints no `[logbook] loaded` line: run
  2's capture holds 0 of them after his tick.
- **The banner row FAILs.** That capture holds 0 `[build]` lines.

**CI at the push:** 34 = 32 pass / 2 skipping (promote, version) / 0 fail. The bench-tools workflow (#735, no path
filter) adds the 34th.

#### Addendum 3, amendment A (2026-11-17 ~01:05Z): the RED step read OFF, and the fixes re-staged

Addendum 3 above is not edited.

**The RED step** (commit `a37e5f8d9c8e`, read 00:47-00:55Z):
- **As registered:**
  - the form: FAIL FO1 and FO2, PASS FO3, and every earlier row PASS;
  - the acceptance: FAIL A1-A5, ok A6, and every earlier row ok;
  - the driver: E1, E2, K3, K5, T3, B1-B3, G1, G2 and G4 red; E3, K1, K2, K4, G3 and G5 green, each for its
    registered reason.
- **OFF:** the driver's existing rows P1-control, P2-P4 and HB1-HB3 failed. T1 and T2 were red for the wrong reason
  (exit 2, without the token line).
- **The cause,** from one dry run, which touches nothing: `STOPPED: the acceptance's own --selftest failed`.
  - The driver's preflight runs the acceptance's and the form's own `--selftest`.
  - Both are red by design at the RED step, so every dry-run row of the driver stops at that line.
  - The registration missed that coupling. It is not a defect in any fix. Nothing was re-graded.

**The re-staging** (ruled 2026-11-17 ~01:00Z):
1. **Stage 1:** fix 4 (the acceptance) and fix 3's `--on` (the form) land alone. Both selftests must read all green,
   with every earlier row unchanged.
2. **The driver's selftest is then run with the driver still OLD.** Predicted:
   - P1-control, P2-P4 and HB1-HB3 back to PASS;
   - RED exactly E1, E2, K3, K5, T1, T2, T3, B1, B2, B3, G1, G2 and G4;
   - T1 red because the dry run exits 0 (no refusal), and T2 red with exit 0 and no presence line;
   - GREEN exactly E3, K1, K2, K4, G3 and G5;
   - every other earlier row (W1-W3, P1, P5, HB4) PASS.
3. **Outside the set:** any other row, any other reason, or an error = OFF. Stop and report, with no reading improvised.
4. **Stage 2:** the driver's fixes, then every row of all three selftests green.

### Addendum 4 (2026-11-18 ~15:45Z): the banner row counts only a boot DURING the capture

**Why.** Run 2's re-run (2026-11-18, skip-reset) graded "the build banner was captured" PASS where FAIL was frozen
(RR4). The banner was not from that run.
- On opening, a USB-Serial-JTAG board hands over the earliest output of its LAST boot in one burst: about 4.6 KB in one
  second. That burst was from a boot ~9.4 h earlier.
- The row grepped for any `[build] env=` line, so it passed whenever an earlier boot's output had gone unread. It could
  not show that the board booted during the run.

**The change** (registered and accepted before the code):
- **The capture** (`fresh-boot-capture.py`, capture mode) writes ONE line, `[capture] backlog ends: <N> line(s) before
  the first 0.5 s of silence`, at the first `readline()` that returns nothing.
  - The threshold is the port's own read timeout. No new threshold was invented.
  - The open, the handle and every other line are unchanged.
- **The row PASSES only if** a `[build] env=` line comes after that marker AND an `rst:0x` line sits between the marker
  and the banner.
- **Otherwise:**
  - with a marker: FAIL;
  - with no marker: UNTRUSTWORTHY. That is every capture written before this change. Such a log is never read as a
    PASS, and nothing about those runs is re-graded.

**Why the `rst:0x` line.** In the five kept captures:
- every banner from a reboot DURING a capture (6 of 6) had the ROM's `rst:0x` line 9 lines before it;
- no backlog held a ROM line (0 of 30 ROM lines fell inside one).

So a 0.5 s pause inside a backlog, which would put the marker before a stale banner, cannot pass the row (plant B11).

**The selftest's plants.** Each must turn exactly the banner row, counted over the whole run:

| plant | input | the row |
|---|---|---|
| B1 | a stale banner, then the marker, then nothing | FAIL |
| B2 | the marker, `rst:`, a banner | PASS |
| B3 | both | PASS |
| B4 | no marker | UNTRUSTWORTHY |
| B5 | no banner at all | FAIL |
| B11 | a pause inside the backlog: the marker before a stale banner | FAIL |

Two controls are printed, not counted:
- the rule before RR4 reads B1 as PASS;
- the rule as first registered (a banner after the marker, without the `rst:`) reads B11 as PASS.

**Named limits, with their direction:**
- **B10, conservative.** A board that printed with no 0.5 s silence from the open until after step 1's banner would put
  the marker after the live banner. The row would read FAIL on a good run.
- **B12, reassuring (a false PASS).** A backlog that carried an `rst:0x` line AND then paused 0.5 s before its stale
  banner would read PASS. It needs ROM output inside a backlog, which has never been seen (0 of 3 stale backlogs).
  There is no plant, because no real backlog has shown the shape.
- **B13, conservative.** A live reboot whose `rst:0x` line does not reach the capture, for example under another USB
  mode, would read FAIL on a good run, never a false PASS.

If a sitting shows B10, B12 or B13, it is reported under that name, not re-read.
