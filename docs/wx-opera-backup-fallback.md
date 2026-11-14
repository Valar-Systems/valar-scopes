# The OPERA backup fallback: registered before its code

**Status:** registration only. No code is written until the reviewer has read this. Then comes the code, red first,
then CI, in this one PR. **The deploy needs the owner's word and the deploy claim**, outside Monday's proxy slot
(2026-11-16 16:00-17:00Z).

## 1. Why: the readings that started it (2026-11-14, read-only)

- **OPERA's primary cache is empty.** `openradar-24h` held no composite after about 20:10Z on 2026-11-11
  (`docs/wx-opera-code-stage.md` section 4.1). At 08:05:07Z on 11-14 it listed 0 keys under `YYYY/MM/DD/OPERA/COMP/` for
  that day and the day before. All ten OPERA products have `frameAgeS` null on production `/healthz`.
- **ORD has moved its services to a backup.** Its overview page (EUMETNET/openradardata-documentation,
  `docs/1-ORD-API-overview.md`, last committed 2026-11-12T09:55:51Z at `f917e6ba8c5c`), verbatim:
  > We are currently experiencing an issue with our cloud provider. Until the issue is resolved, we have temporarily
  > moved our services to the backup domain: radar-backup.meteogate.eu.

  The S3 endpoint it names is `https://object-store.os-api.cci1.ecmwf.int/openradar-24h-backup`. No end date is given,
  and no deprecation of `openradar-24h` is stated.
- **The backup carries exactly what our reader reads.** One read-only check at 08:05:07Z, against the frozen meaning (a):
  - it lists 200 under our ingest's own prefix: 1,279 keys, of which 383 are `OPERA@<stamp>@0@DBZH.tiff` (our
    `epochOfKey` pattern);
  - the newest is 5.1 min old;
  - one range read of its first 8 KB answers 206, with 8,192 bytes, TIFF magic and a Content-Range, which is
    `readWindow`'s own access pattern.
  - A listing at 08:00:00Z had failed with a URLError, which did not recur in two instruments a minute later. It is
    recorded as UNEXPLAINED, with a transient as the leading reading.

## 2. The design, as ruled (the reviewer, 2026-11-14, received by 08:07Z)

1. **A FALLBACK, not a switch.** Each check (one `ingestOnce`: one product, one run) chooses ONE source. It takes the
   primary when the primary's COMP listing for the window is non-empty, and the backup otherwise. **Never mix within a
   check:** its listing and every read come from one host.
2. **The chosen source is a reading, never a silence.** It is written into each frame object's metadata
   (`source: "primary" | "backup"`, beside `coveredShare`), carried into `latest`, and shown per OPERA product in
   `/healthz` as `source`.
3. **The same User-Agent and the same credit.** The product and licence are unchanged. ORD's overview, verbatim:
   > The property rights of OPERA composite products are held by EUMETNET, which has decided to distribute these
   > products under the CC BY 4.0 license.

   **No backup-specific attribution is stated** on the ORD home, overview or discovering-and-accessing pages (all three
   read 2026-11-14). So `/credits`' OPERA line (`proxy/src/photolicense.ts`) is unchanged.
4. **One PR.** This registration is committed first, then the code red first, then CI.

### 2a. What each check does, stated in full

| primary listing | backup listing | the check |
|---|---|---|
| non-empty | **not requested** | the primary: every read from the primary; `source: "primary"` |
| empty (200, no frame key in the window) | non-empty | the backup: every read from the backup; `source: "backup"` |
| fails every attempt (5xx, network, timeout; the existing `DEFAULT_OPTS.list`) | non-empty | the backup, as above, with the primary's failure carried in the result (`primaryListError`) |
| empty | empty | `no-frame`, as today: no GET, nothing written |
| fails every attempt | empty | **THROWS the primary's NoaaError, as today**, its text noting that the backup listed empty. Nothing written |
| empty | fails every attempt | **THROWS the backup's NoaaError.** Nothing written |
| fails every attempt | fails every attempt | **THROWS, with both NoaaErrors' text.** Nothing written |
| **non-empty, but a READ fails every attempt** | **not requested** | **the check FAILS as today** (`NoaaError`): no frame written, `latest` untouched, and **no request to the backup** |

