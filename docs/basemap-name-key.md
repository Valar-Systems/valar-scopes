# K1: keyed names for the basemap's stored packages (predictions first)

**Nothing here is built yet.** This freezes K1's design, rows, plants and staging reading before any code. The code
stacks on S2 and runs only after step 0 and S2 have merged.

## The rulings, verbatim (reviewer chat, pasted by Daniel)

From 2026-11-06 ~02:40Z, condition 4a: keyed names, HMAC under a server secret, before any production binding, with a
check and a sabotage.

From ~04:22Z:

> K1 approved: 32 random bytes generated and piped to `wrangler secret put` on stdin, never printed, never in a file in
> any git tree, and with no human copy. Staging first; production at D4.

From ~21:39Z: "K1: its predictions next, as you plan. Its code stacks on S2."

The proposal these answer is `docs/basemap-a1.md` §4a.

## Why

A query's digest is SHA-256 over two-decimal coordinates and a few fixed parameters. That input is small enough to
enumerate, so an unkeyed digest in a stored object's name gives a device's location to anyone who can list the bucket.
Under an HMAC with a server secret, a listing gives nothing without the secret.

## The change

| | what it does |
|---|---|
| **the secret** | `BASEMAP_NAME_KEY`, a Worker secret on the proxy, one per environment: **32 random bytes as 64 lowercase hex** |
| **`nameKeyOf(raw)`** | returns `raw` when it is exactly 64 lowercase hex, else `undefined`. Anything else counts as no key, so a truncated or mis-pasted secret fails closed |
| **`queryDigest(q, nameKey?)`** | with a key: 24 hex of `HMAC-SHA-256(the 32 bytes, queryKey(q))`. With none: today's SHA-256 |
| **`etagOf(q, rev, storeId, nameKey?)`** | passes the key through, so the ETag and the Cache API key (built from the ETag) are keyed too |
| **the handler** | `const pkg = nameKey ? env.BASEMAP_PKG : undefined`. **A bucket with no key is treated as no bucket:** MS9's path, nothing stored and nothing read from it |
| **`/healthz`** | `basemap` gains `keyed: boolean`. It is a boolean, never the value |
| **`src/types.ts`, `wrangler.toml`** | the optional binding on `Env`, and a comment at each `BASEMAP_PKG` binding saying A1 needs the secret too |

**What does not change:**
- **Where the key is absent, the digest stays as it is today.** That is production until D4: no bucket, no stored name, and
  an ETag sent only to the device that asked.
- **The 24-hex length.**
- **`pkgKey`, `errKey`, `lockKey`, `cacheKey`:** they take the digest they are given.

**Why `keyed` is in `/healthz`.** A bucket with no key turns A1 off with no error: every request becomes a synchronous
build. Nothing else could show that, and it reads exactly like a working proxy.

## Rows

Row ids are `NK1`-`NK6`, used nowhere in the tree. The driver's id pattern needs extending: `caseOf` accepts `M[A-Z]…` and
`BB…`, so `NK…` would read UNNAMED. **That change is part of this PR.** The test key is `"ab".repeat(32)`, set in
`vitest.config.mts` beside the other deterministic test values.

| row | the check | control |
|---|---|---|
| **NK1** | The same two requests as MS8, a 200 and a 404, through a recording bucket. Every key written has the exact shape and holds that query's **keyed** digest. The 200's ETag **equals** `"bm1-<rev>-<store>-<keyed>"`, and `cacheKey` of it holds the keyed digest. **None of them holds the plain SHA-256 digest**, computed in the test from `queryKey(q)` | at least 3 keys were written, and the keyed digest differs from the plain one |
| **NK2** | **A vector computed outside the Worker.** For MS's base query, `queryKey` is `0.37\|0.59\|97\|100584\|5\|`. Under the test key, `queryDigest` equals `231bf7f1f10901272af15985`, computed with Python's `hmac` | with no key, it equals `1fbe09817ec2d79d6ccefcc5`, Python's `hashlib` |
| **NK3** | Two keys, one query: the digests differ and the ETags differ. One key twice: both are identical. The second key's digest equals `36e48f8a2d92521fbddac3ef` | as NK2's |
| **NK4** | **A bucket with no key.** `BASEMAP_NAME_KEY` unset, a recording bucket bound, the store id in KV: 200, one builder call, and **0 bucket calls of any kind** | the same request with the key makes at least 1 bucket call |
| **NK5** | **A malformed key:** 63 hex, 65 hex, uppercase, non-hex, the empty string, and a valid key with a trailing newline, each from a fresh isolate. Each reads as NK4 | the loop ran all six |
| **NK6** | Through `/healthz`: `keyed` is `true` with the test key, and `false` with none and with each of NK5's six. The body never holds the key's text | `basemap` is an object in every case |

