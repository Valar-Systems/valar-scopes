# The `/wx` route label's deploy (#590)

**Read this first.** Production moved from `5c300417` to `4dde12411cdb` on 2026-11-07 at 16:50Z, and the only change a request
can reach is two lines of `KNOWN_ROUTES`. Every prediction read as frozen. `/wx` now has its own line in Analytics
Engine, so when weather ships its traffic can be read from day one.

| | |
|---|---|
| **Worker** | `blipscope-proxy`, production |
| **Before** | commit `5c300417`, version `3053d6b4-dc29-4d1d-8d50-9e8d0eaf7f00` |
| **After** | commit `4dde12411cdb`, version `cfa196cc-77ad-46bc-9fb0-16d488be0f17` |
| **Rollback target** | `3053d6b4-dc29-4d1d-8d50-9e8d0eaf7f00`, recorded before the deploy, not needed |
| **Rollback** | `npx wrangler rollback 3053d6b4-dc29-4d1d-8d50-9e8d0eaf7f00 --env production` |

## The Worker-input diff

Deploying main carried everything merged since `5c300417`, so the diff was read before the slot, from the bundler rather than
from filenames: `wrangler deploy --dry-run --env production --metafile`, which uploads nothing, at `5c300417` and at `6a5e1735548c`,
then main. Nothing the Worker reads changed between that and `4dde12411cdb` (`proxy/src`, `wrangler.toml`, the package files), so
the comparison holds for the deployed commit. The bundles compare module by module: 51 at both ends, none added or removed, 348,657
to 353,530 B. Five modules changed, counted in the bundle's lines, after tree-shaking:

| module in the production bundle | change | from | its production effect |
|---|---|---|---|
| `metrics.ts` | +4: two lines of `KNOWN_ROUTES`, two of comment | #590 | **the intended change:** a label, nothing else |
| `basemap/handler.ts` | +114, -24 | A1 (`9b511dc2`), Q-SW1 (#572), S2 (#589) | inert: behind the guard below |
| `basemap/budget.ts` | +8, the `LAYER_OF` table | #585 | inert: behind the guard. #585's own statement: "It changes no package and no answer." |
| `basemap/store.ts` | +1, a constant | A1 | inert: only the guarded path reads it |
| `basemap/format.ts` | +1, `var TYPE = {...}`. **No source change:** the bundler keeps the table because `LAYER_OF` now reads it | #585, through `budget.ts` | inert: behind the guard |

**The guard.** `if (!env.BASEMAP) return errorResponse(503, "not_configured");` is the first line of `handleBasemap` at both commits,
production binds neither `BASEMAP` nor `BASEMAP_PKG`, and `/healthz` read `basemap: not_configured`. **The sessions' own statements:**
A1, in `docs/basemap-a1.md` row MS9: "`BASEMAP_PKG` unbound: today's behaviour exactly", and in `types.ts`: "Unbound, /basemap behave
exactly as before A1". S2 reads the bucket only where it is bound, and the `wrangler.toml` change touches the dev top level and
staging only: "production gets one at D4".

**Source that changed and left the bundle unchanged:** `basemap/build.ts`, `wx/products.ts`, and `provision.ts`, which was a comment.

**Not in this bundle at all:** every ingest and dev change (#556, #573, #577, #579, the M stage), which reach production only through
their own Workers.

**K1 (#596) is not in this deploy.** It merged at 16:50:15Z, 18 seconds after `deploy.sh` started, as the commit after the deployed one.

## What was predicted, and read

| row | predicted | read |
|---|---|---|
| **D1** | main's CI on the deployed commit finished, read before `deploy.sh` | 32 checks, 0 failed, read twice. The deploy waited 5 minutes past the slot for this |
| **D2** | `/healthz` reports the commit, `ok: true`, `basemap: not_configured` | so, on three reads after the deploy and again at 17:06Z |
| **D3** | `smoke-prod.sh` passes as the test identity | 38 PASS, 0 FAIL |
| **D4** | `/basemap` answers 503 `not_configured` on the deployed Worker | so: the guard, read on production |
| **D5, the label** | one test-identity `/wx/cref` request appears under its own label, whether or not the fleet is polling | so: 18 requests under `/api/v1/blipscope/wx/cref` by 17:06Z, the test identity's among them with a 200 |
| **D5, 5xx** | in the 15 minutes after the deploy, exactly the 503s that D3 and D4 made to `/basemap`, counted from their own logs, all under `/other`, and nothing else on any route | `/other` 503: **2**, one from each, with 2 `err5xx` points and nothing else |

**The baseline for the 5xx rule was read first:** 0 in the 24 hours before the slot, over 42,460 request points. An early read at
16:52Z showed 1 of the 2; it was not graded, because the rule was frozen for 15 minutes.

## What this leaves

- `/wx` and `/other` are separate from this deploy onward, so a query spanning 16:50Z on 2026-11-07 sees both shapes.
- `/api/v1/blipscope/basemap` is still unlisted and lands in `/other`, which is why D3's and D4's 503s appear there.