**AMENDED before any code (the reviewer, 2026-11-14, received by 08:34Z), as ONE principle:** `no-frame` ONLY when every
host consulted answered EMPTY. Any listing failure that leaves the check without a frame THROWS.
- The registered row it replaces read "empty or failed / empty or failed → `no-frame`". That would have made "we could
  not reach either host" read exactly like "OPERA has no frame", which is the outage's own signature.
- **An empty backup says nothing about whether the primary has frames,** so a primary failure stays the error.
- **`IngestResult.source`** is set on every check that chose a source, so every log line names it.

**The last row is the registered answer to "the primary lists but its reads fail".** A fallback inside the check would
be a mixed run, so the check fails. **Its consequence, stated:** a primary that keeps listing frames it cannot serve keeps
that product dark on every check, exactly as today's code would. It is NOT turned into a fallback here. If it is ever
observed, a "prefer the backup after N failed checks" rule is its own registered change.

**Not a mix, and stated so the rows can see it:** two products in one cron invocation may choose different sources.
The choice is per check, and each product's `source` shows its own.

**`source` in `/healthz`:**
- `"primary"` or `"backup"`: the `latest` record's source;
- `null` when there is no frame, OR when `latest` predates this change and so carries no source. Never a made-up
  value, the same rule as `frameAgeS` and `coveredShare`;
- NOAA products carry no `source` key at all, as with `coveredShare`.

### 2b. The User-Agent (the reviewer's ruling, 2026-11-14, received after 08:07Z)

- **One constant for both hosts.** Every request on either host carries exactly `OPERA_UA`. Main's value at this PR's
  base (`proxy/src/wx/ingest.ts:19`, read from `6f6dae93`) is
  `BlipscopeWxIngest/1 (+https://github.com/Valar-Systems/valar-scopes)`.
- **A SEPARATE commit in this PR adds the contact address:**
  `BlipscopeWxIngest/1 (+https://github.com/Valar-Systems/valar-scopes; support@valarsystems.com)`.
  - That is the support address the product already publishes (`proxy/pages/support.html`), and the contact already
    used for NWS (`docs/weatherscope-stage4.md`).
  - It first reaches EUMETNET's and ECMWF's hosts on the DEPLOY. **The owner's word for the deploy is asked to cover
    the UA change explicitly**, since it sends a contact address to outside services.
  - `wx-opera.test.ts`'s User-Agent row compares against the constant, so it follows.
  - **The one transcribed copy,** `tools/wx/opera_fixture.py:31`, is updated in the same commit. UA3 holds the two equal.

## 3. Rows, red first

In a new `proxy/test/wx-opera-fallback.test.ts` (one file, run alone: the vitest port rule), with `doFetch` stubbed by
host. "Red against main" means the test file is written first and run against `6f6dae93`'s code.