**MH2 and MH3 are re-registered,** only to add `keyed: true` to their exact shapes.

**Rows that compute an ETag or digest for the handler** (MS1, MS2, and the rest of MS's) pass the test key. Their
assertions do not change.

## The plants

**New:**

| plant | what it does | predicted failing set |
|---|---|---|
| **S-NKPLAIN** | `queryDigest` ignores the key | **{NK1, NK2, NK3}** |
| **S-NKETAG** | `etagOf` calls `queryDigest(q)`, dropping the key | **{NK1, NK3}** |
| **S-NKOPEN** | `const pkg = env.BASEMAP_PKG`: the store runs with no key | **{NK4, NK5}** |
| **S-NKFORMAT** | `nameKeyOf` accepts any non-empty string | **{NK5, NK6}** |
| **S-NKHEALTH** | `keyed: true`, whatever the key | **{NK6}** |

**Existing plants that move, each by applying its rule to the new rows:**

| plant | change | set |
|---|---|---|
| **S-PETAG** | re-anchored: `queryDigest(q, nameKey)` | {MP3, MQ10, MW9} **gains NK1**, whose ETag check is an equality |
| **S-MS8** | none | {MS3, MS6, MS8} **gains NK1**, since the name no longer has the exact shape |
| **S-QDIGEST** | none | {MQ10} **gains NK2**, since the pinned `queryKey` loses `epsMm` |
| **S-MS9** | re-anchored: `nameKey ? (env.BASEMAP_PKG ?? ({} as R2Bucket)) : undefined` | {MS9}, unchanged |
| **S-HMADEUP**, **S-HNOCACHE** | re-anchored on the line that gains `keyed` | {MH2} and {MH3}, unchanged |

**Why no other set moves:**
- **NK1 reads only names and the ETag, on MS8's flow.** So a plant that breaks that flow is already in MS8's set. The only
  two are S-MS8 and S-PLOG, and S-PLOG changes a log line, which NK1 does not read.
- **NK2's query** has no levels (S-QSORT) and no half-way coordinate (S-QROUND).
- **NK4 and NK5 assert no label and no read count,** so S-MKUNK and the read-count plants do not reach them.

**Before any run:** the anchor check (each anchor found exactly once) and a compile check of every planted file, by hand
until the guard PR lands.

## Counts

**Stated against the tree they are read on.** The rule from ~21:39Z applies: every count here is read again from CI on the
exact commit after any merge of main, before it is restated.

**Frozen at the merge of main, `fa660869a026` (#589), as `80b2c27d6920`.** Main's count below is CI's, on that commit.

| | frozen | how it is built |
|---|---|---|
| **main's suite** | **1,012** (1,011 passed, 1 skipped), CI's `workers` run on `fa660869a026` | read |
| **the suite with K1** | **1,019** (1,018 passed, 1 skipped) | main + NK1-NK6 + MS11 |
| **`baseline[all]`** | **175** cases in 17 files; the other baselines stay 11 and 31 | S2's 168 + 7 |
| **the driver** | **84 runs:** 81 sabotages and 3 baselines, 0 runtime failures, **every one AS PREDICTED** with C-LOADFAIL BLIND among them; exit 0 | S2's 75 + K1's 5 + S-S2NOCATCH |
| **the smoke driver** | CONTROL **1,018 passed, 1 skipped, of 1,019**, 0 failed. S-SQ **{SQ1, SQ2}**. S-SQP **{SQ-C}**. Each run counts 1,019. No raw output kept. TIME_WAIT margin reported | follows the suite |

**Before the smoke run,** as ruled ~23:28Z: read that `SQ1:`, `SQ2:` and `SQ-C:` each title exactly one row across `test/`.

**Anything else, including another count or a set that differs: stop, no fix, report.**

**The earlier figures,** kept as written: 990 main, 1,005 with K1, 174 cases, 83 runs. They predate step 0's and S2's merges,
and MS11.

**CI for the code PR** is predicted when it goes up, from a finished run that postdates main's last job-set change.

## The staging reading (its own slot, after merge)

**The order, so staging never drops to the fail-closed path:** the secret first, then the deploy. Old code ignores the
secret.

**The secret:**

```sh
node -e "process.stdout.write(require('crypto').randomBytes(32).toString('hex'))" \
  | npx wrangler secret put BASEMAP_NAME_KEY --env staging 2>k1-err.log
```

- It goes through a pipe: never printed, never on a command line, never in a file.
- The exit status is read, and stderr goes to its own file. Nothing filters the output.

**A fresh airport,** never built under a keyed name: KSLC, at the slot's parameters.

| row | predicted |
|---|---|
| **SK1** | `/healthz` `basemap.keyed`: **false** before the deploy, **true** after, read twice |
| **SK2** | KSLC's first request: **202 `known-miss`**. The retry after `Retry-After`: **200 `stored`** |
| **SK3** | **The ETag's digest is not the plain one,** computed locally from the imported `queryKey` |
| **SK4** | **The stored name comes from the ETag:** `bm/v1/pkg/<store>/<the ETag's digest>` reads **present** |
| **SK5** | **The plain name** reads **absent**, with SK4 as its anchor |
| **SK6** | the lock reads **absent** |

**Why the probe reads the ETag.** The key has no human copy, so no probe can compute a keyed name. The ETag is public and
carries the digest, so it gives the name without the secret. The probe is `wrangler r2 object get … --remote --pipe`, as
in `docs/basemap-a1.md` §4c, and it prints booleans only.

**Anything else: stop, no fix, report.**

**Staging's objects under unkeyed names** stop being read, and they age out under the 30-day rule.

## Production, at D4

Not part of this PR. D4's plan carries these rows:
- **The secret first,** generated the same way, then the binding, so production never holds a bucket without a key.
- **`/healthz` reads `keyed: true`** before the binding goes on. That is this instrument's reader, and D4's verification
  is where it is read.
- **No device holds a basemap ETag,** so no device re-downloads.

- **`scripts/smoke-prod.sh` prints `keyed`,** as its own line in D4's plan (Q-K1b).

## Ruled (reviewer chat, pasted by Daniel, 2026-11-06 ~22:49Z), verbatim

> Q-K1a: yes. 64 lowercase hex; anything else is no key and fails closed. If the generator's trailing newline reaches the
> secret, it reads as no key, and SK1 reading false catches it.
> Q-K1b: yes, with smoke-prod.sh printing keyed as its own line in D4's plan.
> Q-K1c: yes.

## Corrections found while writing the code, before any run (2026-11-06)

- **NK5 and NK6 gain a sixth malformed key:** a valid key with a trailing newline. That is the case Q-K1a names, so it is
  tested on the host as well as read at SK1. No set moves.
- **The guard's title pattern needs `NK` too,** not only `caseOf`, or the guard would not see the new rows. Both changed.
- **On this tree:** `tsc` is clean on both configs, all 80 anchors are found once each, and 79 planted files parse.
  C-LOADFAIL does not parse, by design. `--selftest-dups` reads control 0, outside copy 0, planted copy 6, exit 0.
- **No K1 test has run.** The counts above are re-read from CI, and frozen, once S2 has merged.

## S2's follow-up, carried here (ruled 2026-11-07 ~01:06Z, verbatim)

> One follow-up, with its own row and plant, in the next basemap code PR: a failed .err read must not fail a request the
> stored package can answer. With Promise.all, an R2 error on the .err read now fails a request that used to be served. A
> .catch to null on that read restores this; a missing .err only means a rebuild.

**The change:** `pkg.get(errKey(id, digest)).catch(() => null)`, on S2's one line.

| | predicted |
|---|---|
| **MS11** | a stored package, with the `.err` read refused: **200 `stored`, 0 builder calls**, the package's bytes. Control: the `.err` read was made once and failed |
| **S-S2NOCATCH** (new) | the `.catch` removed: **{MS11}** |
| **S-MS1** | unchanged plant; its set **gains MS11**: no store id, so no stored answer |
| **S-MS2** | re-anchored; its set **gains MS11**: the package is never read |
| **S-S2SEQ** | re-anchored; **{MS10}, unchanged**: sequential reads still reach the package first |

**Why no other set moves:** MS11 asserts what MS2's first request asserts, so a plant reaching it must be in MS2's set. The
only other one, S-MKHIT, changes MS2's second request, which MS11 does not make.

## Read (2026-11-07, `2a4753a2fd4b`): two plants off, both my misses

| check | read | verdict |
|---|---|---|
| `--selftest-dups` | control 0; outside copy 0; planted copy 6; exit 0 | as predicted |
| **`sabotage-basemap.mjs`, whole** (wait clear 03:03:07Z: TIME_WAIT 43; exit 1 at 04:00:10Z) | baselines **11 / 31 / 175** (17 files); **84 runs,** 0 runtime failures; **79 of 81 sabotages as predicted,** with C-LOADFAIL BLIND among them. Highest TIME_WAIT at a run's start 7,812 (gate 8,000); held for ports 1,613 s | **two off** |
| **S-QDIGEST** | failing **{MQ10, NK2, NK3}** against {MQ10, NK2} | **off, my miss.** Re-registered, ruled ~03:33Z. The prediction stays on record |
| **S-S2NOCATCH** | **BLIND:** 12 baseline cases not run (MS9, MS11, MS10, MX1, MP1, MP2, ...), against {MS11} | **off, my miss: not yet ruled** |
| the smoke driver | **not run:** the chain stops at the driver's exit | |

**All five new plants, and every moved set, read as predicted** except those two: S-NKPLAIN {NK1, NK2, NK3}, S-NKETAG {NK1, NK3},
S-NKOPEN {NK4, NK5}, S-NKFORMAT {NK5, NK6}, S-NKHEALTH {NK6}; S-PETAG, S-MS8, S-MS1 and S-MS2 with their new rows; S-S2SEQ,
S-MS9, S-HMADEUP and S-HNOCACHE unchanged. **MS11 and NK1-NK6 pass unplanted** (`baseline[all]` 175, 0 failing).

**S-QDIGEST, the ruling (~03:33Z):** re-registered as {MQ10, NK2, NK3}, with no code change. NK3 pins a digest over `queryKey`,
so the plant reaches it by the same path as NK2. I applied that reasoning to NK2 and not to NK3.

**S-S2NOCATCH: the cause is not yet proved.** A BLIND means cases went missing, so the file stopped early rather than MS11
going red.
- **The leading explanation:** with the `.catch` removed, `Promise.all` rejects while the package's real R2 body is still
  open and unread. The test then ends with storage unfinished, and vitest-pool-workers' isolated storage (on by default here)
  cannot pop its frame, which ends the file. Its pool carries that failure text.
- **The alternative, not yet excluded:** something else in the plant's failure path ends the file. MS9 is among the missing
  cases although it runs before MS11, and the explanation above accounts for that only if the file's results are reported
  in batches.
- **The driver printed no reason,** only the missing cases. Its output for that run was not kept.

### Q-K1d: the diagnostic, and the fix (ruled ~05:35Z)

> Q-K1d: yes, the diagnostic run as you propose: S-S2NOCATCH's plant on basemap-endpoint.test.ts alone, raw output kept, the
> file restored from a byte copy with its sha checked. Then follow your table:
> - If the first row holds: MS11's stub reads the package's bytes up front and returns a plain body. That's a test change
>   only, and MS11 still asks the same question. Freeze S-S2NOCATCH at {MS11}; run S-S2NOCATCH, S-MS1, S-MS2 and S-QDIGEST
>   against their sets; then the smoke driver whole; then K1's PR.
> - Any other row: stop and report.
> The BLIND stays on record as your miss.

**The diagnostic (05:37Z), on `8d42b553b632`: the first row holds.** The raw output names the row and the cause:
`Failed to pop isolated storage stack frame in test/basemap-endpoint.test.ts's test "MS11: ..."`, then `we were unable to pop
R2 storage`, then `EBUSY: resource busy or locked, unlink ...miniflare-R2BucketObject...`. It read `Tests 34 passed (46)`: the
same 12 cases, so MS9's absence is the file's results being lost, as the batching reading said. The file was restored, with
its sha matching.

**So the alternative is excluded:** the file ended at MS11's teardown, in the pool's own words, on the R2 object MS11 left open.

**The fix, a test change only.** MS11's stub reads the package's bytes up front and returns a plain body, and it refuses the
`.err` only after that read ends. The second half is mine, beyond the ruling's wording: without it, a plant that fails fast
could end the test with the read still in flight, which is the same failure. MS11 still asks the same question.

**Frozen before the run:**

| plant | predicted |
|---|---|
| **S-S2NOCATCH** | **{MS11}** |
| **S-MS1** | {MS1, MS10, MS11, MS2, MS3, MS4, MS6, MS7} |
| **S-MS2** | {MS10, MS11, MS2, MS6} |
| **S-QDIGEST** | {MQ10, NK2, NK3} |

Each run also takes `baseline[all]`, predicted **175** cases in 17 files. Then the smoke driver as frozen: CONTROL 1,018 / 1 /
1,019. **Anything else: stop and report.**
