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
