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
| **NK5** | **A malformed key:** 63 hex, 65 hex, uppercase, non-hex, and the empty string, each from a fresh isolate. Each reads as NK4 | the loop ran all five |
| **NK6** | Through `/healthz`: `keyed` is `true` with the test key, and `false` with none and with each of NK5's five. The body never holds the key's text | `basemap` is an object in every case |

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

| | today's value (not frozen) | how it is built |
|---|---|---|
| **main's suite** | **990** (989 passed, 1 skipped), CI's `workers` run on `51757039ed01`. No proxy source or test has changed since | read |
| **the suite with K1** | **1,005** (1,004 passed, 1 skipped) | main + step 0's 8 + S2's 1 + K1's 6 |
| **`baseline[all]`** | **174** cases | step 0's 167 + MS10 + K1's 6 |
| **the driver** | **83 runs:** 80 sabotages and 3 baselines | step 0's 74 + S-S2SEQ + K1's 5 |

**The smoke driver's CONTROL** follows the suite: 1,004 / 1 / 1,005 on today's values.

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

## Open, for the reviewer

- **Q-K1a:** 64 lowercase hex as the secret's form?
- **Q-K1b:** `keyed` in `/healthz`, with D4's verification as its reader? `scripts/smoke-prod.sh` could also print it, and
  grade it after D4. I recommend that as its own line in the D4 plan, not in this PR.
- **Q-K1c:** the no-key path keeps today's unkeyed ETag, as above?
