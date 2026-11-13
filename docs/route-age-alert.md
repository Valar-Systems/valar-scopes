# The route copy's 10-day alert: registration, predictions first (2026-11-13)

**Status: REGISTERED. No code has changed and no test has run against this change.** It is piece (B) of the owner's
route-copy instruction, built on #695's design (`docs/route-mirror-age.md`). It merges only after the one-off refresh's
production verify PASS and P4's healthz reads (`docs/route-refresh-2026-11.md`, rows P3 and P4). It ships in the next
proxy deploy, in a slot the reviewer gives.

## 0. The ruling

The owner, 2026-11-13 15:35Z: "The health page's route-age line alerts when the copy is more than 10 days old."

#695 registered the second half: "WARN until an approved refresh lands, then FAIL" (`docs/route-mirror-age.md`, "Not in
this PR").

## 1. The change

| file | change |
|---|---|
| `proxy/src/routeshealth.ts:15` | `ROUTES_MAX_AGE_DAYS` 14 -> **10**. This is the rule's one home; smoke and reconcile read it from the body's `maxAgeDays`. |
| `proxy/test/routes-health.test.ts` | RH1 and RH3-RH7 expect `maxAgeDays: 10`. RH3's boundary moves to 10 days (now - 10 d +/- 60 s). |
| `proxy/scripts/smoke-prod.sh:340` | `ROUTES_AGE_MODE="warn"` -> **`"fail"`**, with its comment updated |
| `proxy/scripts/routes-age-verdict.selftest.sh` | **SV7 (new):** `smoke-prod.sh` sets the mode exactly once, to `"fail"`: one line `ROUTES_AGE_MODE="fail"` and none `="warn"`. **SV8 (new, a control):** its counting branch (`PASS*) pass=$((pass+1))`) is present exactly once. |
| `proxy/scripts/ingest-routes.ts:47` | the comment's "stale past 14 days" -> 10 |
| `docs/route-mirror-age.md` | a pointer: N is 10 from this change (the owner, 11-13) |

Unchanged:
- `reconcile-fleet.py` and `routes-age-verdict.sh` read the limit from the body.
- SV1-SV6 and RC1-RC4 are fixtures of those readers. They carry their own `maxAgeDays` and stay as they are.

## 2. What a FAIL stops, written down so it cannot block anything quietly

| surface | what a route-age FAIL does | why |
|---|---|---|
| `proxy/scripts/deploy.sh` | **nothing** | it never runs smoke. `confirm-deploy.sh` reads only `/healthz`'s top-level `commit` (`healthz-commit.sh`) and prints a hint to run smoke by hand. No deploy is refused |
| `smoke-prod.sh` | one more FAIL, and the exit code goes non-zero | any verdict but PASS counts: stale (more than 10 days), `why` absent / unparsable / future / unreadable, a Worker without the `routes` field, or a body it cannot read |
| **a deploy's registration that grades smoke's counts** (for example basemap-deploy's "FAIL 0") | **one more FAIL whenever the copy is stale**, whatever that deploy changed | **Proposed rule, for the ruling:** every deploy registration grades the route-age line as its OWN row, predicted from the copy's age at the read. A FAIL on that row is reported and never triggers a rollback of an unrelated deploy: a rollback cannot change the data's age, and the remedy is a refresh. Only a deploy that changes `routeshealth.ts` or the route path treats it as its own off reading |
| `scripts/reconcile-fleet.py` | no change from today | it already exits 1 on any warning (line 739), and since #695 a stale copy is one. Nothing gates on its exit: CI runs only its fixture selftest |
| CI | nothing | no job runs smoke against a live Worker. The selftests grep smoke's text (`wx-products-lines`, `healthz-commit`) or run it against a stub and grade only the identity it sends (TI2) |
| the device | nothing | firmware never reads `/healthz` `routes` |

**When the alert fires.** The copy is stale once `now - builtAt` exceeds 10 days in milliseconds. `ageDays` then reads
10, during the 11th day after the seal.
- From (A)'s production seal at time T, it fires at T + 10 d, unless the weekly refresh (C) has re-sealed before then.
- If (C) slips, the alert firing is correct, and the limit is not loosened.

## 3. Predictions

### 3.1 Red-first, local (one test file at a time; the TIME_WAIT rule from vitest-pool-workers applies)

| row | state | predicted |
|---|---|---|
| **B-R1** | the tests changed to 10, `routeshealth.ts` still 14 | `routes-health.test.ts`: **red exactly {RH1, RH3, RH4, RH5, RH6, RH7}**, green {RH2, RH8}. RH3 goes red on its second half (10 d - 60 s reads `stale: false` under 14); its first half passes under both |
| **B-R2** | the constant at 10 | 8 of 8 green |
| **B-R3** | SV7 and SV8 added, smoke still `"warn"` | `routes-age-verdict.selftest.sh`: red exactly {SV7}; SV1-SV6 and SV8 green |
| **B-R4** | smoke at `"fail"` | SV1-SV8 green |
| **B-S1, a sabotage** | `stale` computed from floored days (`ageDays > 10`) instead of milliseconds | red exactly {RH3}: at 10 d + 60 s, `ageDays` is 10, so it reads `stale: false`. Every other row is green. A sabotage of the constant itself (11) would only repeat B-R1's red set, so it is not used |
| **B-S2, a sabotage** | the smoke counting line removed | red exactly {SV8} |

### 3.2 CI

The count is predicted in a commit of its own before the code is pushed. It is read from main's push CI at this
branch's base and from the workflows' `paths` filters, the method #695 used. The prediction gets an outside-the-set
row.

### 3.3 After the deploy (the reviewer's slot; read twice, at least 60 s apart)

| row | where | predicted |
|---|---|---|
| **D-B1** | production `/healthz` `routes` | `maxAgeDays` 10; `builtAt` (A)'s production seal; `ageDays` = floor(t - builtAt); `stale` false while t - builtAt is 10 days or less; no `why` |
| **D-B2** | `smoke-prod.sh` against production | `RESULT: PASS -- built <builtAt>, <ageDays> d old (limit 10 d)   (mode fail)`, counted in PASS. Every other verdict is unchanged from the same script's run before the deploy |
| **D-B3** | staging `/healthz` `routes`, when staging is deployed | as D-B1, with staging's seal |

## 4. Outcomes

| outcome | meaning |
|---|---|
| every row as predicted | merge after (A)'s P3 and P4 and the reviewer's approval; deploy in the given slot |
| a red set other than predicted (B-R1, B-R3, B-S1, B-S2) | stop and report before any push of code |
| D-B1 shows `maxAgeDays` 14 | the deploy did not carry this change: stop and report |
| D-B2 FAIL | the copy is past 10 days, or the field is unreadable. Under section 2's proposed rule this is reported and is not by itself a reason to roll back. Stop and report |
| anything else | stop and decide |
