# The route copy's one-off refresh: registration, predictions first (2026-11-13)

**Status: REGISTERED, nothing written.** Every figure below was frozen before any KV write. The results are appended under
section 7 as they arrive, and nothing above that section changes after this commit.

## 0. The commission

The owner, 2026-11-13 15:35Z, relayed by the reviewer at 16:33Z:

> "Route copy: refresh (staging first, $3.72). Then plan the weekly refresh that nothing runs today. It must remove
> routes the source has dropped (the 644 since August), not keep them, since a wrong route is worse than none. The
> health page's route-age line alerts when the copy is more than 10 days old."

This document covers only the one-off refresh, piece (A):
- **Scope:** add and update only, as priced. Nothing is deleted. Dropped routes go in the weekly refresh's first run
  (piece C).
- **The tool:** the existing `proxy/scripts/ingest-routes.ts` and `proxy/scripts/verify-routes.ts`, unchanged, run from
  main `b7184243`. Every wrangler call goes through `scripts/wrangler-run.mjs`.

## 1. The inputs, pinned and read before this registration

**The source** is `vradarserver/standing-data` commit `24ce5424` (2026-11-12T04:49:37Z), still the head at 16:35Z.
- The tarball is `standing-data-24ce54242aca9291432ec67b7bab4d39f038913c/`, 6,323,438 bytes, sha256 `28ab57d0...cbf5bd`.
- Both runs and both verifies read this file through `--file`. The default, the moving `main` tarball, is never used.
  So the dry runs, the writes and the verifies all see the same bytes.

**Dry runs** (16:37Z; each read `meta:routes` once through the helper and wrote nothing):

| | staging | production |
|---|---|---|
| source | 1,591 shards, 628,956 usable, 0 skipped, 8,994 rotations | same |
| previous manifest | 1,575 shards | 1,575 shards |
| diff | **1,591/1,591 shards changed, 628,956 keys** | **634/1,591 shards changed, 568,226 keys** |
| ingest's shrink note | 644 not deleted | 644 not deleted |

**Staging is a full rewrite.** Its manifest has no `ruleRev`, and its hashes predate the rule being folded into each
hash, so every shard reads as changed. The $3.72 assumed staging's diff would equal production's. That assumption
was uk-data's, and it was wrong.

**The two key sets**, compared offline: the 08-25 source `dff309eb`, which production holds, against `24ce5424`.
`routerule.ts` and the verifier's parse were used; counts only.

| | count |
|---|---|
| distinct keys, old / new | 619,103 / 628,956 (each equal to its usable rows: no callsign repeats) |
| shards holding keys, old / new | 1,575 / 1,591 (5 old shards are gone, 21 are new) |
| **dropped** (in old, not in new) | **827** |
| added (in new, not in old) | 10,680, in 573 shards |
| value changed / unchanged | 26,106 / 592,170 |
| manufactured self-loops, new | 0 (38 genuine circular routes) |
| the five canaries in the new source | 5/5 |
| keys starting `ZZ` in the new source | 0 |

**827 dropped, not 644.** The ingest's note is a net per-shard shrink over the shards that still exist. A shard that
gains some callsigns and loses others hides the losses, and the five removed shards are never visited at all. The
weekly refresh's first delete count starts from 827 (piece C).

**Leftover sentinels:** `rt:ZZ` lists 0 keys in both environments. The listing's anchor control, `rt:BAW11`, lists 26
in each, so the zero is a reading and not a blind probe.

## 2. Cost: corrected from $3.72 to about $4.50 (for the reviewer's ruling)

KV writes are billed per calendar month: 1,000,000 included, then $5 per million, with no rounding.

**November so far** (the analytics API, `kvOperationsAdaptiveGroups`, 11-01 00:00Z to 11-13 16:00Z): **281,918 writes**.
- The daily baseline is 1,233-1,734 writes.
- Each Monday's `refresh-data.yml` adds about 131,400: 11-02 had 132,762 and 11-09 had 132,979, split between
  production and staging.
