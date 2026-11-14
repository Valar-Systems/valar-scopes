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

### 2b. The owner's KV-token conditions (2026-11-13 18:25Z, relayed verbatim at 18:33:52Z)

> "Weekly route job, KV token: accepted, since KV can't be scoped tighter. Conditions: - 90-day expiry; remind me 7
> days before it ends; - every action in that workflow pinned by full commit sha; - the step holding the token
> installs only from the lockfile (npm ci); - the job refuses to write any key outside the route prefix. For the
> rotation, price a Worker endpoint whose own secret can write only route keys. Test identity: I'll add the
> "ci-test-device" line to my token file myself when I create the secrets."

| row | condition | the check, red-first, with a control |
|---|---|---|
| **TK1** | "remind me 7 days before it ends" | **Mechanised, not remembered:** every run asks Cloudflare's token-verify endpoint, authenticated by the token itself, for the token's `expires_on`, and prints the DATE only. At 7 days or fewer it opens or updates the `route-refresh` issue and sends ntfy: `ROUTES_KV_TOKEN expires in <N> days`. A token with NO `expires_on` breaks the 90-day condition and warns the same way. **Red-first, on a stubbed verify answer:** an expiry 5 days out warns; no expiry warns; **control:** 30 days out does not warn. A failed verify call is a run failure, never a silent pass |
| **PIN1** | "every action in that workflow pinned by full commit sha" | W10's static check: every `uses:` in `routes-refresh.yml` is `owner/repo@<40 hex>`, with an optional version comment. **Red-first:** a planted `actions/checkout@v4` reads red. **Control:** the real file, all pinned, reads green |
| **NPM1** | "the step holding the token installs only from the lockfile (npm ci)" | W10's static check, over every step whose `env:` names `ROUTES_KV_TOKEN` and every step after one in the same job: the only install allowed is `npm ci`. `npm install`, `npm i`, `yarn`, `pnpm`, `pip install`, and `npx` without `--no-install` are refused. **Red-first:** a planted `npm install` reads red, and so does a planted `npx some-pkg`. **Control:** green |
| **PFX1** | "the job refuses to write any key outside the route prefix" | the writer checks EVERY key of every bulk put and delete BEFORE the first call: `^rt:[A-Z0-9]{2,8}$`, the exact `meta:routes`, or the sentinel shape `^rt:ZZ[0-9A-F]{5}S$`. Anything else means exit non-zero and nothing written. **Red-first, through the fake KV:** a planted `meta:other` pair is refused, and the store file is byte-identical before and after (0 writes). **Control:** the same run without the plant writes |
| **FK1** (the reviewer, after 18:39:58Z) | the test-only `ROUTES_KV_CLI` can never stand in for real KV on the production path | the writer, and the verify once it gains the hook (W3), REFUSE `ROUTES_KV_CLI` when `GITHUB_ACTIONS` is set OR `--env production` is given: exit 2 BEFORE any KV call. **Red-first:** before the guard, `ROUTES_KV_CLI` with `--env production --dry-run` proceeds through the fake (red). After it, it is refused, exit 2, with the fake store byte-identical. **Controls:** `--env staging` through the fake still works; the same with `GITHUB_ACTIONS=true` is refused. **SC1 also asserts** that the workflow file never names `ROUTES_KV_CLI` (a plant reads red) |
| **PR1** | "price a Worker endpoint whose own secret can write only route keys" | **A note, not code,** for the rotation decision in about 90 days. Section 6 below |

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

### 2c. Amendments of 2026-11-14, each ruled by the reviewer before its code

**TK1's threshold is 14 days** (the reviewer, ~03:41Z, accepting the proposal that turned out to be TK1 itself). There
is one row, not two, because two guards on one rule drift apart. Weekly runs then warn once inside 14 days and again
inside 7, which covers the owner's "remind me 7 days before it ends". TK1's red-first rows, on a stubbed verify answer:

| row | stub | predicted |
|---|---|---|
| TK1a | `expires_on` 13 days out | warns: the issue is opened or updated, and ntfy sends `ROUTES_KV_TOKEN expires in 13 days` |
| TK1b | 15 days out | **(control)** no warning |
| TK1c | no `expires_on` | warns that the 90-day condition is broken |
| TK1d | the verify call fails | a run failure, never a silent pass |

**An unchanged source re-seals after a verify** (ruling (i)).
- **The case:** the new source's sha equals meta's `sourceCommit`. Nothing is written and nothing is deleted.
- **The verify:** values and absence against the last change, recomputed from that change's two snapshots. Meta gains
  `prevSourceCommit`, so they can be downloaded by sha.
- **On PASS:** `--seal-only` re-seals, so `builtAt` means "last verified current".
- **Rows:**
  - UN1: an unchanged source with every value correct re-seals, and `builtAt` moves;
  - UN2: the same with one value planted wrong does not seal, and meta stays byte-identical;
  - UN3: the same with one deleted key planted back does not seal.

**The source's own commit date on `/healthz`**, informational, with no alert (the reviewer's addition to ruling (i)).
- An upstream that stops changing must stay visible even though `builtAt` keeps moving.
- **How:** W5 resolves the source's sha through the GitHub commits API, which also gives the commit's date. The writer
  takes it as `--source-date <ISO>` and stores it in meta as `sourceCommittedAt`.
- **W13:** `routeshealth.ts` passes `sourceCommittedAt` through in the `routes` field, as a string, when meta has one,
  and omits it when meta has none. It never invents a value.
- **Rows:**
  - RH9: meta with `sourceCommittedAt` shows it byte-identical;
  - RH9c (the control): meta without it shows no such field;
  - RH9s: a non-ISO value in meta is omitted, not shown.
