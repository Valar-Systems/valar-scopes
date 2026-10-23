# No committed log carries a BSSID (predictions first)

**The ruling (reviewer chat, pasted by Daniel, 2026-10-23), verbatim:** "No history rewrite. The two BSSIDs have been
public since August, GitHub keeps merged PRs' commits reachable regardless, and a rewrite would change every SHA that
approvals, releases and records cite. Whether to take the home network out of location databases is Daniel's call,
outside the repo. In the repo, stop it recurring:
(1) From now on, no committed log carries a BSSID. The fast-join line is redacted the way the SSID already is.
(2) Add a check beside no-real-device-ids that fails on a MAC-shaped value after 'bssid' in any committed file, with a
selftest plant, predictions first. The existing logs stay as they are, and the check allows exactly those 13 files by
name.
(3) At the firmware's next change, the fast-join log prints only the BSSID's last two bytes, since customers' logs can
carry it too."

## What is on main (`1b724bc`), measured before any change

- **The 13 files.** A MAC-shaped value after "bssid" (`bssid[^0-9a-f]{0,20}(xx[:-]){5}xx`, any case) appears in exactly
  13 tracked files, all under `bench-logs/`. Each carries the fast-join line, `[WiFi] fast join: ch=1 bssid=<MAC>`.
- **Where the SSID is redacted.** `scripts/bench-capture.ps1`'s `Protect-Line` redacts at write time: the password,
  and the three forms of the scan list (`<ssid redacted>`).
- **Two other recorders skip that guard entirely,** found while measuring:
  - `scripts/bench-trace.ps1` writes each line raw (`Add-Content`): no password, SSID or BSSID rule.
  - `scripts/fresh-boot-capture.py` writes each line raw as well. Four of the 13 files are its logs.

  This is the "a guard existed, and one path did not go through it" shape (CLAUDE.md).
- **A second firmware form.** `include/JoinDiagScreen.h:285` prints every heard BSSID as `[jdiag]   bss <MAC>`. No
  committed file carries that form today.

## The change

1. **One redaction rule set for every recorder.** `Protect-Line` moves out of `bench-capture.ps1` into
   `scripts/redact-line.ps1`, which `bench-capture.ps1` **and** `bench-trace.ps1` both dot-source.
   - It gains the BSSID rule: a MAC after `bssid` or `bss` becomes `<bssid redacted>`, the way the SSID becomes
     `<ssid redacted>`.
   - `fresh-boot-capture.py` gets the same rules in Python.
   - **So the two implementations cannot drift,** one file of test lines and their expected redactions,
     `scripts/redact-vectors.txt`, is checked against both: `pwsh` and `python` in CI, the same vectors, and the
     whole output compared.
2. **The check, beside `no-real-device-ids`:** `scripts/check_bssid.py`, run with its selftest in the `device-ids`
   workflow.
   - It fails on a MAC-shaped value after `bssid` (or `bss`) in any tracked text file **except exactly the 13 files,
     named**.
   - It also fails when a named file no longer carries one: a stale allowance is refused, as `skus.py` refuses a stale
     reviewed mention.
3. **The firmware:** the fast-join line prints only the last two bytes, `bssid=..:..:..:..:AB:CD`. **So does the jdiag
   line.**

**Beyond the ruling, named so they can be struck:**
- the `bss` form, in both the check and the redaction;
- the password and SSID rules reaching `bench-trace.ps1` and `fresh-boot-capture.py`, because they come with the
  shared rule set;
- the jdiag line's two bytes.

**Not changed, reported:** `scripts/wifi-join-capture.ps1` puts the SSID in its log's **file name**. No such file is
committed today.

## Predictions

| | check | predicted |
|---|---|---|
| **B1** | `check_bssid.py` on main's tree | passes, with **exactly the 13 named files** allowed and nothing else found |
| **B2** | its selftest, one plant at a time in a copy of the tree | each turns it red naming exactly the planted file: **(a)** a new file with the fast-join form (`bssid=` and a placeholder MAC); **(b)** the other case and separator (`BSSID: ` and a dash-separated placeholder); **(c)** the jdiag form (`bss ` and a placeholder); **(d)** one of the 13 with its MAC removed (stale) |
| **B3** | the redaction vectors, through `pwsh` (`redact-line.ps1`) and `python` (`fresh-boot-capture.py`) | **identical output, equal to the expected column.** The fast-join line becomes `bssid=<bssid redacted>`; the jdiag line `bss <bssid redacted>`; the firmware's new two-byte form is left alone; the password and SSID lines as `Protect-Line` already does. *Sabotage:* removing the BSSID rule from either implementation turns B3 red |
| **B4** | the firmware | the fast-join and jdiag lines print `..:..:..:..:%02X:%02X`; every firmware env builds in CI |
| **B5** | the rest | `check_device_ids.py`, `skus.py --check-readers` and the host suite pass. `bench-capture.ps1` still records: its `Protect-Line` now comes from the dot-sourced file, and `pwsh -Command` loads both scripts without error |

**Amended before any code ran (2026-10-23):** B2's plants were first spelled out here as full placeholder MACs,
which would have tripped B1 on this file itself. They are now described. The selftest builds its MACs at run
time, and the vectors use `{MAC1}` / `{MAC2}` tokens that `check_redaction_vectors.py` fills in. **No committed
file of this PR holds a literal MAC after `bssid`.**

**Outside the set:** stop and report.
