# No committed file names the bench network (predictions first)

**The ruling (reviewer chat, pasted by Daniel, 2026-11-04), verbatim:**

> New, in parallel with #513: the Wi-Fi scrub. Daniel's decision: current files only, no history rewrite.
> - What: main's tree holds two Wi-Fi network names (in "Associated with" lines, 77 of them), two router BSSIDs (22
>   occurrences, bench-logs/, since 2026-08-07) and private LAN addresses (127, in 23 files). They are in bench-logs/,
>   tools/weatherscope/board/ records and docs/wifi-join-failure-eero.md. Find the rest with your own scan.
> - The PR replaces each one with a fixed placeholder. Never print a value: compare in memory, print counts only.
> - A CI guard in the forbidden.py style refuses, in any committed file: a network name in an association or ssid
>   context, a BSSID in any bssid context, and a private LAN address. Device MACs and device ids are out of its scope.
> - An allowlist covers documented examples, pinned by path and exact line.
> - Its control: a planted name, a planted BSSID and a planted LAN address are each refused, and the placeholders pass.
> - Predictions first: each category's count before (as your scan measures it) and 0 after.
> - Weatherscope's #514 redacts its own five new files. Other sessions' branches meet the guard when they next merge
>   main, and each redacts its own.

Every number below was read by a script that compares values in memory and prints only counts, classes and
`path:line`. No value appears in this file, in a commit message, or in any tool output of this work.

## What is on main (`892fe2b`), measured before any change

**The scan.** Every tracked file: 1,002 decode as UTF-8 text and are scanned. 414 are skipped, and every one of them
has a binary extension (jpg 245, png 72, wxc 55, bmpk 31, bin 5, woff2 3, 3mf 2, pdf 1). No tracked file is UTF-16 or
another non-UTF-8 text. *Not scanned, said out loud:* text inside images and inside the one PDF
(`docs/Blipscope-WiFi-Field-Test.pdf`; its source page, `docs/field-test-guide.html`, is scanned).

### The contexts

A **network name** is the value in one of these contexts (any case):

| context | form | where it comes from |
|---|---|---|
| `assoc` | `Associated with "<ssid>"` (quoted; also `\"..\"` and `'..'`) | the firmware's join log |
| `connecting` | `Connecting to SAVED AP: <ssid>` (or `NEW`) | WiFiManager |
| `wm_scan` | `*wm:[2] AP: -71 <ssid>`, `*wm:[2] DUP AP: <ssid>` | WiFiManager's scan list |
| `ssid_colon` | `SSID: <ssid>`, colon directly after the word | logs, the setup screen |
| `ssid_eq` | `ssid = "<ssid>"` (quoted), `ssid=<ssid>` (bare, no spaces), `data-ssid='<ssid>'` | code, URLs, the portal's HTML |
| `ssid_key` | a quoted `ssid` key with a quoted value, as in JSON | records |
| `ssid_arg` | `-Ssid "<ssid>"`, `--ssid "<ssid>"` | script usage |
| `connected_with` | ESP-IDF's `connected with <ssid>, aid` | the IDF log; none in the tree today |

A value **passes** when it is empty, a placeholder (`<...>`), a printf format (`%s`), a template variable (`${x}`,
`{x}`), or a device's own setup-AP name (`<edition>scope-` and six hex digits, made from its MAC: out of scope).
Anything else is a finding.

