# D4's plan: serve a repeat without a build on a fresh isolate, and bound the cold build

**Status: a plan, with predictions frozen before any code.** Docs only: no code and no deploy. Nothing here is built until
the plan is ruled. D4's original scope, the production `BASEMAP` binding and the smoke's `/credits` half
(`docs/basemap-deploy.md`), waits for these prerequisites.

## The rulings, verbatim (reviewer chat, pasted by Daniel)

From ~02:45Z, on MM4's staging read:

> - For D4: at this traffic a fresh isolate is the normal case. A repeat and an If-None-Match revalidation must be
>   served without a build, even on an isolate that has never seen the store id. D4's plan says how, predictions
>   first. MM2's build wall (8.9-14.5 s here) stays a prerequisite beside it.

From ~03:55Z:

> D4: yes. Write the plan doc as a draft PR, docs only: no code, no deploy, predictions first. I haven't seen your
> read-only facts report, so put those facts in the doc's first section.
> - (a) A store id every isolate can read: compare the options (cost per read, latency, consistency when the store
>   changes), and give each a prediction and a sabotage.
> - (b) Counting and timing the builder's reads inside the builder is code. Propose it in the plan, and build it only
>   after the plan is ruled. The parallel-read decision is then taken against the device's timeout, with that timeout
>   read from the firmware.
> - (c) The MM4 re-read under D4, registered the same way as tonight's: B as the anchor control, and the rate reported.

## 1. The facts (read-only, 2026-11-05)

### 1.1 A fresh isolate is the normal case (MM4, `docs/basemap-branch-log.md`)

- In five groups on staging, **15 of 15 requests reached an isolate that did not know the store id** (`unknown-store`).
- That includes every repeat (R) and every `If-None-Match` revalidation (I) sent 10-23 s after a build, and repeats
  inside one data centre.
- **Every request paid a full build.** The handler skips the builder only when the isolate holds the store id
  (`proxy/src/basemap/handler.ts:53-59`), for 60 s, in that isolate's memory.

### 1.2 What a build reads (`proxy/src/basemap/build.ts:45-121`)

**The read order:**
1. `current`, then the manifest, coverage and the index.
2. **One read per cell** in the disc.
3. **One per terrain tile**, then terrain-sources.

**How:** each read is its own `await`, one after another. The builder holds nothing in memory between builds
(`worker.ts`, `build.ts`: no module-level cache).

### 1.3 How many reads, counted

**The instrument** is R2's own operation count for the bucket `blipscope-basemap` (GraphQL
`r2OperationsAdaptiveGroups`, the dataset `proxy/scripts/r2-class-a.ts` reads).

| window | what ran | GetObject reads | bytes |
|---|---|---|---|
| 2026-11-04 22:30Z to 11-05 00:46Z | **CONTROL:** nothing | **0** | - |
| 21:31Z | **CONTROL:** EGLL, `404 no_coverage` (V4) | **3**: the code's count (`current`, manifest, coverage), and MW12's | 4.0 MB |
| 21:24-21:25Z | KDEN, one cold build alone (MM2) | **57** | 17.7 MB |
| 22:23Z | Bend r 97, one build alone (V2) | **62** | 25.3 MB |
| 00:47-00:56Z | KDEN, MM4's 15 builds | **796**: 53.1 per build | 220 MB: 14.7 MB per build |

**Not settled.** Identical KDEN builds should read identically, so 15 × 57 = 855, not 796. By group, three builds each,
the counts are 173, 156, 158, 156 and 153.
- **The two explanations:**
  - (i) the dataset's counts are sampled or approximate;
  - (ii) the builds really differ.
- **Neither is excluded.** The dataset accepts neither `avg { sampleInterval }` nor `count`, so it cannot say whether
  it samples. The instrument in §3 settles it.

### 1.4 How long a read takes: not measurable without code

- **The only figure is an estimate:** (wall − CPU) ÷ reads ≈ (11.0 s − 2.2 s) ÷ 53 ≈ **165 ms per read**.
- **Why it is only an estimate:** the wall also holds the response's transfer and the awaits between reads.

### 1.5 The build's wall and CPU (GraphQL `workersInvocationsAdaptive`, the builder)

- **Builds:** across D3 and MM4, 21 builds (6 in D3, 15 in MM4) took **7.6-22.2 s wall** and **1.4-4.0 s CPU**.
- **`no_coverage`:** 0.68 s wall.

### 1.6 The device's timeout, read from the firmware (main at `35937ed`)

- **No firmware requests `/basemap` today.** Weatherscope draws a stored Bend package that is built into the image
  (`src/weatherscope/WxBendData.S`). `git grep` finds no `/basemap` URL in `src/` or `include/`.
