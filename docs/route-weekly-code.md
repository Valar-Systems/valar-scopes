# The weekly route refresh, code: registration, predictions first (DRAFT, 2026-11-13)

**Status: REGISTRATION, for review. No code is written yet.** This is condition 1 of the reviewer's acceptance of
`docs/route-weekly-refresh.md` (#702 at b7aa114d: conditions 1-8 and section 2.2's owner conditions): every piece is
registered with its red-first rows before it is written. It writes production on a schedule, so it gets full rigour:
red-first, CI, a staging rehearsal, then production.

**Nothing runs in CI until the owner has created the `routes-refresh` environment, at least `ROUTES_KV_TOKEN` and
`ROUTES_NTFY_TOPIC`, himself.** Until then SC2 reads `not set` and the job refuses to start. **This is on the
deadline's path:** condition 2's first attended run is a `workflow_dispatch` in this environment, so the environment and
the KV token must exist before about 11-24. Without the identity secrets the run goes ahead with the read-back NOT RUN
(RB1).

## 1. The pieces

| # | file | what |
|---|---|---|
| W1 | `proxy/scripts/routes-guards.ts` (new) | G1-G6 and the freeze rule as pure functions, with no I/O. Each takes counts and sets and returns `{ ok, reason }`. |
| W2 | `proxy/scripts/ingest-routes.ts` | a key-level mode inside the writer, so the value construction stays in one place (its own rule). New flags:<br>- `--key-level` with `--prev <tar>` (the snapshot the current meta was built from, proven against `meta:routes.shards` before anything is written);<br>- `--prev-also <tar> --prev-manifest <file>` (an older snapshot whose keys are delete candidates only; first run only);<br>- `--no-seal`;<br>- `--seal-only --receipt <file>`;<br>- `--readback off`;<br>- the overrides `--max-deletes`, `--allow-shard` and `--allow-removed`, refused when `GITHUB_EVENT_NAME=schedule`;<br>- `--out-written` and `--out-deleted` (local files for the verifier, never printed).<br>Meta gains `sourceCommit`, `sourceSha256`, `mode`, `deleted`, `pendingDeletes` and `readback`. |
| W3 | `proxy/scripts/verify-routes.ts` | `--expect-absent <file>` (every deleted key must be gone); `--check-values <file>` (every written key read back in bulk and compared byte for byte, using `kv bulk get` through the helper; this closes the hole where a changed value's dropped write leaves the old value present); `--receipt <out>` (written only on PASS). |
| W4 | `proxy/scripts/routes-freeze.sh` (new) | reads `ROUTES_FREEZE_UNTIL`: unset or past means run, future means skip with a stated line, malformed means FAIL. A malformed freeze never quietly runs and never quietly skips. |
| W5 | `.github/workflows/routes-refresh.yml` (new) | the job, in this order:<br>1. freeze;<br>2. resolve the new source's sha;<br>3. read meta's `sourceCommit` through the helper;<br>4. download both snapshots by sha;<br>5. `--key-level --no-seal`;<br>6. verify (absent + values + receipt);<br>7. at most one repair, then verify again;<br>8. `--seal-only`;<br>9. on failure or refusal, open or update a `route-refresh` issue, counts only.<br>Staging, then production through `needs:`. **The schedule ships disabled.** `workflow_dispatch` only, until the first attended run passes in both environments. |
| W6 | `scripts/reconcile-fleet.py` | prints the count of open `route-refresh` issues (`gh api`, a count only), with a warning when it is above 0. **It depends on 5c's fix (2026-11-13):** `read_healthz` sent no User-Agent, so Cloudflare answered 403 and #695's route-age block has printed "could not read /healthz" since it merged. #695's RC rows graded fixtures and could not see the live 403. Reconcile's route-age line counts as a reader of a failed run only once that fix is on main; W6 builds on it and does not touch it. |
| W7 | `proxy/scripts/routes-guards.selftest.mts` and `routes-freeze.selftest.sh` (new), run in `workers.yml` | the red-first rows below |
| W8 | `proxy/scripts/sabotage-routes-weekly.mjs` (new) | the plants below, each with its stated red set |
| W9 | `proxy/scripts/routes-notify.ts` (new) | the failure readers (condition 4, and the owner's ntfy yes):<br>- open, or comment on, ONE GitHub issue labelled `route-refresh`, through `GITHUB_TOKEN` (`issues: write`);<br>- POST the fixed ntfy template to `ROUTES_NTFY_TOPIC`, on failure only.<br>Both carry counts and the failure class only. The class is from a closed set: `refused-g1` ... `refused-g6`, `verify-fail`, `seal-refused`, `source-unreachable`, `secrets-not-set`, `frozen-malformed`. |
| W10 | `scripts/check_routes_workflow.py` (new), run in `device-ids.yml` beside the other tree guards | SC1, SC3 and SC5: the static checks over `.github/workflows/` |
| W11 | `proxy/scripts/routes-env-check.sh` (new) | SC2: the `routes-refresh` environment read from the GitHub API as booleans: main-only, plus which secret NAMES are set. The workflow's first step. It refuses to start unless main-only is true AND `ROUTES_KV_TOKEN` and `ROUTES_NTFY_TOPIC` are set. The three identity secrets are optional: without them the read-back is NOT RUN (RB1, condition 7) |
| W6b | `scripts/reconcile-fleet.py` | leaves out a `ci-test-device` read from the token file, as it leaves out `test-device` (the CI identity enrols like a device). The value is never printed |

**A scheduled time is not a start time (measured 2026-11-13).** `refresh-data.yml`'s cron says 06:17Z Monday. Its
last three scheduled runs started at 13:10Z (10-26), 16:03Z (11-02) and 16:28Z (11-09), between 7 and 10 hours late.
So the freeze check runs at the job's actual start, never against the cron time. Any reasoning of the form "05:47Z
misses the recording window" is void.

**Who sets the freeze, and how:** whoever schedules a recording, before it starts, with
`gh variable set ROUTES_FREEZE_UNTIL --body <ISO UTC>`. After it, they run `gh variable delete ROUTES_FREEZE_UNTIL`.
Repository admin is needed; the reviewer relays it for sessions.

## 2. Red-first rows (each is red against the tree before its code, green after, with a control)

| row | the case | red before | green after |
|---|---|---|---|
| GR1 (G1) | new rows at 98.9% of prev | no guard | refuses |
| GR1c | new rows at 99.1% of prev | (control) | passes |
| GR2 (G1) | a truncated source (half the shards) | no guard | refuses ALL, writes included |
| GR3 (G2) | 1 anchor missing from the new source | no guard | refuses deletes |
| GR3c | all 50 present, and all 50 present in prev (the anchor list's own control) | | passes |
| GR4 (G3) | a surviving shard loses 26% and 21 keys | no guard | refuses deletes |
| GR4c | 26% but 20 keys; or 25% and 100 keys | | passes |
| GR5 (G4) | 4 shards removed; or 3 removed holding 201 keys | no guard | refuses deletes |
| GR6 (G5) | 251 deletes, scheduled | no guard | refuses |
| GR6o | 827 deletes, `--max-deletes 1000`, attended | | passes |
| GR6s | 827 deletes, `--max-deletes 1000`, `GITHUB_EVENT_NAME=schedule` | | **refuses: no override on a schedule** |
| GR7 (G6) | a delete key `rt:AAL1:2`, or `rt:ZZ12345S`, or one absent from prev | no guard | refuses |
| GR8 (seal) | the verify forced to fail (one written key planted missing) | (today's tool seals in-run) | `meta:routes` byte-identical before and after; the control without the plant seals |
| GR9 (seal-only) | `--seal-only` with no receipt; with a receipt for another commit; with a receipt for another env | | refuses each |
| GR10 (values) | one written key's value planted wrong | presence-only verify passes | `--check-values` FAILs naming 1 key |
| GR10m (the value check's mismatch control, from #700's D2) | a known-correct key compared against a deliberately wrong expected value | | exactly 1 mismatch, shown before any result is believed |
| GR10a (the value check's absent control) | the made-up `rt:ZZQQQQQX` | | reads `null`, never an empty match |
| GR11 (absent) | one deleted key planted back | no absence check | `--expect-absent` FAILs naming 1 key |
| FZ1-FZ4 (freeze) | future, past, unset, malformed | no freeze | skip, run, run, FAIL |
| RB1 (readback off) | no test identity | the tool refuses (today it needs one) | the run proceeds and prints `readback: NOT RUN (the routes-refresh test identity is not set)`; meta records `readback: "not run"`. Never silent. Used only for an attended run before the owner has entered the identity |
| SC1 (no PR or fork trigger) | a copy of the workflow with `pull_request_target:` planted; and a second, otherwise clean workflow that names `environment: routes-refresh` | no check | each reads red, naming the file and the trigger. **Control:** the real workflow reads green |
| SC2 (main-only) | the API answer stubbed as: no environment; a policy of protected branches; names `["main","dev"]`; `ROUTES_KV_TOKEN` missing | no check | `not set`, `main-only: false`, `main-only: false`, `kv token: not set`. Each refuses the start. **Controls:** with all five set it prints `main-only: true`, `kv token: set`, `ntfy: set`, `read-back identity: set` and starts; with the three identity secrets absent it starts and prints `read-back identity: not set` (so RB1) |
| SC3 (no echo) | planted `run: echo ${{ secrets.ROUTES_KV_TOKEN }}`; planted `set -x`; planted `printenv` | no check | each reads red. **Control:** the real workflow reads green |
| SC4 (ntfy template) | every failure class rendered | no template | no 16-hex run and no coordinate in any of them. **Control:** each is non-empty and names its class. **Sabotage:** a class whose text includes a sample key reads red |
| SC5 (only ROUTES_* secrets) | planted `secrets.CLOUDFLARE_API_TOKEN` | no check | red. **Control:** the real workflow, green |
| RC5 (reconcile) | fixture: 2 open `route-refresh` issues | no line | `Route refresh: 2 open route-refresh issue(s)` and a warning; 0 issues prints the line with no warning |
| RC6 (reconcile) | fixture: a token file with `ci-test-device`, and that id in the KV fixture | the id is listed like a board | left out, as `test-device` is. **Control:** with the line absent, the id is listed (the exclusion is what removed it) |

**Sabotages (W8), each with exactly its stated red set:**
- G1 threshold set to 0: {GR1, GR2}.
- G6's shape regex loosened to `^rt:`: {GR7}.
- `--no-seal` writes meta anyway: {GR8}.
- `--seal-only` accepts any receipt: {GR9}.
- the value comparison always true: {GR10, GR10m}.
- the freeze ignored: {FZ1}.
- SC1's trigger list loosened to allow `pull_request_target`: {SC1}.
- SC3's scan limited to `env:` blocks: {SC3}.
- SC5's allowed-name list widened to `secrets.*`: {SC5}.
- the ntfy template given a free-text field: {SC4}.

## 3. CI

Predicted in its own commit before the code push, from main's CI after its last workflow change. The new selftests are
steps inside `workers.yml`'s existing job, so the check-run total is expected unchanged. `routes-refresh.yml` has no
`pull_request` trigger, so it adds no check-run to a PR. Both are stated as predictions, with an outside-the-set row.

## 4. The staging rehearsal and the first runs

1. **A dry run of each environment**, attended. It prints counts only: `sourceCommit`, rows, writes, deletes by kind
   (surviving shards, removed shards), and which guards would refuse.
2. **The first run's predictions are registered from that dry run:** the exact write and delete counts. Deletes are
   expected near 830: 827, plus the 24ce5424 keys the new source dropped. The overrides are named and counted.
3. **Staging:** the run, verify PASS (presence, values, absence), seal. Its KV writes and deletes are read from
   analytics against the prediction before production is scheduled (condition 5).
4. **Production:** the same, attended.
5. **The final row, S-ON: turning the schedule on is its OWN step** (ruled 18:23:38Z). It is a one-line PR that
   un-comments the `schedule:` trigger. It is made only after the first attended run's verify PASS (presence, values
   and absence) in BOTH environments, and merged only with the reviewer's approval at its exact sha. It is the change
   that makes production writes happen weekly with no person present, so nothing else rides in it. It must land before
   about 11-24 01:30Z, or the alert fires.

**The owner's deadline** (in his digest): the `routes-refresh` environment, `ROUTES_KV_TOKEN` and `ROUTES_NTFY_TOPIC`
must exist by about **11-22**, leaving margin before the 11-24 alert. Without them the first attended run cannot
happen, and the alert firing is correct.
