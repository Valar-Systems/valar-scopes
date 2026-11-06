# The smoke's `/basemap` check sends the device's query (predictions first)

**Why this PR exists:** D3 (the staging `BASEMAP` binding, `docs/basemap-deploy.md`) was stopped before deploying,
2026-11-04 ~21:00Z. The smoke's `/basemap` check cannot pass once a binding exists, and **D3's row V2 is that check**.

## The defect

**What fails.** `proxy/scripts/smoke-prod.sh:356` requests

```
$BASE/api/v1/blipscope/basemap?lat=$LAT&lon=$LON&r=$R
```

**Where.** The route's parser, `parseQuery` (`proxy/src/basemap/query.ts:29-40`), **requires**:
- `eps`: integer millimetres, 1,000 to 10,000,000;
- a `layers` set;
- `levels`, when terrain is asked for.

The contract is `docs/basemap-route.md` §3: `?lat=&lon=&r=&eps=&layers=&levels=`.

**Why it has never shown.** The handler (`proxy/src/basemap/handler.ts:45-47`) answers `503 not_configured` before it
parses the query whenever there is no binding.
- At D0 the smoke's `/basemap` check passed on that inert answer, and the missing parameters were never parsed.
- **With D3's binding, the same request reaches `parseQuery`** and is refused `400 bad_eps`.
- `basemap-verdict.sh` then reads it, correctly, as "FAIL -- HTTP 400 … neither the inert answer nor a package".

It is a self-hiding failure: the inert answer was indistinguishable from a working request.

**Which D3 row it blocks.**
- **V2: "smoke's `/basemap` passes 'wired' at a cleared public centre"** (`docs/basemap-route.md` §9). It would read
  FAIL on staging.
- **D4's production smoke** would fail the same way.

## The change

1. **The smoke's `/basemap` request sends the query Weatherscope sends**, at the smoke's own `LAT` and `LON`:
   - `r=97` (`make_package.py`'s `R_MM = 97_000_000`);
   - `eps=100584` (`TOLERANCE_MM`);
   - `layers=water,rivers,roads,peaks,places,terrain`, all six of the route's layers (`docs/basemap-route.md:2116`:
     "r 97, all six layers, its three levels, ε 100,584");
   - `levels=24743664:246,49487328:246,98974656:246` (`LEVELS_MM` × `LEVEL_N`).

   The values are named constants in the smoke, with a comment pointing at `make_package.py`.
2. **A test from the other side,** `proxy/test/smoke-basemap-query.test.ts`:
   - It reads `smoke-prod.sh` itself (`?raw`) and extracts its `/basemap` URL.
   - It substitutes the smoke's own defaults for `LAT` and `LON` and its `BM_*` constants.
   - It runs the result through the route's own `parseQuery`.
   - It compares the query with `tools/weatherscope/basemap/make_package.py`, also read as text (`?raw`). Weatherscope's
     side of the contract, not a transcription.
3. **`basemap-verdict.sh`** and its CI selftest are **unchanged**.

## Predictions

| row | check | predicted |
|---|---|---|
| **SQ1** | the smoke's `/basemap` URL, as the test extracts and fills it | `parseQuery` returns ok |
| **SQ2** | that query against `make_package.py` | `r` 97 = `R_MM`/10⁶; `eps` 100584 = `TOLERANCE_MM`; levels = `LEVELS_MM` × `LEVEL_N`, ascending; the layer mask = all six layers |
| **SQ-C** | the control: the URL as it stood before this PR (`lat`, `lon` and `r` only) | `parseQuery` refuses it **`bad_eps`**: the defect, reproduced through the route's own parser |
| **the whole proxy suite, locally** | `npx vitest run` | **68 files (67 passed, 1 skipped); 885 tests: 884 pass, 1 skipped, 0 failed**. That is main's 882 (read first, below) plus the new file's 3. A timeout is an instrument failure: stop and report |

**Read first, on main `43afaac` (the base), before predicting the count:** `npx vitest run`, **67 files (66 passed, 1 skipped); 882 tests: 881 passed, 1 skipped, 0 failed**; exit 0; no timeout.

**The sabotages: every test evaluated against each, before running.**
- `sabotage-smoke-basemap.mjs` runs the WHOLE suite per sabotage and compares the exact set of failed tests with the
  set registered here.
- It waits for TIME_WAIT to fall below 8,000 before each run, and gives up after 10 minutes: a RIG exit, never a
  reading.
- Files are restored from byte copies, with their sha256 checked.

