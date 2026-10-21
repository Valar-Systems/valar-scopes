# The Bend site feed: live aircraft for valarscopes.com, from what the proxy already fetches

**Asked by Daniel, 2026-10-21.** valarscopes.com shows live aircraft over Bend, Oregon. Its Worker (`valarscopes-site`, in the same Cloudflare account) calls the proxy through a **service binding**. The requirements:
- serve only the Bend area;
- never trigger an upstream fetch;
- be reachable only through the binding, with no device credentials;
- carry no enrichment;
- add no upstream load and only cheap CPU to device traffic;
- be self-contained behind one switch, so it can be removed in one small PR.

**His decisions (2026-10-21):**
1. **A durable snapshot, in a Durable Object.** KV's worst-case visible age is about 100 s, and KV can't enforce once-per-30 s strictly across colos.
2. **The write runs in `waitUntil`** and can never slow or fail a device response; its errors are counted.
3. **At most one write per 30 s, across every isolate and colo,** checked against the stored row's own time.
4. **The age is shown, and no planes are shown once the snapshot is more than 2 minutes old.**
5. **Reads are cached for up to 10 s in the visitor's colo,** so a traffic spike can't flood the one instance.
6. **The worst-case age stays under 90 s,** predicted in its typical and worst cases.
7. **Centred on downtown Bend without publishing his tile.** When a device fetch covers downtown, the snapshot keeps only the planes inside a fixed **30 mi (48.28 km)** circle around 44.058 N, 121.315 W, and only when that whole circle lies inside the fetched area. Nothing about the device's tile, radius bucket or limit is ever stored or served.
8. **Predictions first, with a sabotage for each condition, and a reviewer approval.** Daniel does the deploy.

This document freezes the design and the predictions **before any code**.

## The design

**One file: `proxy/src/sitefeed/bend.ts`.** It holds the writer hook, the Durable Object class `BendSnapshotDO`, and the entrypoint `BendFeed`.

**The switch:** `SITE_FEED_BEND = "on"` (a var). Anything else is off. When off, the device path makes **no** Durable Object call, and the entrypoint answers `{ available: false, reason: "disabled" }`.

**The writer, on the device path:**
- **Where:** a hook in `blips.ts`'s `buildFresh`, after a successful upstream fetch. It is handed the **full** normalised list from the upstream, before the device's `limit` cut. The cut list would be incomplete, and its edge, a disc centred on the device's tile, would reveal the tile.
- **When:** only when the whole circle fits inside the fetched area: `haversine(D, tileCentre) + R ≤ upstreamDistNm(bucket) × 1.852 km`, where D is downtown Bend and R is 48.28 km.
- **What:** the aircraft within R of D, plus the upstream's `t` and the source's name. Nothing from the request is kept: no tile, bucket, limit or device.
- **How often:**
  - **a per-isolate guard,** at most one send per isolate per 30 s, which keeps Durable Object traffic small;
  - then `ctx.waitUntil(stub.fetch(…))`, with its rejection caught and counted (`write_error`).
- **Safety:** the whole hook sits inside `try/catch`, and the device's response never awaits it.

**The Durable Object, `BendSnapshotDO`** (one instance, `idFromName("bend")`):
- **A write** reads `storage.get("snap")` and **refuses** (counted as `refused_30s`) when the stored row's own `writtenAt` is within 30 s of now. Otherwise it stores the new snapshot.
- **The 30 s rule lives in storage, not in memory,** so it holds across isolates, colos and a Durable Object restart.
- **A read** returns the stored row.

**The reader, the entrypoint `BendFeed`** (`WorkerEntrypoint`, RPC method `snapshot()`):
1. It reads the colo cache (`caches.default`, a fixed internal key, 10 s).
2. On a miss it reads the Durable Object and puts the result in the colo cache for 10 s.
3. It computes `age_s = now − t`. **Over 120 s, `aircraft` is `[]` and `stale` is `true`,** decided on the server, so no site can show old planes.
4. **No snapshot** gives `{ available: false, reason: "empty" }`, the equivalent of a 503.
- `bend.ts` imports nothing from `upstreams/`.
- **It isn't an HTTP route at all.** Public requests reach the default `fetch` handler, which has no path to it. A named entrypoint is reachable only through a service binding that names it.

