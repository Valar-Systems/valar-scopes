# The Python token readers: one helper, one rule (registration, predictions first, 2026-11-12)

**For:** the reviewer. **Status:** frozen before any code. **Merge condition, set by the reviewer:** not before
weatherscope's `shsr` deploy and its readings are done. Weatherscope has been sent the file list.

## 0. Why

CLAUDE.md's standing rule names the token file in `BLIPSCOPE_TOKEN_FILE`, set per shell. `proxy/scripts/token-file.ts`
applies that rule for the TypeScript readers. **Twenty Python instruments open `~/.config/blipscope/tokens` directly
instead (23 places).** They ignore the variable, and apply none of its refusals. The reviewer, 2026-11-12: "Two readers
of one secret file should apply one rule."

## 1. What gets built

| file | what |
|---|---|
| `tools/token_file.py` | the helper. `token(purpose)` and `tokens(purposes)` return values, never printed. It mirrors `token-file.ts`'s `readTokenFileText` and `parseTokenFile`; the refusals are listed below. `--selftest` runs T1-T10 (section 3). `--check` prints, for the file the variable names, only booleans and key NAMES |
| the 20 readers | each replaces its direct open with the helper, loaded by path through `importlib`, since the tools run under `python -I`, which drops the script's folder from `sys.path`. No other line changes |
| `scripts/check_token_file_readers.py` | **the guard that runs.** It refuses any tracked `.py` that opens the token file directly (an `open(` or `expanduser(` naming `blipscope/tokens`). `--selftest` plants one (G2) |
| `.github/workflows/device-ids.yml` | three steps: the guard's `--selftest`, the guard, and the helper's `--selftest`. No new job, so no job-set change |
| `tools/token_file_equiv.py` | the before/after harness (section 4). It is run locally, and its output goes into section 5 |

**The refusals, one-to-one with `token-file.ts`:**
- `BLIPSCOPE_TOKEN_FILE` unset or blank;
- the file missing;
- the path inside this repo or inside any git working tree;
- on POSIX, a file readable by group or others (`mode & 0o077`). On Windows only a note is printed, as in the TS file;
- a non-blank, non-`#` line without `key=value`. The error gives the line number only, never the content;
- a key that appears twice;
- the key missing, or empty.

**Purposes:** the four these readers use: `analytics-read`, `observability-read`, `r2-wx-read-id` and
`r2-wx-read-secret`. Any other key is refused.

**Errors name the variable, the path, the key and a line number, never a value.** Exceptions are caught at the
helper's boundary, and the tool exits non-zero with that text.

## 2. The 20 readers

- `tools/budget/`: `budget_read.py`, `live_listeners.py`, `logs_bytes.py`.
- `tools/wx/`: `account_cost_read.py`, `classa_24h.py`, `classa_hourly.py`, `cost_read.py`, `frames_get.py`,
  `latest_check.py`, `logver.py`, `m_clock.py` (two places), `pd6_fleet.py`, `r2_ops.py`, `r3_5b.py`, `r3_list.py`,
  `r3_minute_join.py`, `r3_reads.py`, `r3p.py`, `s_clock.py` (two places), `s_reads.py`, `share_range.py`.
- All came from uk-data's PRs (#602, the cost record, #641, #671, #684).
- The only script that calls them is `tools/wx/s32_wait.sh` (`latest_check.py`, `r3p.py`); `ingest_deploy.sh` calls
  none.

## 3. The helper's controls (`--selftest`, each one also run red first)

| | planted | must |
|---|---|---|
| T1 | the variable unset | refuse |
| T2 | it names a missing file | refuse |
| T3 | the file inside a temporary git working tree | refuse |
| T4 | POSIX only: mode 0644 | refuse. On Windows it is skipped, and says so, never passes silently. CI runs it on Linux |
| T5 | a bare line (no `=`) holding a planted secret | refuse, with the line number |
| T6 | a key twice | refuse |
| T7 | the key missing | refuse |
| T8 | the key empty | refuse |
| T9 | a well-formed file outside any git tree | return exactly the planted value |
| T10 (control) | every refusal above | its message does not contain any planted value. The whole message is scanned, not a fragment |

## 4. Proving that no reader changes behaviour (the reviewer's condition 2)

`tools/token_file_equiv.py` runs each of the 20 tools twice: its file on main (BEFORE) and on this branch (AFTER).
- **Both runs use the same planted token file.** BEFORE finds it through a temporary `HOME`/`USERPROFILE`
  (`.config/blipscope/tokens`); AFTER through `BLIPSCOPE_TOKEN_FILE`, pointing at an identical copy outside any git
  tree. The planted values are synthetic.
- **A runner patches the network and the clock.** `urllib.request.urlopen` and `subprocess` are patched to record the
  first request (method, URL, every header, the body) or the first command, then stop the tool. The clock is frozen,
  because SigV4 signs the time.
- Each tool gets the argv needed to reach its first request; the per-tool table goes into section 5.

**Predicted:**
- **E1, a valid planted file: 20 of 20 tools send a byte-identical first request (or command) BEFORE and AFTER.** Same
  tokens, same signature.
- **E2, the needed key missing: 20 of 20 refuse BEFORE and AFTER,** with no request sent. The message text differs,
  since AFTER's is `token-file.ts`'s.
- **E3, the deliberate differences, recorded per tool and not counted as regressions:**
  - the variable unset: BEFORE reads the default path, AFTER refuses;
  - a malformed line, or a duplicate key: BEFORE picks a value, AFTER refuses;
  - the file inside a git tree: BEFORE reads it, AFTER refuses.
