# `/healthz`'s commit: read the top-level key only (predictions first, 2026-11-11)

**For:** the reviewer (the orchestrator). **Status:** predictions, frozen before any code. Nothing is written yet.
**Deadline:** this lands on main before map-server's #639 staging deploy of the builder, which waits for it (ruled 19:12:42Z).

## 1. The defect

`/healthz` returns `{"ok":true,"commit":"<proxy sha>",...,"basemap":{"rev":..,"store":..,"commit":<builder sha or null>,..}}`
(`proxy/src/index.ts:92`, `proxy/src/basemap/handler.ts:56-58`). Two scripts read the stamp with a pattern that matches
**every** key named `commit`, at any depth:

| reader | line (main `78c2bec2`) | what it does with the stamp |
|---|---|---|
| `proxy/scripts/confirm-deploy.sh` | `:74` `LIVE="$(grep -oE '"commit":"[^"]*"' "$HZ" \| cut -d'"' -f4)"` | the gate that proves a deploy landed |
| `proxy/scripts/smoke-prod.sh` | `:259` the same pattern on `LAST_BODY` (the `/healthz` body: the auth check between them uses its own `curl`) | the build-stamp check: FAIL on `dev`, absent, or `-dirty` |

Today `basemap.commit` is `null` in production, so the pattern matches once. Once a builder is bound and has answered once
in an isolate, that isolate's `/healthz` carries **two** `commit` strings:
- **confirm-deploy:** `LIVE` becomes two lines, so `classify` reads `WRONG_SHA` on a good deploy. It is per isolate, so it
  can also confirm by luck from an isolate with no build yet. Either way the gate stops meaning anything.
- **smoke's stamp check fails in the reassuring direction.** The old `COMMIT` is two lines, and the `case` tests the whole
  string:
  - a top-level `dev` beside a real builder sha passes `!= "dev"`;
  - a top-level `x-dirty` followed by the builder's sha doesn't end in `-dirty`, so it passes;
  - a missing top-level stamp reads the builder's sha and passes.
- **Why a nested read is not harmless here:** `deploy.sh` stamps the builder and the proxy with the same sha (`:176`,
  `:263`). So `basemap.commit` can equal the sha being confirmed, and a reader that picks the nested key could confirm a
  proxy deploy that never landed.

## 2. Every reader of `commit` from `/healthz` in the repo

Found by `git grep` at `78c2bec2` over every non-doc file: first every file that names `healthz`, then every line in the
repo reading a JSON `commit` field (`.commit`, `["commit"]`, `"commit":`, `jq ... commit`).

| reader | how | verdict |
|---|---|---|
| `confirm-deploy.sh:74` | `grep -oE`, every match | **the defect** |
| `smoke-prod.sh:259` | the same | **the defect** |
| `tools/wx/r12_prod.sh:17` | `grep -oE '"commit":"[0-9a-f]+' \| head -1`: the first match, so the top-level one by key order. Print-only, ungraded | correct today by position, and **a second path around the fix**. It moves to the same helper, with uk-data told (its tool) |
| `proxy/test/buildstamp.test.ts:16-19` | `res.json()`, then `body.commit` | top-level by construction. Unchanged |
| `proxy/test/basemap-health.test.ts` | `toEqual` on the `basemap` object only | not a stamp reader. Unchanged |
| `proxy/scripts/sabotage-basemap.mjs` | edits the handler's source; reads no `/healthz` | unchanged |
| `tools/wx/pd_wx.sh:33`, `wx-opuk-verdict.sh`, `relay/setup-relay.sh` | read `wx`, or the relay's own `/healthz` | not a commit reader |

Not readers: `deploy.sh:176,263` write the stamp; `github-api.ts:120` reads a GitHub commit; `flash_manifest.py:162` writes
its own field.

**Also found: the check that would catch this doesn't run.** `proxy/scripts/test-confirm-deploy.sh` grades only `classify`
and `report`, never the extraction at `:74`, and no workflow runs it. It's referenced only from a comment in
`deploy.sh:276`.

## 3. The change

- **New `proxy/scripts/healthz-commit.sh`,** sourced by all three readers, with two functions:
  - **`healthz_commit`:** reads a body on stdin and prints its **top-level** `commit` if it is a string. It prints
    nothing if the key is absent, not a string, or the body isn't JSON. It parses with `node`'s `JSON.parse`. `node`
    is already required wherever these scripts run (`deploy.sh` runs `npx wrangler`, and CI runs smoke through `npx
    tsx`). A missing `node` prints nothing, which reads as `WRONG_SHA` and as a smoke FAIL: loud, never a pass.
  - **`stamp_verdict <commit>`:** smoke's existing decision, moved here unchanged so it can be graded: `PASS`,
    `FAIL-dirty`, or `FAIL-absent` (empty or `dev`). smoke prints exactly the lines it prints today.
- **`confirm-deploy.sh:74`** becomes `LIVE="$(healthz_commit < "$HZ")"`. The loop, `classify`, `report` and the exit codes
  don't change.
- **`smoke-prod.sh:259`** becomes `COMMIT="$(printf '%s' "${LAST_BODY:-}" | healthz_commit)"`, and its `if`/`case` calls
  `stamp_verdict`. The heading, the `commit reported:` line and the pass/fail counting don't change.
- **`tools/wx/r12_prod.sh:17`** prints `healthz_commit`'s output, with the same label.
- **CI:** `workers.yml` gains two **steps**, not jobs, beside the existing selftests:
  - the new `proxy/scripts/healthz-commit.selftest.sh`;
  - the existing `test-confirm-deploy.sh`, which no workflow ran until now.