- **The timeout a `/basemap` fetch would inherit** is the shared `Get()` path's (`src/HttpRequestManager.cpp:303-304`
  and `:376-377`):
  - **5,000 ms** TCP connect;
  - **5,000 ms** per-read stream timeout;
  - up to **2 attempts**.
- The 8,000 ms pair (`:523-524`) belongs only to the validated-roots TLS path: OTA and the version check.
- **So:** the builder sends no byte until a build ends. A cold build of 7.6-22 s would trip the 5 s wait for the
  response on every attempt, and **no device could receive a cold-built package today.**

### 1.7 What the proxy is bound to (`proxy/wrangler.toml`)

- `ENRICH_KV` (KV);
- `BEND_SNAPSHOT` (Durable Object);
- `WX` (R2);
- `METRICS`;
- `BASEMAP` (the builder, staging only).

**There is no binding to the store bucket.** The store id reaches the proxy only in a builder response's
`x-basemap-store` header.

### 1.8 The platform, from Cloudflare's documentation (read 2026-11-05)

**KV:**
- reads $0.50 per million beyond 10 million a month;
- eventually consistent;
- `cacheTtl` minimum 30 s (default 60);
- hot reads "typically within 10ms".

**The Cache API (`caches.default`):** local to one data centre. "The contents of the cache do not replicate outside of
the originating data center."

**R2:** Class B (reads) $0.36 per million beyond 10 million, with strong consistency per object.