| sabotage | turns red (and only these) |
|---|---|
| **S-SQ:** `eps` removed from the smoke's `/basemap` URL | **SQ1** (not ok: `bad_eps`) and **SQ2** (`eps` absent). Nothing else: no other test reads `smoke-prod.sh` |
| **S-SQP:** `parseQuery` defaults a MISSING `eps` to 100584 (`?? ""` becomes `?? "100584"`) | **SQ-C** only. MQ6's empty case sends `eps=` (present, empty), which this does not touch, and no other test omits `eps` entirely (read: `proxy/test`) |

**CI**, predicted in a PR comment right before the push, from a finished run with every step's `if:` evaluated per
leg: this PR touches `proxy/`, so `device-ids`, `firmware`, `no-traffic-captures` and `workers` run, as on #525.

**Disk:** C: 26 GB free at 21:10Z. This is test runs only, no build or download; the proxy's `node_modules` is reused.

**Outside the set:** stop and report, with no fix until a ruling.

**After merge:** D3 resumes as ruled. V2 reads the fixed smoke, and V2's `/basemap` request is one more builder
invocation, counted in MM2's GraphQL window.

## Read (local, `1cb7e12`; Windows; every reading as registered)

| | read | verdict |
|---|---|---|
| **typecheck** | `tsc --noEmit` clean for `tsconfig.json` and for `tsconfig.scripts.json` | as expected |
| **SQ1, SQ2, SQ-C** | `test/smoke-basemap-query.test.ts`: 3 tests, all pass | as predicted |
| **the whole proxy suite** | **68 files (67 passed, 1 skipped); 885 tests: 884 passed, 1 skipped, 0 failed**; exit 0; no timeout | as predicted |
| **the sabotages** (`sabotage-smoke-basemap.mjs`, the WHOLE suite each) | the control: 884 passed, 1 skipped, of 885, 0 failed. **S-SQ: exactly {SQ1, SQ2} failed** (2 of 885). **S-SQP: exactly {SQ-C} failed.** Both files restored byte-identical. TIME_WAIT at each start: 186, 7,782 and 414, all under the 8,000 gate | as registered |

## A2.12: the port gate holds over the whole run, and an ungraded run keeps its output (predictions first)

Ruled (reviewer chat, pasted by Daniel, 2026-11-06 ~00:56Z), verbatim:

> 3. The A2.12 follow-up (keep the raw output of an unparseable run; hold the TIME_WAIT gate over the whole run, each
>    with its own check) is its own small PR after A1's push.

**Why.** This driver refused twice, and neither refusal could be diagnosed:
- **#534's rig refusal** (2026-11-05 ~00:08Z): the gate read TIME_WAIT under 8,000 at the run's start only. The run
  started at about 7,900, and 15,721 was read just after.
- **A1's two instrument failures** (2026-11-05 ~21:38Z): 16,715 was read just after, with another session's
  whole-suite run on the PC at the same time.
- **Both times the driver threw away the run's output.**

**Read first, on main `7b70745a`** (2026-11-06 01:30Z, under the ruled conditions: TIME_WAIT 22, no other `vitest` or
`sabotage-` process):
- `npx vitest run`: **72 files (71 passed, 1 skipped); 961 tests: 960 passed, 1 skipped, 0 failed**; exit 0, in 46 s.
- **TIME_WAIT peaked at 13,283** in samples taken every 5 s during the run, **and read 13,357 just after.** One run
  from an empty pool comes within about 3,000 of Windows' 16,384 ephemeral ports.

### The change (`proxy/scripts/sabotage-smoke-basemap.mjs`)

1. **The start gate:** a run starts only when TIME_WAIT is **under 2,000**, polled every 5 s. Past 20 minutes it is a
   RIG (exit 2), never a reading. Both numbers are the ruling's.
2. **The gate holds over the whole run.**
   - TIME_WAIT is sampled every 5 s **during** the run, and once at its end, and each run's line prints its start
     value, its peak and how long it waited.
   - A run whose peak reaches **15,000** is an **instrument failure**, whatever its summary says. A port-starved
     run can drop a file's cases without marking them failed.
   - **Why 15,000:** it sits between every count read after a passing whole-suite run (12,768 to about 13,800, and
     13,357 above) and every count read after a failing one (15,721 and 16,715).
3. **An ungraded run keeps its output.** When a run is not graded, its stdout and stderr go whole to a file under
   `os.tmpdir()/sabotage-smoke-basemap/`. The driver prints the file's path, the byte counts and the last 20 lines.
   - "Not graded" means: timed out, no parseable summary, the ceiling reached, or the reader failed mid-run.
   - The same goes for a CONTROL run that refuses for failures.

**Three defects found while writing it, fixed here.** Each has its own check, and each can be struck:
- **The reader failed open.** A failed `netstat` gave 0 lines, so a count of 0, which opened the gate. It now refuses
  when `netstat` errors or prints no connection table (W4).