- Mondays 11-16, 11-23 and 11-30 are still to come in November. The 7-day rate behind the $3.72 (0.608 M a month)
  averaged one Monday per week, which undercounts a calendar month with five.

| term | writes |
|---|---|
| November to 11-13 16:00Z (measured) | 281,918 |
| rest of November, baseline (17.33 days x 1,233-1,734) | 21,400-30,100 |
| Mondays 11-16, 11-23, 11-30 (3 x 125,000-140,000) | 375,000-420,000 |
| **(A) staging:** 628,956 + the sentinel + the meta | **628,958** (+10,000 per retried chunk) |
| **(A) production:** 568,226 + the sentinel + the meta | **568,228** (+10,000 per retried chunk) |
| **November total** | **1,875,504-1,969,204** (central 1,899,604; the high end allows four retried chunks) |
| **overage at $5/M** | **$4.38-$4.85 (central $4.50)** |

**The difference from $3.72 has two causes:**
- staging's full rewrite: +60,730 writes, +$0.30;
- November's real baseline: 0.70 M, not 0.608 M (+$0.47).

**Spent so far: $0.00 overage in November, read, not estimated.**
- **The read:** `tools/budget/budget_read.py` over 11-01 00:00Z to 11-13 16:00Z, with `--observation`, divided by its
  30-day factor k = 720/304.
- **Its anchor:** the read's KV writes, reads and lists (281,918, 2,853,717 and 2,222) equal the direct KV read above
  exactly.

| line | November so far (read) | included a month | overage so far | the month at this rate | its overage |
|---|---|---|---|---|---|
| Workers requests | 905,707 | 10,000,000 | $0.00 | 2,145,096 | $0.00 |
| Workers CPU (ms) | 16,448,699 | 30,000,000 | $0.00 | 38,957,446 | $0.18 |
| KV reads | 2,853,717 | 10,000,000 | $0.00 | 6,758,803 | $0.00 |
| KV writes | 281,918 | 1,000,000 | $0.00 | the calendar model above | $4.38-$4.85 with (A) |
| KV lists | 2,222 | 1,000,000 | $0.00 | 5,263 | $0.00 |
| KV deletes | 0 (none reported this month) | 1,000,000 | $0.00 | 2 with (A) | $0.00 |
| R2 Class A | 379,569 | 1,000,000 | $0.00 | 898,979 (90%: a flag, not over) | $0.00 |
| R2 Class B | 1,980,504 | 10,000,000 | $0.00 | 4,690,667 | $0.00 |

The lines this read does not carry are Durable Object requests and GB-s, R2 storage, and Logs events (derived). The
budget's monthly rows put each under its allowance, with no flag.

**November with (A): about $4.68 of overage** ($4.50 for KV writes and $0.18 for CPU), which is 47% of the owner's
$10 a month.
- **shsr's own costs are unread until X10.** shsr (live since 06:15Z) adds CPU time and R2 Class A from today, and
  Class A could cross its 1,000,000. Each 100,000 over is $0.45.
- **Flag:** if the ten OPERA ingests come back in November, the budget's OPERA-running overage ($5.26 a month, about
  $3 pro rata for the rest of the month) would take November to about 80% of $10.

**One assumption is stated, not verified: the billing period is the calendar month.** Cloudflare bills usage per
billing period, and this account's period start has not been read (no billing-read token is held). The bound holds
whatever the alignment:
- (A)'s 1,197,186 writes cost between **$0.99** (all of the period's included million still unused) and **$5.99**
  (none of it left).
- November's overage therefore lies between $1.17 and $6.17 while OPERA stays silent: inside $10 either way.