- **This is a proxy change.** It rides this PR and needs its own later deploy, not the 11-16 one, whose carried set was
  fixed at plan time (#714).
- Whether an old `sourceCommittedAt` should ALERT is the owner's call. No threshold is proposed until a week of real
  data exists.

**Seal last, by construction, in the key-level mode.**
- A key-level run that writes or deletes must be given `--no-seal`; without it, it is refused (exit 2) before any KV
  call.
- The only key-level path that writes `meta:routes` is `--seal-only --receipt <file>`.
- **Row KL1:** a key-level write without `--no-seal` is refused, with the fake store byte-identical.
- **Control:** the same with `--no-seal` writes the route keys and leaves `meta:routes` byte-identical (GR8's
  stage-2 half).

### 2d. W3, the verify, and the writer's replay (registered 2026-11-14 ~04:20Z, before their code)

**The verify (`verify-routes.ts`) gains:**
- **The fake-KV hook and FK1.** It reads `ROUTES_KV_CLI` like the writer, and refuses it under `GITHUB_ACTIONS` or
  `--env production` (exit 2, before any KV call).
- **`--check-values <the writer's --out-written file>`.**
  - Every written key is read back with `kv bulk get` through the helper, 100 keys a call, and compared byte for byte.
  - Two controls run first, from #700's D2:
    - GR10m: the first written key, compared against a deliberately wrong value, gives exactly 1 mismatch;
    - GR10a: the made-up `rt:ZZQQQQQX` reads null.
  - Counts are printed: checked, match, mismatched, absent. The keys go to a local file only.
- **`--expect-absent <the writer's --out-deleted file>`.** Every deleted key must read null. A present one is a
  failure, counted.
- **The two files' run facts must agree** with each other, with `--env`, and with `--file`'s own source commit. A
  mismatch refuses (exit 2).
- **`--receipt <path>`.** The path is removed at the START of the run, so a stale receipt never survives a later
  failure. The receipt is written only if ALL of these hold:
  - coverage PASS;
  - 0 manufactured self-loops;
  - both controls behaved;
  - 0 mismatched, 0 absent written keys and 0 present deleted keys;
  - the run's `deletesRefused` false;
  - and both `--check-values` and `--expect-absent` were given.
- **The receipt's shape** is the one `--seal-only` reads:
  `{v, result:"PASS", env, sourceCommit, sourceSha256, prevSourceCommit, prevMetaSha256, written, deleted, deletesRefused, readback, sentinel?, checkedAt}`.

**The writer's `--replay`, for an unchanged source (UN1-UN3).**
- `--key-level --replay --prev <the last change's prev> --file <the sealed source>` writes and deletes NOTHING.
- It proves three things:
  - `--file`'s shard hashes equal meta's;
  - meta's `sourceCommit` is `--file`'s commit;
  - meta's `prevSourceCommit` is `--prev`'s commit.
- Then it emits the last change's sets as `--out-written` and `--out-deleted`, so the verify checks them again, and a
  receipt lets `--seal-only` re-seal.
- The value construction stays in the writer.

**Rows:**

| row | the case | red before | green after |
|---|---|---|---|
| GR10 | one written key's value planted wrong in the store | presence-only verify passes | `--check-values` FAILs, 1 mismatched; no receipt |
| GR10m | the mismatch control | | exactly 1, shown before any result |
| GR10a | the absent control | | null, never an empty match |
| GR11 | one deleted key planted back | no absence check | `--expect-absent` FAILs, 1 present; no receipt |
| RCP1 | any failure, with a stale receipt already at `--receipt`'s path | the stale file survives | the path is gone after the run |
| RCP2 | the control: a clean run | | the receipt exists, and `--seal-only` with it seals (the end-to-end GR8 with W3) |
| FKV | `ROUTES_KV_CLI` with `--env production` on the verify | proceeds through the fake | exit 2, nothing read |
| UN1 | replay of an unchanged, correct source | `--replay` unknown | the verify PASSes, `--seal-only` re-seals, `builtAt` moves |
| UN2 | the same, one value planted wrong | | no receipt, meta byte-identical |
| UN3 | the same, one deleted key planted back | | no receipt, meta byte-identical |

### 2e. FK1, revised by the reviewer (2026-11-14 ~04:20Z, before W7's code)

**The ruling:** "Tie the refusal to the property that actually matters, a REAL CREDENTIAL in reach, not to 'running in
CI'." FK1 refuses `ROUTES_KV_CLI` when ANY of these holds:
- **the routes-refresh workflow:** `GITHUB_WORKFLOW` is `routes-refresh`, or `GITHUB_WORKFLOW_REF` names
  `.github/workflows/routes-refresh.yml`;
- **`--env production`;**
- **a real token variable in the environment, judged by NAME only and never by value:** `ROUTES_KV_TOKEN`,
  `CLOUDFLARE_API_TOKEN`, or `BLIPSCOPE_TOKEN_FILE` naming a file that exists.

In `workers.yml` none of these holds, so the fake-KV selftest runs there as a normal step. The rejected options:
- (a), unsetting a variable for the child process, is a path round the guard;
- (c), local-only, leaves a guard that never runs.

**One rule, one home:** `proxy/scripts/routes-fk1.ts` exports `fk1Refusal(env, target, exists)`. It returns the NAME of
the condition that holds, or null, and never a value. Both the writer and the verify call it before any KV call. Its
rows are in `routes-guards.selftest.mts`:

| row | environment | predicted |
|---|---|---|
| FK1a | a workers-like environment: none of the conditions, `--env staging` | allowed (null) |
| FK1b | `GITHUB_WORKFLOW=routes-refresh` | refused, naming the workflow |
| FK1b2 | `GITHUB_WORKFLOW_REF` ending in `/.github/workflows/routes-refresh.yml@refs/heads/main` | refused |
| FK1c | `--env production` | refused |
| FK1d | `ROUTES_KV_TOKEN` set | refused, naming `ROUTES_KV_TOKEN` |
| FK1e | `CLOUDFLARE_API_TOKEN` set | refused, naming `CLOUDFLARE_API_TOKEN` |
| FK1f | `BLIPSCOPE_TOKEN_FILE` naming a file that exists | refused |
| FK1g | **the control for FK1f:** `BLIPSCOPE_TOKEN_FILE` naming a file that does not exist | allowed |
| FK1h | a refusal's text contains none of the variables' VALUES (planted values, checked absent) | true |

**End to end,** the writer selftest's FKV row (the verify on `--env production`) stays, and a new row FKW runs the
writer with `CLOUDFLARE_API_TOKEN` set. Each refuses with exit 2 and the store byte-identical.

**Sabotage S-FK1:** the credential condition dropped. Red is exactly {FK1d, FK1e, FK1f}.

**The sabotage red sets, re-registered 2026-11-14 ~04:35Z, before either run, with the corrections stated:**
- **S-FK1**, all three credential conditions dropped (both variables and the file):
  - the guards selftest: red exactly **{FK1d, FK1e, FK1f, FK1h}**;
  - the writer selftest: red exactly **{FKW}**.
  - The set above left out FK1h. Its planted environment holds only credential conditions, so with them dropped its
    refusal is empty, and the row reads red.
- **S-VAL**, the shared value comparison `same()` made always true:
  - the GR10m control then sees 0 mismatches, and the verify refuses to judge (exit 2);
  - so every verify that checks values stops, and red is exactly **{GR10m, GR10a, RCP2, GR10, GR11, UN1, UN2, UN3}**;
  - FKV and RCP1 stay green.
  - W8's list said {GR10, GR10m}. It was written before the control gated the whole verify. And until this commit the
    control did not even share the comparison it guards, a defect found while writing this row and fixed in the same
    commit.

### 2f. W5, the workflow, and W10, its static checks (registered 2026-11-14 ~05:10Z, before their code)

**The shape: a thin workflow over ONE run script.** `proxy/scripts/routes-refresh-run.sh <staging|production>` is the
job's whole logic, so it can be graded end to end through the fake KV. The YAML stays small enough for W10 to read
every line of it.

**The run script, in order:**
1. **The environment check** (W11). A refusal is class `secrets-not-set`.
2. **The freeze** (W4). Exit 4 ends the run green with the stated line. A malformed freeze is `frozen-malformed`.
3. **The token check** (TK1). A warning sends `token-expiring` and the run continues; a failure stops it.
4. **The source:**
   - main's sha and commit date from `gh api repos/vradarserver/standing-data/commits/main`;
   - the tarball downloaded by that sha from codeload, with its sha256.
   - Either failing is `source-unreachable`.
5. **Meta, through the helper:**
   - **no `sourceCommit` (a meta (A) or the full mode wrote) REFUSES:** the first key-level run is the attended local
     one (section 4), never CI's;
   - the snapshot of meta's `sourceCommit` is downloaded by sha.
6. **The write:**
   - **a changed source** runs `--key-level --no-seal --prev <meta's sourceCommit> --file <new>`, with
     `--readback on|off` from W11's output;
   - **an unchanged source** runs `--replay --prev <meta's prevSourceCommit> --file <new>` (UN1-UN3);
   - a G1 refusal is `refused-g1`;
   - when the deletes are refused, the first failing guard names the class, `refused-g2` to `refused-g6`.
7. **The verify,** with `--check-values`, `--expect-absent` and `--receipt`.
8. **At most one repair.** On a verify FAIL, the same write runs once more (the writes are idempotent), then the
   verify again. A second FAIL is `verify-fail`.
9. **The seal:** `--seal-only --receipt <r> --source-date <the commit date>`. A refusal is `seal-refused`.
10. **On any class,** `routes-notify.ts` runs with it and the counts, and the job fails. With no class, the job is green.

**The workflow (`.github/workflows/routes-refresh.yml`):**
- **Triggers:**
  - `workflow_dispatch` only, with an input `target`, `staging` or `both`;
  - the `schedule:` block (Tuesday 05:47Z) ships COMMENTED OUT; S-ON un-comments it, in its own PR.
- **Two jobs:**
  - `staging`;
  - `production`, which `needs: staging` and runs only for `both` (and later for the schedule).
- **Each job:**
  - `environment: routes-refresh`, `concurrency: routes-refresh`, `permissions: contents: read, issues: write`;
  - checkout and setup-node pinned by full sha (PIN1), each sha read from the GitHub API at the time of writing,
    never typed;
  - `npm ci` in `proxy/`, then every tool through `npx --no-install tsx` (NPM1);
  - one step that maps `secrets.ROUTES_*` into its environment: `CLOUDFLARE_API_TOKEN` from `ROUTES_KV_TOKEN` for the
    helper, plus the ntfy topic and the identity, with `GH_TOKEN` from `github.token`. No other `secrets.` name appears
    (SC5).

**W10, `scripts/check_routes_workflow.py`, run in device-ids.yml.**
- **SC1:**
  - `routes-refresh.yml`'s triggers are only `workflow_dispatch` and `schedule`;
  - no other workflow names `environment: routes-refresh`;
  - no workflow names `ROUTES_KV_CLI`.
- **SC3:** in `routes-refresh.yml`, no `run:` text contains `secrets.`, `set -x` or `printenv`.
- **SC5:** every `secrets.<NAME>` in it is a `ROUTES_` name.
- **PIN1:** every `uses:` is `owner/repo@<40 hex>`, with an optional `# vN` comment.
- **NPM1, stricter than its row, which is simpler to check:** nowhere in `routes-refresh.yml` does `npm install`,
  `npm i `, `yarn`, `pnpm`, `pip install`, or an `npx` without `--no-install` appear.

**`--selftest`** plants one copy per row, each of which must read red, and the real file must read green (the
control). **`--sabotage <name>`** weakens one check, and the selftest's red set must change exactly as stated:
- SC1's trigger list loosened turns {SC1t} green;
- SC3's scan limited to `env:` blocks turns {SC3e} green;
- SC5's allowed names widened to `secrets.*` turns {SC5} green.

**The run script, end to end, through the fake KV** (`routes-run.selftest.sh`). Test hooks: `ROUTES_SOURCE_DIR` (local
tarballs by sha) and `ROUTES_RUN_GH` (canned API answers). FK1 refuses both under the same conditions as
`ROUTES_KV_CLI`:

| row | the case | predicted |
|---|---|---|
| RR1 | a changed source | write, verify, seal; meta's `sourceCommit` is the new one; no class |
| RR2 | the next run, with the same source | replay, verify, re-seal; `builtAt` moves |
| RR3 | one value planted wrong, and it stays wrong after the one repair (planted again between the write and the verify) | class `verify-fail`; meta byte-identical; the notify called with that class |
| RR4 | a meta with no `sourceCommit` | refused before any write ("the first run is attended") |
| RR5 | a future freeze | exit 0 with the stated line; nothing read from KV |

**CI for this push** (its own commit, CI-C5, before the push):
- the new workflow has no `pull_request` trigger, so it adds no check-run;
- W10 and W7 add STEPS to the one-job `device-ids` and `workers` workflows;
- so the predicted total is unchanged at 36.

### 2g. W8's re-run, the driver's baseline, and the stale-temp sweep (the reviewer's rulings, 2026-11-14 ~05:50Z)

**The first full driver run is RECORDED: 7 of 14 OFF.** It was the driver's instrument failure #1:
`execFileSync("npx")` cannot start without a shell on Windows (ENOENT), so six graders never ran, and their errors read
as red `[]`. S-GR8 is recorded OFF too, because its registration predated W3. An off reading is not re-graded.

**The driver, as ruled:**
- Graders run as `node node_modules/<pkg>/<bin>`, with no npx and no shell, on both OSes.
- **The BASELINE comes first:** every grader on the clean tree must read its full count before any plant: guards 50,
  writer 25, freeze 4, run 7, routes-health 11, and reconcile's RC5-RC6 group present with red none.
- A grader that reads no rows, or the wrong count, is **BLIND and STOPS the driver** (exit 2). It never yields `[]`.
- **The baseline's own sabotage:** `--sabotage-blind` points the guards grader at a binary that does not exist. The
  driver must stop at the baseline with `BLIND`, exit 2, with no plant applied.

**Every set re-derived against the W3 and TMP rows, row by row, before the re-run:**

| plant | its grader | what the plant reaches | the set |
|---|---|---|---|
| S-GR9 | writer | GR9b, c, e, f, g lose their refusal; RCP2 and UN1-UN3 hold VALID receipts, so they are unaffected | unchanged |
| **S-GR8** | writer | the plant writes meta "planted" on EVERY `--no-seal` run: GR8's own, and the W3 section's write. So RCP2's `--seal-only` meets an unparsable meta and refuses (RCP2 red), and UN1-UN3 have no sealed store to replay (red). KL1 and PFX1 refuse before any write; FKW is a dry run; RB1's line still prints | **{GR8, RCP2, UN1, UN2, UN3}** |
| S-FK1 | guards | FK1d, e, f and h. (FKW in the writer selftest would also go red, but the driver grades this plant with guards only) | unchanged |
| S-VAL | writer | as recorded: the control stops every value-checking verify | unchanged |
| S-RH9s | routes-health | RH9s only; RH9 and RH9c hold well-formed values | unchanged |
| S-TK1 | guards | TK1a only | unchanged |
| S-FZ | guards | FZ1 only. (FZC1 and RR5 would also go red in their own selftests, which do not grade this plant) | unchanged |
| S-FZC | freeze | FZC1 only. (RR5 would also go red in the run selftest, which does not grade this plant) | unchanged |
| S-SC4 | guards | SC4 only | unchanged |
| S-SC2 | guards | SC2b and SC2c | unchanged |
| S-RR3 | run | RR3 only | unchanged |
| S-TMP-W, S-TMP-V | writer | TMP1 only; the plants change no result, only what is left behind | unchanged |
| S-RC6 | reconcile | RC6 only | unchanged |

Only S-GR8 predated W3.

**The stale-temp sweep (accepted for #708).**
- **One home:** `proxy/scripts/routes-tmp.ts` exports `tmpDir()`, the exit cleanup, and `sweepStale(dir, prefixes,
  now)`. Both tools import it, replacing their two copies.
- At start, each tool removes ITS OWN prefix dirs older than 6 hours:
  - matched by prefix AND by content, as the 11-14 deletion was;
  - a count is printed, never a name;
  - another prefix and `Temp\claude` are never touched.
- **The 6-hour floor is what keeps a concurrent run's live dirs safe.** The code says so.
- **Rows, in the guards selftest, on a private dir:**

| row | planted | predicted |
|---|---|---|
| SW1 | a stale dir (mtime 7 h ago) holding a `standing-data-*` tree | removed |
| SW2 | a fresh dir (1 h ago), the same content | kept |
| SW3 | a stale dir holding a foreign file | kept, and counted as foreign |
| SW4 | a stale dir of ANOTHER prefix | untouched, and not counted |

- **Sabotage S-SW:** the age test off. Red exactly {SW2}.

**Where the writer rows are graded:** GR8, GR9, PFX1, RB1, KL1, and W3's GR10, GR10m, GR10a, GR11, RCP1, RCP2, FKV and UN1-UN3, run in a new
`proxy/scripts/routes-writer.selftest.sh`, through the fake KV only. W7 wires it into `workers.yml` beside
`routes-guards.selftest.mts`.

## 3. CI

Predicted in its own commit before the code push, from main's CI after its last workflow change. The new selftests are
steps inside `workers.yml`'s existing job, so the check-run total is expected unchanged. `routes-refresh.yml` has no
`pull_request` trigger, so it adds no check-run to a PR. Both are stated as predictions, with an outside-the-set row.

**CI-C1, the first code push (predicted 2026-11-13 ~18:40Z, before the push).** The push carries W1, the parse and
`readMeta` refactor, the fake KV, the anchors and W2 stage 1. It changes `proxy/scripts/**`, one comment in
`proxy/test/route-mirror.test.ts`, and `docs/`. It changes no workflow.

| row | predicted |
|---|---|
| **CI-C1** | **36 check-runs = 34 success + 2 skipped (promote, version) + 0 other,** across four `pull_request` workflows, all success: device-ids, firmware, no-traffic-captures, workers. The baseline: main's last workflow change is #698 (aa40aa43), whose head ran 36 = 34 + 2. There is none since, on main 287dc5ec. `typecheck:scripts`, which covers the new and changed scripts, passed locally |
| outside the set | any other total or split: stop and decide |

**CI-C1, read: AS PREDICTED.** The head 66b6af9f ran 36 = 34 success + 2 skipped, 0 other, finished and stable at
19:12:12Z. The workers checkout log: `HEAD is now at ac0d449 Merge 66b6af9f... into 287dc5ec...`.

**CI-C2, the FK1 push (predicted 2026-11-13 ~19:13Z, before the push):** **36 = 34 success + 2 skipped + 0 other,**
across the same four workflows. The push changes `proxy/scripts/ingest-routes.ts` and `docs/` only. Main is now
e9aa69a1 (#706, #707), and neither changed a workflow; the last workflow change is still #698. Outside the set: stop and
decide.

The new selftest (`routes-guards.selftest.mts`) is NOT yet a CI step. W7 wires it into `workers.yml` in a later
commit, with its own prediction.

**CI-C3, the stage-2 push (predicted 2026-11-14 ~04:05Z, before the push).**
- **The push carries:** the upper-bound ruling, section 2c, and W2 stage 2 (`ingest-routes.ts`, plus the new
  `routes-fixture.mts` and `routes-writer.selftest.sh`, neither yet a CI step).
- **The baseline:** main is `ce726004` (#713), read with `git ls-remote` at ~04:00Z. The last workflow change is #711
  (`26f486ab`): three steps added to device-ids' one job, and no new job. #703's head ran 36 = 34 + 2 against it.
- **Locally, before the push:** `typecheck:scripts` exit 0. `git merge-tree` against main is clean; main's
  `ingest-routes.ts` change (#703's comment) merges without conflict. #711's coordinate gate on a local merge into
  `ce726004` is in section 7.
- **Predicted: 36 check-runs = 34 success + 2 skipped (promote, version) + 0 other,** across the four
  `pull_request` workflows, all success: device-ids (now also running #711's gate over this PR's added lines),
  firmware, no-traffic-captures and workers.
- **Outside the set:** any other total or split. Stop and decide.

**CI-C4, the W3 push (predicted 2026-11-14 ~04:40Z, before the push).**
- **The push carries:** sections 2d and 2e, W3 (`verify-routes.ts`), the revised FK1 (`routes-fk1.ts`, used by both
  tools), the writer's `--replay`, and the extended selftests. `routes-writer.selftest.sh` is still not a CI step (W7).
- **The baseline:** main is `5fd4da8a` (#714), read with `git ls-remote` at 04:31:52Z. Since CI-C3, #715 and #714
  changed no workflow; the last workflow change is still #711.
- **Predicted: 36 check-runs = 34 success + 2 skipped (promote, version) + 0 other,** across the four
  `pull_request` workflows, all success.
- **Outside the set:** any other total or split. Stop and decide.

**CI-C4b, the push of W13, TK1, W4, W9 and W11 (predicted 2026-11-14 ~05:10Z, before the push).**
- **The push carries:** main `cb65380c` merged in; W13 (`proxy/src/routeshealth.ts` and its test); TK1, W4, W9 and W11
  under `proxy/scripts/`; sections 2f and 7. It changes no workflow: W5, W7 and W10 are the next push, CI-C5.
- **The baseline:** main is `cb65380c` (read with `git ls-remote` at ~05:09Z). No workflow changed since #711.
- **Predicted: 36 check-runs = 34 success + 2 skipped (promote, version) + 0 other,** across the four
  `pull_request` workflows, all success. The workers job now runs `routes-health.test.ts` with RH9-RH9s inside the
  suite; locally it reads 11 of 11.
- **Outside the set:** any other total or split. Stop and decide.

**CI-C5, the push of W5, W7, W10 and the run script (predicted 2026-11-14 ~05:25Z, before the push).**
- **This push CHANGES workflows,** which a `pull_request` run takes from the PR's own merge:
  - `routes-refresh.yml` is new, with no `pull_request` trigger, so it adds no check-run;
  - `workers.yml` gains four steps in its `test` job;
  - `device-ids.yml` gains two steps in its one job.
- **Predicted: 36 check-runs = 34 success + 2 skipped (promote, version) + 0 other.** The four `pull_request`
  workflows all succeed, and each new step passes on Linux, the first time any of them runs there.
- **Outside the set:** any other total or split, or a new step red. Stop and decide; a Linux-only failure is a finding,
  never a re-baseline.
- **Added before the push (2026-11-14 ~05:50Z):** the push also carries W6/W6b (`reconcile-fleet.py` and its selftest,
  which already runs in CI), the temp-dir fix with TMP1 and TMP1-ctl, and W8's driver (not a CI step). None changes a
  workflow, so the predicted total is unchanged. The writer and run selftests are now 25 and 7 rows.

## 4. The staging rehearsal and the first runs

1. **A dry run of each environment**, attended. It prints counts only: `sourceCommit`, rows, writes, deletes by kind
   (surviving shards, removed shards), and which guards would refuse.
2. **The first run's predictions are registered from that dry run:** the exact write and delete counts. Deletes are
   expected near 830: 827, plus the 24ce5424 keys the new source dropped. The overrides are named and counted.
3. **Staging:** the run, verify PASS (presence, values, absence), seal. Its KV writes and deletes are read from
   analytics against the prediction before production is scheduled (condition 5).
   - **The analytics count is an UPPER BOUND only: a cost stop, never a completeness grade** (the reviewer's ruling,
     2026-11-14 ~02:17Z, on (A)'s P5).
     - A read ABOVE the predicted writes plus the stated retry and background allowance stops the run's next step.
     - A read BELOW the prediction grades nothing.
   - **Why:** in (A) the same count read 0.039% under the run's real writes on staging, and 0.052% under on
     production. D1 then showed that none of the missing writes had been stamped outside the window
     (`docs/route-refresh-2026-11.md`, P5 and D1). At that precision the meter cannot say whether a write landed.
   - **Completeness is graded by the run's own receipt and the verify** (condition 8: presence, values and absence),
     never by the meter.
4. **Production:** the same, attended.
5. **The final row, S-ON: turning the schedule on is its OWN step** (ruled 18:23:38Z). It is a one-line PR that
   un-comments the `schedule:` trigger. It is made only after the first attended run's verify PASS (presence, values
   and absence) in BOTH environments, and merged only with the reviewer's approval at its exact sha. It is the change
   that makes production writes happen weekly with no person present, so nothing else rides in it. It must land before
   about 11-24 01:30Z, or the alert fires.

**The owner's deadline** (in his digest): the `routes-refresh` environment, `ROUTES_KV_TOKEN` and `ROUTES_NTFY_TOPIC`
must exist by about **11-22**, leaving margin before the 11-24 alert. Without them the first attended run cannot
happen, and the alert firing is correct.

## 6. PR1: a route-write Worker endpoint against the KV token, priced (a note for the rotation decision, about 90 days)

**The alternative.** The workflow holds no KV token. It POSTs batches of `[key, value]` and `delete` lists to a
route-write endpoint on a Worker. The Worker holds its own secret, accepts only the route key set (PFX1's shapes,
checked server-side) and writes through its existing `ENRICH_KV` binding. The GitHub secret then authorizes route
writes on one endpoint and nothing else.

| | the KV token (as built) | a Worker endpoint |
|---|---|---|
| **what the GitHub secret can reach** | every KV namespace on the account, read and write | route keys on one namespace per environment, through one endpoint |
| **KV writes and deletes** | the same count either way: about 10,000-14,000 writes a week per environment | the same: the writes are KV writes inside the Worker, priced identically |
| **extra Worker requests** | none | about 15 a run per environment: one per batch of about 1,000 keys (a Worker invocation's KV-operation limit). That is about 130 a month, negligible against 10 M included |
| **extra CPU** | none | parse and validate about 14,000 keys a run: well under one second of CPU a run, negligible against the account's CPU line, which is already over its allowance (about $0.18 a month at today's rate) |
| **account-wide cost difference** | | **about $0 a month** |
| **what it removes** | | the account-wide KV reach of a secret held by CI, which is the owner's stated concern |
| **what it adds** | | a NEW authenticated write path into production KV. It needs its own guards: the secret compared in constant time, the key-shape check server-side (PFX1 again, not trusted from the client), a per-run batch cap, a rate limit, its own audit log line, and a deploy of the proxy (the deploy claim). It is also a second implementation of PFX1, so it carries the "two guards on one rule drift" risk this repo has met before |

**Recommendation for the rotation decision:** the endpoint, if the owner's priority is the KV token's reach. Its cost
is engineering and a guarded write path, not money. That decision is the owner's, at rotation.

## 7. Results, local (2026-11-13)

| row | read | verdict |
|---|---|---|
| W1 red-first | `routes-guards.selftest.mts` against a permissive stub (every guard ok, freeze always run): red exactly {GR1, GR2, GR3, GR3p, GR4, GR5a, GR5b, GR6, GR6s, GR7a, GR7b, GR7c, FZ1, FZ4}; green {GR1c, GR3c, GR4c1, GR4c2, GR4o, GR5c, GR6o, GR6c, GR7c0, FZ2, FZ3} | **as predicted** |
| W1 | the module: 25 of 25 pass. The first draft's GR1 and GR1c used toy counts under the absolute band; GR1c failed and GR1 passed for the wrong reason. They now use counts at 628,956 and assert WHICH rule refused | **as predicted after that fix**, recorded |
| refactor | the full mode's dry run (staging and production, against 24ce5424 and dff309eb) through the fake KV loaded with the saved pre-refresh manifests: 4 of 4 byte-identical before and after `parseSource`, again after `readMeta`, and again after W2. The fake reproduces the real reads (1591/1591, 634/1591 with 568,226, 0/1575) | **as predicted** |
| K1 | staging, `--key-level --dry-run`, `--prev` 24ce5424, `--prev-also` dff309eb with the 08-25 manifest:<br>- proofs 1591/1591 (rule) and 1575/1575 (plain);<br>- writes 0; deletes 827 (715 surviving, 112 from 5 removed shards);<br>- G1, G2, G6 ok; G3, G4, G5 refuse;<br>- plan: delete NONE, so no seal | **as predicted** |
| K2 | the same with `--max-deletes 1000 --allow-removed 5 --allow-shard <G3's one shard>`: all six guards ok; plan "delete 827" | **as predicted** |
| K3-K7 | an override under a schedule exits 5. An unproven `--prev` is refused (957/1596). A mismatched `--prev-also` is refused (NEITHER scheme whole). No `--dry-run` exits 2. A key-level flag in the full mode exits 2 | **as predicted** |
| FK1 | red-first: before the guard, `ROUTES_KV_CLI` with `--env production --dry-run` proceeded through the fake, exit 0 (red). After it: refused with exit 2 and the fake store byte-identical (no call). Key-level `--env production`: exit 2. `GITHUB_ACTIONS=true` with `--env staging`: exit 2. **Control:** `--env staging` through the fake, outside CI: runs (`diff: 1591/1591 ...`). As a consequence, the refactor proof's two production dry runs through the fake are now refused by design; later identity checks use staging | **as predicted** |
| W2 stage 2, the fixture | `routes-fixture.mts` builds synthetic snapshots: 41 shards of 10,000 plus the 50 anchors (prev 410,050 rows, new 410,065; 70 writes, 25 deletes). Its prev manifest uses the writer's own `RULE_REV`, imported. Through stage 1's dry run (the control that the fixture passes the real guards): prev proof 42/42, writes 70 (added 40, changed 30), deletes 25 from surviving shards, G1-G6 all ok | **as predicted** |
| W2 stage 2, red-first | `routes-writer.selftest.sh` against stage 1 (2026-11-14 03:50Z): **12 of 12 red** {KL1, GR8, RB1, PFX1, GR9a, GR9b, GR9c, GR9e, GR9f, GR9g, GR9d, SD1}, with the store byte-identical wherever a row checks it. **Found by the run:** GR8's "a deleted key is gone" half could not fail, because the base store never held those keys. The store is now seeded with the changed and deleted keys at their old values, and re-run red, GR8 names "a deleted key still present" | **as predicted** (all red) |
| W2 stage 2, green | after the code: **12 of 12 PASS**. `typecheck:scripts` exit 0. The full mode's staging dry runs (24ce5424, dff309eb) are byte-identical to their baselines. Production through the fake is refused by FK1, by design. K1's key-level dry run is byte-identical to the committed HEAD's on the same input; the older `k1.out` predates the `shard(s):` lines | **as predicted** |
| W2 stage 2, sabotages | each plant asserted its anchor exactly once and that the file changed, and was restored from a byte copy with sha256 equal:<br>- **S-GR9**, `--seal-only` accepts any receipt: red exactly {GR9b, GR9c, GR9e, GR9f, GR9g}. GR9a still refuses, because it has no receipt at all;<br>- **S-GR8**, `--no-seal` writes meta anyway: red exactly {GR8}.<br>The first S-GR8 attempt did NOT plant: its anchor occurred twice, so the assertion refused. Its unsabotaged "0 red" was not read as a result, and the harness now stops when a plant fails | **as predicted** |
| PFX1 on real data | both pinned sources counted (BOM-aware; the first count read 0 usable, a broken probe, caught because the control demands the writer's own totals): 24ce5424 628,956 usable, dff309eb 619,103, and **0** callsigns outside `^[A-Z0-9]{2,8}$` in either. So PFX1 cannot refuse a real run | **0 outside** |
| W3 + FK1 + replay, red-first | (2026-11-14 04:18-04:19Z) the writer selftest, extended to 22 rows, in an environment with an invalid token and an empty home, so no call could reach real KV: the 12 earlier rows green, and the 10 new rows red exactly as predicted {FKV, GR10m, GR10a, RCP2, GR10, RCP1, GR11, UN1, UN2, UN3}. UN1-UN3 were red through their dependency on RCP2's sealed store, not through their own missing code. The guards selftest: W1's 25 green, and FK1a-h's 9 red (module missing) | **as predicted** |
| W3 + FK1 + replay, green | `typecheck:scripts` exit 0. The guards selftest: **34 of 34**. The writer selftest: **23 of 23** (the 12, FKW, and the 10 W3 rows), run with no token variable present (each checked by name, absent) | **as predicted** |
| a defect found before the sabotage | GR10m's control compared with its own expression, not the comparison the value check uses, so it could not catch a broken comparison. Both now go through one `same()`. Re-checked green, 23 of 23, before any sabotage | **fixed before the run** |
| W3 sabotages | each plant asserted its anchor exactly once, stopped the harness if it did not apply, and was restored from a byte copy with sha256 equal:<br>- **S-FK1**, the credential conditions dropped: the guards selftest red exactly {FK1d, FK1e, FK1f, FK1h}, the writer selftest red exactly {FKW};<br>- **S-VAL**, `same()` always true: red exactly {GR10m, GR10a, RCP2, GR10, GR11, UN1, UN2, UN3}, with FKV and RCP1 green | **as re-registered** (3fa96148) |
| main merged in | `cb65380c` merged into this branch (merge, not rebase) before W13 touched `routeshealth.ts`, which #703 had changed on main. Clean. After it, the guards selftest still 34/34, the writer selftest 23/23 and `typecheck:scripts` exit 0 | **clean** |
| W13, red-first | `routes-health.test.ts` alone (TIME_WAIT 53-230, no other vitest running): red exactly {RH9}; RH1-RH8, RH9c and RH9s green (the field is absent today, so the control and the omission row pass trivially) | **as predicted** |
| W13, green | `routeshealth.ts` passes `sourceCommittedAt` through only in the writer's one form (`YYYY-MM-DDTHH:MM:SSZ`, the same as SD1's), and omits anything else. **11 of 11**. The Worker's `tsc --noEmit` exit 0 | **as predicted** |
| W13, sabotage | **S-RH9s** (registered in the run's own message before it ran): the form check loosened to "any string". Red exactly {RH9s}. Restored from a byte copy, sha256 equal | **as predicted** |
| TK1, the endpoint | read from Cloudflare's docs before the code, not from memory: `GET https://api.cloudflare.com/client/v4/user/tokens/verify` with `Authorization: Bearer`, which answers `{success, result: {id, status, not_before?, expires_on?}}`. An account-owned token verifies at `/accounts/<id>/tokens/verify`. Which kind the owner creates is the owner's choice, so `routes-token-check.ts` asks the user endpoint and then the account one | **documented** |
| TK1, red-first | the guards selftest gains TK1a-d, plus **TK1e, added here before the code: a token that is not active is a failure.** Against the tree before `routes-token-check.ts`: the 34 earlier rows green and the 5 TK1 rows red (module missing) | **as predicted** |
| TK1, green | **39 of 39**: TK1a 13 days warns, `ROUTES_KV_TOKEN expires in 13 days (2026-12-03)`; TK1b 15 days is ok; TK1c no expiry warns; TK1d a failed call fails; TK1e an inactive token fails. `typecheck:scripts` exit 0. With no `ROUTES_KV_TOKEN`, the script prints `ROUTES_KV_TOKEN is not set` and exits 1. Exit codes: 0 ok, 3 warn (W9 maps it to the issue and ntfy), 1 fail | **as predicted** |
| TK1, sabotage | **S-TK1** (registered in the run's own message before it ran): the 14-day threshold replaced with "already expired". Red exactly {TK1a}. Restored from a byte copy, sha256 equal | **as predicted** |
| W4, red-first | `routes-freeze.selftest.sh`, FZC1-FZC4 (the exit code and the stated line the workflow sees): 4 of 4 red, because `routes-freeze.sh` did not exist (exit 127) | **as predicted** |
| W4, green | `routes-freeze.sh`, a thin wrapper over `routes-freeze.ts`, which applies `G.freeze` (the one home, FZ1-FZ4) at the job's ACTUAL start. FZC1 2099 skips, exit 4, `freeze: SKIP -- frozen until ...`; FZC2 2000 runs, exit 0; FZC3 unset runs, exit 0; FZC4 malformed fails, exit 1. **4 of 4**. `typecheck:scripts` exit 0 | **as predicted** |
| W4, sabotages | each registered in the run's own message before it ran, and restored from a byte copy with sha256 equal:<br>- W8's **freeze ignored**, planted in `G.freeze` itself: the guards selftest red exactly {FZ1}, and the freeze selftest red exactly {FZC1}, because the wrapper inherits the rule;<br>- **S-FZC**, the wrapper mapping "skip" to exit 0: red exactly {FZC1} | **as predicted** |
| W9, red-first | the guards selftest gains SC4set (the closed set, 12 classes: the registered 11 plus `token-expiring`, TK1's warning), SC4 (no 16-hex run, no coordinate, no `rt:` key in any class), SC4c (the control: every class non-empty and naming itself) and SC4x (a class outside the set throws). Before `routes-notify.ts`: red {SC4set, SC4c, SC4x}. SC4 itself read 12/12 "clean" on empty strings, which is exactly why SC4c exists | **as predicted** |
| W9, green | **43 of 43**. A dry run renders `Route refresh, production: verify-fail` with its fixed sentence and `Counts: mismatched 1.` The run link goes only into the GitHub issue, never into ntfy. The topic is never printed: ntfy reports only `HTTP <status>`. The issue path ensures the `route-refresh` label exists, then comments on the open issue or opens one. Both live paths are first exercised on the attended run | **as predicted** |
| W9, sabotage | W8's **free-text field**, a sample key planted in one class's fixed text (registered in the run's own message before it ran): red exactly {SC4}. Restored from a byte copy, sha256 equal | **as predicted** |
| W11, red-first | the guards selftest gains SC2a-e and two controls, SC2ok (all five set) and SC2rb (the identity absent, so RB1). Before `routes-env-check.ts`: all 7 red, the 43 earlier rows green | **as predicted** |
| W11, green | **50 of 50**:<br>- no environment, a protected-branches policy, names [main, dev], a missing KV token and a missing ntfy topic each refuse the start;<br>- all five set starts, printing `main-only: true`, `kv token: set`, `ntfy: set`, `read-back identity: set`;<br>- without the identity it starts with `read-back identity: not set`, and `readback=off` goes to `$GITHUB_OUTPUT`.<br>Secrets are seen only as booleans. **"Main-only" means a custom branch policy whose only entry is the branch `main`.** The registered `routes-env-check.sh` is the `.ts` itself, called from the workflow with `npx --no-install tsx`, so there is no wrapper to drift from it | **as predicted** |
| W11, sabotage | **S-SC2** (registered in the run's own message before it ran): "main-only" loosened to "any policy". Red exactly {SC2b, SC2c}. Restored from a byte copy, sha256 equal | **as predicted** |
| W9, two classes added for W5 | `meta-not-key-level` (RR4: the first run is attended) and `run-failed` (any failure no other class names, such as a PFX1 refusal, which is never expected on real data). SC4set now expects 14. **Red-first, and an instrument failure found in it:** the first red run CRASHED at SC4, because the loop threw on the first unknown class, so SC4, SC4c, SC4x and every SC2 row never ran. A crash is an instrument failure, never a red, so the loop now reads an unrenderable class as an empty notice. **Proved:** with the two classes removed again, the run reads red exactly {SC4set, SC4c} with all 50 rows run. Restored: **50 of 50** | **fixed and proved** |
| W5's run script, the FK1 check | `routes-fk1.ts --check <env>` refuses the run script's test hooks by the SAME rule: staging with no credential exit 0; `CLOUDFLARE_API_TOKEN` set exit 2; production exit 2. **Found while writing it:** a heredoc ate one backslash of a regex (`/\/g` became `/\/g`), a syntax error in a module three tools import. It was rewritten with the Edit tool and checked by bytes (`chr(92)`: one fixed form, no broken form), and the guards selftest re-ran green | **as predicted, after the fix** |
| W5's run script, red-first | `routes-run.selftest.sh`, RR1-RR5, with `routes-refresh-run.sh` moved aside: 5 of 5 red (exit 127). Restored by bytes | **as predicted** |
| W5's run script, green | **5 of 5**, through the fake KV, with no token variable present:<br>- RR1 writes, verifies and seals the new source, so meta's `sourceCommit` is new, `prevSourceCommit` is prev, and `sourceCommittedAt` is the commit date;<br>- RR2 replays the unchanged source and re-seals, so `builtAt` moves;<br>- RR3, a value planted wrong after every write, takes its one repair and ends `verify-fail` with meta byte-identical, and the dry-run notice reads `Route refresh, staging: verify-fail`;<br>- RR4, a meta with no `sourceCommit`, is refused as `meta-not-key-level` with the store byte-identical;<br>- RR5, a future freeze, ends green with the stated line, with KV pointed at a script that does not exist, so no KV was read | **as predicted** |
| W5's run script, sabotage | **S-RR3** (registered in the run's own message before it ran): the second verify's failure swallowed. Red exactly {RR3}. A second layer held: with no receipt written, `--seal-only` refused, so meta stayed unchanged and the class read `seal-refused`, not `verify-fail`. Restored from a byte copy, sha256 equal | **as predicted** |
| W5, the workflow | `.github/workflows/routes-refresh.yml`: `workflow_dispatch` only (`target`: staging or both), with the schedule commented out for S-ON. Two jobs, `staging` then `production` (`needs: staging`, only for `both`). Each has `environment: routes-refresh`, `concurrency: routes-refresh`, `permissions: contents: read, issues: write`; checkout `11d5960a326750d5838078e36cf38b85af677262` and setup-node `49933ea5288caeca8642d1e84afbd3f7d6820020`, both v4, each read from `gh api repos/<action>/commits/v4` and never typed; `npm ci`; then one step mapping only `secrets.ROUTES_*` (and `github.token`, `vars.ROUTES_FREEZE_UNTIL`) into `routes-refresh-run.sh`. `routes-freeze.sh` now calls `npx --no-install tsx`, so nothing inside the token job can download | **built** |
| W10, the static check | `scripts/check_routes_workflow.py`, stdlib only, line-based. **No separate red-first run before its code: its plants ARE its red side, and its first run found two defects of mine.** First, the trigger parser's `on:\s*` crossed the newline and read the block's first line as an inline trigger (a false SC1t on the real file); it is now `[ \t]*`. Second, **S3 as registered ("the scan limited to env: blocks") cannot work**, because env blocks legitimately map `${{ secrets.ROUTES_* }}`, so that scan flags everything. Re-registered before its next run: "the scan sees no run: line", turning exactly {SC3e, SC3x, SC3p} green. Then: the real workflows read clean; each of the 10 plants (SC1t, SC1e, SC1k, SC3e, SC3x, SC3p, SC5, PIN1, NPM1, NPM1x) reads its own row red; S1 turns exactly {SC1t} green, S3 exactly {SC3e, SC3x, SC3p}, S5 exactly {SC5}. `SELFTEST PASSED` | **as re-registered** |
| W7, the CI wiring | `workers.yml`'s `test` job (proxy leg only; no job-level env, so FK1 allows the fake) gains four steps: `routes-guards.selftest.mts` (50 rows), `routes-freeze.selftest.sh` (4), `routes-writer.selftest.sh` (23), `routes-run.selftest.sh` (5). `device-ids.yml` gains `check_routes_workflow.py` and its `--selftest`. Steps only, no job. The proxy test job took 5.3 minutes on CI-C4 against its 20-minute limit; these add about 3-4 minutes, measured on Windows | **built** |
| W6 + W6b, red-first | `reconcile-fleet.selftest.py` gains RC5a (2 open: the line with `<== OPEN` and a warning), RC5b (0: the line, no warning), RC5c (unreadable: "could not count", and a warning, never a silent 0), RC6 (`ci-test-device` left out like `test-device`) and RC6-ctl (without the line, only `test-device` is left out). The planted ids are allowlisted fakes. Against the source before the code: red exactly {RC5a, RC5b, RC5c, RC6}; RC6-ctl and every earlier group green | **as predicted** |
| W6 + W6b, green | RC5-RC6 green. **An existing guard caught a defect in the new code:** U1 (#665) requires `encoding="utf-8", errors="strict"` on every text-mode `subprocess` call in the file, and the new issue count had neither; U1-ctl was off for the same reason, its plant now counting 2. Fixed; the whole selftest PASSED. The live count read once, a count only: 0 open `route-refresh` issues | **as predicted, after the fix** |
| W6b, sabotage | **S-RC6** (registered in the run's own message before it ran): `ci-test-device` dropped from the accepted keys. Red exactly {RC6}. Restored from a byte copy, sha256 equal | **as predicted** |
| **the temp-dir leak (the reviewer, 2026-11-14 ~05:2xZ)** | **found by the reviewer, not by any check of mine:** the writer and the verify created temp dirs (`mkdtempSync` under the system temp: `routes-`, `routes-new-`, `routes-prev-`, `routes-also-`, `routes-kl-`, `routes-seal-`, `routes-meta-`, `routes-rp-new-`, `routes-rp-prev-`, `verify-`) and never removed them. Each holds an extracted snapshot of ~13 MB, and the selftests and sabotages made dozens per run: 371 dirs, 4.72 GB, on a disk S2 needs at 30 GB or more. The W8 driver was in progress and was stopped by pid. **Its kill left the S-GR9 plant in `ingest-routes.ts`, restored from HEAD's blob** (`hash-object` equal to the blob, diff empty). The 371 were deleted by exact prefix AND by content (each held only a `standing-data-*` tree or the tools' own JSON; 0 name-matching dirs failed that). Free: 33.93 GB before, 38.89 GB after. `Temp\claude` and every other prefix were untouched | **fixed below** |
| the fix | every temp dir goes through one `tmpDir(prefix)` (10 call sites, exact replacement, 0 `mkdtempSync(join(tmpdir(), ...` left), and a `process.on("exit")` handler removes them all. **Not try/finally:** every refusal here is `process.exit()`, which skips `finally`; an exit handler runs on it and on the uncaught-error path. Only a `kill -9` skips it. `typecheck:scripts` exit 0 | **built** |
| TMP1, the check that runs | both fake-KV selftests point their children's temp at a private dir (`TMPDIR`, and `TEMP`/`TMP` in Windows form) and end with **TMP1** (0 `routes-`/`verify-` dirs left there) and **TMP1-ctl** (a child's `os.tmpdir()` IS the private dir, so TMP1 cannot pass because the children wrote elsewhere). Green: the writer selftest 25 of 25, the run selftest 7 of 7; the system temp held 0 of these prefixes before and 0 after | **as predicted** |
| TMP1, the plant | the reviewer's "skip the cleanup once", in both tools (registered in the run's own message before it ran): the writer selftest red exactly {TMP1}, with 32 dirs left; the run selftest red exactly {TMP1}, with 19 left, all in the private temp that the selftests' traps remove. Restored from byte copies, sha256 equal. W8 gains S-TMP-W and S-TMP-V | **as predicted** |
