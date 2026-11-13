# reconcile-fleet's /healthz read sends a User-Agent (registration; predictions before the code)

**Found** in v19's step 8, day 2 (2026-11-13, 17:30Z). The fleet read printed a line nobody had predicted:
`INSTRUMENT WARNING: route mirror: could not read /healthz`. Measured at 17:31Z:
- `https://scopes.valarsystems.com/healthz` answers Python's default User-Agent, `Python-urllib/3.14`, with **403**
  (`server: cloudflare`);
- the same request from Python with User-Agent `curl/8.9.1` or `blipscope-reconcile-fleet` gets **200**, and so does
  plain curl.

`read_healthz` (`scripts/reconcile-fleet.py`) sends no User-Agent. So the route-mirror block from #695 has never read
/healthz from Python. Its warning is honest: it says it could not read, rather than passing silently. But it can never
report the mirror's age. `scripts/provision_one.py` already records the same edge behaviour ("Cloudflare's edge 403s
the default `Python-urllib/3.x`").

**Why the selftest stayed green.** The RC rows hand `route_mirror_lines` a fixture body. Nothing exercises the request
that `read_healthz` builds, so a 403 on every real call was invisible to it. The new row takes its input from the
request itself.

## Other Python tools that reach the Worker with no User-Agent

There are none besides this call. Measured on main b0c9412f:
- 36 Python files call `urlopen` and set no User-Agent anywhere.
- Their targets are `api.cloudflare.com`, R2's S3 API, AWS S3 (`noaa-mrms-pds`), a board on the LAN, or localhost.
  Two of them, `check-config-form.py` and `check-wx-legend.py`, take a `--url` that names a device.
- `api.cloudflare.com` accepts Python's default UA. This script's own Analytics Engine query (`:329`) sends none and
  read the OTA buckets in the same day-2 run.
- `provision-device.py` and `provision_one.py` already send a UA.
- `tools/wx-replay/proxy.py` forwards the device's own headers by design, so it is out of scope.

## The change

`read_healthz` sends a `Request` with
`User-Agent: Blipscope-ReconcileFleet/1 (+https://github.com/Valar-Systems/valar-scopes; support@valarsystems.com)`.
This follows the ingest tools' form (the repo address) plus the support address, as ruled. Only this call changes.
The GitHub `version.txt` read and the Analytics Engine query both read today and are left as they are.

## Predictions

**Selftest** (`scripts/reconcile-fleet.selftest.py`, which CI's workers job runs). No network: `urlopen` is replaced
by a stub that records what it was handed.

| row | what it does | predicted |
|---|---|---|
| **RH1** | `read_healthz("production")` through the stub | the stub receives one `Request` for the production /healthz URL, and its User-Agent carries both `support@valarsystems.com` and `github.com/Valar-Systems/valar-scopes` |
| **RH1-ctl** | the same call | the stub was called exactly once, with the production /healthz URL. Otherwise RH1's verdict means nothing, so it reads BLIND |
| **RH-S1** (plant, in memory) | the User-Agent header removed from `read_healthz` | RH1 red, RH1-ctl as predicted |
| every existing row (TX2, S-TXRECON, R1 and its plants, U1, U1-ctl, BX1-BX4, S-BX, RC, RC-S1) | unchanged | as before, `SELFTEST PASSED` |

**Rehearsed red first.** The new rows run against main's unchanged script and must read:
- RH1 **OFF**: the stub receives a plain URL string, with no User-Agent;
- RH1-ctl as predicted: it is still called once;
- RH-S1 **BLIND**: its anchor exists only after the change.

**Live, by hand, not in CI.** These read production /healthz through the module's own `read_healthz` and
`route_mirror_lines`, with no fleet read and no wrangler:

| row | script | predicted |
|---|---|---|
| **L-red** | main's | lines `["  Route mirror (rt:): could not read /healthz"]`, warnings `["route mirror: could not read /healthz"]` |
| **L-green** | changed | one line `  Route mirror (rt:): built <ISO>, A d old, R rows (limit 14 d)  <== STALE`, with A in {78, 79} and R a positive integer; warnings `["route mirror: A d old, past its 14-day limit"]` |

L-green's STALE is the true reading, not an instrument failure: the mirror is past its limit, and 14 is main's
`ROUTES_MAX_AGE_DAYS`.

**CI: 35 checks, 33 success and 2 skipped, as #679** (the same three kinds of file: this script, its selftest and a
doc). If the count differs, the job that differs is named before anything is judged. #694 and #695 added jobs to main
since #679.

**Outside the set:** any other reading means stop and report.
