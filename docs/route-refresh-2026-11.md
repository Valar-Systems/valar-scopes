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

**Amendment to P2, ruled by the reviewer 2026-11-13 17:45:09Z, before production runs.** S2 printed five lines this
section did not list. They are wrangler's own output from the meta write, which runs with inherited stdio
(`ingest-routes.ts:763-766`; the data puts pipe theirs away). P2 now also expects, between the last canary line and
`meta written`, exactly these five, as S2 read them:
1. a blank line;
2. `(weather emoji) wrangler 4.35.0 (update available 4.162.0)`;
3. a separator line of box-drawing characters;
4. `Resource location: remote`;
5. `Success!`.

Anything else there, such as a warning or an update notice in another form, is outside the set: stop.
- **Ruled 17:51Z:**
  - `wrangler 4.35.0` must match exactly. Any other installed version is outside the set: stop.
  - The `(update available X)` clause may carry any number, or be absent. It describes the npm registry at run time,
    not the run. It is reported either way and is not a stop.
  - The other four lines must match exactly.

**S3 / P3, the verify after the write.**
- `expected: 1591 shards, 628956 distinct keys`.
- `shards complete: 1591/1591` and `keys present: 628956/628956`.
- `RESULT: PASS`.
- 0 manufactured self-loops (38 genuine).
- `extra` at least 827, since nothing is deleted.
- Exit 0.

