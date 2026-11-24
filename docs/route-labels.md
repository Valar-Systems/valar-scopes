# Route labels: every route the router dispatches gets its own Analytics Engine label

**Status: predictions first** (registered before the test or the fix exists; ruled by the orchestrator, 2026-11-24). Code
and docs. Deploys nothing: it reaches production with the next proxy deploy from main, which D4's plan makes D4's own.

## Why

The proxy records each request in Analytics Engine under `routeTemplate(pathname)` (`proxy/src/index.ts`, `record()`), and
`routeTemplate` returns the path only when it is in `KNOWN_ROUTES` (`proxy/src/metrics.ts`), and `/other` otherwise. Three
routes the router dispatches are in neither the set nor a template, so their requests have been recorded as `/other`:

- **`/basemap`**, under both prefixes, since the route was added on 2026-10-29 (#486). D4's V6-5 ("fleet `/basemap` requests
  in A: 0") cannot be read while it sits in `/other`. **This gap was already written down:** `docs/wx-route-label-deploy.md`
  (2026-11-07) says "`/api/v1/blipscope/basemap` is still unlisted and lands in `/other`". It was recorded and not acted on,
  which is the written-down-and-not-read failure this repo's `CLAUDE.md` describes.
- **`airport/<code>`** (Follow's single-airport endpoint), under both prefixes: a pattern with no template.
- **`wx/<product>` under the legacy `/v1/` prefix:** the twelve products are listed under `/api/v1/blipscope/` only.

**Why it survived:** a second path around a guard. `KNOWN_ROUTES` is the guard for analytics visibility. `pages.test.ts`
enforces it for pages, and `metrics.test.ts` KR1/KR2 for the current `/wx` paths. Nothing enforced it for every route the
router dispatches, and #486 added one.

## The change

- `KNOWN_ROUTES` gains `/api/v1/blipscope/basemap` and `/v1/basemap`, and the twelve `/v1/wx/<id>`, generated from the same
  `PRODUCTS` list KR2 reads, so they cannot drift from it.
- `routeTemplate` gains `/api/v1/blipscope/airport/:code` and `/v1/airport/:code`. **The label stored is the template; the
  airport code never enters the dataset.** A device's airport is location-adjacent, so it is kept out by construction.

## The test, its input taken from the router's own source (`proxy/test/route-labels.test.ts`)

- It reads `proxy/src/index.ts`: the exact suffixes (`api.suffix === "x"`), the patterns (`api.suffix.match(/^x\/([^/]+)$/)`,
  one example each; `wx` takes every `PRODUCTS` id) and both prefixes (`API_PREFIX`, `API_LEGACY_PREFIX`). It reads
  Weatherscope's suffixes from `proxy/src/weatherscope/handler.ts`, under its own prefix (it has no legacy one).
- One case per path: `routeTemplate(path)` is not `/other`. A parse control: at least the 5 exact suffixes and 4 patterns read
  today must be found, or the test fails rather than passing over nothing.
- **The airport row:** a real-shape request through `record()` (path `/api/v1/blipscope/airport/KBOS`, and the legacy one) into
  a fake `METRICS` binding: the written point's `blob1` and `index1` are exactly the template, and `KBOS` appears in no blob
  and no index.

## Predictions, frozen before any run

| run | tree | predicted |
|---|---|---|
| R1 | main `0391e8440bef` plus the test file only | **43 cases, exactly 16 red:** `basemap` x2, `airport/<example>` x2, `/v1/wx/<id>` x12; the airport row red (its path is `/other`) |
| R2 | with the fix | **43 of 43 green;** the airport row green |
| R3 | the fix with `/v1/basemap` dropped (a copy) | **exactly 1 red:** `/v1/basemap` |
| R4 | the fix with the airport template returning the raw path (a copy) | **the airport row red** (the code appears in a blob) |
| R5 | the proxy's whole test suite, with the fix | **every existing test passes**, plus the new file's |

**Outside the set:** a red count other than 16 in R1, or any other row: stop and report before anything is changed.

## What changes in the dataset at deploy

From the first production deploy that carries this, `/other` stops holding `/basemap`, Follow's airport calls and legacy
`/wx` requests; each gets its own label. **A query that spans the deploy sees both shapes.** Every reader of the route
dimension, named:

- **`dashboard/src/analytics.ts`:** `REQUEST_POINTS` (`blob1 LIKE '/%'`) keeps every total unchanged; its route lists
  (`BLIPS_ROUTES`, `PHOTO_ROUTES`, the enrich pair) are unaffected; it has no per-route breakdown that shows `/other`.
- **`dashboard/test/usage.test.ts:54`:** still passes (`/other` and the new labels all start with `/`).
- **`scripts/reconcile-fleet.py`** (liveness per device, hour and route): route values change for these three; liveness does
  not.
- **`tools/wx/pd6_fleet.py`, `proxy/scripts/nophoto-share.ts`, `proxy/scripts/usage-stats.ts`:** they read other families or
  the enrich routes; unaffected.
- **Docs that read fleet `/other` as a bound:** `docs/wx-server.md` (W5, :596, :732, :800, :939: fleet `/other` as the upper
  bound on fleet `/wx`, from before `/wx` had a label), `docs/wx-device.md` (:765, :852) and `docs/wx-route-label-deploy.md`
  (:53, :61). They stay true of their own windows. **After this deploy, fleet `/other` is no longer a bound on `/basemap`,
  airport or legacy `/wx` requests;** a reading about those reads their own labels.

## CI for this PR, predicted before the push

It touches `proxy/` and `docs/`, so the same five pull-request workflows run as for #696 and #790. **Predicted: 39 check runs,
37 success, 2 skipped (`promote`, `version`), 0 failed,** tested on the merge into main as of the push, read twice at least
65 s apart. Anything else: stop and read which job.

## Readings (2026-11-24, local, before the push; the predictions above are unchanged)

| run | read | verdict |
|---|---|---|
| R1 | 46 tests: 18 failed, exactly the 16 predicted paths (`/api/v1/blipscope/basemap`, `/v1/basemap`, `airport/EXAMPLE1` under both prefixes, and `/v1/wx/` for all twelve products) and both airport rows; the parse control passed | as predicted |
| R2 | 46 of 46 green | as predicted |
| R3 | exactly 1 red: `/v1/basemap` | as predicted |
| R4 | 1 red: the airport row for `/api/v1/blipscope/airport/KBOS`, the template the plant changed; the legacy case, untouched, stayed green | as predicted |
| R5 | `npm run typecheck` exit 0. `npx vitest run`: 4 files crashed at file level inside the local Workers runtime (Windows sockets: `ConnectEx(): #52`), and 63 tests never ran | **no local verdict:** see below |

**R5's control, ruled before it ran:** the same `npm ci` and `npx vitest run` on unchanged main, in its own worktree. It
failed differently: the Workers runtime never started (`No such module ... mock-agent.cjs`), and no test ran. A different
error on the same machine means the local pool is unreliable here (the ruled outcome for "the error differs"), so neither run
judges the change. **R5 is graded by CI's Linux `test (proxy)` job:** predicted all pass, the 46 new cases among them, and
nothing skipped that ran before. A failure there in any of the four crashed files: stop.

## Readings note (the merge turn, 2026-11-24; everything above is unchanged)

- **The control's outcome is OUTSIDE, not "the error differs".** The control on unchanged main ran no test at all: the
  runtime never started. That is not a second reading of the same instrument with a different error; it is an instrument
  that measured nothing. It was ruled OUTSIDE the registered set, with the consequence written above unchanged: no local
  verdict, and R5 graded by CI.
- **The finding, on its own:** on this Windows machine the local Workers test pool failed two different ways on two trees,
  `ConnectEx(): #52` mid-run on the change and the `mock-agent` module missing at start on unchanged main. A local run of the
  proxy suite there can fail before it measures anything, so a crash in it is an instrument failure, never a red.
- **R5, graded by CI** (`test (proxy)` on the PR's first push, merged into 0391e844, read twice): 86 files (85 passed,
  1 skipped) and 1,200 tests (1,199 passed, 1 skipped). The 46 cases of `route-labels.test.ts` pass, and the four files that
  crashed locally pass. That is the prediction: all pass, the 46 new cases among them, nothing skipped that ran before.
