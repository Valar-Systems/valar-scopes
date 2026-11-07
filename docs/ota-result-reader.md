# A3.1's reader: the devices behind `version.txt`, by last OTA result

**Read this first.** With nothing drawn on screen for a refused update (A2.1, A4.1), the OTA record's `refused-<reason>` is the
only fleet-level sign of a mislabelled publish, or of an accept path that breaks at R+1. Nothing routinely reads it today.
`docs/weatherscope-stage3.md` A3.1 specifies the reader and gives it to blipscope-5c. Weatherscope's A12.104 makes it a condition of R,
and its A12.172 found it missing from main. **Ruled 2026-11-07 ~23:23Z:** O11 can run without it, and R cannot ship without it.
This page freezes the design and the predictions before any code.

## The design, frozen

**Where:** `scripts/reconcile-fleet.py`, in the INSTRUMENTS section, on every run, after Instrument A. Every fleet question
already goes through this tool, so a person sees the line without choosing to.

**What it prints:** one block of counts, and no device id. EXAMPLE numbers, not a prediction:

```
  Behind version.txt (published 18): 3 of 41 device(s), by last OTA result (90 d):
    ok 1, fail--106 1, no ota point 1                    sum 3 = behind 3
    covers radar devices on the cloud feed only (A3.2)
```

**The inputs:**

| input | read from | when it cannot be read |
|---|---|---|
| the published version | `https://github.com/Valar-Systems/valar-scopes/releases/latest/download/version.txt`, the URL every device fetches (`OTA_RELEASE_BASE` in `OtaUpdater.cpp`) | no behind count, and an instrument warning. It must parse as a positive integer, and it is **never defaulted**: a 0 would put every device "ahead", the most reassuring wrong answer there is |
| each device's firmware | the `fw:` rows the script already reads, test identity excluded | `--no-detail`: the line says "not assessed", as Instrument A does |
| each device's last result | Analytics Engine, index `ota` (`proxy/src/metrics.ts`: `blob2` result, `blob4` device), the last 90 days, the newest point per device | the behind count still prints, then "results unread" with the reason, and an instrument warning |

**The token** is the `analytics-read` line of the file `BLIPSCOPE_TOKEN_FILE` names, as `usage-stats.ts` reads it. The script
refuses the same cases `token-file.ts` refuses (unset, missing, inside a git working tree, a duplicate line, an empty value), and
never prints the value.

**Behind** means a `fw:` row whose `fw` is an integer below the published version. A row with no integer `fw` is counted as
"fw unknown", neither behind nor dropped.

**The buckets.** Each behind device lands in exactly one:
- its newest token, by name, when the token has a known shape: `ok`, `incomplete`, `fail-<integer>`, or
  `refused-truncated|no-record|bad-record|foreign` (`include/OtaInstall.h`, `src/OtaUpdater.cpp`);
- `unrecognised` for any other value. It is counted, never dropped and never coerced (CLAUDE.md, "a truncated row is a lie");
- `no ota point (90 d)` when the window holds none. A device sends a record only after an OTA attempt and the read clears it, so
  most current devices have none.

**The sum check runs, not just reads.** The buckets must total the behind count. If they don't, the line says `SUM MISMATCH` and
the run raises an instrument warning (exit 1).

**What it cannot see**, printed with the line: A3.2's blind spot. Radar devices off the cloud feed, and every non-radar edition,
send no OTA record.

**CI, and a gap it closes.** `workers.yml` runs `reconcile-fleet.selftest.py`, but its `paths` list has no `scripts/` entry, so a
PR that changes only `reconcile-fleet.py` never runs its own selftest. The guard exists and the path that edits the script goes
round it. Both `paths` lists gain `scripts/reconcile-fleet*.py`.

## R1: host rows, in `scripts/reconcile-fleet.selftest.py`

These run on a fixture, with no KV, no Analytics Engine and no network. The ids are EXAMPLE values built at run time.

**The fixture:** published version 19. The `fw:` rows are 2 current at 19, 1 ahead at 20, 1 with no `fw`, and 6 behind at 18, 18,
17, 18, 16 and 18. The behind devices' newest points:

| device | points in the window | predicted bucket |
|---|---|---|
| b1 | `ok` | `ok` |
| b2 | `refused-foreign` | `refused-foreign` |
| b3 | `fail--106` | `fail--106` |
| b4 | none | `no ota point` |
| b5 | `weird-token` | `unrecognised` |
| b6 | older `refused-no-record`, newer `ok` | `ok` |

A current device carries a `refused-bad-record` point, which is not counted.

| row | predicted |
|---|---|
| **R1a** | behind 6 of 10; the buckets are exactly `ok 2, refused-foreign 1, fail--106 1, unrecognised 1, no ota point 1`; sum 6 = behind 6; no warning |
| **R1b** | a `refused-foreign` bucket is printed |
| **R1c** | b6 lands in `ok`: the newest point wins |
| **R1d** | `version.txt` unreadable (`""`, `v19`, `0`): no behind count and a warning, in each of the three cases |
| **R1e** | the results unreadable: behind 6 still printed, then "results unread", and a warning |
| **R1f** | no fixture id appears in the output. CONTROL: the block is there and holds `behind 6` |
| **R1g** | the current device's `refused-bad-record` is in no bucket |

**Sabotages.** Each is an in-memory edit of the source; the file is never written. Each must turn exactly its rows red, and its
anchor must match once or the driver says BLIND:

| plant | the edit | red rows, exactly |
|---|---|---|
| **S-R1drop** | tokens of no known shape are dropped (A3.1's named sabotage) | R1a, reported as a `SUM MISMATCH` |
| **S-R1first** | the oldest point wins | R1a, R1c |
| **S-R1zero** | an unreadable `version.txt` reads as 0 | R1d |
| **S-R1id** | the block prints each behind device's id | R1f |

**Outside the set:** any other row red, a plant that turns no row red, or a BLIND anchor. I stop and report, with nothing
improvised on the spot.

## R2: one live run, read-only

**Scope, decided before it runs:**
- **Reads:** production KV `fw:` rows (what the script reads today), `ota` points over 90 days (device, result, timestamp), and
  the public `version.txt`. Nothing is written.
- **The provisioning record is not read:** `--log` points at an empty CSV outside the repo.
- **Ids stay in a file:** stdout and `--json` go to files outside the repo. Only the new block and the SUMMARY line's counts are
  read out, and both files are deleted after.

| row | predicted |
|---|---|
| **R2a** | the presence control passes (live `fw:` rows above 0), and `version.txt` reads **18**, v18 being the latest release |
| **R2b** | the buckets sum to the behind count, and that count equals one computed separately, from the `--json` file's `fw:` rows alone |
| **R2c** | no `refused-*` bucket among the behind devices. A device below 18 runs firmware from before the identity check, which cannot send one |
| **R2d** | no `unrecognised` bucket: the Worker sanitises the token, and every firmware writes a known shape |
| **R2e** | the run exits 1 from the empty CSV's discrepancies alone, with no instrument warning from this block |

**Outside the set:** any other reading, including a sum that does not match. I stop and report. Whether it is the instrument or the
fleet is decided after reading, not on the spot.

## CI prediction for the PR

**35 = 32 success / 3 skipped / 0 failed.** This is the docs-only base of 32 = 29/3/0 from #614's finished run, plus the three
`workers.yml` jobs (`manifest-drift`, `test (dashboard)` and `test (proxy)`). They run because the PR changes `workers.yml`, which
is in its own `paths`. This matches #596's finished run. The last job-set change on main is `84409651`, re-derived from the
workflows' history.
