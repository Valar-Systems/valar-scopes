# D4's V6: the fleet's own traffic around the map's production switch (`tools/basemap/v6_fleet.py`)

**Status: the instrument, registered before its code, with its selftest and red first.** It reads nothing live here. Its
live reads are D4's, after the switch. It depends on #792 (route labels), which gives `/basemap` its own label; this PR
merges after it.

## What V6 asks (D4's plan, section 9, as accepted)

Did switching the map on in production disturb the fleet? T is the proxy deploy that binds the builder (the switch).

| | window | what |
|---|---|---|
| B | `[T-60m, T)` | the hour before |
| A | `[T+5m, T+65m)` | the hour after, from five minutes in |
| FF | `[T, T+15m)` | fail fast |

- **Controls, before any row is believed:** C1, B has fleet requests and at least 4 devices; C2, the device difference
  counted against an empty window equals B's device count (so the difference function is shown working on live data);
  C3, the 24 h of 4xx over all production points is above 0 (so 4xx is visible to the query). Any control off: BLIND, a STOP.
- **Rows:** V6-1 fleet 5xx in A 0; V6-2 fleet 4xx in A 0, with B's beside it (B's above 0 is OUTSIDE: V6-2 cannot separate
  D4 from background); V6-3 devices in B and not in A 0 (not an automatic rollback: ordinary device churn looks the same);
  V6-4 fleet `/blips` A over B in [0.4, 2.5]; V6-5 fleet `/basemap` in A 0 (A only: before #792 deploys, `/basemap` was
  recorded as `/other`).
- **Fail fast:** any fleet 5xx or 401/403 in FF, or in A, sets "R-1 at once".
- Read twice, at A's close + 10 and + 20 minutes; any value that moves between the two reads is OUTSIDE.

## The instrument

- **Analytics Engine** (`blipscope_proxy`), counts sample-weighted as P5 and PD6. The account comes from
  `proxy/basemap.wrangler.toml`; the token and the two excluded device ids (the records device and the test identity) from
  the token file. **No token, device id or query value is ever printed:** device sets are kept in memory and only their sizes
  and differences are printed.
- **A positive filter:** a fleet request row is a point whose route starts with `/` (every per-request route the proxy
  writes, `/other` included). Every other family in the dataset carries its own name in that column (`err5xx`, `enrich_gap`,
  `ota`, `boot`, `usage`, `provision`, `sitefeed`) and is excluded by what it is. The dashboard's `REQUEST_POINTS` uses the
  same rule.

## Selftest (`--selftest`: planted rows only; the network is blocked and the block shown firing)

The route list is read from `proxy/src/metrics.ts` and the family list from every `writeDataPoint` call in `proxy/src`, on
the tool's own tree, so neither can drift from the code.

| row | checks |
|---|---|
| ST0 | the network block fires on its own control |
| ST1 | the windows |
| ST2 | one planted row per route the proxy writes: every one counted (63 routes on this tree) |
| ST2b | `/blips` and both basemap routes are among them |
| ST3 | one planted row per other family (7), with a non-empty device column and a large weight: every count and the device set unchanged |
| ST4 | no device id and no excluded id in the output |
| ST5, ST6 | the device difference, and C2 against an empty window |
| ST7 | grading: a clean pair, each row off in turn, the V6-4 band edges (0.39 and 2.51 off, 0.40 and 2.50 in), OUTSIDE, the rollback flag |
| ST8 | BLIND: too few devices, no 4xx anywhere, a count that is not a value, no `/blips` in B |
| ST9 | compare: equal reads agree, a moved count is OUTSIDE |
| ST10 | no network attempt |

**Read 2026-11-24:** 12 of 12 rows PASS, exit 0, stderr 0 B.

## Red first (`tools/basemap/v6_fleet_sabotage.py`)

The baseline passes every row; then each plant, one exact edit in a temporary copy, must fail exactly its rows.

| plant | must fail | read |
|---|---|---|
| the `/` filter dropped | ST3 | as registered |
| the filter narrowed to `/api/` (the control that real routes still count) | ST2 | as registered |
| B \ A reversed | ST5, ST6, ST7 (C2 shares the difference function, by design) | as registered |
| A starting at T | ST1 | as registered |
| V6-4's lower bound 0.3 | ST7 | as registered |
| BLIND graded as a pass | ST8 | as registered |
| the device set printed | ST4 | as registered |
| compare ignoring a moved value | ST9 | as registered |

**Read 2026-11-24:** all 8 as registered, stderr 0 B, no copy left.

## CI for this PR, predicted before the push

On top of #792's commit it touches `proxy/`, `tools/` and `docs/`, so the same five pull-request workflows run as for #792.
**Predicted: 39 check runs, 37 success, 2 skipped (`promote`, `version`), 0 failed,** tested on the merge into main as of
the push, read twice at least 65 s apart. No workflow runs this tool or its driver yet.
