# Failed requests carry the data centre that served them, and nothing that identifies the device (predictions first)

**Why.** S3's W5 miss was 5 fleet `/blips` 503s from one device, with no upstream answering, at 17:59-18:00Z on
2026-10-23. Whether they came from a different Cloudflare data centre than the other devices' requests could not be
answered: `RequestMetric` has a `colo`, but the per-request point never writes it.

**The ruling (reviewer chat, pasted by Daniel, 2026-10-23), verbatim:** "Stopping to ask was right. None of (a), (b) or
(c) as written. The colo is recorded only on points whose status is 5xx, and never beside a device id: a separate
point, indexed on its own, carrying the route, the status, the upstream and the colo, with no device id and no key.
Nothing on 2xx, 3xx or 4xx points changes. The README's privacy section gains one sentence in the same PR, saying that
failed requests are logged with the Cloudflare data centre that served them and nothing that identifies the device,
and support.html says the same if it lists what's collected. Predictions first: a host test that a 5xx point carries
the colo and no device id, a 2xx point carries no colo, and the sabotage (a device id planted on the error point) goes
red."

## The change

- **`proxy/src/metrics.ts`, `record()`:** when `status >= 500`, it writes **one more** point, the `err5xx` family:
  `blobs: ["err5xx", route, upstream, colo]`, `doubles: [status]`, `indexes: ["err5xx"]`.
  - No device id, no firmware and no key.
  - The per-request point is written exactly as before, still with **no** colo.
  - Below 500, nothing changes.
- **Privacy text, in the same PR:**
  - the README's Privacy & telemetry section gains one sentence;
  - `proxy/pages/support.html` gains the same, since its Privacy section lists what a cloud device sends;
  - and its embedded copy is regenerated.
- **The dataset's documentation** (`proxy/README.md`'s Analytics Engine list) gains the family.

**Double counting, checked before the change.** The dashboard's request queries select `blob1 LIKE '/%'`
(`dashboard/src/analytics.ts` `REQUEST_POINTS`). A family name such as `err5xx` cannot match, so a 5xx is not counted
twice. The dashboard test's list of rejected families gains `err5xx`, so this stays checked.

## Predictions

| | check | predicted |
|---|---|---|
| **K1** | host, `proxy/test/metrics.test.ts`: a 503 `/blips` request with a device id, firmware and a colo | **two** points. The request point equals what it was before the change (no colo). The `err5xx` point equals exactly `{blobs: ["err5xx", "/v1/blips", "", "SEA"], doubles: [503], indexes: ["err5xx"]}`: **no blob is the device id or the firmware** |
| **K2** | host: the same request at 200, at 304 and at 404 | **one** point each, exactly as before, and **no blob is the colo** |
| **K3** | the sabotage: a device id planted on the `err5xx` point in `metrics.ts` | **K1 red**, and only K1. Restored byte-identical |
| **K4** | the dashboard test | `REQUEST_POINTS` rejects `err5xx`, along with `usage`, `boot`, `ota` and `enrich_gap` |
| **K5** | the rest | `embed-pages.mjs --check` passes; the proxy and dashboard suites pass; `tsc` is clean |
| **K6** | after review, merge and deploy (standing rule) | `/healthz` reports the merge, and smoke-prod has 0 FAIL. **The live positive control is passive:** the first real fleet 5xx after the deploy must produce an `err5xx` point with a non-empty colo, and **0 `err5xx` points may carry a 16-hex value in any blob.** A 5xx cannot be forced in production without harm, so this is read when one occurs and reported as "not yet exercised" until then |

**Outside the set:** stop and report.

## Results (local, before CI)

| | read | verdict |
|---|---|---|
| **K1** | a 503: **two** points. The request point equals `{blobs: ["/v1/blips","MISS","","s3-128",<dev>,"19"], doubles: [503,12,0,1], indexes: ["/v1/blips"]}`, as before the change. The `err5xx` point equals exactly `{blobs: ["err5xx","/v1/blips","","SEA"], doubles: [503], indexes: ["err5xx"]}`, and no blob is the device id or the firmware | **as predicted** |
| **K2** | 200, 304 and 404: **one** point each, equal to the request point as before, with no `"SEA"` blob | **as predicted** |
| **K3** | the device id planted on the `err5xx` point in `metrics.ts`: **K1 red, and only K1** (1 failed, 44 passed) | **as predicted** |
| **K4** | the dashboard suite: 9 files, **101 tests** pass. `REQUEST_POINTS` rejects `err5xx` along with `usage`, `boot`, `ota` and `enrich_gap` | **as predicted** |
| **K5** | `embed-pages.mjs --check`: current (4 pages). Proxy suite: **48 files, 675 tests** (671 + 4 new). `tsc` clean. `check_device_ids`, `skus.py --check-readers` and `check_bssid` OK | **as predicted** |
| **K6** | after review, merge and deploy | not yet |

**Corrected during K3, recorded rather than smoothed over.** The first restore after the plant used
`git checkout -- src/metrics.ts`. That restored the **committed** file, from before the uncommitted change, not the
pre-plant one. **The sha256 check refused it ("NOT restored").** The change was then re-applied by the same exact-once
edit; its diff is only the `err5xx` block, and the suite passes on it.