**Workers Caching** (a Worker's own cache, `[cache] enabled = true`):
- **What it caches:** `GET` and `HEAD` only. It is consulted **before** the Worker runs, including on service-binding
  `fetch()` calls.
- **Its key:** the target entrypoint, the path and query string, the **Worker version** (by default) and `ctx.props`.
  The host is not in the key.
- **Tiered by default:** a lower tier near the eyeball, plus an upper tier every lower tier consults on a miss.
- **Pricing:** every request to the cached Worker is billed at the standard request rate, **including service-binding
  calls that are otherwise free**. A hit bills no CPU.
- **Purging:** `ctx.cache.purge()` by tag or prefix.
- **Bypass:** a response with `Set-Cookie`, or a request with `Authorization`, is not cached.

## 2. (a) A store id every isolate can read: the options

**What every option keeps:**
- An ETag still names its store (`bm1-<rev>-<storeId>-<digest>`).
- A store's objects are immutable and stay in R2 after a re-ingest.

So "the old store" during a changeover means **the previous valid package, not a wrong one**.

| | where the id lives | cost per `/basemap` request | latency added | when the store changes | R, same data centre | R, another data centre | I |
|---|---|---|---|---|---|---|---|
| **O1** KV key `bm:store` | `ENRICH_KV`, written when the store is sealed (or by the proxy on a builder 200), read with `cacheTtl` 30 | 1 KV read ($0.50/M past 10 M) | ~10 ms hot; more when cold | the new id is visible after `cacheTtl` and propagation: tens of seconds of the previous store | Cache API hit | **builds** | 304, no build |
| **O2** a deploy-time variable `BASEMAP_STORE` | the proxy's `vars` | 0 | 0 | only on a proxy deploy. A re-ingest without one leaves the proxy naming the old store while the builder builds `current`: the handler would have to send the builder the store to build, or refuse | Cache API hit | **builds** | 304, no build |
| **O3** a read-only binding of the store bucket | R2 `bm/v1/current`, read by the proxy | 1 Class B read ($0.36/M past 10 M) | one R2 read: about 165 ms (§1.4, an estimate) | strong: the next request sees the new id | Cache API hit | **builds** | 304, no build, after one R2 read |
| **O4** the Cache API holds the id | `caches.default`, per data centre, filled on a builder 200 | 0 | ~ms | per data centre, until its TTL | hit, once that data centre has built once | **builds** | 304 only where that data centre has built |
| **O5** Workers Caching on the builder | the builder's cached response carries `x-basemap-store`. The proxy calls the builder with `GET` and the normalised query in the URL | 1 billed builder request, hit or miss (CPU only on a miss) | a cache hit: lower tier ~ms; upper tier one round trip | the key holds the builder's version, not the store. A re-ingest serves the previous store's packages until `max-age` or a purge by tag (`Cache-Tag: bm-store-<id>`) | builder-cache hit | **builder-cache hit, wherever the upper tier holds it** | 304 from the cached response's store id: no build |

**Recommendation: O5.**
- It is the only option where a repeat in **another** data centre is served without a build. O1 to O4 fix the 304 and
  the same-data-centre repeat only.
- It adds no new state to keep consistent, beyond a purge on re-ingest.
- **Its cost** is one billed builder request per `/basemap` request.
- **Its constraint** is that the proxy-to-builder call changes from `POST` with a JSON body to `GET` with the normalised
  query.
- **The fallback is O1** if either is ruled out.

**Each option's prediction and sabotage.** These freeze what the option's PR would assert. Only the ruled option's
become binding.

| option | the prediction (host test, through the proxy's own dispatch, like MK) | the sabotage, and the row it must redden |
|---|---|---|
| O1 | with the KV key set, a **fresh** isolate's `If-None-Match` is a 304 with **0** builder calls | the KV read skipped: the 304 row reads 1 builder call |
| O2 | with the variable set, the same 304 with 0 builder calls; **and** a variable naming a store that is not `current` is refused, never served | the refusal deleted: the mismatch row serves a package |
| O3 | with `current` readable, the same 304 with 0 builder calls and **exactly 1** store read | `current` read twice: the read-count row reads 2 |
| O4 | after one build in a data centre, a fresh isolate's I there is a 304 with 0 builder calls | the store id's cache entry never written: the row reads 1 builder call |
| O5 | the second identical request, on a fresh isolate, is served from the builder's cache: **the builder's code runs once over two requests** | the builder's response marked `no-store`: the row reads 2 runs |

**On staging**, each option also predicts the re-read in §4 (its `R` and `I` columns above). That cannot be shown in
miniflare.

## 3. (b) The builder's reads: the instrument, proposed (built only after this plan is ruled)

**The change:**
- `r2Reader` counts and times each read.
- The builder writes **one** log line per build: `evt: "bm_build"`, with the reads, the bytes, the total read time, the
  longest single read and the wall time.
- It carries counts and milliseconds only: **no query, no coordinate, no device id.**

**Its rows and sabotages, frozen now:**

| row | asserts | its sabotage |
|---|---|---|
| **BB1** | a build over MW's synthetic store logs `reads` equal to the store stub's own count (MW9: 5) | one read left uncounted: BB1 reads 4 against 5 |
| **BB2** | `no_coverage` logs `reads: 3` | the line written only on a 200: BB2 finds no line |
| **BB3** | the line carries none of the query's numbers (MW's origin 0.37, 0.59) | the query's `lat2` added to the line: BB3 red |

**What its first staging reading will mean,** registered now (one KDEN build, read three times, each in its own minute):

| the reading | means |
|---|---|
| `reads` exactly 57, all three times | (i): the R2 dataset under-counted MM4's builds. The count is the code's |
| `reads` varying between builds | (ii): builds differ. Find why before any parallel-read change |
| a total read time under half the wall | the reads are not where the wall goes. The parallel-read decision is re-argued |
| none of the above | the instrument is not yet trustworthy: stop and decide |

**The parallel-read decision, taken against the device's timeout (§1.6):**
- **The target, proposed: a cold build at or under 2.5 s of wall.** That is half the 5 s wait for a response, leaving
  margin for the TLS handshake, the network and the second attempt's budget.
- **If parallel reads cannot reach it,** the plan comes back with the alternatives, each the firmware owner's or
  D4's to choose:
  - warming the cache for known device locations;
  - a longer timeout on the device's future `/basemap` fetch;
  - a "202, retry" answer.

## 4. (c) The MM4 re-read under D4, registered the same way as tonight's

**The requests:**
- **Groups:** five, G1-G5, each with B, R (about 10 s after B's response) and I (`If-None-Match`, about 20 s after).
- **Spacing:** each group's B more than 60 s after the previous group's last response, and more than the chosen
  option's freshness window after anything that could warm it.
- **Who and where:** the test identity, KDEN, Weatherscope's query.

**B is the anchor control in every group.** B must carry the chosen option's **cold** label, which proves the instrument
sees the label. The label set grows to name the new paths, for example `builder-hit` and `builder-miss` under O5. The
exact set and the R/I outcome table are frozen in the chosen option's PR, before any request.

**The rate is reported, not graded:** R's and I's label split over the five groups, with the data centre beside each.

**Outside the set:** stop and report.

## 5. Questions for the ruling

- **Q-D4a:** which option? Recommended: O5, with O1 as the fallback.
- **Q-D4b:** may the instrument in §3 (BB1-BB3) be built?
- **Q-D4c:** is the cold-build target at or under 2.5 s of wall?
- **Q-D4d:** no firmware calls `/basemap` yet, so its future fetch's timeout is the firmware owner's decision. Should
  §1.6's finding go to them now, since a cold build cannot reach a device under today's 5 s?