A **BSSID** is a MAC-shaped value after `bssid` or `bss` (`check_bssid.py`'s pattern, unchanged). A **private LAN
address** is any address in 10/8, 172.16/12 or 192.168/16.

### Network names: 113 findings, 113 lines, 32 files

- **107 are one name, the bench network's:**
  - 71 in `assoc` lines (the firmware's join log);
  - 36 in `connecting` lines (WiFiManager).
  - By directory: bench-logs 98, tools/weatherscope/board 9. They are in 28 files.
- **6 are not a network**, and go on the allowlist:
  - `scripts/redact-vectors.txt:11`, `:12` and `:13`: the redaction test's inputs, deliberately unredacted.
  - `scripts/wifi-join-capture.ps1:29`: a usage example.
  - `include/WiFiManagerHelpers.h:569`: a comment showing the scan entry's HTML.
  - `src/AircraftManager.cpp:4169`: UI copy for an absent SSID.
- **59 contexts already pass:** placeholders, one printf format, and 28 setup-AP names.

**This differs from the ruling's description.** My scan finds one distinct network name, in 71 association lines,
not two names in 77.
- The 80 association contexts in the tree are:
  - 71 that hold the name;
  - 8 placeholders, all `<ssid>` (Weatherscope's runs of 2026-10-30, and the eero doc);
  - 1 printf format (`src/main.cpp`).
- The name is also in 36 WiFiManager lines, which the ruling does not list.

If the ruling's second name is a value my contexts do not see, please name its line or its context, and it becomes a
row here.

**`docs/wifi-join-failure-eero.md`:** my scan finds no network name, BSSID or private address in it. Its one
association line already holds `<ssid>`, and it has no MAC-shaped value and no private address at all. The same
request applies: name what was seen there.

**The bench name also appears 4 times outside any network context:**
- where: `proxy/photos/credits.html` (2) and `proxy/photos/manifest.json` (2);
- what: word-bounded, inside photo attributions; it is a common word.

Those are licence attributions. The scrub replaces only inside a context, so it does not touch them (row W7).

### BSSIDs: 22 findings, 22 lines, 13 files, 2 distinct values

All are in `bench-logs/`, each the fast-join line (`[WiFi] fast join: ch=1 bssid=<MAC>`). They are exactly the 13
files `check_bssid.py` allows by name (`docs/bssid-guard.md`).

### Private LAN addresses: 152 occurrences, 149 lines, 37 files, 13 distinct values

- **bench-logs: 127 in 23 files**, the ruling's count. tools/weatherscope/board: 0.
- **The portal address, 192.168.4.1: 40 occurrences in 40 lines, 16 files.**
  - 31 are in bench-logs, every one a WiFiManager portal line: `*wm:AP IP address:`, `*wm:[2] -> `, and its DNS
    server.
  - 9 are in docs/, proxy/ and src/, each naming the setup portal.
  - None of the bench's own addresses shares the portal's /24.
- **The rest: 112 occurrences.**
  - **100 are real, and the scrub replaces them,** in 100 lines across 23 files: bench-logs 96, docs 2, scripts 1,
    src 1. Outside bench-logs they are:
    - `docs/nvs-config-flip-2026-08-27.md:15`;
    - `docs/v11-quiet-hour-plan.md:499` and `scripts/check-config-form.py:415`, both the bench board's address as the
      logs carry it;
    - `src/game/GameClient.cpp:14`, a dev-server address in a build-flag comment.
  - **12 are documented examples, and go on the allowlist,** in 10 lines across 7 files:
    - `README.md:308`, `src/AircraftManager.cpp:543`, `src/ConfigurationWebServer.cpp:857`, `:1090`, `:2281` and
      `:2374`, `src/speed/SpeedFeedClient.h:33` and `tools/claudescope-sidecar/README.md:42`: the "e.g." value and the
      config form's `placeholder=` hints;
    - `include/QrRender.h:24`: an illustrated URL length;
    - `proxy/test/locatepage.test.ts:88`: a fixture of private hosts the page must refuse.

**How I separated real values from examples, so the reviewer can check it:**
- A value that appears in any log is real wherever it appears, even under "e.g.".
- A value that is in no log is an example only where its line marks it as one: "e.g." or "for example", a
  `placeholder=` attribute, a test fixture, or an illustrated length.
- Everything else is real. That is why the two unmarked values (the nvs doc and the game-client comment) are
  scrubbed, though neither is in a log.

### The categories, before and after

| category | findings before (empty allowlist) | scrubbed | allowlisted | passes by value | findings after |
|---|---|---|---|---|---|
| network name | **113** (113 lines, 32 files) | 107 (107 lines, 28 files) | 6 lines, 6 findings | | **0** |
| BSSID | **22** (22 lines, 13 files) | 22 (13 files) | 0 | | **0** |
| private LAN | **152** (149 lines, 37 files) | 100 (100 lines, 23 files) | 10 lines, 12 findings | 40, the portal (Q-W2) | **0** |

**The scrub touches 32 files:** bench-logs 19, tools/weatherscope/board 9, docs 2, scripts 1, src 1. The allowlist has
**16 entries**.

## The change

1. **The scrub.** Each value is replaced in place by one fixed placeholder per category, only inside its context:
   - `<ssid redacted>` for a network name;
   - `<bssid redacted>` for a BSSID;
   - `<lan-ip redacted>` for a LAN address.

   The first two are the tokens the recorders' redaction already writes. A scratch script does it; it is not
   committed. It computes the new text in memory, writes it, and prints counts per category and per file. Nothing
   else in any line changes.
2. **The guard, `scripts/check_wifi.py`,** in `forbidden.py`'s style: it prints verdicts, `path:line` and the
   category, never a value.
   - **Its contexts and pass rule** are exactly the tables above.
   - **Its allowlist is `scripts/wifi-allow.txt`.** Each entry is the path, the SHA-256 of the exact line (without its
     line ending), and a reason.
     - The hash pins the exact line without repeating its value, so the allowlist carries nothing to guard.
     - An entry whose path and line match no tracked line is refused as **stale**.
     - An entry whose line has no finding is refused as **allowing nothing**.
     - The same line at another path, or with one character changed, is not allowed.
   - **Unreadable is FAIL.** A tracked file that is neither UTF-8 nor UTF-16 with a BOM, and whose extension is not
     on the binary list, is refused by name. A UTF-16 file with a BOM is decoded and scanned.
   - **BLIND is exit 2.** The guard exits 2 if `git ls-files` fails, if it scans no file, or if it sees no network
     context at all. After the scrub the tree still holds at least 107 placeholder contexts, so seeing none means the
     guard cannot see.
   - Exit codes: 0 clean; 1 a finding, a stale or idle entry, or an unreadable file; 2 rig or BLIND.
3. **CI.** The `device-ids` workflow runs the guard and its selftest.

**Beyond the ruling, named so they can be struck:**

- **Q-W1: `check_bssid.py` folds into the new guard.** After the scrub its 13 named files carry no BSSID, and it refuses
  a stale allowance, so as it stands the scrub turns it red.
  - *Recommended:* one guard per rule. It is deleted; its three plant forms (the fast-join line, `BSSID:` with dashes,
    the jdiag `bss` line) move into the new selftest; `docs/bssid-guard.md` gains a superseded note; and two comments
    that cite it (`scripts/check_redaction_vectors.py`, `scripts/redact-vectors.txt`) are updated.
  - *Alternative:* keep it, with an empty allowance and without its stale-file plant. Two guards would then hold one
    rule.
- **Q-W2: 192.168.4.1 passes by value.**
  - Why it identifies nothing: it is the ESP32 SoftAP's fixed portal address, the same on every board. Where a router
    uses it (eero's default gateway), it is the same on every such router.
  - *Alternative:* allowlist its 40 lines by path and line, 56 entries in all.
- **Q-W3, a follow-up, not in this PR.** The recorders' shared rule set (`scripts/redact-line.ps1`,
  `scripts/redact_line.py`) does not redact the `assoc` line, the `connecting` line or a LAN address, so every future
  bench log meets this guard unredacted. I recommend adding those rules the #473 way, vectors included, as their own
  PR.

## Predictions

| | check | predicted |
|---|---|---|
| **W1** | the guard on main's tree (`892fe2b`), with the PR's allowlist | **exit 1**, with exactly the "scrubbed" column: name 107, BSSID 22, LAN 100 findings. Its 16 entries are each used once; 0 stale, 0 unreadable |
| **W1b** | the same, with an empty allowlist | **exit 1**: name 113, BSSID 22, LAN 112 (152 with the portal not exempt) |
| **W2** | the scrub | **107 + 22 + 100 replacements in 32 files**, and nothing else. Every changed line differs from main only at the replaced spans, every touched file keeps its line count and line endings, and no other file changes |
| **W3** | the guard on the branch | **exit 0**: 0 findings in each category; 16 entries, each used; 0 stale; 0 unreadable. It sees at least 107 network contexts, all passing |
| **W4** | the control (selftest), each plant on its own, in a copy of the tree | each turns the guard red naming exactly the planted file and category: **(a)** a name in an `assoc` line; **(b)** a name after `ssid=`; **(c)** a BSSID in the fast-join form; **(d)** `BSSID:` with dashes; **(e)** the jdiag `bss` form; **(f)** one address each from 10/8, 172.16/12 and 192.168/16 (3 findings). **The placeholders pass:** a file holding all three placeholders in their contexts has 0 findings |
| **W5** | the allowlist and the rig, in the same selftest | **(g)** an allowlisted line copied to another path is red; **(h)** the allowlisted line with one character changed is red, and its entry is stale; **(i)** an entry for a line with no finding is refused; **(j)** a tracked text file that does not decode is refused as unreadable; **(k)** an empty tree exits 2 (BLIND) |
| **W6** | the rest | `check_redaction_vectors.py` and its selftest pass (only comments change); `check_device_ids.py` and `check_workflow_concurrency.py` (with its selftest) pass |
| **W7** | the bench name in the tree after the scrub | **exactly 4 occurrences, all in `proxy/photos/`** (the attributions). So the scrub missed no context and touched no attribution |

**The sabotages.** Each must turn its row red, and each is run once and observed red before the row is believed:

| sabotage | turns red |
|---|---|
| **S-W3**: one scrubbed log restored from main | W3, naming that file, in all its categories |
| **S-W4N**: the `assoc` and `ssid_eq` contexts removed from the guard | W4 (a) and (b) |
| **S-W4B**: the BSSID pattern removed | W4 (c), (d), (e) |
| **S-W4L**: the LAN pattern removed | W4 (f) |
| **S-W4P**: the placeholder pass removed | W4's placeholder control, and W3 |
| **S-W5G**: the allowlist keyed on the line's hash alone, path ignored | W5 (g) |
| **S-W5J**: an undecodable file skipped instead of refused | W5 (j) |
| **S-W5K**: the BLIND rule removed | W5 (k) |
| **S-W7**: the scrub widened to replace the name anywhere, not only in a context | W7 (the attributions change) |

**CI on the PR:** `device-ids` is green with the new steps. Every other workflow is unaffected: outside logs and docs,
the scrub changes one comment line in `src/` (`GameClient.cpp:14`) and one message string in a script
(`check-config-form.py:415`). `firmware` builds as on main.

**Outside the set:** stop and report, with no fix until a ruling.

## Results

### Two misses, recorded before anything was pushed

**W2, first run: OFF-PREDICTION.** The scrub replaced **name 93**, BSSID 22, LAN 100 in 32 files. W2 predicted name
107.
- **What the guard saw.** On the scrubbed tree it reported **14 name findings**, every one a WiFiManager
  `Connecting to NEW AP: <ssid>` or `SAVED` line, in 7 bench-logs.
- **The cause.** Those logs end lines in `\r\r\n` (serial output; one of them holds 759 bare CRs). The guard strips
  every trailing CR from a line; the scrub stripped one. A `connecting` value cannot cross a CR, so on those 14 lines
  the scrub's pattern never matched, while the guard's did.
- **Checked against main's blobs, not inferred.** The lines that end in two or more CRs number 2,109. Comparing the two
  line definitions over all of them gives a gap of exactly **name 14, BSSID 0, LAN 0, in 7 files**.
- **The alternative, and why the data excludes it.** The 14 might have been other networks, values the scrub would
  have refused. They are not: the fixed scrub compares every value to the bench name in memory and refuses anything
  else, and it refused none.
- **The fix (local, unpushed until a ruling).** The 32 files were restored to HEAD (32 of 32 byte-identical). The
  scrub now uses the guard's line, stripping every trailing CR and restoring them on write. Re-run from the clean tree:
  **name 107, BSSID 22, LAN 100 in 32 files**, W2's numbers.

**W3, first run: OFF-PREDICTION.** It read **name 1**. The finding was in this PR's own guard: a selftest label in
`scripts/check_wifi.py` read as a quoted value after an `ssid` assignment. The label was reworded. The guard was
right: it was the first file of this PR it had been run over.

**Found while building, before any row ran (not misses):**
- **Decode first.** The guard's first draft skipped files by extension: 420, against the 414 that are truly binary.
  Six tracked files with a "binary" extension decode as text: five ASCII STEP files and one terrain `.bin`. The guard
  now decodes first and scans whatever decodes; the binary list only excuses a file that does not decode, as this doc
  already described.
- **The selftest's anchor.** Every planted tree carries an anchor, so that a plant is judged FAILED rather than BLIND.
  The first anchor was a placeholder in an `assoc` line, which would have reddened every plant through BLIND under
  S-W4N or S-W4P, not just the plant each sabotage targets. It is now three contexts of three kinds, passing by a
  printf format and a setup-AP name.

### The sabotages' red sets, registered before any of them ran

**The mechanism.** S-W4* and S-W5* are in-memory edits to the guard's source, applied to a loaded copy. Each edit is
asserted to apply exactly once, and the file on disk is checked byte-identical before and after.

Each sabotage is read three ways:
- **W3:** the guard on the real tree.
- **full:** the selftest as CI runs it, with its precheck, which first requires the real tree to pass.
- **rows:** the selftest's rows without that precheck. `selftest(precheck=False)` exists only for this.

The inputs behind each set were read first:
- 142 placeholder contexts sit outside allowlisted lines, in 39 files;
- the entries' lines hold LAN only (10), `ssid_eq` only (3), `wm_scan` (2) or `ssid_arg` (1);
- the README entry's line holds no network context;
- no allowlisted line occurs at a second path.

| sabotage | W3 | full | rows red (and only these) |
|---|---|---|---|
| **S-W4N**: `assoc` and `ssid_eq` removed | exit 1: 3 entries **idle** (their `ssid_eq` lines lose their finding), 0 findings | exit 2 (RIG) | (a), (b) |
| **S-W4B**: the BSSID pattern never matches | exit 0 (no BSSID left to find, none allowlisted) | exit 1 | (c), (d), (e) |
| **S-W4L**: the LAN pattern never matches | exit 1: 10 entries **idle** | exit 2 (RIG) | (f), (f)'s count, the real allowed line, (g), (h) |
| **S-W4P**: the placeholder pass removed | exit 1: **name 142** | exit 2 (RIG) | the placeholders row |
| **S-W5G**: allowlist matched on the line's hash alone | exit 0 | exit 1 | (g) |
| **S-W5J**: an undecodable file counted as binary | exit 0 | exit 1 | (j) |
| **S-W5K**: BLIND never set | exit 0 | exit 1 | (k) |
| **S-W3**: `bench-logs/bindctl-2026-08-07-2023.log` restored from main | exit 1: **3 findings, all in that file**: name 1, BSSID 1, LAN 1 | exit 2 (RIG) | not run |
| **S-W7**: the scrub run with `--everywhere` on a fresh copy of main | W7: the bench name **0 times** (the 4 attributions replaced), and both `proxy/photos` files changed | | |

**Two consequences, said before they are seen:**
- S-W4B is invisible to W3. The tree holds no BSSID, so only the selftest's plants can catch a dead BSSID pattern.
  That is what the control is for.
- S-W4N and S-W4L are caught by W3 itself, through the idle rule. An allowance whose line stops producing a finding is
  refused, so removing a context or a range is visible on the real tree.
