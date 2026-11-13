# The account budget: every product's share of every Cloudflare allowance (registration, predictions first, 2026-11-12)

**For:** the reviewer. **Status:** the design and its predictions, frozen before the data file, the checker or any
read for it exists. The build lands as a further commit to this PR, with its readings.

## 0. The ruling

The owner, 2026-11-12 17:19Z, relayed by the reviewer at 17:19:58Z: "Missileer's stream: no change, and the US-ingest
change stays shelved. The whole-account rule stands, made checkable." His four points, verbatim:

> 1. One account-wide budget file lists each product's measured share of every free allowance (R2 writes and reads,
>    KV reads and writes, Workers CPU and requests).
> 2. Every cost estimate adds its line there and states the account total.
> 3. The digest flags any allowance over 80% (today: R2 reads, 9.4 M of 10 M with OPERA and US radar).
> 4. When convenient, read-only: how often anyone listens to 'Listen live' (playlist requests per hour), so on-demand
>    streaming can be judged with numbers later.

**Why it exists.** Two repos each budgeted the same per-account free tier as their own. valar-eam-feed's
`station/live/README.md` counted Missileer at 81.2% of R2's free 1 M Class A, and says "Other Class A users on the
account that this repo cannot see (for example the Blipscope Worker) are not counted." Weather was budgeted the same way
from this side. Together they cross the 1 M, which Cloudflare bills as a whole second million ("Billable unit
rounding", R2's pricing page).

## 1. What gets built (the next commit)

| file | what |
|---|---|
| `docs/account-budget.json` | the data: allowances, reads, products, rows, scenarios (section 2) |
| `scripts/account_budget.py` | the checker (section 3). `--selftest` runs its controls (section 4). `--render` writes section 6's table, and `--render-check` refuses a stale one |
| `tools/budget/budget_read.py` | the read: one window, every billed line, attributed to products (section 2). Read-only, counts only, token from the token file, never printed |
| `tools/budget/build_budget.py` | assembles the data file from the reads of section 5.1, the ten's derived running rows, `shsr`'s projected rows and the derived Logs rows |
| `tools/budget/sabotage_account_budget.py` | red first: plants S1-S6 in a copy of the checker, each asserted to apply, and requires each control to fail with exactly its predicted set |
| `.github/workflows/account-budget.yml` | CI on any change to the paths above: `--selftest`, the sabotage driver, then the checker (it prints the flags into the log), then `--render-check` |
| this doc | section 6 gains the rendered table and the readings |

## 2. The data

**Allowances** (Workers Paid; the prices are `tools/wx/account_cost_read.py`'s `PRICE`, from Cloudflare's docs, read
2026-11-11):
- **Required by the ruling:** R2 Class A (writes), R2 Class B (reads), KV reads, KV writes, Workers CPU, Workers requests.
- **Also carried, since they are billed:** Workers Logs events, R2 storage, KV list and delete, Durable Object
  requests and GB-s.
- Each allowance carries its included amount, its price, and whether it rounds up to a whole unit (Read A: R2 and DO,
  as published).

**Products,** each with its source in the account's analytics:

| product | Workers script(s) | R2 bucket(s) | KV namespace |
|---|---|---|---|
| Missileer live stream | (none: an external S3 client, the station) | `missileer-live` | |
| Missileer clips and public export | (none) | `missileer-clips` | |
| Blipscope proxy | `blipscope-proxy` | its reads of `blipscope-wx` (see below), `blipscope-basemap` | production `ENRICH_KV` |
| proxy staging | `blipscope-proxy-staging` | | staging's namespace |
| US radar ingest (`cref`) | `blipscope-wx-ingest` | its share of `blipscope-wx` | |
| Europe radar ingests (the ten) | `blipscope-wx-ingest-op*` | their share of `blipscope-wx` | |
| basemap builder (staging) | `blipscope-basemap-staging` | `blipscope-basemap-pkg-staging` | |
| dev ingest (parked) | `blipscope-wx-ingest-dev` | `blipscope-wx-dev` | |
| kiosk site and app | `valarscopes-app`, `valarscopes-site` | | |
| US radar SeamlessHSR (`shsr`) | when built (projected until then) | its share of `blipscope-wx` | |
| anything else | every script, bucket or namespace the read finds that no row above names | | |

**`blipscope-wx` is shared by three writers and readers.** R2's analytics give it by bucket and operation type, not
by caller. The split is by hour:
- In OPERA's silence the bucket's Class A is the US ingest's alone (the proxy never writes it).
- With OPERA running, the extra is the ten's.
- Class B (reads) in silence is the US ingest plus the proxy's `/wx` serving, which R2's analytics cannot separate. That
  row is named for both, never assigned to one.

**States and scenarios.** Most products have one state ("now"). The ten have `silent` and `running`; `shsr` has
`absent` and `built`. A scenario picks one state for each:
- "today": the ten silent, `shsr` absent;
- "OPERA running": the ten running, `shsr` absent;
- "OPERA running + shsr": the ten running, `shsr` built.

**Rows:** product, state, allowance, value a month, `basis` (measured, derived or projected), and the read it came
from. **Reads:** each names its tool, its window, and the account's own total for every allowance in that window.

## 3. The checker

For each scenario and each allowance, it prints:
- the sum and the included amount, and the percentage;
- the cost under Read A;
- **`FLAG`** at 80% or more, and **`OVER`** past 100%;
- each scenario's total monthly overage.

**The anchor:** for every read, the rows taken from it must sum to that read's account total for each allowance,
within 1%. A product missing from the attribution, or one counted twice, fails it.

**Exit codes:**
- 0: the file is valid, and the flags are printed (a flag is the digest's input, not a CI failure);
- 2: a malformed file (an unknown allowance or product, a row without its read, a duplicate) or a failed anchor;
- 1: a failed `--selftest` control, or a stale render.

## 4. Its controls (`--selftest`), each required to behave before the real file is believed

| control | planted | must |
|---|---|---|
| P1 | a row adding 1.0 M KV reads to "today" (72% to about 82%) | print `FLAG` on KV reads |
| P2 | every row scaled to a third | print no `FLAG` anywhere, so the flag is not always on |
| P3 | a row naming an allowance that does not exist | exit 2 |
| P4 | the Missileer live stream's rows removed | exit 2 on the anchor (Class A and Class B) |
| P5 | section 6's table edited by one digit | `--render-check` exits 1 |

## 5. Predictions for the real file (frozen now, before `budget_read.py` exists or has run)

**The window for "today":** 24 hours wholly inside OPERA's silence, ending at a whole hour after 21:00Z on 11-12, x 30.
The ten's `running` rows come from 2026-11-11 17:00-20:00Z, the only whole hours they all ran. `shsr`'s rows are
`cref`'s measured rows, labelled projected.

| allowance | today | OPERA running | OPERA running + shsr |
|---|---|---|---|
| Workers CPU (30 M) | **FLAG, OVER**: 105-135% | **FLAG, OVER**: 210-245% | **FLAG, OVER**: 280-335% |
| R2 Class A (1 M) | **FLAG**: 88-97% | **FLAG, OVER**: 105-115% (Read A $4.50) | **FLAG, OVER**: 118-128% (Read A $4.50) |
| R2 Class B (10 M) | 30-50% | **FLAG**: 85-99% | **FLAG**: 89-103%. Past 10 M is Read A $0.36 |
| KV reads (10 M) | 60-80% (no flag; at 72% on 11-12's 7-day read) | same | same |
| KV writes (1 M) | 50-70% | same | same |
| requests (10 M) | 20-35% | same | same |
| Logs events (20 M) | 30-50% | same | same |
| R2 storage, DO | under 50% each | same | same |

- **The total overage under Read A:** today $0.05-$0.40, which is CPU only; OPERA running $5.15-$5.80; OPERA running +
  `shsr` $5.55-$6.40. All of these are inside the $10 allowance. "With OPERA and US radar" in the owner's point 3 is the
  third column.
- **The anchor:** "today"'s rows sum to the read's account totals within 1% on every allowance.
- **The Class B note:** a first look (11-12, 17-20Z against 00-16Z) suggests that the ten's running Class B is larger
  than M6's projected 4.30 M. That is why the running column's Class B band is wide.
- **Outside this table, which stops and is reported:**
  - a scenario's flags differ from the bold cells;
  - the anchor fails;
  - a script, bucket or namespace appears that section 2 does not name;
  - any control in section 4 does not behave.

### 5.1 The window, changed before the graded read (ruled 2026-11-12 17:38:35Z)

**What prompted it: an ungraded dry run.** `budget_read.py` was run on the 24 hours ending 17:00Z on 11-12, to test
its mechanics, not to grade. That window still held OPERA's last running hours. It showed that a 24-hour window
misreads bursty lines:
- **KV writes:** 50,250 a month at that day's rate, against 608,177 on the 7-day read. A weekly or occasional job
  dominates them.
- **KV reads:** 8.37 M a month (84%) at that day's rate, against 7.16 M (72%) on the 7-day read.

**The ruling, as registered here:**
- **"today" is the 7-day run rate (x 30/7),** the basis of the monthly bill and of `tools/wx/account_cost_read.py`.
- **Only the OPERA-dependent rows** (the ten's scripts, and `blipscope-wx`'s Class A and B) come from the silent hours
  inside it, from 2026-11-11 21:00Z. That is a separate read, anchored on its own.
- **In each read, the groups the other read supplies are kept as rows in state `excluded`,** which no scenario sums. So
  each read still anchors against its own independent totals.
- **The section 5 bands stay frozen exactly as written,** and are graded on this window. Any miss is recorded as mine.
- **The 24-hour rate is printed beside every allowance,** labelled "24 h rate (observation, not graded)". It comes from
  a read of the 24 hours ending at the graded read's hour, and is stored as totals only, so no scenario can sum it. The
  burstiness stays visible instead of being averaged away.
- **One control is added, P6:** a row in state `excluded`, and an observation read with totals far past every
  allowance, change no flag and no sum.
- **The anchor's totals come from separate ungrouped queries** (no script, bucket or namespace dimension). The rows
  therefore cannot vouch for themselves: a truncated grouped query, or a group the mapping skipped, shows as a gap. The
  dry run matched them to 0.000% on all seven anchored allowances. Storage and DO have no coarser total and are not
  anchored.

### 5.2 December's Workers Logs: bytes per event (registered 2026-11-12 ~18:15Z, before its read)

**Why.** From 2026-12-01, Workers Logs is billed by the gigabyte: ingest (50 GB a month included, then $0.25 a GB) and
storage (10 GB included, then $0.10). That replaces November's price per event. So the Logs line needs bytes per event.
`docs/wx-account-cost.md` measured it from the telemetry API's JSON per event, one hour each: **1,714 B on 11-11 and
1,650 B on 11-12.**

**What this read adds:**
- **Three whole hours at different times of day.** Each must come back unsampled (ABR 1), or it is refused, never
  graded. They are the 21:00Z, 03:00Z and 09:00Z hours after this registration.
- **Per service:** events per invocation, and bytes per event.
- **The tool:** `tools/budget/logs_bytes.py`. It is read-only, counts and sizes only, never prints an event, and reads
  its token from the token file.

**What it cannot see, stated now.** Cloudflare bills "the event body, the default enriched attributes, and custom
attributes, pre-compression". The API's JSON is the closest available measure: it carries `$metadata` keys and JSON
punctuation, which may not match the billed bytes. Only December's dashboard or invoice can settle that. The figures
below are labelled "API bytes".

**Predictions:**
- **Bytes per event, overall, in each of the three hours:** 1,500-1,850.
- **The proxy's:** 1,500-1,900.
- **The ingests':** 800-2,500, since no per-service read exists.
- **Events per invocation:** the proxy 2.9-3.4; the ingests 2.5-3.5 (11-11 read 3.12 and 3.00).
- **December's Logs at today's fleet,** with events from the budget's "today" (about 8.3 M a month):
  - ingest **12-16 GB** a month (24-32% of 50), no flag;
  - storage, at 7 days' retention (x 7/30), **2.8-3.7 GB** (28-37% of 10), no flag;
  - **$0** in every scenario.
- **Outside these bands:** stop and report. A sampled hour is refused and replaced by the next hour of the same
  time-of-day slot.
- **The checker is not changed by this.** December's GB allowances join the data file in their own change, registered
  before 12-01.

### 5.3 The three hours, read: stopped at the third, one figure outside its band

`tools/budget/logs_bytes.py`, the 21:00Z, 03:00Z and 09:00Z hours, each read at five past the next hour.

| hour (Z) | ABR | pages | proxy B/event | ingests B/event | overall B/event | events/inv, proxy | events/inv, ingests | ingests' share of events |
|---|---|---|---|---|---|---|---|---|
| 11-12 21:00 | 1 | 9 | 1,744.9 | 818.2 | 1,638.9 | 3.25 | 3.03 | 11.44% |
| 11-13 03:00 | 1 | 6 | 1,765.9 | 814.0 | 1,587.0 | 3.13 | 3.01 | 18.79% |
| 11-13 09:00 | 1 | 5 | 1,717.0 | 826.2 | **1,496.0: OFF** | 3.29 | 3.01 | 24.81% |
| band (5.2) | 1 | | 1,500-1,900 | 800-2,500 | 1,500-1,850 | 2.9-3.4 | 2.5-3.5 | not registered |

Hour 3's overall figure is 4.0 B per event under the band's foot. Every other figure in all three hours is inside its
band. As 5.2 requires, the read stopped there and was reported. December's projection has not been computed.

**Cause: the mix of the two services, which the overall band did not model.** The overall figure is the
event-weighted mean of two services whose bytes per event differ by about a factor of two. The ingests run on a
schedule, so their events per hour hardly move (1,978, 2,160, 2,160). The proxy's events follow the fleet's traffic
(15,305, 9,334, 6,545). At 09Z the ingests are 24.81% of events. At that hour's per-service figures, the overall
figure drops below 1,500 once their share passes 24.36%. The band came from two single hours of the overall figure
(1,714 and 1,650), with no share attached.

**The alternatives, and what excludes each:**
- **The proxy's events got smaller.** Excluded: 1,717.0 is inside its band and within 2.8% of the other two hours.
- **The read lost pages.** Excluded on three counts:
  - In every hour the page count fits the event count at 2,000 a page (9, 6, 5).
  - Every hour is unsampled.
  - Events per invocation, which checks events against an independent count (the analytics API's invocations), is
    in band for both services. A lost page would lower it, and the proxy's 3.29 is the highest of the three hours.
- **A third script pulled the figure down.** Hour 3's "all" row has 2 events and 1 invocation more than the proxy
  and the ingests together. They come from a script that is neither. The tool prints groups, not scripts, so it is
  not named here; naming it means re-reading the hour, which waits for the ruling. Excluded as the cause: the proxy
  and the ingests alone give 1,495.96.

**Proposed fix, for the ruling.** Grade hours by service, and drop the per-hour overall band. The per-service bands
held in all three hours. The overall figure moves with the time of day by construction. The projection needs the
month's mix, not an hour's: today's budget has the ingests at 18.14% of the month's events (cref, the ten silent,
wx-dev: 1,518,231 of 8,368,065) and the proxy at 78.42%. The other 3.44% (kiosk, proxy staging, basemap staging:
287,763) is on scripts that none of the three hours measured.

**Ruled by the reviewer, 2026-11-13 (after 10:12Z):**
- Hour 3's overall figure stays **OFF on the record** (1,496.0 against 1,500-1,850). It is not re-graded.
- **From now on, Logs-bytes hours are graded by service:** the proxy's and the ingests' bytes per event and events per
  invocation, each against its own band. The per-hour overall band is dropped, because it assumed a mix that an hour
  cannot hold.
- The third script is not named. Its 2 events do not move the verdict, and a re-read just to name it is not worth it.

### 5.4 December's projection: the method, frozen before its arithmetic (frozen in 8cfc6477 before any figure; approved by the reviewer 2026-11-13)

Both methods are computed and reported. Neither is picked after the figures are seen.
- **A, as 5.2 reads:** the mean of the three hours' overall bytes per event x 8,368,065 events a month.
- **B, by service:** three products, each from its own measured mean:
  - the proxy's three-hour mean x 6,562,071;
  - the ingests' three-hour mean x 1,518,231;
  - the 287,763 unmeasured events at the proxy's mean. That is the larger of the two figures, so the error is on the
    high side.
- **The figure quoted as December's estimate is B,** because it follows the month's mix and not three hours' mix.
  This choice is frozen here, before either figure exists.
- **Units and scenarios:**
  - GB means 10^9 bytes, the unit 5.2's own band arithmetic uses (8.3 M x 1,500-1,850 B = 12.5-15.4 GB).
  - Storage is ingest x 7/30.
  - The OPERA-running and OPERA + shsr scenarios use the same method on their own events.
- **Bands, unchanged from 5.2:** ingest 12-16 GB and storage 2.8-3.7 GB at today's fleet; $0 in every scenario.

**Outcomes:**

| outcome | meaning |
|---|---|
| A and B both inside both bands, $0 everywhere | as predicted |
| one of A and B inside, the other outside | both are reported, then stop |
| either one outside a band, or any scenario not $0 | stop and report |
| none of the above (for example, the budget's events change before the arithmetic) | the inputs moved; stop and decide |

### 5.5 December's projection, computed: as predicted

**Means of the three hours:** overall 1,573.97 B per event (A); proxy 1,742.60 and ingests 819.47 (B).

**Events:** from `docs/account-budget.json` at main b7184243, the same 8,368,065 that 5.4 froze. shsr runs as
`blipscope-wx-ingest-shsr`, one of the ingests, so in B its 137,106 events take the ingests' mean.

| scenario | events a month | of them, ingests | A: ingest GB | A: storage GB | **B: ingest GB** | **B: storage GB** | $ |
|---|---|---|---|---|---|---|---|
| today (the ten silent) | 8,368,065 | 1,518,231 | 13.17 | 3.07 | **13.18** (26.4% of 50) | **3.08** (30.8% of 10) | 0 |
| OPERA running | 8,358,635 | 1,508,801 | 13.16 | 3.07 | **13.17** (26.3% of 50) | **3.07** (30.7% of 10) | 0 |
| OPERA running + shsr | 8,495,741 | 1,645,907 | 13.37 | 3.12 | **13.29** (26.6% of 50) | **3.10** (31.0% of 10) | 0 |

**Outcome: the first row of 5.4, as predicted.**
- At today's fleet, A and B are both inside both bands: ingest 12-16 GB, storage 2.8-3.7 GB.
- Every scenario costs $0.
- A and B differ by at most 0.09 GB, because the month's ingest share (18.14%) sits close to the three hours' average.

**December's estimate (B): 13.2 GB ingested and 3.1 GB stored a month at today's fleet, $0.**

**What this still cannot see (from 5.2).** These are API bytes. The billed bytes may differ, and only December's
dashboard or invoice settles that. December's GB allowances join `docs/account-budget.json` in their own change,
registered before 12-01.

## 6. The table (rendered by the checker, from the next commit)

<!-- account-budget:begin (rendered by scripts/account_budget.py --render; do not edit) -->
| allowance | included | today | OPERA running | OPERA running + shsr | 24 h rate (observation, not graded) |
|---|---|---|---|---|---|
| R2 Class A (writes) | 1.000 M | 0.854 M (85%) **FLAG** | 1.027 M (103%) **OVER**, $4.50 | 1.159 M (116%) **OVER**, $4.50 | 0.931 M (93%) |
| R2 Class B (reads) | 10.000 M | 4.118 M (41%) | 9.488 M (95%) **FLAG** | 9.920 M (99%) **FLAG** | 4.474 M (45%) |
| R2 storage (GB-month) | 10.00 | 3.46 (35%) | 3.46 (35%) | 3.86 (39%) | not read |
| KV reads | 10.000 M | 7.226 M (72%) | 7.226 M (72%) | 7.226 M (72%) | 8.295 M (83%) |
| KV writes | 1.000 M | 0.608 M (61%) | 0.608 M (61%) | 0.608 M (61%) | 0.047 M (5%) |
| KV lists | 1.000 M | 0.005 M (1%) | 0.005 M (1%) | 0.005 M (1%) | 0.006 M (1%) |
| KV deletes | 1.000 M | 0.000 M (0%) | 0.000 M (0%) | 0.000 M (0%) | not read |
| Workers CPU (ms) | 30.000 M | 35.917 M (120%) **OVER**, $0.12 | 68.158 M (227%) **OVER**, $0.76 | 94.564 M (315%) **OVER**, $1.29 | 32.797 M (109%) |
| Workers requests | 10.000 M | 2.636 M (26%) | 2.633 M (26%) | 2.676 M (27%) | 2.805 M (28%) |
| Workers Logs events (November prices) | 20.000 M | 8.368 M (42%) | 8.359 M (42%) | 8.496 M (42%) | not read |
| Durable Object requests | 1.000 M | 0.400 M (40%) | 0.400 M (40%) | 0.400 M (40%) | not read |
| Durable Object GB-s | 0.400 M | 0.001 M (0%) | 0.001 M (0%) | 0.001 M (0%) | not read |
| **overage a month (Read A)** | | **$0.12** | **$5.26** | **$5.79** | |
<!-- account-budget:end -->

### 6.1 The graded read (2026-11-12, window ending 21:00:00Z, run 21:05:12-27Z)

**The reads, as section 5.1 registered them:**
- `r-7d` (2026-11-05 21:00Z to 11-12 21:00Z, take "rest");
- `r-silent` (2026-11-11 21:00Z to 11-12 21:00Z, take "opera");
- `r-run` (2026-11-11 17:00-20:00Z; the ten's running rows derived);
- `r-24h`, the observation (the same 24 hours as `r-silent`).

`build_budget.py` wrote 93 rows from them. No unattributed script, bucket or namespace appeared. **Both anchored reads
held,** so the checker exited 0.

**Graded against section 5, frozen before any read:**

| allowance | today | OPERA running | OPERA running + shsr |
|---|---|---|---|
| Workers CPU | 120% OVER, in 105-135: **as predicted** | 227% OVER, in 210-245: **as predicted** | 315% OVER, in 280-335: **as predicted** |
| R2 Class A | FLAG as predicted, but **85.4%, under 88-97: OFF** | OVER as predicted, but **102.7%, under 105-115: OFF** | OVER as predicted, but **115.9%, under 118-128: OFF** |
| R2 Class B | 41%, in 30-50: **as predicted** | 94.9% FLAG, in 85-99: **as predicted** | 99.2% FLAG, in 89-103: **as predicted** |
| KV reads / KV writes | 72% / 61%, in 60-80 / 50-70: **as predicted** | same | same |
| requests / Logs | 26% / 42%, in 20-35 / 30-50: **as predicted** | same | same |
| R2 storage, DO | 35% / 40%, under 50: **as predicted** | same | 39% / 40%: **as predicted** |
| overage (Read A) | $0.12, in $0.05-$0.40: **as predicted** | $5.26, in $5.15-$5.80: **as predicted** | $5.79, in $5.55-$6.40: **as predicted** |

- **The flags are exactly the predicted set in every scenario,** and the overage is inside its band in every scenario.
- **The three misses are mine, and they share one cause.** Class A's bands were built from S3-7's 24 hours ending
  16:24Z, while the ruled window is 7 days. Those 7 days hold Missileer's dip on 11-08 to 11-10 (15,409, 22,878 and
  22,974 a day, against about 26,000 otherwise). That lowers `missileer-live`'s 7-day rate, and with it every
  scenario's Class A, by about 2 points.
  - The 24-hour observation reads Class A at 93%, inside the "today" band, which is consistent with that cause.
  - The ungraded dry run at 17:00Z had already shown it, and it was reported then. The bands stayed frozen, as ruled.
- **What the 24-hour observation shows that the graded columns average away:** KV reads at **83%** on the last day,
  against 72% over the week; KV writes at 5% on the day, against 61% over the week; Class A at 93% against 85%.
- **For the digest,** the checker's own line:
  `FLAGS (80% or more): today: cpu_ms OVER, r2_class_a FLAG; OPERA running: cpu_ms OVER, r2_class_a OVER, r2_class_b FLAG; OPERA running + shsr: cpu_ms OVER, r2_class_a OVER, r2_class_b FLAG`

## 7. Point 4, the listener read (after the budget file)

Read-only, when convenient. The measure is requests per hour for `kph/live.m3u8`, the playlist every listener reloads
about every 10 s. That needs per-object or per-path counts. R2's analytics, as read so far, give bucket and operation
type only, and the edge caches the playlist for 2 s (`public, max-age=2`), so R2 sees at most one fetch per data
centre every 2 s, however many listen. The read will say plainly what it cannot see; a zone's HTTP analytics for
`live.missileer.watch`, if the token can read them, is the first candidate.

### 7.1 Registered 2026-11-12 ~21:55Z, before any of it is read

**What changed since section 7 was written.** A schema check (an introspection query, which reads no data) shows that
R2's analytics DO carry `objectName`, as a dimension and as a filter (`objectName_ilike`), and also `eyeballRegion`.
So per-object counts of what reaches R2 are readable. The zone's HTTP analytics are not needed for a first answer.

**Which object measures what,** from valar-eam-feed's `station/live/README.md` (read-only, at `cff6c038`):

| object | Cache-Control | who fetches it | so R2's count measures |
|---|---|---|---|
| `kph/state.json` | `no-cache, max-age=0` | the site, for every visitor (the switch); the streamer, once a segment | visitors' polls plus the streamer's 360 an hour: **not listeners** |
| `kph/voice.json` | `max-age=2` | the site, for every visitor (the voice indicator) | visitors: **not listeners** |
| `kph/live.m3u8` | `max-age=2` | every visitor's waterfall (it names the `.wf` files) and every player | visitors: **not listeners** |
| `kph/<seg>.wf` | immutable, a year | the site's waterfall, "for every visitor" | visitors, once per data centre: **not listeners** |
| `kph/<seg>.ts` | immutable, a year | **only a playing listener** | **active listener data centres:** each fetches each 10 s segment once, so up to 360 an hour per data centre |

**The measure, then: `.ts` GETs per hour, divided by 360, is the number of data centres with at least one listener
in that hour.** It is a lower bound on listeners, with three things it cannot see, stated now:
- several listeners served by one data centre count once;
- if Cloudflare's tiered cache is on for that zone, an upper tier can serve several data centres with one R2 fetch, so
  even data centres are undercounted (whether it is on is not readable with this token);
- a listener who joins mid-hour counts as a fraction of an hour.

`eyeballRegion` per hour adds a count of distinct regions with a listener.

**The tool:** `tools/budget/live_listeners.py`. Read-only, counts only. Per hour for 7 days, the `missileer-live`
GetObject counts split by object class (`.ts`, `.wf`, `live.m3u8`, `voice.json`, `state.json`, other), plus the
`.ts` regions. Its control: the classes must sum to the bucket's own GetObject count for each hour, within 1%, read by
a separate query with no `objectName` dimension. Otherwise it exits 2.

**Outcomes, registered before the read:**

| outcome | what the `.ts` hours show | meaning for on-demand streaming (M1) |
|---|---|---|
| **L0** | `.ts` GETs are 0 in at least 80% of hours | nobody listens most of the time: M1 would save most of the 0.778 M |
| **L1** | `.ts` GETs are above 0 in at least 80% of hours | someone listens nearly always: M1 would save little |
| **L2** | anything between: sporadic listening | M1 saves about the listener-free share of hours, which is reported |
| **L3** | the control fails, or `objectName` returns no `.ts` rows while the bucket's GetObject count is non-zero | the instrument cannot see per-object counts here: stop and report |

**My prediction: L2.** The basis: the bucket's non-streamer GETs average about 3,500-3,900 an hour, but the visitor
objects above can account for all of that, so the bucket total does not separate the outcomes. I hold L2 at low
confidence; L0 and L1 are both live.

**CI for this registration (predicted before the push):** it changes `docs/account-budget.md`, which is in
`account-budget.yml`'s path filter, and leaves section 6's table alone, so `--render-check` stays current. Main is
`f3dd6b62`. **Predicted: 33 = 31/2/0.**

### 7.2 Read 2026-11-12 21:54:57Z: the 7 days ending 21:00Z. Outcome L1, against my L2

`python3 -I tools/budget/live_listeners.py 2026-11-12T21:00:00Z 7` (one query per day and per class).

**The control held in all 168 hours:** the six classes, each counted by its own filter, `other` included (an AND of
`objectName_notlike`), summed to the bucket's unfiltered GetObject count within 1% every hour.

**L1 (registered: `.ts` above 0 in at least 80% of hours): `.ts` reached R2 in 151 of 168 hours (90%).** My
prediction was L2, so the prediction is OFF. Its stated basis, that the bucket total could not separate the outcomes,
held; the outcome simply fell on L1.

| reading | value |
|---|---|
| listener data-centre-hours | 148.8 in 7 days, 21.3 a day |
| by UTC day | 11-05 1.6 (the window starts at 21:00Z), 11-06 30.9, 11-07 33.0, 11-08 17.0, 11-09 17.3, 11-10 11.0, 11-11 9.8, 11-12 28.1 |
| `.ts` an hour | min 0, median 362, max 724 |
| hours with 0 / 1-179 / 180-359 / 360-379 / 380-719 / 720 or more | 17 / 39 / 18 / 26 / 56 / 12 |
| mean `.ts` by UTC hour | 207-471, highest around 08-12Z and 19-20Z |
| eyeball regions with a `.ts` fetch, per hour | **always exactly one region.** No hour shows two |

**What it means, and the alternative it cannot exclude:**
- **Something fetches the live audio nearly every hour, always from one region.** In 40% of hours there is more than
  one data centre's worth (over 380 an hour), which means a second cache or player in the same region.
- **The alternative is an automated fetcher in that region,** such as a monitor, a kiosk, or something on the
  station's own side that plays or checks the stream. R2's analytics cannot exclude it. The day-to-day spread (9.8 to
  33 data-centre-hours) argues against one constant fetcher being all of it. A constant fetcher plus some people fits
  the data as well as people alone.
- **What would separate them:**
  - the zone's HTTP analytics for `live.missileer.watch` (clients and user agents, by count), which needs a zone-read
    token this machine does not hold;
  - or a site-side "playing" beacon, which is not read-only;
  - or the owner, who may simply know whether anything of his plays the stream continuously.
- **For on-demand streaming (M1):**
  - if these fetches are real listeners, M1 saves at most the listener-free share, about 10% of hours, roughly 0.08 M
    Class A a month;
  - if most are an automated fetcher that could be stopped, it saves far more.
  - The region's name is not printed here, by the location rule.

## 8. CI for this registration (predicted before the push)

**Predicted: 32 check runs: 29 success, 3 skipped, 0 failed.**
- It changes this doc only, which is in no workflow's path filter.
- **The baseline** is #667's head run (docs only): 32 = 29/3/0.
- **The build commit adds `account-budget.yml`,** whose one job runs when its paths change. That commit is predicted at
  **33 = 30/3/0**, registered again before its push.
- **Re-predicted 2026-11-12 19:07Z, before the build's push, for a job-set change on main:** #675 (main `099f1946`)
  makes `ship-alert` run report-only on every PR. A docs-only PR is now **32 = 30/2/0** (the reviewer's announcement,
  19:06:30Z). So:
  - **the build commit: 33 = 31/2/0;**
  - every head already pushed (`86a7930b` to `6dca0e6d`) finished under the old set at 32 = 29/3/0, as predicted then.