**Deletes:** one per environment (the sentinel's cleanup). Lists: one verify listing of about 630 pages per verify,
far inside the lists allowance.

**P5 re-registered, and P6 added, before production (ruled 18:02:49Z, on D1 and D2 reading clean):**
- **P5's new band:** **567,944 to 568,228 + 10,000 x retries + 100 per hour of the window.**
  - The floor is 568,228 less 0.05%.
  - That 0.05% is the undercount MEASURED on staging (0.039%: 245 of 628,958), rounded up. It is not assumed.
  - Outside the band: stop.
- **P6, new: production's D2 after P4,** with the same three controls first. It checks the values of production's
  added and value-changed keys against the pinned source. **Predicted: 0 mismatched and 0 absent.**
  - **If N > 0:** repair those N, then run P6 again, reporting counts. The tool has no single-key repair, so the
    repair is `--force-shard` for exactly the shards that hold the N keys. Its writes are counted against P5's
    retries line.
  - Production is reported done only after P6.

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

The deploy claim was taken at 2026-11-13T17:17:41Z, covering enrich-kv:staging and enrich-kv:production, after the
reviewer's 17:17:05Z go: weatherscope's combined deploy was closed and verified at 17:16:40Z. The reviewer ruled at
17:19:40Z that it is held through the staging-to-production gap.

| row | read | verdict |
|---|---|---|
| S0 | staging `/healthz` at 17:17:47Z and 17:18:50Z, both HTTP 200, commit b7184243: `{"builtAt":"2026-08-25T23:31:26.817Z","ageDays":79,"rows":619103,"stale":true,"maxAgeDays":14}`; the two reads identical | **as predicted** |
| S1 | pre-verify, 17:18:59-17:23:20Z, exit 1:<br>- `expected: 1591 shards, 628956 distinct keys`; `actual: 619103 keys present under rt:`;<br>- `shards complete: 1018/1591`; `keys present: 618276/628956`;<br>- `RESULT: FAIL -- 10680 keys missing across 573 shard(s)`;<br>- 0 manufactured self-loops (38 genuine); `extra ... 827`.<br>Staging holds exactly the old corpus, with no runtime rows. The sample callsigns in the FAIL lines were kept in a local file, not reported | **as predicted** (missing 10,680 in 10,500-10,680; complete 1,018 in 1,018-1,040; extra 827, at least 827) |
| S2, attempt 1 | 17:23:46-17:23:50Z, exit 1. The five pre-write lines are exactly as predicted (airports 6413/0; 1591 shards, 628956 usable; rule rev 3, 8994; previous manifest 1575; `diff: 1591/1591 shards changed -> 628956 keys to write`; the 644 NOTE). Then `REFUSING: no test identity`: **`BLIPSCOPE_TOKEN_FILE` was not exported in the background shell, my omission.** It stopped at the gate before the sweep, the anchor or any write. Staging's `meta:routes`, read afterwards, is byte-identical to the copy saved at 16:53Z; with the variable exported, the test identity resolves for both environments (a check that prints source names only) | **OFF: a refusal before any write**. Stopped and reported |
| S2, attempt 2 | re-run on the reviewer's 17:25Z ruling, with `BLIPSCOPE_TOKEN_FILE set: true` printed before the start; 17:25:21-17:44:28Z, exit 0.<br>- the five pre-write lines are identical to attempt 1's, and there is no `swept` line;<br>- `anchor: ZZ.....A is absent`;<br>- 63 bulk-put chunks (the last 8957, 628957/628957, at 17:43:58), 0 retries;<br>- `wrote 628956 route keys (+1 sentinel)`;<br>- the sentinel's provenance proved on attempt 1, after 1 s;<br>- `sample: 12/12`;<br>- canaries 5/5 (LHR-JFK, DFW-HND, DXB-JFK, FRA-JFK, SYD-LHR);<br>- `meta written: rows=628956 written=628956 sentinel=OK canaries=5/5` at 17:44:27, so **the seal fell between 17:44:23 and 17:44:27**;<br>- `cleanup: ... deleted`.<br>Five lines of wrangler banner from the meta put were outside the predicted set; they were accepted at 17:45:09Z, and P2 is amended above | **as predicted** (ruled 17:45:09Z). Staging is **sealed, unverified** until S3 |
| S3 | verify, 17:44:55-17:49:13Z, exit 0:<br>- `expected: 1591 shards, 628956 distinct keys`; `actual: 629783 keys present under rt:` (= 628,956 + 827);<br>- `shards complete: 1591/1591`; `keys present: 628956/628956`;<br>- `RESULT: PASS -- every expected key present, in every shard`;<br>- 0 manufactured self-loops (38 genuine); `extra ... 827`;<br>- 0 per-shard failure lines; no stderr other than wrangler's config warnings | **as predicted. Staging VERIFIED at 17:49:13Z**: the loop closed at the first verify, with no repair |
| S4 | staging `/healthz` at 17:55:33Z and 17:56:36Z (11 and 12 minutes after the seal, after S3's PASS), both HTTP 200, commit b7184243: `{"builtAt":"2026-11-13T17:44:25.919Z","ageDays":0,"rows":628956,"stale":false,"maxAgeDays":14}`; the two reads identical | **as predicted** (builtAt inside 17:44:23-17:44:27Z) |
| S5 | the analytics API (`kvOperationsAdaptiveGroups`), window 17:25:00-17:45:00Z, read at 17:59:50Z and again at 18:00:56Z with identical results. Staging namespace: **write 628,713**, delete 1, list 2, read 57. Production in the same window: write 48, list 6, read 5,632. The reader's control: it read 11-09 as 67,301 production and 65,678 staging writes, equal to the per-day read | **OFF: 245 BELOW the 628,958-628,978 band.** The outcome table has no row for under, so "anything else": stopped before production, and reported for the ruling |
| D1 (approved 18:02:49Z) | the same reader over 17:15:00-17:55:00Z at 18:03:30Z: staging write 628,713, the same as the run window. The sub-windows 17:15-17:25 and 17:45-17:55 hold no staging writes. The wider window's 1,256 staging lists are S1's and S3's two listings | **(b) excluded**: no write fell outside the run window |
| D2 (approved 18:02:49Z) | every key's value read back with `kv bulk get` through the helper, 100 keys a call (380 calls, 18:04:45-18:07:28Z), compared byte for byte with the pinned source, built as the writer builds it. The controls ran first:<br>- (i) a known-correct key compared against a deliberately wrong value: exactly 1 mismatch;<br>- (ii) a made-up key `rt:ZZQQQQQX`: read `null`;<br>- (iii) 1,000 unchanged keys on a fixed stride: 1,000 match.<br>**added: 10,680 checked, 10,680 match, 0 mismatched, 0 absent. value-changed: 26,106 checked, 26,106 match, 0 mismatched, 0 absent.** | **(c) excluded for every key whose value matters.** What the data cannot exclude: dropped writes to UNCHANGED keys, which are harmless and look exactly like an analytics undercount. So the 245 (0.039%) is read as the analytics being imprecise, and P5 is re-registered below |
| P0 | production `/healthz` at 01:30:59Z and 01:32:04Z on 11-14, both HTTP 200, commit aa40aa43: `{"builtAt":"2026-08-26T17:25:54.476Z","ageDays":79,"rows":619103,"stale":true,"maxAgeDays":14}`; the two reads identical | **as predicted** |
| P1 | pre-verify, 01:32:17-01:36:34Z, exit 1:<br>- `expected: 1591 shards, 628956 distinct keys`; `actual: 619103 keys present under rt:`;<br>- `shards complete: 1018/1591`; `keys present: 618276/628956`;<br>- `RESULT: FAIL -- 10680 keys missing across 573 shard(s)`;<br>- 0 manufactured self-loops (38 genuine); `extra ... 827`.<br>Production, like staging, holds exactly the old corpus with no runtime rows. Before P2, production's `meta:routes` was read again at 01:37:05Z: byte-identical to the copy saved at 16:54Z (86,938 bytes) | **as predicted** (missing 10,680 in 10,000-10,680; complete 1,018 in 1,018-1,040; extra 827, at least 827) |
| P2 | `BLIPSCOPE_TOKEN_FILE set: true` printed first; 01:37:20-01:52:31Z, exit 0.<br>- the pre-write lines as predicted: airports 6413/0; 1591 shards, 628956 usable; rule rev 3, 8994; previous manifest 1575; `diff: 634/1591 shards changed -> 568226 keys to write`; the 644 NOTE; no `swept` line;<br>- `anchor: ZZ.....A is absent`;<br>- 57 bulk-put chunks (the last 8227, 568227/568227, at 01:52:04), 0 retries;<br>- `wrote 568226 route keys (+1 sentinel)`;<br>- the sentinel's provenance proved on attempt 1, after 1 s;<br>- `sample: 12/12`; canaries 5/5;<br>- the five ruled lines, with `wrangler 4.35.0 (update available 4.162.0)`;<br>- `meta written: rows=628956 written=568226 sentinel=OK canaries=5/5` at 01:52:30, so **the seal fell between 01:52:27 and 01:52:30**;<br>- `cleanup: ... deleted`.<br>stderr: wrangler's config warnings and the token-file reader's Windows-permissions note only | **as predicted**. Production **sealed, unverified** until P3 |
| P3 | verify, 01:52:54-01:57:06Z, exit 0:<br>- `expected: 1591 shards, 628956 distinct keys`; `actual: 629783 keys present under rt:` (= 628,956 + 827);<br>- `shards complete: 1591/1591`; `keys present: 628956/628956`;<br>- `RESULT: PASS -- every expected key present, in every shard`;<br>- 0 manufactured self-loops (38 genuine); `extra ... 827`;<br>- 0 per-shard failure lines; no stderr other than wrangler's config warnings | **as predicted. Production VERIFIED at 01:57:06Z**, at the first verify, with no repair |
| P4 | production `/healthz` at 02:03:40Z and 02:04:41Z (11 and 12 minutes after the seal, after P3's PASS), both HTTP 200, commit aa40aa43: `{"builtAt":"2026-11-14T01:52:29.338Z","ageDays":0,"rows":628956,"stale":false,"maxAgeDays":14}`; the two reads identical | **as predicted** (builtAt inside 01:52:27-01:52:30Z) |
| P6 | the D2 tool against production, `dff309eb` to `24ce5424`, 380 calls, 02:04:57-02:07:18Z, exit 0. The controls ran first:<br>- (i) a known-correct key against a deliberately wrong value: exactly 1 mismatch;<br>- (ii) the made-up key: read `null`;<br>- (iii) 1,000 unchanged keys on a fixed stride: 1,000 match.<br>**added: 10,680 checked, 10,680 match, 0 mismatched, 0 absent. value-changed: 26,106 checked, 26,106 match, 0 mismatched, 0 absent.** The local mismatched and absent files are both empty lists | **as predicted** (0 mismatched, 0 absent) |
| P5 | the analytics API, window 01:37:00-01:53:00Z (the run, rounded outward to whole minutes, fixed before the read), read at 02:07:41Z and again at 02:08:46Z with identical results. Production namespace: **write 567,930**, delete 1, list 4, read 1,451. The reader's control: 11-09 read again as 67,301 production and 65,678 staging writes, equal to the earlier reads | **OFF: 14 BELOW the 567,944-568,254 band** (568,228 + 0 retries + 100 x 16/60). 298 below the 568,228 the run wrote: an undercount of 0.052%, against staging's 0.039%. Stopped and reported (NOT CLEAN, 02:09:26Z) |

**Ruled by the reviewer on the NOT CLEAN (received 02:17Z on 11-14, after the reviewer's terminal crash):**
- P5 is recorded OFF, with no re-grade.
- No repair: P3 (every key present) and P6 (all 36,786 added and value-changed keys correct) exclude a data
  defect.
- The deploy claim was released at 02:17:57Z.
- The weekly job (C) takes the analytics write count as an upper bound only, a cost stop. Completeness is graded by
  the run's own receipt and verify (condition 8 of its plan).
- A production D1 is approved, read-only, needing no claim. It tests alternative (c): that the analytics stamped
  some of the run's writes outside its window.

**P5's alternatives, as reported at 02:09:26Z:**
- (a) the analytics count undercounts by more than staging's one sample suggested;
- (b) the bulk put dropped up to about 300 writes. P3 and P6 exclude this as a harm, but not as an event: a dropped
  write could only fall on a key whose stored value was already correct;
- (c) the analytics stamped some of the run's writes outside the window;
- (d) the reader is broken. Excluded by the 11-09 control and the two identical reads.

**D1 on production: what each result will mean, frozen before the read.**
- **The reads.** The same reader as P5 over two sub-windows, 01:27:00-01:37:00Z and 01:53:00-02:03:00Z on 11-14.
  Each is read twice, at least 60 s apart, and then the 11-09 control runs again.
- **W** is production's `write` count, summed over both sub-windows.
- **The background allowance is 100 writes across the 20 minutes, which is 300 an hour.** Production showed 48
  writes in staging's 20-minute run window, 144 an hour. The run's own writes all fell between 01:37:20 and
  01:52:31Z.

| W, and the other readings | meaning |
|---|---|
| W at least 398 (298 or more above the allowance) | (c) explains P5 |
| W at most 100 | about 0 run-attributable writes: (a) and (b) remain, and the analytics write count is recorded as unable to grade a run at this precision |
| W from 101 to 397; or a sub-window's two reads disagree; or the control does not read 67,301 production and 65,678 staging writes | none of the above: the instrument is unreliable at this scale. Stop and record |

Staging's counts in the sub-windows are reported and not graded: they cannot bear on production's window.
