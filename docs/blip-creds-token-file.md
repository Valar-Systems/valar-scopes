# BLIP_KEY / BLIP_DEVICE: from the Windows user environment to the token file

**Ruled 2026-10-29** (reviewer chat, pasted by Daniel): "BLIP_KEY/BLIP_DEVICE leave the Windows user environment for
the token file, read per shell like the other tokens. Daniel moves the values himself; you change smoke-prod's reader,
reviewed." Not urgent. This file is the plan and its predictions, pushed before any code.

## What reads the pair today

Every reader, found by searching the tree for `BLIP_KEY` and `BLIP_DEVICE`. The Worker's own `src/` uses are
different names for different things.

| reader | what it does with the pair | what it establishes today |
|---|---|---|
| `proxy/scripts/smoke-prod.sh` | every authed check carries `X-Blip-Key` + `X-Blip-Device` | refuses to run without both (exit 2); never prints either |
| `proxy/scripts/watch-upstream.sh` | the same pair, polled | refuses without both; never prints either |
| `proxy/scripts/ingest-routes.ts` | reads keys back through the Worker after an ingest | **refuses** to report done without both ("nothing can be read back") |
| `proxy/scripts/ingest-airports.ts` | rule 3's read-back | runs without them, but prints "RULE 3 NOT EXERCISED" |
| `proxy/scripts/confirm-deploy.sh` | prints the hint `BLIP_KEY=... ./scripts/smoke-prod.sh` | nothing; text only |

**So "smoke-prod's reader" is four readers.** If only smoke-prod changed, the day the values leave the user
environment `watch-upstream.sh` and both ingests would stop: the ingests by refusing, `watch-upstream.sh` by exiting.
That is the second-path failure CLAUDE.md names. So all four move together, through **one** reader.

## The change

1. **`proxy/scripts/token-file.ts`** gains two purposes, `blip-key` and `blip-device`. Its parser already skips keys it
   does not know, so adding the lines to the file breaks no other script. The rules are unchanged, and each is
   inherited by every reader below:
   - the file must lie outside any git working tree;
   - on POSIX it must not be readable by others;
   - every line is `key=value`;
   - a key may not appear twice, and its value may not be empty;
   - no value is ever printed.
2. **The two TypeScript ingests** call `resolveToken("blip-key", "BLIP_KEY")` and
   `resolveToken("blip-device", "BLIP_DEVICE")`. That is the existing pattern: the variable wins when set, otherwise the
   file.
3. **The two bash readers** do not get a second parser in bash. When either variable is unset, the script re-runs
   itself once through `proxy/scripts/with-blip-creds.mts`.
   - The wrapper resolves both through `token-file.ts` and starts the same script with them in the **child's
     environment only**.
   - Nothing passes on argv, stdout, a file or a pipe.
   - A guard variable stops a second re-run: still unset the second time means the script's own "not set" error.
4. **`confirm-deploy.sh`'s hint** names the token file instead of `BLIP_KEY=...`.
5. **What Daniel does, after the merge:** add `blip-key=` and `blip-device=` lines to his token file, then remove the
   two user variables. No value passes through me at any step.

## Predictions, frozen before the code

| | check | predicted |
|---|---|---|
| **T1** | `token-file.test.ts`: both new purposes parse; an unknown key is still skipped; inside a git tree, others-readable, malformed, duplicate and empty are still refused for the new purposes | all pass; the existing cases are unchanged |
| **T2** | the ingests, with the variables set | the variable is used, and the file is not opened (a test double that throws on read) |
| **T3** | the ingests, with the variables unset and the file set | the file's values; with neither, `REFUSING`/`NOT EXERCISED` exactly as today, now naming the token file |
| **T4** | `with-blip-creds.mts --selftest`, with a fake file (`EXAMPLE`-marked values) and a child that prints only the **lengths** it received | the child sees both, at the right lengths; a scan of every byte the wrapper and the child wrote finds **neither value** |
| **T5** | the bash readers, with the variables set | no re-run (the guard variable stays unset in the child), so behaviour is as today |
| **T6** | the bash readers, unset twice | the script's own "not set" error, and exactly one re-run |
| **T7** | after Daniel's move, with the user variables gone | `smoke-prod.sh` against production exits 0, as it does today |