**The three ways to run it** (the reviewer rules, or asks the owner, since the owner's approval named $3.72):

| option | cost | what it gives up |
|---|---|---|
| (i) as approved: staging first, then production, both shard-level | about $4.50 | nothing; recommended |
| (ii) production only now; staging when the weekly refresh's key-level writer exists | about $1.35 | "staging first"; staging stays 80 days stale until then |
| (iii) both with the weekly refresh's key-level writer (36,786 writes each) | $0 in November | the refresh waits for piece C's code and its review |

## 3. The order, and what gates each step

1. **Wait for weatherscope** to report the combined proxy deploy closed and verified (its slot: 11-13 17:00-23:30Z).
   Its predictions read today's route-age line, so nothing here moves that line under them.
2. **Take the deploy claim** for the whole run:
   `session-claim.py take deploy --covers enrich-kv:staging,enrich-kv:production`, extended as needed and released
   after P5.
3. **Respect the windows.** No write may overlap:
   - 11-14 00:00-01:30Z;
   - 11-15 00:00-03:00Z, 18:00-19:30Z and 20:00-20:50Z;
   - 11-16 04:00-07:30Z.
   A step starts only if it can finish before the next window: allow 60 minutes for an ingest and 20 for a verify
   (neither duration was ever recorded, so neither is a prediction). Staging may run tonight and production after
   01:30Z on 11-14.
4. **Staging, rows S0-S5, then production, rows P0-P5.** Production starts only once every staging row reads as
   predicted.
5. **The repair loop:** if a verify fails, re-run the ingest with `--force-shard` for exactly the shards it names, then
   verify again. At most two repairs per environment; a third failure stops the run.

**"Seal meta last" cannot be done with the tool as it stands (for the ruling).** `ingest-routes.ts` writes
`meta:routes` at the end of its own run, after its own checks: the sentinel's provenance, a 12-key byte-identical
sample and the canaries. The full per-shard verify runs after that. There is no flag to defer the seal.
- **Option (a), recommended:** accept the in-run seal. If the verify then finds a gap, `/healthz` calls a gapped build
  fresh until the force-shard repair re-seals it, a window of minutes.
- **Option (b):** a small reviewed PR adding `--no-seal` and `--seal-only` first. That delays (A).

Whichever is chosen, `builtAt` is the time of the LAST successful ingest's seal, including a repair's.

**Ruled by the reviewer, 2026-11-13 16:50:35Z.**
- **Cost: option (i).** The owner, 16:49Z, verbatim: "Route refresh at ~$4.50: fine, it's under the $10 allowance.
  Proceed under your ruling." The write-count stop before production stands as registered.
- **Seal: option (a), for (A) only, on five conditions:**
  1. The run is done at verify PASS, or at a force-shard repair followed by a second verify PASS. Between the seal and
     that PASS it is reported as **"sealed, unverified"**, never as done.
  2. If anything interrupts between the seal and the PASS, the first act on resuming is the verify. Nothing else touches
     enrich-kv until it passes.
  3. The healthz-after rows (S4, P4) wait for verify PASS **and** for at least 11 minutes after the seal.
  4. Production starts only after staging's verify PASS and both of S4's reads.
  5. **This is a recorded deviation from CLAUDE.md's KV rule:** "write -> verify -> force-shard repair -> verify, and only
     then seal the meta key". `ingest-routes.ts` seals inside its run, before the full verify. That is accepted for
     (A) only, because a person runs the loop to the end.
- **The weekly job (C) must seal last by construction:** a `--no-seal`/`--seal-only` or equivalent, red-first.

## 4. Predictions, per environment

Each run is stamped line by line in UTC, with stderr kept in its own file. Verifier output is kept whole in a local
file. What gets reported is counts only, because its failure lines carry sample callsigns.

### Staging (S) and production (P)

| row | what | staging | production |
|---|---|---|---|
| S0 / P0 | `/healthz` `routes`, before | `{"builtAt":"2026-08-25T23:31:26.817Z","ageDays":79,"rows":619103,"stale":true,"maxAgeDays":14}`, with ageDays 80 from 23:31:26Z | `{"builtAt":"2026-08-26T17:25:54.476Z","ageDays":79,"rows":619103,"stale":true,"maxAgeDays":14}`, with ageDays 80 from 11-14 17:25:54.476Z (the field exists only after the combined deploy). Computed by script: 78 before 11-13 17:25:54.476Z, which the frozen D-P1 row misstated as 79 (reported 16:44:57Z) |
| S1 / P1 | **pre-verify (the control):** `verify-routes --file` the pinned source, before the write | see below | see below |
| S2 / P2 | the ingest, `--file` the pinned source | see below | see below |
| S3 / P3 | verify, after the write | see below | see below |
| S4 / P4 | `/healthz` `routes`, two reads at least 60 s apart, the first at least 11 minutes after "meta written" (the per-isolate cache is 10 minutes) | `builtAt` between the stamps of the run's `sample:` and `meta written` lines; ageDays 0, rows 628956, stale false, maxAgeDays 14, no `why`; both reads identical | same |
| S5 / P5 | KV writes in the run's window, by namespace, from the analytics API, read at least 15 minutes after the run | 628,958 to 628,958 + 10,000 x retries + 20 | 568,228 to 568,228 + 10,000 x retries + 100 per hour of the window |

**S1 / P1, the pre-verify (the control: the verifier must be seen to fail before its PASS means anything).**
- `expected: 1591 shards, 628956 distinct keys`.
- `RESULT: FAIL`, exit 1.
- Missing keys:
  - staging: 10,500-10,680;
  - production: 10,000-10,680.
  The source added 10,680 keys. Some of those callsigns may already sit as bare `rt:<cs>` negative rows (24 h TTL),
  which count as present.
- Complete shards: 1,018-1,040 of 1,591 (573 shards hold an added key).
- `values: 0 manufactured self-loops (38 genuine circular routes, which are real)`.
- `extra` is at least 827: the dropped keys plus runtime rows.

**S2 / P2, the ingest.** The lines up to `diff` repeat section 1's dry run, then:
- staging: `diff: 1591/1591 shards changed -> 628956 keys to write`;
- production: `diff: 634/1591 shards changed -> 568226 keys to write`;
- `NOTE: 644 callsigns disappeared ... NOT deleted`;
- no `swept` line;
- `anchor: ZZ.....A is absent`;
- the bulk puts: staging 63 chunks (628,957 = 62 x 10,000 + 8,957); production 57 chunks (568,227 = 56 x 10,000 + 8,227);
- 0 retries;
- `wrote 628956` / `wrote 568226` route keys (+1 sentinel);
- sentinel `PROVENANCE PROVED` on attempt 1, with 1-8 still in band;
- `sample: 12/12`;
- canaries 5/5;
- `meta written: rows=628956 written=628956 sentinel=OK canaries=5/5` (staging), or `written=568226` (production);
- `cleanup: rt:ZZ.....S deleted`;
- exit 0.

**S3 / P3, the verify after the write.**
- `expected: 1591 shards, 628956 distinct keys`.
- `shards complete: 1591/1591` and `keys present: 628956/628956`.
- `RESULT: PASS`.
- 0 manufactured self-loops (38 genuine).
- `extra` at least 827, since nothing is deleted.
- Exit 0.

**Deletes:** one per environment (the sentinel's cleanup). Lists: one verify listing of about 630 pages per verify,
far inside the lists allowance.

## 5. Outcomes

| outcome | meaning, and what happens |
|---|---|
| every row as predicted | done. Piece B may merge after P3/P4 |
| S1/P1 reads PASS, or fewer missing keys than its band | the verifier cannot see the new keys' absence: **stop before any write** |
| S1/P1's `extra` is under 827 | production did not hold all of the old source: stop and decide |
| a `REFUSING` line from the ingest | it stopped itself; nothing past its gate was written. Stop and report |
| S3/P3 FAIL | the repair loop (section 3, step 5), then verify again |
| S4/P4 differs from the meta just written, after 11 minutes | the health surface is not reading the build: stop and report |
| S5/P5 above its upper bound | **stop**. At staging, production does not start |
| anything else, including a line not predicted here | the instrument or the inputs moved: stop and decide |

## 6. What this does not do

- It deletes nothing. The 827 dropped callsigns keep serving their old routes until piece C's first run.
- It changes no code and deploys nothing. Piece B, the 10-day alert, merges only after P3 and P4.

## 7. Results

(Appended as each row is read.)
