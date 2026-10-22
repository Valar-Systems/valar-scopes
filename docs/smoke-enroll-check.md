# smoke-prod's enroll check: a probe id the gate can see (predictions first)

**Why.** `proxy/scripts/smoke-prod.sh`'s check "enroll POST, unsolved token -> 403 (both secrets bound, gate live)"
posts the fixed id `a1b2c3d4e5f6a7b8`. That id is on `scripts/device-id-allowlist.txt`, as every committed id must
be. Since `e4c768a` (2026-09-23), production refuses fake ids **before** the Turnstile gate
(`proxy/src/enroll.ts`, `refusesFakeId`). So production answers `{"error":"fake_device_id"}`, the check expects
`"unverified"`, and **it has failed on every production run for a month**. It was found as #461's P3 miss. The
reviewer's ruling of 2026-10-22: fix the check first.

**The fix.** The check posts a **random 16-hex id generated at run time**:
- `od -An -N8 -tx1 /dev/urandom`;
- regenerated if it is on the fake allowlist;
- never committed, so the device-id hook and `check_device_ids.py` see no new literal.

Such an id passes the shape check and the fake-id refusal, and reaches the gate under test: `verifySolve`
rejects `not-a-real-solve`, and the reply is 403 `unverified`. **Nothing is written** on that path, because the
ledger (`enr:dev:<id>`) and the day counter are written only after a solve verifies (`enroll.ts:157` returns
before them). Each run costs one Turnstile siteverify call. A random id that happened to equal a real device's
would get the same 403, because the ledger is read only after the solve.

## Predictions (frozen before the change)

| | check | predicted |
|---|---|---|
| **E1** | the fixed check against production (`2460deb`, untouched) | **HTTP 403 `{"error":"unverified"}`, PASS.** The whole run: **PASS 32, FAIL 1, SKIPPED 1**, and the one FAIL is `/wx` (404; that build has no route) |
| **E2** | the fixed check against staging | **403 `unverified`, PASS** |
| **S-E1** | sabotage: the old fixed fake id put back | production answers **403 `fake_device_id`**, and the check **FAILs**. The check still tells the fake-id refusal from the gate |
| **S-E2** | sabotage: a random id with **no token** | **400 `bad_request`**, and the check **FAILs**. Its marker isn't matched by any refusal, only by the gate's |
| **E3** | the committed script | `check_device_ids.py` passes, and no 16-hex literal is added |
| **escape** | anything else | stop and report |

## Results (2026-10-22, on `2ad063b`, with E2:08's own credential)

| | read | verdict |
|---|---|---|
| **E1, the check** | production `2460deb`: **HTTP 403 `{"error":"unverified"}`, PASS** | **as predicted** |
| **E1, the whole run** | **PASS 33, FAIL 1, SKIPPED 0**. The one FAIL is `/wx` (404) | **MISS on the count clause:** predicted PASS 32, FAIL 1, SKIPPED 1 |
| **E2** | staging: **403 `unverified`, PASS** | **as predicted.** The run's other FAILs, 10 in all, are staging refusing the credential (seen before this change) and `/wx` (staging at `6bb61df` has no route) |
| **S-E1** | `SMOKE_ENROLL_ID` set to the old fixed id: **403 `fake_device_id`, FAIL** | **as predicted** |
| **S-E2** | `SMOKE_ENROLL_TOKEN` empty: **400 `bad_request`, FAIL** | **as predicted** |
| **E3** | `check_device_ids.py`: OK, 965 files; **0** 16-hex literals added. The fixed id stays only in the two enroll-PAGE GETs, which production renders for any id and which passed before | **as predicted** |

**E1's count miss, and why it is recorded rather than re-graded.** Every check's verdict, before the fix (the
P3 run on `2460deb`) against E1, differs in exactly three rows:
1. the live hex the run picked (`abbc4e`, then `a67a16`);
2. `/v1/photo`, SKIPPED then PASS: the earlier aircraft had no stock photo, and today's (a C172) has one;
3. the enroll check, FAIL then PASS.

The second row is a skip that depends on the live sky, and E1's count clause froze it in by quoting the earlier
run's tally. **The alternative is that the fix changed `/v1/photo`, and the diff excludes it:** the photo row
moved with the live hex, the fix touches only the enroll POST's id, and every other row is identical. So the
fix's own effect is exactly the one change predicted.