**Sabotages, each in a copy, each predicted red:**
- **S-echo:** the wrapper logs the key. T4 red.
- **S-noguard:** the guard removed. T6 red: a second re-run, caught by a re-run counter.
- **S-filefirst:** the file read before the variable. T2 and T5 red.

**Outside the set:** stop and report.

T7 runs only after Daniel has moved the values. Until then every reader behaves exactly as today, because the
variables are set.

## Results (2026-10-30)

| | read | verdict |
|---|---|---|
| **T1** | `token-file.test.ts`: both purposes parse beside the old ones, and an unknown key is skipped. For `blip-key`, inside a git tree, others-readable, malformed, duplicate and empty are each refused, and no message carries a value. The 19 existing cases are unchanged and pass | **as predicted** |
| **T2** | `resolveBlipCreds` with both variables set returns them, with source `BLIP_KEY + BLIP_DEVICE`. Its read double throws, and it is never called | **as predicted**, graded at the one function both ingests call (below) |
| **T3** | the variables unset and the file set: the file's values, with source `BLIPSCOPE_TOKEN_FILE (blip-key) + BLIPSCOPE_TOKEN_FILE (blip-device)`. With neither: a `TokenFileError` naming the variable, never a value | **as predicted**, at the same function |
| **T4** | `with-blip-creds.mts --selftest`: one re-run, the child holds both values at their lengths (31, 16), and neither value is in any byte written | **as predicted** |
| **T5** | the selftest's child, with the variables set: no re-run (`reexec 0 depth 1`). **The real readers too:** `smoke-prod.sh` and `watch-upstream.sh` with `EXAMPLE` values and `BASE=http://127.0.0.1:9` print no wrapper line, get past their credential gate (33 check headers; the watch line), and print no value | **as predicted** |
| **T6** | the selftest's child, unset twice: exactly one re-run (`depth 2`), and the file's reason named. **The real readers:** with the variables unset and a file without the lines, each prints exactly one wrapper line naming `no "blip-key" line`, then its own "not set", and exits 2. No value is printed | **as predicted** |
| **T7** | `smoke-prod.sh` against production after Daniel's move | **waits on Daniel** |

**How T2 and T3 reach the ingests, since the ingests were not run.** An ingest run downloads the corpus before it
checks credentials, and then writes KV, so neither is run for a test.
- Both call `resolveBlipCreds()` and nothing else for the pair: `git grep` finds no other read of `BLIP_KEY` or
  `BLIP_DEVICE` in them.
- On a `TokenFileError` each keeps its existing path, and adds one line naming the file's reason:
  - `ingest-routes.ts` prints `REFUSING` and exits 1;
  - `ingest-airports.ts` prints `RULE 3 NOT EXERCISED`.
- So T2 and T3 are graded at that function and by reading the two call sites. That is the weaker form, said here so
  it is not mistaken for a run.

**The sabotages, each in a copy except where stated:**

| | the plant | read | verdict |
|---|---|---|---|
| **control** | none, in the copy rig | `SELFTEST PASSED` | the rig reproduces the baseline |
| **S-echo** | the wrapper logs the key | exactly "T4: neither value appears" BAD | **as predicted** |
| **S-noguard** | both checks of the guard variable removed (the helper's and the wrapper's) | exactly T6 BAD, `ran: runaway, depth 4`, caught by the re-run counter | **as predicted** |
| S-noguard-bash (extra) | only the helper's check removed | exactly T6 BAD, with no run: the wrapper's own check refused the second re-run | as predicted. The selftest does not print the child's stderr, so the "refusing" line itself was not read |
| S-noguard-wrapper (extra) | only the wrapper's check removed | `SELFTEST PASSED`: the helper's check alone holds | as predicted |
| **S-filefirst** | both layers: the helper goes to the file with the variables set, and `resolveToken` reads the file first | the selftest: exactly T5 BAD, `ran: key 31 dev 16 reexec 1 depth 2`. vitest, the TypeScript plant **in place**: exactly 2 failed, "CI's named variable wins…" and T2 ("the file was read"); 22 passed | **as predicted** |

- After every plant, the real files' sha256 is unchanged and `git diff` is clean. The in-place plant was restored
  from a byte copy.