- **The reader would read low at the ceiling.** `spawnSync`'s default buffer is 1 MB. At about 66 bytes a row, that
  is about 15,900 rows, so near 15,000 TIME_WAIT the output is truncated and the count silently low. That figure is
  computed, not measured. The 8,000 gate never reached it; the 15,000 ceiling does. The buffer is now 64 MB, and an
  overflow is a refusal (W5).
- **A RIG mid-plant left the sabotage in the tree.** The gate's RIG called `process.exit` inside the plant's `try`,
  which skips `finally`. A RIG is now returned from inside a plant, and the file is restored first (W2-R).

**Unchanged:**
- the two sabotages (S-SQ and S-SQP) and their registered sets;
- the exit codes;
- the whole-suite command.

The ruled "no other session's `vitest` or `sabotage-` process" condition stays in the operator's wrapper, as before.
The driver does not check it.

### Predictions

**1. `--selftest`.** No vitest runs and no repo file is touched. Every check runs the driver's own `gradedRun`,
`withPlant` and readers on planted commands (small `.cjs` files in a temp dir) and planted TIME_WAIT readings. The
planted values reach the code through a parameter, never an environment variable. **12 of 12 as registered, exit 0:**

| id | planted | predicted |
|---|---|---|
| **R1** | a run that prints a marker on stdout, another on stderr, exits 1, and has no summary | `summary not parsed`; the file is kept, and it holds both markers |
| **R1-C** | CONTROL: a run printing `Tests  3 passed (3)` | graded, 3 passed of 3; no file kept |
| **W1** | TIME_WAIT 100 at the start and the first sample, **15,000 only at the second sample**, 100 after | `ports: TIME_WAIT peaked at 15000 ...`; the file is kept |
| **W1-C** | CONTROL: the same with 14,999 | graded, peak 14,999; no file kept |
| **W2-R** | a temp file planted, then a run whose gate reads 2,000 | a RIG is returned; the file is restored byte-identical |
| **W2** | the gate reads 2,000 for the whole (planted, 400 ms) cap | RIG; the planted command never ran (its marker file is absent) |
| **W2-C** | CONTROL: the gate reads 1,999 | it starts; the marker file is present |
| **W3** | the reader **throws only at the second sample** | `the TIME_WAIT reader failed during the run (planted reader failure)`; the file is kept |
| **W4** | `netstat` text with a `TIME_WAIT` row and no table header | refused, never counted |
| **W4-C** | CONTROL: a table of 3 `TIME_WAIT` rows and 1 `ESTABLISHED` | 3 |
| **W5** | the real `netstat` against a 1,000-byte buffer | refused, never a truncated count |
| **W5-C** | CONTROL: the real reader on this machine | an integer |

**2. `--selftest-plants`.** Each plant edits a **copy** of the driver's code, above a cut line, so a plant's anchor is
never also found in the plant table. The copy then runs `--selftest`. The checks it reddens (NOT AS REGISTERED, or
never printed) and its exit status must equal the registered ones exactly. **8 of 8 as predicted, exit 0:**

| plant | the defect | red, exit |
|---|---|---|
| **C-PLANTS** | none: the copy, asserted equal to the source | {}, 0 |
| **P-RAW** | `keepRaw` writes nothing (it still prints its path) | {R1, W1, W3}, 1 |
| **P-INRUN** | no sampling during the run (start and end only) | {W1, W1-C, W3}, 1 |
| **P-START** | the start gate admits 2,000 (`<=`) | {W2-R, W2}, 1 |
| **P-CEIL** | the ceiling needs more than 15,000 (`>`) | {W1}, 1 |
| **P-TABLE** | the table-header check removed | {W4}, 1 |
| **P-BUF** | `netstat`'s error and exit status ignored | {W5}, 1 |
| **P-RIGEXIT** | the old behaviour: the gate's RIG exits the process | {W2-R, W2, W2-C, W3, W4, W4-C, W5, W5-C}, 2: the copy dies inside W2-R's plant, so nothing after it prints |

**3. The driver itself, whole, once,** under the ruled conditions, checked by the operator's wrapper first (TIME_WAIT
under 2,000 and no other session's `vitest` or `sabotage-` process, polled every 15 s, capped at 20 minutes). Exit 0:

    CONTROL (unsabotaged): 960 passed, 1 skipped, of 961; 0 failed (TIME_WAIT start <2,000, peak <15,000; ...)
    S-SQ: as registered; failed 2 (want 2): ["SQ1","SQ2"] ...; TIME_WAIT start <2,000, peak <15,000; ...
    S-SQP: as registered; failed 1 (want 1): ["SQ-C"] ...; TIME_WAIT start <2,000, peak <15,000; ...
    ALL AS REGISTERED