- **G1:** the guard on this branch finds 0 direct opens.
- **G2:** the guard's plant is flagged (red first).
- **G3:** the guard run on main's tree flags exactly the 20 files and 23 places in section 2, so it is anchored on real
  data.
- **The real file** (`--check`, booleans and key names only): `BLIPSCOPE_TOKEN_FILE` set and outside any git tree; all
  four keys present once; no malformed line. Then the AFTER tools find their tokens on this machine.
- **Escape:** anything outside E1-E3, G1-G3, T1-T10 or the real-file check stops and is reported. A tool that cannot
  reach a first request under the harness is reported by name, never dropped.

## 5. Readings (2026-11-12, 22:28-22:36Z)

**Two misses in my own registration, both found by the build, both mine:**
- **The count: section 2 lists 21 files, while the text above it says 20.** The guard on the unmigrated tree (G3) found
  21 files and 23 places, which is exactly section 2's list. The "20" came from a search on main BEFORE #684 added
  `tools/budget/live_listeners.py`. A count and its own list are two claims, and I did not check them against each
  other. G3 is graded on the list: **as predicted (21 files, 23 places); the count's "20" is OFF.**
- **The purposes: "the 4 keys the readers use" was wrong.** `tools/wx/pd6_fleet.py` also reads `records-device` and
  `test-device`, two ids that are on `token-file.ts`'s own list of purposes. The helper's `PURPOSES` carries all six.

| check | result |
|---|---|
| helper T1-T10, Windows | 9 PASS; **T4 SKIP**, stated, not passed |
| helper T1-T10, Linux (WSL Ubuntu 24.04) | **10 of 10 PASS**, T4 included |
| helper red first (`tools/token_file_sabotage.py`, S1-S10 in a copy) | **every plant failed exactly its control,** on WSL; on Windows all except S4, which is stated "not run" (POSIX only). CI runs it on Linux |
| G2 (the guard's plant; plus a clean control that mentions the path in a comment) | **caught, and not flagged,** respectively; a tracked plant in a fresh git tree is found by the tree scan |
| G3 (the guard on the unmigrated tree) | 21 files, 23 direct opens: section 2's list exactly (the count is noted above) |
| the migration (`../migrate_tokens.py`, one-off, not committed) | 21 files, 23 places, every needle found exactly once, each file's line endings kept |
| G1 (the guard on the migrated tree) | **0 files, 0 direct opens** |
| compile | all 21 readers compile under `python -I` |
| **E1** (a valid planted file) | **21 of 21: the first request is byte-identical** BEFORE (main `eed76e5e`) and AFTER, and it carries the planted token |
| **E2** (no keys at all) | **21 of 21: both refuse,** with no request sent |
| **E3** (the deliberate differences) | **21 of 21: AFTER refuses in all four cases** (variable unset, a bare line, a duplicate key, the file inside a git tree). BEFORE read a token in every case except `pd6_fleet.py`'s duplicate, which already exited BEFORE: its last-wins dict gave a device id with a `-DUP` suffix, out of shape |
| **the harness, red first** | one AFTER tool planted to read the wrong key (`classa_24h.py`, `observability-read`), in a copy: **E1 DIFFERENT for that tool alone, 20 as predicted, 1 off** |
| the real file (`--check`; booleans and key names only) | `BLIPSCOPE_TOKEN_FILE` set; the file exists, outside every git tree; 0 malformed lines; all six keys present once: **CHECK PASSED.** So with the variable exported, every reader finds its token on this machine |

**One re-run, and why:** the harness first planted `records-device` and `test-device` as two 16-hex ids that were all
zeros but their last digit. The pre-commit hook refused those as real-looking device ids. They were replaced by the hook's
allowlisted synthetic ids (`0123456789abcdef`, `beefbeefbeefbeef`), and E1-E3 were run again at 22:36Z, with the same
result: 21 of 21 as predicted.

**CI at `9773d84c` read OFF, my defect: 33 = 30/2/1, against 33 = 31/2/0** (read at 22:53:56Z and 22:55:02Z).
- **What failed:** `device-ids`, at the guard itself. It flagged `scripts/check_token_file_readers.py` lines 4 and 41:
  its own docstring's sentence about `open(` and the path, and its selftest's planted string. Every other token-file
  step passed on Linux, T4 and S4 included. The tested merge was `9773d84c` into `eed76e5e`, on an unchanged main.
- **Why the local G1 said 0:** the guard scans tracked files, and it was itself untracked when G1 ran. The check's own
  environment lacked the property it certifies.
- **The fix, ruled (a)+(b), with no exemption by filename:** the docstring is reworded, and the planted string is
  built in two pieces, so no source line matches while the runtime plant still does.
- **Read AFTER staging:** G1 0 files, with the guard tracked and scanning itself; G2 still catches its plant. The
  guard's own scanner on its own source gives no line now, and lines 4 and 41 on `9773d84c`'s version (the control).

**What the merge changes for anyone running these tools:** export `BLIPSCOPE_TOKEN_FILE` in the shell first, as
CLAUDE.md already says. With it unset, a reader refuses, naming the variable, where today it reads the default path.
That is E3's first row, and the reason the merge waits for weatherscope's `shsr` readings.

## 6. CI (predicted before the push)

**This registration commit,** docs only: **32 = 30/2/0** (main `eed76e5e`; the docs-only baseline since #675).

**The build commit:** `tools/budget/**` is in `account-budget.yml`'s path filter, so that job runs once more.
`device-ids.yml` gains steps but no job. **Predicted: 33 = 31/2/0.** It read 33 = 30/2/1 (section 5).

**The fix commit,** re-predicted before its push: **33 = 31/2/0** on main `eed76e5e`.