**The binding, for `valarscopes-site`:**
```toml
[[services]]
binding = "BLIPSCOPE_BEND"
service = "blipscope-proxy"
entrypoint = "BendFeed"
```
The site calls `await env.BLIPSCOPE_BEND.snapshot()`.

**The response, v1:**
```json
{ "available": true, "v": 1,
  "centre": { "lat": 44.058, "lon": -121.315 }, "radius_km": 48.28,
  "t": 1761080000, "age_s": 23, "stale": false,
  "source": "adsb.fi", "attribution": "aircraft data: adsb.fi",
  "aircraft": [ { "hex": "a1b2c3", "cs": "ASA123", "lat": 44.1, "lon": -121.2,
                  "alt_ft": 31000, "gs_kt": 452, "track_deg": 271, "vrate_fpm": 0,
                  "category": 3, "seen_s": 2 } ] }
```
- `source` is `adsb.fi` or `adsb.lol`. The relay used isn't exposed.
- For adsb.lol, `attribution` reads `aircraft data © adsb.lol contributors, ODbL 1.0`.

**The counts,** in Analytics Engine, with no location: `written`, `skipped_not_covered`, `skipped_isolate_guard`, `refused_30s` and `write_error`.

## The age a visitor sees (the prediction Daniel asked for)

It has three parts:
- **u:** the upstream's own lag at fetch, 0-10 s;
- **w:** the time since the last accepted write. That is the 30 s gate, plus the wait for the device's next tile refresh, r;
- **c:** the colo cache, 0-10 s.

`blips.ts` refreshes a tile at least every 12 s while a device polls (`DEFAULT_STALE_SERVE_MS`), so **r ≤ 12 s**.

| | u | w | c | total |
|---|---|---|---|---|
| **typical** | ~3 | ~15 + r/2 ≈ 21 | ~5 | **~29 s** |
| **worst, while his Blipscope runs** | 10 | 30 + 12 | 10 | **≤ 62 s**, under the 90 s bar |
| **his Blipscope off** | | | | the age grows; past 120 s the response carries no planes |

## Predictions (frozen before any code)

Each has a sabotage that must turn it red. Unless stated, they are vitest checks in the Workers pool.

| | check | predicted |
|---|---|---|
| **B1** | geometry | `covers()` is true when D's circle fits inside the fetched circle and false 1 m past the boundary. Only aircraft within R of D are kept |
| **B2** | privacy | For a device tile **away from downtown** (44.10/-121.25, bucket 160, limit 5): the stored row and `snapshot()` contain no key but the ones above, `centre` equals the downtown constant, and the JSON text contains neither the tile's lat nor its lon string. *Sabotage S-B2:* the writer uses the tile as the centre, and the check goes red |
| **B3** | completeness | A device asking `limit=5` while 20 aircraft are inside the circle gives a snapshot with **20**. *Sabotage S-B3:* building from the `limit`-cut list goes red |
| **B4** | condition 2, never slows or fails a device | With the Durable Object's `fetch` rejecting, the device's `/blips` response is 200 and byte-identical to the switch-off response, and `write_error` is counted once. *Sabotage S-B4:* awaiting the write in-line turns the device's request into an error, and the check goes red |
| **B5** | condition 3, 30 s across isolates | On a fake Durable Object state whose storage outlives the instance: a write at t0 is accepted; at t0+10 s on a **new instance** it is refused; at t0+31 s it is accepted. *Sabotage S-B5:* keeping the last-write time in an instance field lets the new-instance write through, and the check goes red |
| **B6** | condition 4, no stale planes | A snapshot 121 s old gives `aircraft: []`, `stale: true`, `age_s: 121`. At 119 s the planes are present. *Sabotage S-B6:* removing the cutoff goes red |
| **B7** | condition 5, the colo cache | Two `snapshot()` calls 5 s apart make **one** Durable Object read. One 11 s later makes a second. *Sabotage S-B7:* skipping the cache goes red |
| **B8** | never upstream | `snapshot()` with no stored row gives `{available:false, reason:"empty"}`, with **0** fetches to any host. *Sabotage S-B8:* a fall-back to the upstream chain goes red |
| **B9** | the switch | Off: the device path makes 0 Durable Object calls, and `snapshot()` gives `reason:"disabled"`. On: it writes |
| **B10** | nothing else moves | Every existing proxy test passes unmodified, and `tsc` is clean |

