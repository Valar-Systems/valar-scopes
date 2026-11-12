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