- **No `raw output kept` line:** every run is graded.
- **The tree is clean after.**
- **Each peak is predicted near 13,300**, from the read-first run above.

**4. Then the credits driver once,** unchanged by this PR: CONTROL 35 of 35, and its four sabotages as registered.

**CI**, baseline named: **34 check runs: 31 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.**
- **The baseline:** #559's finished run, a `proxy/` PR. Its first check run started 2026-11-06T01:08:09Z, after
  main's last workflow change `12569fa0` (2026-11-05T21:37:41Z).
- `workers.yml` has no job-level path condition, so a PR touching only `proxy/scripts/` runs the same jobs.

**The outcomes, written before any run:**

| reading | means | then |
|---|---|---|
| everything above as predicted | the change works as registered | open the PR, read CI |
| a selftest line NOT AS REGISTERED, or a plant OFF-PREDICTION | a check or a plant is not what this table says | stop and report, with the printed lines; no fix until a ruling |
| a driver run is an INSTRUMENT FAILURE on the ceiling (peak at or above 15,000) | the ceiling, or the 2,000 start gate, does not fit this suite | stop; its raw output is kept; ruling |
| a driver RIG at the start gate's 20-minute cap | the PC is busy | stop and report |
| CONTROL other than 960 / 1 / 961 | the tree moved, or the suite is unstable | stop and report |
| **anything else** | the instrument is unreliable at this scale | stop and decide |

**Not in this PR, for a ruling** (Q-A212a): a graded sabotage run's **total** is never compared with the CONTROL's.
- A partial runtime failure drops a file's cases **without failing them**, so today a sabotage could read "as
  registered" over a suite missing whole files.
- `sabotage-basemap.mjs` already guards its own runs this way (C-LOADFAIL: every baseline case must be seen again).
- **I recommend its own small PR after this one,** with its own plant: a run with one file's cases dropped must read
  as an instrument failure.

### Read (2026-11-06, `09b55a40`, Windows; every reading as predicted)

| step | read | verdict |
|---|---|---|
| **1. `--selftest`** (01:40Z) | 12 of 12 as registered, exit 0. W1's 15,000 at the second sample read `ports: TIME_WAIT peaked at 15000`. W2 read a RIG, and its command never ran. W5-C read 22 off the real `netstat`. The temp files were removed, and the tree was clean | as predicted |
| **2. `--selftest-plants`** (01:40-01:41Z) | 8 of 8 as predicted, exit 0: C-PLANTS {} 0; P-RAW {R1, W1, W3} 1; P-INRUN {W1, W1-C, W3} 1; P-START {W2-R, W2} 1; P-CEIL {W1} 1; P-TABLE {W4} 1; P-BUF {W5} 1; P-RIGEXIT {W2-R, W2, W2-C, W3, W4, W4-C, W5, W5-C} 2 | as predicted |
| **3. the driver, whole** (wait clear 01:42:12Z: TIME_WAIT 13, no other `vitest` or `sabotage-` process; exit 0 at 01:47:57Z) | CONTROL **960 passed, 1 skipped, of 961; 0 failed** (start 13, peak 13,355). **S-SQ {SQ1, SQ2}**, failed 2 (start 311, peak 13,357; held 103 s). **S-SQP {SQ-C}**, failed 1 (start 299, peak 13,350; held 103 s). Both files restored byte-identical. ALL AS REGISTERED. No `raw output kept` line | as predicted |
| **4. the credits driver** (wait clear 01:49:36Z: TIME_WAIT 932; exit 0 at 01:49:53Z) | CONTROL 35 of 35. S-CV-BLANKET {CV1, CV2, SA1, QC1, F16, P68}; S-SA {SA1, QC1}; S-RD {RD1, QC1, P89}; S-QC1 {QC1}. Counts as registered. ALL AS REGISTERED. The tree was clean after | as predicted |

**What P-RIGEXIT left behind.** Its copy died inside W2-R's plant, as predicted, so that copy's temp dir was not
cleaned up. Its plant target still read **`PLANTED`**: the old behaviour leaves a sabotage in the file, which is what
W2-R guards. The dir was synthetic and has been removed.

**The four peaks agree to within 7:** 13,357 after the read-first run, then 13,355, 13,357 and 13,350, from starts of
22, 13, 311 and 299.
- **The reading:** each run makes a fixed number of loopback connections, about 13,350, and the TIME_WAIT left from the
  previous run expires during the next.
- **The alternative,** a limit near 13,357 that caps the count, is excluded. The same counter on the same machine read
  15,721 and 16,715 after the two failing runs.
- **So the margin is about 1,650 under the 15,000 ceiling,** and about 3,000 under the ports. A suite that grows by a
  tenth reaches the ceiling, and the ceiling is then what says so.