- **The guard is checked twice,** in the helper and in the wrapper. The plan named one guard. S-noguard removes both,
  as "the guard removed" means. The two extra rows show that each check alone stops a second re-run.

**What was added before the sabotages ran, and why:**
- **The re-run counter.** S-noguard's frozen prediction says "caught by a re-run counter". The selftest had none, so
  a runaway would have hung it instead of failing. Its child now counts its own starts (`CREDS_DEPTH`) and stops
  itself past 3. The unplanted selftest still passes, with `depth 1` and `depth 2` where predicted.

**A rig failure, recorded and not counted.** The first run of the copies placed them one level under the system temp
directory.
- `token-file.ts` takes the repo root as two directories above itself, so each copy's "repo" became the temp
  directory. That directory holds the selftest's token file, so every copy refused it as "inside a git working tree".
- Every copy failed T4 and T6 for that reason, the unplanted ones included. The control that would have shown it,
  an unplanted copy, was not in that run.
- The in-place TypeScript plant also missed its anchor (the checkout is CRLF). The driver then ran vitest anyway, so
  that run's "exit 0" is not a result either.
- **The fix:**
  - copies sit at `<tmp>/repo-<label>/proxy/scripts`;
  - the control runs first and must pass, or the run stops;
  - anchors follow the file's own line ending;
  - a failed in-place plant restores and stops.
- Everything above is from the second run. **The real-reader rows are from the first run.** They ran on the real files
  in place, which the copy rig does not touch.

**What CI does not cover.**
- **The wrapper's types:** `with-blip-creds.mts` is the only `.mts` in `proxy/scripts`, and `typecheck:scripts`
  includes only `scripts/**/*.ts`. So no CI step typechecks it. It was checked once by hand
  (`tsc --noEmit --strict --module nodenext`, with its import): exit 0.
- **The selftest:** T4-T6 run only when someone runs the selftest. Running it in the Workers workflow would make them
  a check that runs. That is proposed here, not added.

## Follow-up: the wrapper's selftest in CI (approved 2026-10-30), frozen before the change

The reviewer: "as its own small follow-up after the merge, if CI's Node runs .mts the way the bench's does. Say which."

**Which Node.** CI's Workers job sets up Node "22". A CI log of this PR's last run reads
`Found in cache @ /opt/hostedtoolcache/node/22.23.4/x64`, so CI runs **v22.23.4**; the bench runs v24.18.0. Node's
changelog has type stripping on by default from 22.18.0. So the prediction is that CI runs the `.mts` the way the
bench does. That is a reading of the changelog, not an observation, and C1 is the observation.

**The change:** one step in `workers.yml`'s test job, on the proxy leg only, after smoke's `/basemap` selftest:
`node scripts/with-blip-creds.mts --selftest`.

| | check | predicted |
|---|---|---|
| **C1** | the step's own log on the PR's first head with the step | the four `ok` lines (`depth 2`, `depth 2`, `depth 1`, `depth 2`, as on the bench) and `SELFTEST PASSED`, exit 0. A type-stripping `ExperimentalWarning` on stderr may appear; it is not a failure |
| **C2** | the dashboard leg | the step is skipped there (`if: matrix.worker == 'proxy'`) |
| **C3** | CI's total on that head | none failed |

**C1 is read from the step's log, not from the job's colour.** A skipped or wrongly guarded step is green too.

**Outside the set:** stop and report. That includes Node refusing the `.mts` (for example
`ERR_UNKNOWN_FILE_EXTENSION`). The remedy (Node 24 for the proxy leg, or a flag) waits for a ruling.

### Results for the selftest in CI (2026-10-30)

| | read | verdict |
|---|---|---|
| **C1** | the step's own log on `090f6a3`, proxy leg, Node v22.23.4: the four `ok` lines (`depth 2`, `depth 2`, `depth 1`, `depth 2`) and `SELFTEST PASSED`. No `ExperimentalWarning` appeared | **as predicted**: CI runs the `.mts` the way the bench does |
| **C2** | the dashboard leg's log holds no line of the step | **as predicted** |
| **C3** | CI's total on `090f6a3`: 38 runs, 35 success, 3 skipped, none failed | **as predicted** |
