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