| row | asserts | against main |
|---|---|---|
| **FB1** | primary non-empty: only the primary is listed and read (0 backup requests); the stored frame's `source` is `"primary"` | **red** (no `source`) |
| **FB2** | primary empty: the backup is listed; every GET goes to the backup host (0 primary GETs); `source: "backup"` | **red** (the backup is never listed) |
| **FB3** | the primary's listing fails every attempt: the backup is listed and used; `source: "backup"` | **red** |
| **FB4** | both empty: `no-frame`, both listed, 0 GETs, nothing written | **red** (main never lists the backup) |
| **FB4b** | (the amendment) both listings fail every attempt: the check throws, its text naming the primary's `OPERA list` failure; 0 GETs; nothing written | **green on main too** (main throws on the primary's failure): a REGRESSION row |
| **FB4c** | (the amendment) the primary fails every attempt, the backup lists empty: the check throws the primary's `OPERA list` error; 0 GETs; nothing written | **green on main too**: a REGRESSION row |
| **FB5** | primary non-empty, its head GET fails every attempt: the check throws; 0 requests to the backup host; no frame; `latest` byte-identical | **green on main too**: a never-mix REGRESSION row, not counted red-first |
| **FB6** | `latest` carries the stored frame's `source`; a frame stored without it (before this change) gives a `latest` with no `source` | **red** |
| **FB7** | the primary's return, at unit level: backup frames at E1-E3, `latest` E3 (backup); the primary lists E2-E4. E4 is stored from the primary, E2 and E3 are never rewritten (their metadata stays `backup`), and `latest` moves E3 to E4 with `source: "primary"`. **Its lagging case:** the primary lists only E1-E2 (older than `latest`), so `latest` reads `kept-newer` and never goes backwards | **red** (no `source`) |
| **HZ1** | `/healthz`'s `wx`: `source` per OPERA product (`"primary"`, `"backup"`, or null with no frame or an old `latest`); no `source` key on NOAA products | **red** |
| **UA1** | every request in FB1-FB7, on either host, carries exactly `OPERA_UA` (requests without it: 0) | **green on main** (main makes only primary requests, each with it): a regression row |
| **UA2** | the separate commit: `OPERA_UA` contains `support@valarsystems.com`, and `wx-opera.test.ts`'s UA row still passes against the constant | red before that commit, green after |
| **UA3** | the one transcribed copy, `tools/wx/opera_fixture.py:31` (Python cannot import the TS constant), equals `OPERA_UA` byte for byte (a vitest reading the .py file's text) | **green on main** (both are the same string today); it goes red if UA2's commit misses the copy, which is the point |

**How the FB rows assert, fixed now so the sabotage sets below are determinable:**
- FB1, FB2, FB3, FB4, FB5 and FB7 each assert the EXACT SET of (host, kind) their requests reached, where the kind is
  list, head or tiles.
- FB4b and FB4c assert the throw, the `OPERA list` text, no head or tiles request, and nothing written. They do NOT
  assert the host set, so main (which never lists the backup) passes them too.
- FB6 asserts only `latest`'s `source`.
- HZ1 seeds `latest` records directly and runs no ingest.

**Red-first, predicted, by test** (14 tests; FB7's lagging case is its own `it`):
- red exactly **{FB1, FB2, FB3, FB4, FB6, FB7, FB7 (lagging), HZ1}**, plus UA2 until its commit;
- green: FB4b, FB4c, FB5, UA1 and UA3 (regression rows).

**Then green:** all 14. Every existing `wx-*` test unchanged.

**Sabotages, each registered here and run after green** (a byte-copy restore with sha256 equal):
- **S-MIX:** the reads go to the OTHER host than the listing. Red exactly **{FB1, FB2, FB3, FB5, FB7}**. FB5's stub serves
  the backup's GETs, so a mixed read succeeds there and its "0 backup requests" fails. FB4 makes no read; FB6 and HZ1 do
  not assert hosts; UA1 and UA3 stay green.
- **S-FALLREAD:** a failed primary read falls back to the backup inside the check. Red exactly **{FB5}**.
- **S-SRC:** `source` is not written into the frame metadata. Red exactly **{FB1, FB2, FB3, FB6, FB7}**. HZ1 seeds its
  records, so it stays green.

**Outside any of these sets:** stop and decide.

## 4. The rows coupled to OPERA's return, re-registered (the reviewer, 2026-11-14, received by 08:07Z)

The fallback brings the ten's frames back BEFORE OPERA's primary does. So every row that waits on OPERA's return is
re-triggered, **with the source recorded beside each reading**:

- **T_R** (`docs/wx-opera-code-stage.md` section 4.1) becomes the first hour in which `opuk` stores a frame newer than
  1794427800 **from EITHER source**. It is read from R2 `latest` (`opera_latest.py`), now with its `source`.
  - The moved 7-day rows (cost, MD-6, OPERA timeouts) are otherwise unchanged. Each hour's reading prints the source(s)
    its frames came from.
  - "OPERA returns in a changed form" also covers a BACKUP frame that differs from the frozen shape.
- **The nine's routing** (the one deploy carrying all nine, with full rigour, including the never-backwards transient
  at `WxPolicy.h:152` as a predicted row) is triggered by the first frame from either source, with the source
  recorded. Its rows are registered with that deploy, not here.
- **New row, PR-RET: the primary's eventual return.** In production, the first check that lists the primary non-empty
  writes a frame with `source: "primary"`, and `/healthz` flips that product's `source` from `"backup"` to `"primary"`.
  - `latest` never moves backwards (`advanceLatest` is forward-only).
  - No frame key is written twice, because a frame is created with `If-None-Match: *`. A backup frame for the same
    epoch is kept, not replaced.
  - Read from `/healthz` twice, at least 60 s apart, and from `opera_latest.py`.
  - **A stated hazard:** a primary that returns LAGGING the backup is chosen anyway (the rule is "non-empty"). So
    `latest` holds the backup's newer epoch until the primary passes it, and the product can read stale meanwhile.
    That is reported, not graded.

## 5. Cost

- **The ten's builds resume.** That is the cost stage 3 measured and projected while OPERA ran
  (`docs/wx-opera-stage3.md`):
  - **$0.97-$1.19 a month at Read P**, mostly CPU (about 2.6-3.5 M ms a month);
  - R2 Class A about **0.174 M a month**.
- **Against the account's month projection of 2026-11-14 ($9.64,** read with OPERA DOWN, so without the ten's builds):
  - **CPU:** about +$0.05-0.07;
  - **Class A at Read A** (whole millions, developers.cloudflare.com/r2/pricing, "Billable unit rounding"): 1.009 M plus
    0.174 M stays inside the second million, so **+$0.00**;
  - **so about $9.71 at Read A: under the $10 overage allowance, with a margin of about $0.29.** At Read P it is lower.
    **Stated for the owner,** because the margin is thin.
  - **SUPERSEDED by 5a below (the reviewer's gap, 2026-11-14).** The $9.64 it starts from is a 30-DAY RUN RATE, not
    calendar November.

### 5a. November and December, by calendar month, prorated by deploy date (the reviewer's request, 2026-11-14)

**The correction this table makes.** The $9.64 sent to the owner on 2026-11-14 ~06:48Z was labelled "the month
projected". It was a 30-day run rate: it priced shsr's measured 120,780 Class A a month as if shsr had run all month.
shsr went live about 2026-11-13 06:16Z, so calendar November holds about 17.7 days of it. **Calendar November without
the fallback does not cross 1 M Class A.** The fallback from Nov 17 is what crosses it.

**Instrument:** `budget_read.py`'s month-to-date read (2026-11-01T00Z to 11-14T06Z, 318 h, `budget-mtd-1114.json`),
recomputed by calendar month. **Lines:**
- **Every line but shsr** is at its MTD rate for all 720 h.
- **shsr** is its one MTD day, plus its measured rate (X10) for the remaining 402 h.
- **KV writes by event:** background 45k a month; refresh-data 132.8k each Monday (five in November, four in
  December); the (A) refresh once, 1.197 M, in November only.
- **The ten, when building:** 0.174 M Class A and, at the upper end, 3.5 M CPU ms a month (`docs/wx-opera-stage3.md`).
- **The levers, as sent to the owner:** weather (shsr's one ListObjects per invocation) 43,050 Class A a month;
  Missileer (the write cut) 250,000 a month.

**Prices:** Workers CPU $0.02 per M ms over 30 M; KV writes $5.00 per M over 1 M; R2 Class A $4.50 per M over 1 M.
- **Read A:** whole millions (developers.cloudflare.com/r2/pricing, "Billable unit rounding").
- **Read P:** proportional.

**Dates assumed:** the fallback, and each lever, from **2026-11-17 00:00Z** (336 h of November's 720). December is the
whole month.
- **"No fallback" in December** means OPERA's primary is still down all December.
- **If the primary returns,** the ten cost what the "fallback" rows show either way, because the cost is the ten
  building, from either source.

**November 2026, calendar month**

| fallback | lever | R2 Class A | vs 1 M | CPU $ | KV $ | total, Read A | margin to $10, A | total, Read P | margin, P |
|---|---|---|---|---|---|---|---|---|---|
| none | none | 963,506 | -36,494 | 0.42 | 4.53 | **$4.95** | +5.05 | $4.95 | +5.05 |
| none | weather from Nov 17 | 943,416 | -56,584 | 0.42 | 4.53 | **$4.95** | +5.05 | $4.95 | +5.05 |
| none | missileer from Nov 17 | 846,840 | -153,160 | 0.42 | 4.53 | **$4.95** | +5.05 | $4.95 | +5.05 |
| from Nov 17 | none | 1,044,706 | +44,706 | 0.46 | 4.53 | **$9.49** | +0.51 | $5.19 | +4.81 |
| from Nov 17 | weather from Nov 17 | 1,024,616 | +24,616 | 0.46 | 4.53 | **$9.49** | +0.51 | $5.10 | +4.90 |
| from Nov 17 | missileer from Nov 17 | 928,040 | -71,960 | 0.46 | 4.53 | **$4.99** | +5.01 | $4.99 | +5.01 |

**December 2026, whole month**

| fallback | lever | R2 Class A | vs 1 M | CPU $ | KV $ | total, Read A | margin to $10, A | total, Read P | margin, P |
|---|---|---|---|---|---|---|---|---|---|
| none | none | 1,009,181 | +9,181 | 0.61 | 0.00 | **$5.11** | +4.89 | $0.65 | +9.35 |
| none | weather | 966,131 | -33,869 | 0.61 | 0.00 | **$0.61** | +9.39 | $0.61 | +9.39 |
| none | missileer | 759,181 | -240,819 | 0.61 | 0.00 | **$0.61** | +9.39 | $0.61 | +9.39 |
| from Nov 17 | none | 1,183,181 | +183,181 | 0.68 | 0.00 | **$5.18** | +4.82 | $1.50 | +8.50 |
| from Nov 17 | weather | 1,140,131 | +140,131 | 0.68 | 0.00 | **$5.18** | +4.82 | $1.31 | +8.69 |
| from Nov 17 | missileer | 933,181 | -66,819 | 0.68 | 0.00 | **$0.68** | +9.32 | $0.68 | +9.32 |

**What it says, for the owner:**
- **No row in either month exceeds $10.** The tightest is November with the fallback and without the Missileer lever:
  $9.49 at Read A, a margin of $0.51. That row is where the fallback's $4.50 Class A step lands.
- **The weather lever alone does not pull November with the fallback under 1 M** (+24,616). The Missileer lever does,
  by 71,960.
- **Without the fallback, November does not cross** (by 36,494). That is about 5 times shsr's ±13k a month prorated
  (about ±7k), but it rests on the other lines holding their MTD rate.
- **December's KV writes stay under 1 M** (four Mondays, no route refresh): $0. December's Class A crosses unless a
  lever lands. The fallback, or OPERA's own return, adds 0.174 M.
- **The other allowances, all inside their included amounts in the same MTD read** (30-day scaled):
  - R2 Class B 4.78 M of 10 M;
  - KV reads 7.02 M of 10 M; KV lists 11k of 1 M;
  - Workers requests 2.20 M of 10 M; Durable Object requests 0.31 M of 1 M;
  - R2 storage 3 GB of 10 GB.
  - Workers Logs is not in that read. `docs/account-budget.md` 5.5 projects December at 12-16 GB of 50 GB.
  - The weekly route job (#708) and the first attended run add under 2k KV operations each.
- **Uncertainty, stated:**
  - every steady line is the MTD rate extrapolated linearly;
  - shsr's ±13k a month;
  - the ten's 0.174 M is stage 3's measurement while they ran.
  - A Read A row within about ±10k of 1 M is a coin flip on a $4.50 step. None of these rows is that close; the nearest
    is November with the fallback and the weather lever, at +24,616.

**Computed by** `nov_dec_table.py` (scratchpad tool; offline; inputs as above), 2026-11-14.
- **While the primary is empty, each check makes one extra listing** (the backup's): about 10 a minute in all across
  the ten, each a small XML answer. These are outside requests, with no R2 or KV cost. The CPU is unmeasured and is read
  in the deploy's rows.

## 6. The deploy (not in this PR's scope; listed so nothing is assumed)

- **The ten ingest Workers** (`proxy/wx-ingest-opera.wrangler.toml`) carry the fallback.
- **The proxy Worker** carries `/healthz`'s `source`. That waits for a proxy deploy, which is not Monday's slot.
- **Until both are deployed,** a frame's source is readable from `latest` itself (`opera_latest.py`).
- **Each deploy has its own predicted rows,** registered before it, on the owner's word and under the deploy claim.

## 7. Results, local (2026-11-14)

| row | read | verdict |
|---|---|---|
| red-first | the test file committed alone (`9d08c288`), run against main's code (`6f6dae93`) at 08:59Z: 9 failed {FB1, FB2, FB3, FB4, FB6, FB7, FB7 (lagging), HZ1, UA2}, 5 passed {FB4b, FB4c, FB5, UA1, UA3}. Each red is an assertion, for its registered reason: no `source`; the backup never listed; the primary's listing error uncaught; OPERA_UA without the address | **as predicted** |
| green | the code (`a9f06a2b`): `operaListing` as amended, one host per check, `source` in the frame metadata, `latest`, `IngestResult` and `/healthz`. `tsc --noEmit` exit 0. 13 of 14, with UA2 waiting for its own commit | **as predicted** |
| UA3's red | the UA commit's constant alone: UA3 red ("the copy found; equal false"), so it catches a UA change that misses the copy | **as registered** |
| UA2 + UA3 green | `eaba7a92`: the constant and `tools/wx/opera_fixture.py:31` together. **14 of 14** | **as predicted** |
| every wx file | all 12 `test/wx-*.test.ts` in one run: **179 passed** (the 165 existing plus the 14 new). Every existing wx test unchanged | **as predicted** |
| sabotages, run 1: **instrument failure #1, no verdict** | the first, scratch driver read **0 of 14** tests under all three plants and printed OFF.<br>**Cause:** a no-plant diagnostic was blind the same way. vitest was given the 8.3 SHORT path (`DANIEL~1`) as its cwd, and workerd could not resolve its own modules (`No such module "@fs/C:/Users/..."`).<br>**Alternative (the plants broke vitest):** excluded by the same no-plant diagnostic.<br>**Second defect:** "could not see" printed as OFF. Each plant applied and each restore was proven: `ingest.ts` equal to HEAD's blob, porcelain clean | **recorded** (the reviewer, 09:06Z) |
| sabotages, run 2 | `scripts/sabotage-wx-opera-fallback.py` (`30d11e29`, committed before the run, as ruled): the long path as cwd, a BASELINE first, and BLIND (never OFF) under 14.<br>**Read at 09:07Z:** BASELINE 14 of 14, 0 red. **S-MIX** red exactly {FB1, FB2, FB3, FB5, FB7}. **S-FALLREAD** red exactly {FB5}. **S-SRC** red exactly {FB1, FB2, FB3, FB6, FB7, FB7 (lagging)}. Each restored from a byte copy, sha256 equal; `ingest.ts` equal to HEAD's blob after | **as registered** |

## 8. CI

- **CI-F1**, the registration push (docs only), at `3bf4d660`: 33 = 31 success + 2 skipped. Read twice, 08:42:08Z and
  08:43:38Z. Tested commit `a7c5759` (the merge into `6f6dae93`). **As predicted.**
- **CI-F2**, the docs push (5a and the amended 2a), at `b43126d6`: predicted 33 = 31 + 2 (docs only, the same anchor).
- **CI-F3, the code push: predicted (2026-11-14 09:08Z, before the push) 36 = 34 success + 2 skipped + 0 other.**
  - The diff now touches `proxy/**`, which `workers.yml` is filtered to: the three runs a docs-only PR lacks.
  - The anchor is #695 (proxy, docs and scripts; 36 at its head), with no workflow change on main since.
  - `tools/wx/` triggers nothing extra: only `account-budget.yml` (`tools/budget/**`) and `web-renderer.yml`
    (`tools/web-renderer/**`) filter on `tools/`.
  - `workers.yml`'s proxy job runs the whole proxy suite, the new file included, on Linux.
  - **Outside the set:** stop and decide.

## 9. The review's fixes (CHANGES REQUESTED at `cdb19df1`; registered 2026-11-14 09:32Z, before the code)

**The review found no defect in the code. One gap blocks the merge (B1), and N1-N7 are to fix before it.**

| finding | the change | rows |
|---|---|---|
| **B1:** the ruled row "primary EMPTY, backup listing FAILS: THROW" has no test, yet it is production's state now (the primary lists empty). Replacing `ingest.ts:153`'s throw with a no-frame return keeps all 14 tests and all 3 sabotages green | none to the code (it is right); a test, and a plant | **FB4d:** primary empty, the backup's listing fails every attempt: throws the backup's error, 0 GETs, nothing written. **S-EMPTYFAIL** (the :153 throw replaced with a no-frame return): red exactly **{FB4d}** |
| **N1:** a thrown check's log line never names its host (`ingest-worker.ts:32-36` logs `err` and `attempts`), so the :153 throw reads exactly like the old primary-list error | every OPERA request's label names its host: `OPERA primary list`, `OPERA backup list`, `OPERA primary get`, `OPERA backup get`. The combined throws keep both texts | **ERR1:** four messages, one per path. FB4c's throw begins `OPERA primary list` and names "the backup listed empty". FB4b's holds `OPERA backup list`. FB5's begins `OPERA primary get`. A backup read failing (primary empty, backup lists, backup reads fail) begins `OPERA backup get`. ERR1 never uses FB4d's scenario, so S-EMPTYFAIL turns exactly {FB4d} |
| **N3:** FB3 and FB4b are loose; UA1 covers FB1-FB3 only | FB3 asserts `primaryListError` begins `OPERA primary list`. FB4b asserts both hosts' texts. FB4c asserts `OPERA primary list` (its old `OPERA list` text changes with N1). **UA1 extends to every scenario** (FB1-FB7, FB4b-d, and ERR1's backup-read case) | FB3, FB4b, FB4c tightened; UA1 widened |
| **N4:** `OPERA_HOST[source ?? "primary"]` (`ingest.ts:327`) is unreachable but a silent default | an OPERA check that reaches a read without a source THROWS (`OPERA check without a source`) | no row: unreachable by construction (`operaListing` returns a source with every non-empty listing). It becomes a refusal instead of a default |
| **N6:** smoke's per-product line (`wx-products-lines.sh`) does not print `source`, and it is the reader people look at | OPERA lines (id beginning `op`: every OPERA product and no NOAA one, per `products.ts`) end ` source=<primary\|backup\|null\|->`. `-` is a Worker before the field, the script's convention. NOAA lines are unchanged | `wx-products-lines.selftest.sh`: cases (a) and (b) gain ` source=-` on their ten OPERA lines (the committed fixture predates the field). **New case (g):** opuk `backup`, opis `primary`, opib `null`, cref with no source key. Every case is still a whole-output compare |

**Red-first predictions** (16 tests: the 14 plus FB4d and ERR1):
- **against main's code** (`6f6dae93`'s `ingest.ts` and `handler.ts` planted from a byte copy, then restored with sha256
  equal): red exactly {FB1, FB2, FB3, FB4, FB4b, FB4c, FB4d, FB6, FB7, FB7 (lagging), HZ1, ERR1, UA2, UA3}; green {FB5,
  UA1};
- **against `cdb19df1`'s code:** red exactly {FB3, FB4b, FB4c, FB4d, ERR1}; green the other 11.
  - **Corrected 09:34Z, before any run:** the first draft had FB4d green here. But FB4d asserted N1's text (`OPERA
    backup list`), which `cdb19df1` cannot produce; its throw already happens there.
  - **READ 09:34:49Z, as corrected:** 5 failed {FB3, FB4b, FB4c, FB4d, ERR1}, 11 passed.
  - **Then RULED (the reviewer, received by 09:35Z): one property per row.** FB4d holds two (B1's throw, and N1's
    prefix). The prefix lives in ERR1 only; FB4d asserts "it throws (any OPERA error), 0 GETs, nothing written".
  - **RE-REGISTERED for the two runs, before either:**
    - **against `cdb19df1`:** red exactly {FB3, FB4b, FB4c, ERR1}, 12 green, FB4d among them;
    - **against main's code:** red exactly {FB1, FB2, FB3, FB4, FB4b, FB4c, FB4d, FB6, FB7, FB7 (lagging), HZ1, ERR1,
      UA2, UA3}, green {FB5, UA1}. FB4d's red there is B1's own: main returns no-frame;
    - **under S-EMPTYFAIL:** red exactly {FB4d}.
- **the smoke selftest against today's script:** red on cases (a), (b), **(c)** and (g); green on (d), (f), (f2) and (e).
  **Corrected 09:38Z, before the run:** the first draft had (c) green. But (c)'s body (a Worker before the health
  fields) holds `opuk`, so its line gains ` source=-` too.

**After:** 16 of 16. The smoke selftest PASSES. Every `wx-*` file passes. `tsc` exit 0.

**Sabotages, per-test sets (N2's amendment: every set names its tests, never a row):**

| plant | red exactly |
|---|---|
| S-MIX | {FB1, FB2, FB3, FB5, FB7, ERR1}. ERR1 joins, because its read-failure cases stop throwing when reads go to the other host. FB7 (lagging) stays green |
| S-FALLREAD | {FB5, ERR1}. ERR1 joins: FB5's scenario no longer throws |
| S-SRC | {FB1, FB2, FB3, FB6, FB7, FB7 (lagging)} |
| S-EMPTYFAIL | {FB4d} |

**N2, recorded as OFF:**
- 2026-11-14 09:07Z: S-SRC read {FB1, FB2, FB3, FB6, FB7, FB7 (lagging)}. Section 3 registers {FB1, FB2, FB3, FB6, FB7}.
- The driver's "FB7 is the row" reading was frozen at ~09:04Z, AFTER the code existed (`a9f06a2b`). So it is not a
  pre-code registration, and the run is OFF against section 3. No re-grade.
- **The explanation:** section 3's "FB7" meant two tests, FB7 and its lagging case, and S-MIX's "FB7" meant one. That
  is why every set above now names tests.
- It is this row's first OFF.

**N5 (no code):**
- `proxy/scripts/wx-opuk-measure.ts:42` and `tools/wx/opera_fixture.py:30` still read the PRIMARY only. They are
  measurement and fixture tools, not the ingest, and they are correct for the frames they were built against.
- `docs/wx-opuk.md:77` quotes the old User-Agent as a historical record, and is left as it is.

**N7, two hazards beyond the registered lag:**
- **A primary holding one old frame keeps the backup off for up to 20 min.** "Non-empty" means a frame key inside the
  20-min lookback. A primary that publishes once and stops is chosen until that frame leaves the window, and those checks
  read `already-stored`.
- **A hanging primary costs up to 27 s before the backup is tried:** three 8 s list attempts, with 1 s and 2 s waits.
  Across midnight it is 54 s, because two day prefixes are listed.
- Both are stated, not graded.

**CI-F4 (the push of these fixes):** predicted **36 = 34 success + 2 skipped + 0 other**, the same job set as CI-F3,
whose read this waits for.
