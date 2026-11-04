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
