# No tracked file's NAME carries a LAN address either (predictions first)

**The ruling (reviewer chat, pasted by Daniel, 2026-11-04 ~19:15Z), verbatim:**

> My independent scan of main at ec4f0d4, plus #521 and #522, by value for what the scrubs removed (the network
> name, the two BSSIDs, the seven LAN addresses): the network name: none. The BSSIDs: none. The addresses: none in
> any file's contents, but two file NAMES carry one: bench-logs/prelash-backup/logbook-<address>-20260812-1303.json
> (both added 2026-08-18 in 273062b). check_wifi.py reads contents only.
> New small PR, predictions first: rename both files so no name carries an address (fix any reference to them), and
> make check_wifi.py check every tracked path too, with a sabotage: a planted file whose name holds a private address
> turns it red, naming exactly that path. Files only, no history rewrite, as Daniel chose.

## What is on main (`ec4f0d4`), read before any change

- **The guard's patterns applied to every tracked PATH** (1,520 paths, in memory):
  - exactly **2 paths** have a finding, both LAN, both in `bench-logs/prelash-backup/`;
  - they hold **2 different addresses**, one per name, and the two names are otherwise identical.
  - No path has a network-name or BSSID finding.
- **That directory holds only those two files.** Nothing in the tree names the directory or either file, and no glob
  matches them, so there are no references to fix.
- **The guard on main:** exit 0 on contents, with 1,520 tracked and 0 findings. It does not read paths.
- **S-W4P's input** (placeholder contexts outside allowlisted lines) is **149** on main. It was 143 on #517's branch,
  and main has gained files since.

## The change

1. **The rename.** Each file is renamed with its address replaced by `lan-ip-redacted-1` or `lan-ip-redacted-2`,
   numbered in the sorted order of the old names. Angle brackets cannot be in a Windows file name. The contents do not
   change.
2. **The guard reads every tracked path too.** Each path goes through the same three categories as a line of content,
   before the file is opened, so a binary's name is checked as well.
   - A path finding is reported against the path itself.
   - **When it prints a path, the guard masks each finding's span with its placeholder.** A path that carries an
     address would otherwise print the address, and the guard never prints a value.
   - A path finding cannot be allowlisted.
3. **The selftest's new row, (m):** a planted file whose NAME holds a private address (synthetic, built at run time,
   outside the portal's /24) and whose contents are clean.
4. **The harness:**
   - the new sabotage **S-WPATH** (the path check removed);
   - `(m)` among the rows;
   - W8 extended to (m)'s address.

## Predictions

| | check | predicted |
|---|---|---|
| **P1** | the new guard on main's tree (`ec4f0d4`) | **exit 1: exactly 2 findings, both LAN, both PATHS** in `bench-logs/prelash-backup/`. 0 content findings. **0 private addresses in its printed output** (each path masked) |
| **P2** | the guard on the branch | **exit 0**: 0 path findings and 0 content findings; 16 entries, each used |
| **P3** | the rename | `git diff -M --name-status` against main: **exactly 2 renames, both R100** (contents byte-identical), and the doc and the two scripts. No other file |
| **S1** | the selftest | every row as predicted, **16 rows**: the 15 of before, and **(m)**: exit 1 with **exactly 1 finding, that path's, LAN** |
| **W8** | the harness | (m)'s planted address equals no real address, along with every other plant |
| **the harness** | `check_wifi_sabotage.py` | each sabotage as registered below; 0 instrument failures; **exit 0** |

**The sabotages' red sets: every row evaluated against every sabotage, (m) included**

(m)'s inputs are the LAN pattern, the portal comparison, the path check and the anchor.

| sabotage | W3 | full | rows red (and only these) |
|---|---|---|---|
| **S-WPATH** (new): the path check removed | exit 0: the branch has no path finding | exit 1 | **(m)** |
| **S-W4L**: the LAN pattern dead | exit 1, idle 10 | exit 2 | (f) one, (f)'s count, the real allowed line, (g), (h), (l), **and now (m)**: 7 |
| **S-W2PORTAL**: the exemption widened to the portal's /24 | exit 0 | exit 1 | (l) only. (m)'s address is outside that /24 |
| **S-W4P**: the placeholder pass removed | exit 1: **name 149** | exit 2 | the placeholders row |
| S-W4N, S-W4B, S-W5G, S-W5J, S-W5K, S-W3, W2', S-W7 | as before | as before | as before. None reads (m)'s inputs |

**CI:** predicted in a PR comment right before the push, from the last finished run, with every step's `if:`
evaluated per leg, and from main as it is at that moment.

**Disk:** C: 20 GB free at 19:23Z, under 30 GB, so no large build. This is two renames and the guard's own runs.

**Outside the set:** stop and report, with no fix until a ruling.

## Results (local, Windows, Python 3.14; every reading as registered)

| | read | verdict |
|---|---|---|
| **P1** | the new guard with `--root` on a clean checkout of `ec4f0d4`: exit 1; 1,520 tracked; findings name 0, BSSID 0, **LAN 2, both the paths** in `bench-logs/prelash-backup/`, each printed masked. **0 private addresses in its printed output.** Stale, idle and unreadable 0 | as predicted |
| **the rename** | done in memory, numbered in sorted order: `logbook-lan-ip-redacted-1-…` and `-2-…`. Neither new name has a finding | as designed |
| **P2** | the guard on the branch: exit 0; name 0, BSSID 0, LAN 0; 16 entries, each used | as predicted |
| **P3** | `git diff -M --name-status` against main: **R100 twice** (the two renames), `A docs/wifi-paths.md`, `M` the two scripts. Nothing else | as predicted |
| **S1** | the selftest: **16 rows**, every one ok. **(m): exit 1, exactly 1 finding, that path's, LAN** | as predicted |
| **the harness** | the control green. **S-WPATH: W3 0, full 1, rows red (m)**. **S-W4L: W3 1 (idle 10), full 2, rows red (f) one, (f)'s count, the real allowed line, (g), (h), (l), (m)**. **S-W4P: name 149**. S-W2PORTAL: (l) only. S-W4N, S-W4B, S-W5G, S-W5J, S-W5K, S-W3, W2' and S-W7 as before. 0 instrument failures; the guard byte-identical; **exit 0** | as registered |
| **W8** | the planted name, MACs and addresses, `(m)`'s included, and the README example line's address: none equals a real value | as predicted |