## 4. The rows (`healthz-commit.selftest.sh`, synthetic bodies, whole-output comparisons)

The bodies are synthetic and use fake shas: `1111111` for the proxy, `2222…` (40) for the builder.

| row | body | predicted |
|---|---|---|
| **HC1** | top `1111111`, `basemap.commit` `2222…` | `healthz_commit` prints exactly `1111111`, one line |
| **HC2** (the control) | **production's shape today:** top `1111111`, `basemap` with `commit: null` | `1111111`, byte-equal to what the old pattern prints on the same body |
| **HC3** | top `1111111`, `"basemap":"not_configured"` | `1111111`, equal to the old pattern's output |
| **HC4** | top `commit: null`; no `commit` key; the body `not json`; an empty body | each prints nothing |
| **HC5** | no top-level `commit`, `basemap.commit` `2222…` | prints nothing. The old pattern prints `2222…` |
| **CD1** | HC1's body through `classify 1 "$(healthz_commit)" 1111111` | `CONFIRMED`, and `report` exits 0 |
| **CD2** (the control) | HC2's body, the same | `CONFIRMED`, as before |
| **SM1** | top `dev`, `basemap.commit` `2222…` | `stamp_verdict` = `FAIL-absent` |
| **SM2** | HC1's body | `PASS`, and the commit it reads is exactly `1111111` |
| **SM3** | top `1111111-dirty`, `basemap.commit` `2222…` | `FAIL-dirty` |
| **SM4** | HC5's body | `FAIL-absent` |
| **SM5** | no `commit` anywhere | `FAIL-absent` |
| **R12** | HC1's body through `r12_prod.sh`'s print line (run with `curl` replaced by the body) | `healthz commit [1111111]` |

`test-confirm-deploy.sh` is unchanged and must pass as it does by hand today: 5 classifications, 4 exit codes, 0 failed.

## 5. The plants (in the selftest, on a temporary copy of the helper; each edit asserted applied, the whole red list compared)

Every row, R12 included, sources the helper from `HZC_HELPER`, which defaults to the real file. A plant points it at the
planted copy, so all three readers see the plant at once, as they would see a real change to the helper.

| plant | the edit | red set |
|---|---|---|
| **P-OLD** | `healthz_commit` replaced by the old pattern, `grep -oE '"commit":"[^"]*"' \| cut -d'"' -f4` | **{HC1, HC5, CD1, SM1, SM2, SM3, SM4, R12}**. CD1 reads `WRONG_SHA`; SM1, SM3 and SM4 read `PASS`, the reassuring direction; R12 prints two lines. HC2, HC3, HC4, CD2 and SM5 stay green: on today's production shape the old pattern and the fix agree, which is the control |
| **P-FIRST** | the tempting half-fix: the old pattern with `\| head -1`, `r12_prod.sh`'s positional read | **{HC5, SM4}**: with the top-level stamp missing it reads the builder's sha, which `deploy.sh` can make equal to the deploy's own |

A plant whose red set isn't its registered set stops the step, and nothing is read into it.

## 6. CI, predicted before each push

- **This registration, docs only:** 32 check runs = 29 success, 3 skipped, 0 failed. The baseline is #638's finished docs-only
  run at `bdcc6bac2d2b`. Main's last workflow change, `e2c34daf` (#633), added steps and no job.
- **The code commit** (it touches `proxy/`, `tools/wx/`, and one workflow by steps): 35 = 32 / 3 / 0, on uk-data's
  baseline for a `proxy/` change (#627's finished head run, 35 = 32/3/0), with no job-set change since. Before that push, I
  re-check whether `tools/wx/` reaches any workflow's path filter; if it adds a job, the prediction is re-registered first.
- **Locally before the push:** both selftests, the workers vitest suite under the suite rule (one whole-suite run at a time,
  TIME_WAIT under 2,000), and `device-ids`' checks.

## 7a. The code (approved at `61500cadbdf8`, 19:18:26Z): what was added beyond the registration, and the results

**Added beyond the registration:**
- **Wiring rows W1-W3:** each reader calls the helper as wired, and none keeps a `"commit":"` grep of its own.
  - W1: confirm-deploy's `LIVE` line;
  - W2: smoke's `COMMIT` line and its `stamp_verdict` call;
  - W3: `r12_prod.sh`'s print line.
  - A plant edits only the helper, so W1-W3 stay green under both plants and the registered red sets are unchanged.
  - Each was proved able to fail. Reverting each reader's line by itself turned exactly its own row red, and the file was
    restored by bytes with its sha256 checked.
- **`r12_prod.sh` also gains one source line** for the helper, beside its `test-identity.sh` one. uk-data agreed to the
  change to its tool.
- **The overlap with #643** (uk-data's per-product lines in `smoke-prod.sh`): no fixed order; whichever is approved first
  merges first. This hunk is confined to the build-stamp block and its own source line. If #643 lands second, its H6 (e)
  re-checks its three counts against this `smoke-prod.sh`; if this lands second, I re-check them.

**Results, before the push:**
- **`healthz-commit.selftest.sh`:** all 16 rows pass on the helper as written. P-OLD's red set is {HC1, HC5, CD1, SM1, SM2,
  SM3, SM4, R12} and P-FIRST's is {HC5, SM4}, both as registered.
- **`test-confirm-deploy.sh`:** 9 passed, 0 failed, as registered.

## 7. After the merge

- **No deploy is needed.** These are operator scripts, not Worker code. The next proxy deploy's own `confirm-deploy` is the
  first real use. Its result is read, and `smoke-prod.sh`'s stamp section must print exactly one `commit reported:` value.
- **Out of the set, at any step:** stop and report. Nothing is improvised.