**After Daniel's deploy (read-only, like SC7):**
- **L1:** `written` > 0 and `write_error` = 0 over the first hour.
- **If `written` stays 0 while `skipped_not_covered` grows:** the 30 mi circle doesn't fit his device's fetched area. Stop and tell him, with no location in the report.
- **L2:** the age the site reads, at least twice 10 minutes apart while his Blipscope runs, is **≤ 62 s**. Over 90 s: stop.

**Outside the set:** stop and report.

## Removing it (also in `proxy/README.md`)

The whole footprint, removed in one PR:
- `src/sitefeed/bend.ts` and its test;
- the hook's two lines in `blips.ts`;
- the export line in `index.ts`;
- the `SITE_FEED_BEND` var;
- the README section.

**A Durable Object class can't simply be deleted.** The same PR adds a `[[migrations]]` entry with `deleted_classes = ["BendSnapshotDO"]` in place of the binding. Turning the switch off first stops all writes and reads without a deploy of code.

## Not decided here, for Daniel

**Licensing for a public page.**
- The README records adsb.fi's written caching and redistribution permission (2026-07-30), and my notes record that it covers customer devices.
- Whether it covers showing the data on a **public website** isn't recorded.
- adsb.lol's ODbL requires attribution wherever its data is shown.
- The response carries `attribution` so the credit can't be dropped by accident. Whether adsb.fi's grant extends to the site is for Daniel to confirm before it goes live.

## Addendum 1, 2026-10-21 (before any code): the configuration check

Cloudflare's docs say named environments **do not inherit** Durable Object bindings, so the binding must be repeated under `env.staging` and `env.production`, beside one top-level `[[migrations]]` entry (`new_sqlite_classes = ["BendSnapshotDO"]`). That asymmetry is invisible in the file, so it is checked on what would ship, not on what was written:

| | check | predicted |
|---|---|---|
| **B11** | `npx wrangler deploy --dry-run --env staging` and `--env production`; nothing is deployed | each lists the Durable Object binding `BEND_SNAPSHOT` (class `BendSnapshotDO`) and the var `SITE_FEED_BEND`. *Sabotage S-B11:* with the production binding removed, the production dry-run doesn't list it, and the check goes red |

## Results: local (2026-10-21, before CI)

Graded on `5c055b3`. Every check came out as predicted, and every sabotage turned its check red. Each plant asserted that its anchor appeared exactly once and that the file changed, and each was restored and verified against the commit.

| | read | sabotage |
|---|---|---|
| **B1** | `covers()` is true 1 m inside the bucket-80 boundary and false 1 m outside it; never true for bucket 40; only planes within R kept | (structural; B2-B3 depend on it) |
| **B2** | from the tile 44.10/-121.25, the stored row's keys are exactly `aircraft, source, t`, and it contains neither `44.10` nor `-121.25` | **S-B2** (tile as centre): red |
| **B3** | the device got its `limit` of 5, and the snapshot holds all **20** planes inside the circle and none of the 3 outside it | **S-B3** (the `limit`-cut list): B2/B3 red, and B9 red too (it also expects 20) |
| **B4** | the Durable Object's `fetch` rejects: the device gets 200 and a body identical to the switch-off response (apart from `t`), and `write_error` is counted once | **S-B4** (awaited in-line): red |
| **B5** | written at t0, refused at t0+10 s on a new instance, written at t0+31 s | **S-B5** (instance field): red |
| **B6** | at 121 s: `aircraft: []`, `stale: true`; at 119 s the planes are present | **S-B6** (no cutoff): red |
| **B7** | one Durable Object read for reads at 0 s and +5 s; a second at +11 s | **S-B7** (no cache): red |
| **B8** | empty gives `{available:false, reason:"empty"}`, with exactly one `GET /get` and **0** global `fetch` calls | **S-B8** (an upstream fall-back): red |
| **B9** | off: 0 Durable Object calls and `disabled`. On, against the **real** Durable Object binding: written, and read back with 20 planes and the downtown centre | (S-B3 above) |
| **B10** | the full proxy suite: **44 files, 585 tests passed**, every existing test unmodified; `tsc` clean | (n/a) |
| **B11** | `wrangler deploy --dry-run` for staging and production each list `env.BEND_SNAPSHOT (BendSnapshotDO)` and `env.SITE_FEED_BEND ("on")` | **S-B11** (production binding removed): not listed, so red |

**What B11 showed beyond its prediction:** with the production binding removed, the dry-run **still exits 0**. Wrangler would deploy production with the feed silently off (`enabled()` needs the binding). That's the reason the check reads the artifact rather than the file or the exit code. A CI step that runs this dry-run would make it permanent. I'd propose that separately.

**Still to come:** CI on this push. L1 and L2 follow Daniel's deploy.

## Addendum 2, 2026-10-21 (before the code it predicts): the adsb.fi permission, and Bend only by construction

**The permission, as Daniel reported it (2026-10-21):** Samuli (adsb.fi) is OK with valarscopes.com showing live adsb.fi data **for Bend only.**
- This answers the licensing question in "Not decided here" above, for that scope and nothing wider.
- Showing any other location needs Samuli's OK first.
- Every page that shows the data credits adsb.fi **with a link to adsb.fi.**

**So the feed stays Bend-only by design.** There is one fixed downtown-Bend snapshot and **no way to ask for any other place.** That is now checked, not just stated:

| | check | predicted |
|---|---|---|
| **B12** | Bend only, by construction | `BendFeed.prototype.snapshot` takes **no** parameters (`length === 0`). Called with a place anyway (`snapshot(45.52, -122.68)`), it returns the downtown-Bend `centre` and `radius_km`, and the Durable Object namespace is asked for the name `"bend"` and nothing else. *Sabotage S-B12:* a `snapshot(lat, lon)` that recentres the answer turns the check red |
| **B13** | the credit carries its link | every `available: true` response carries `attribution_url`: `https://adsb.fi` for adsb.fi, and `https://adsb.lol` for adsb.lol, alongside its ODbL `attribution`. *Sabotage S-B13:* removing the field turns the check red |

L1 and L2, after Daniel's deploy, are unchanged. **The deploy is unblocked on the adsb.fi side.** It still waits for the Weatherscope session's review and the reviewer's approval of #458.

### Results for addendum 2 (graded on `9d378d5`)

| | read | sabotage |
|---|---|---|
| **B12** | `snapshot.length === 0`. Called with Portland (45.52, -122.68), it returns the downtown `centre` and `radius_km` 48.28, and the namespace was asked only for `"bend"` | **S-B12** (a `snapshot(lat, lon)` that recentres): red |
| **B13** | `attribution_url` is `https://adsb.fi` for adsb.fi and `https://adsb.lol` for adsb.lol | **S-B13** (field removed): red |

The full proxy suite passes (44 files, 587 tests), and `tsc` is clean. The README section now carries the permission, its Bend-only scope, and the linked credit.
