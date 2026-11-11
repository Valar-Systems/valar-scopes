# The whole account's monthly cost, before any of the ten OPERA ingests deploys (predictions first, 2026-11-11)

**For:** Daniel, through the reviewer chat. **Status:** predictions, frozen before the read. **Nothing deploys.**

## The rulings, verbatim

~23:23Z on 2026-11-07 (`docs/wx-opera-code-stage.md`):

> Before any production deploy of the ten, the whole account's monthly cost goes to Daniel, with weather on every board
> and the Weatherscope added, since the $2 no longer covers it all.

~05:06Z on 2026-11-11: "Next: the cost write-up, predictions first, as you planned."

## 1. What the account is billed for (Cloudflare's docs, read 2026-11-11)

Every earlier cost record here (`docs/wx-cost.md`, `docs/wx-opera-europe.md` §4, `docs/wx-opera-dev-measure.md` M6,
`docs/weatherscope-stage4.md`) priced three lines: Workers CPU, R2 Class A and R2 Class B, plus requests in
`wx-cost.md`. The account uses more than that.
- The proxy binds KV, R2, Analytics Engine and a Durable Object.
- The dashboard binds KV.
- **Every Worker but the basemap's production env has Workers Logs on at a sampling rate of 1.**

| line | included a month (Workers Paid) | over that |
|---|---|---|
| **subscription** | | **$5.00 a month**, whatever the usage |
| Workers requests (crons count) | 10 M | $0.30 / M |
| Workers CPU | 30 M ms | $0.02 / M ms |
| **Workers Logs, until 2026-11-30** | 20 M events (an invocation is 1, plus 1 per `console.log`) | $0.60 / M |
| **Workers Logs, from 2026-12-01** (Cloudflare Observability pricing) | 50 GB ingested; 10 GB-month stored | $0.25 / GB; $0.10 / GB-month |
| R2 Class A | 1 M | $4.50 / M |
| R2 Class B | 10 M | $0.36 / M |
| R2 storage | 10 GB-month | $0.015 / GB-month |
| KV reads | 10 M | $0.50 / M |
| KV writes, deletes, lists | 1 M each | $5.00 / M each |
| KV storage | 1 GB | $0.50 / GB-month |
| Durable Objects requests | 1 M | $0.15 / M |
| Durable Objects duration | 400,000 GB-s | $12.50 / M GB-s |
| Analytics Engine | **no price found** in Cloudflare's docs (two searches) | carried unpriced, flagged |

**Two facts from the docs that change earlier figures:**
- **R2 rounds billable usage up to the next whole unit:** "If you have performed one million and one operations, you
  will be billed for two million operations." **Read A** takes that literally: any overage is billed as at least one
  whole million, so crossing Class A's 1 M at all costs **$4.50**, not the proportional cents M6 used ($0.51 for 0.114 M
  over). **Read P** is proportional, as every earlier record assumed. Both are carried. Which one Cloudflare bills is
  for Daniel's invoice to settle, and the invoice is the one artifact that can.
- **Workers Logs changes price on 2026-12-01,** from events to gigabytes. Ingestion counts "the event body, the default
  enriched attributes, and custom attributes, pre-compression". So the December figure needs bytes per event, which no
  record here has measured.

## 2. The instrument: `tools/wx/account_cost_read.py` (read-only)

It extends `cost_read.py` and keeps its queries and its run rate (the last 7 full days x 30/7) unchanged. It adds one
line per billed product:
- **Workers requests** by script.
- **KV operations** by action type, and **KV storage**.
- **R2 storage.**
- **Durable Objects** requests and duration.
- **Workers Logs:** events per invocation, and the mean bytes per event, from the telemetry API with the token file's
  `observability-read` line, over one full hour. It prints counts and byte totals only, never an event: events carry
  request fields.
- **Analytics Engine:** points written in the last 7 days.

Tokens come from the token file and are never printed.

## 3. Predictions for the read (from earlier records only, frozen before it)

The run rate a month is the last 7 full days x 30/7, as in `cost_read.py`.

| row | line | predicted | from |
|---|---|---|---|
| **A1** | Workers requests, whole account | **1.5-3.5 M** | 1.82 M a month on 2026-11-06, plus two crons at 43,200 each |
| **A2** | Workers CPU | **22-40 M ms** | 39.24 M on 2026-11-07, which still held days of opuk's 1,676 ms builds before 1c's 240 ms |
| **A3** | R2 Class A | **0.80-1.00 M** | 0.929 M and 0.933 M. **Over 1.00 M means the account already crosses, and under Read A it already pays $4.50** |
| **A4** | R2 Class B | **4.0-6.0 M** | 4.80 M and 4.81 M, plus two boards' `/wx` reads |
| **A5** | R2 storage | **under 10 GB** | |
| **A6** | KV reads, writes, deletes, lists, storage | **each under its included amount** | no earlier read |
| **A7** | Durable Objects requests and duration | **each under its included amount** | no earlier read |
| **A8** | Workers Logs events a month | **under 20 M** (A1 x 1 to 4 events an invocation) | no earlier read |
| **A9** | Analytics Engine points a month | **within 0.5x-1.5x of the proxy's requests** (one point a request) | `CLAUDE.md`'s usage section; `wx-cost.md` |
| **A10** | today's overage, Read P | **$0.00-$0.60** | |
| escape | any row outside its band | **stop and report before the scenarios are computed**: a line over its included amount today is a finding in its own right | |

## 4. The model (frozen now; its inputs come from the read)

**A Blipscope board with weather on, a month:**
- `/wx`, from `docs/wx-cost.md`: 43,200 requests; 0.31-1.16 M ms CPU; 86,400-129,600 R2 Class B.
- Everything else it asks the proxy for, from the read: the proxy's run rate divided by **17 boards** (`wx-cost.md`'s
  count of active boards, read 2026-11-06), for requests, CPU, KV operations, DO usage, Logs events and AE points. It
  errs high, because 2 of the 17 already have weather on.
- Today's count isn't re-read here. The count at the cut is blipscope-5c's to read (`docs/wx-cost.md`, "What happens
  next").

**A Weatherscope, a month** (`docs/weatherscope-stage4.md`): 69,840 requests; 0.56-1.28 M ms; 86,400-129,600 Class B;
none of the radar's other traffic. Its Logs events are counted at the proxy's events per invocation. **Q113 (a)'s radar
product adds a fixed $0.47-$0.57** once any Weatherscope ships.

**The ten OPERA ingests** (M6, `docs/wx-opera-dev-measure.md`, as frozen):
- +36.52 M ms CPU;
- +0.181 M Class A;
- +4.30 M Class B;
- +0.389 M requests: nine new crons at 43,200 each, since opuk's is already in the read;
- their Logs events at the ingest Workers' measured events per invocation.

**Fixed:** the $5.00 subscription.

**The table** gives the account's monthly bill, subscription included, for these cells:
- Blipscope boards with weather on: 17, 50, 200 and 600;
- Weatherscope boards: 0, 50 and 200;
- each without and with the ten;
- under Read P and Read A;
- with Workers Logs at November's prices and at December's.

Each total names its largest line.

**Predicted shape of the table, frozen now:**

| cell | predicted |
|---|---|
| **S1** 17 boards, no Weatherscope, without the ten (today with weather on) | Read P: **$5.50-$6.50** (`wx-cost.md`'s $0.79 plus the subscription, plus any line A6-A8 finds) |
| **S2** the same with the ten | Read P: **S1 + $0.70-$1.50**. Read A: **S1 + $5.20-$6.00**, because the ten take Class A over 1 M |
| **S3** 600 boards, no Weatherscope, without the ten | **over $40**, and **Workers Logs is one of its two largest lines at November's prices**, a line no earlier record counted |
| **S4** the ten's own share | **under $2 a month under Read P, and over $2 under Read A** |
| escape | a cell whose largest line is outside every row above | stop and report |

## 5. What this does not decide

Whether to deploy the ten, and under which allowance, is **Daniel's call**. This record gives him the bill for each
case, and the levers that move it: `docs/wx-cost.md`'s F1-F3, Logs sampling, and the order of the ten.

## 6. Read 2026-11-11 (predictions at fcd968d2)

**The read was taken twice,** 05:14:46Z and 05:17:15Z, each over the 30 days before it. The two agree to within the
window's slide:
- requests 2,162,241 against 2,162,293;
- CPU 39,886,338 against 39,878,319 ms;
- R2 storage, KV storage, events per invocation and bytes per event identical.

The Logs hour (04:00-05:00Z) read **ABR 1**, unsampled. The anchor controls hold: KV reads and DO requests are both
non-zero, as the proxy's use of them requires.

| row | read (a month, run rate) | |
|---|---|---|
| **A1** | **2.16 M** requests: the proxy 1.96 M, `valarscopes-app` 78 k, cref's ingest 43 k, opuk's 31 k, the rest under 23 k each | as predicted |
| **A2** | **39.9 M ms.** cref's ingest (`blipscope-wx-ingest`) is **27.9 M of it, 70 %**, about 645 ms an invocation. The proxy is 7.3 M, opuk's ingest 3.0 M | as predicted |
| **A3** | **0.862 M** Class A. **`missileer-live` is 0.702 M of it**; `blipscope-wx` is 0.140 M | as predicted |
| **A4** | **4.52 M** Class B: `missileer-live` 2.94 M, `blipscope-wx` 1.40 M | as predicted |
| **A5** | **3.1 GB**, the largest bucket `blipscope-basemap` at 1.76 GB | as predicted |
| **A6** | KV reads **6.83 M**, writes **0.607 M**, lists 5 k, no deletes; storage **0.27 GB** | as predicted |
| **A7** | DO requests **0.376 M**; duration **1,215 GB-s** (the `duration` field and active time x 128 MB agree); storage 0.26 MB | as predicted |
| **A8** | **3.11 events an invocation** (proxy 3.12, both ingests 3.00); **1,714 bytes an event**; **6.7 M events a month** | as predicted |
| **A9** | **1.63 M** points, 0.83 x the proxy's requests | as predicted |
| **A10** | **$0.20** over the subscription (CPU only) | as predicted |

**A finding the read made, and a fix to the model's code (made after the read, so it is recorded here):**
- **KV writes split almost evenly between production and staging:** 325 k a month on production's namespace
  (`733bf900...`) and 282 k on **staging's** (`8b6e92ed...`). Staging's writes don't grow with the fleet.
- §4 says a board's other usage is "the proxy's run rate divided by 17". **The code scaled every namespace's operations
  instead, so it over-counted.** It now scales production's namespace and holds the others flat.
- The first read has no namespace split, so it reproduces the as-run figures: **$442-$462 at 600 boards, against
  $394-$413 after the fix.** Below 50 boards the two agree to the cent. Everything below is after the fix.

**The bill a month, the $5 subscription included,** under Read P with November's Logs prices (each cell low-high):

| Blipscope boards with weather | no Weatherscope | with the ten | + 50 Weatherscopes | + 200 Weatherscopes |
|---|---|---|---|---|
| **17** (today) | **$5.29-$5.55** | **$6.26-$6.74** | $6.37-$8.46 | $34.13-$40.71 |
| **50** | $15.07-$16.15 | $17.76-$19.32 | $24.17-$27.33 | $53.26-$60.92 |
| **200** | $117.54-$123.99 | $120.83-$127.28 | $127.71-$135.75 | $156.80-$169.34 |
| **600** | $393.65-$413.11 | $396.94-$416.40 | $403.82-$424.88 | $432.91-$458.46 |

**Read A** (R2 and DO billed in whole units) adds **$4.50 to every cell with the ten.** The ten take Class A from
0.862 M to 1.043 M, past the free 1 M, because `missileer-live` already uses 0.70 M. With the ten at today's fleet that
is **$10.88-$11.14.**

**December's Logs prices** lower every cell from 50 boards up: $12.03-$13.11 at 50, $353.04-$372.50 at 600. That
assumes 1,714 bytes an event and 7 days stored.

**What the money is, line by line, with no Weatherscope and without the ten (Read P, November, low-high):**

| boards | Logs | KV reads | KV writes | requests | R2 Class B | CPU |
|---|---|---|---|---|---|---|
| 17 | $0 | $0 | $0 | $0 | $0 | $0.29-$0.55 |
| 50 | $3.05 | $5.04 | $1.19 | $0 | $0-$0.27 | $0.78-$1.59 |
| 200 | $47.61 | $35.13 | $15.55 | $6.56 | $4.19-$7.27 | $3.00-$6.36 |
| 600 | $166.45 | $115.38 | $53.83 | $25.61 | $16.63-$25.93 | $8.91-$19.08 |

**What this changes:**
- At fleet scale the bill is **the radar's own traffic**, not weather.
  - Today's boards average, each, about 116,000 requests a month, 401,000 KV reads, 19,000 KV writes and 3.1 log events a request.
  - **Workers Logs at a sampling rate of 1, and KV reads, are the two largest lines from about 50 boards up.**
  - Weather's own share at 600 is the 43,200 requests a board, its CPU and its Class B. Its Logs events are about
    80 M of 297 M.
- **`docs/wx-cost.md`'s $29-$46 at 600 counted requests, CPU and R2 only.** It held the rest of the proxy's traffic
  flat, as it said. The account at 600 is about ten times that.
- **The per-board figures err high:**
  - they divide the whole production proxy by today's 17 boards, which includes bench boards on soaks and traffic that
    is not a board's;
  - the 17 is 2026-11-06's count.
- **The ten's own share:**
  - at today's fleet: **$0.97-$1.19** under Read P, **$5.59** under Read A;
  - in every larger cell: **$2.69-$3.29** under Read P, because the fleet has by then used the free tiers the ten would
    otherwise sit inside.

**Grades against §3 and §4:**
- **A1-A10: all as predicted.**
- **S1: OFF at its low end.** $5.29-$5.55, against $5.50-$6.50. The band was built from `wx-cost.md`'s high-end $0.79,
  and the low end of the per-board range wasn't applied to it: my miss. The reading stands.
- **S2: as predicted:** +$0.97-$1.19 under Read P, inside +$0.70-$1.50; +$5.59 under Read A, inside +$5.20-$6.00.
- **S3: as predicted:** over $40, and Logs is the largest line.
- **S4: as predicted at today's fleet; frozen without a fleet size.** In every larger cell the ten's Read P share is
  over $2, as above. A prediction that names no fleet size should not have been frozen. Also my miss.

**The levers, by size at 600 boards** (none is applied here; each is its own change, predictions first):
- **Workers Logs' sampling rate on the proxy.** At 0.1 the Logs line falls from $166 to about $6 (November). Nothing
  changes on a board. The cost is observability: every tail and Logs read here assumes rate 1.
- **KV reads,** 3.5 a proxy request: a cache in front of the hottest keys.
- **KV writes on production,** about 19,000 a board a month: what they are hasn't been read.
- **`docs/wx-cost.md`'s F1-F3** for weather's own lines.
- **Class A, for the ten:** #570's "one `latest` record for all OPERA products" would keep the ten's Class A to about
  0.1 M. That stays under 1 M with `missileer-live` as it is, so Read A's $4.50 never applies.
- **cref's ingest, 70 % of today's CPU:** the map-from-R2 change that took opuk from 1,676 ms to 240 ms has not been
  made for cref.

## 7. Ruled (reviewer chat, pasted by Daniel, 2026-11-11 ~05:25Z), verbatim

> The cost record:
> 1. The KV fix: accepted. The code now matches §4, and both results stay recorded.
> 2. S1 and S4: on record as misses, with no re-registration. The readings stand; the bands were wrong.
> 3. Push it as a docs PR, CI predicted first. It joins the order after blipscope-5c's v19-cut docs PR.
>
> Daniel's decisions:
> - The ten: change first, then deploy. Make #570's "one latest record" change, predictions first, including a read of
>   the whole account's R2 Class A afterwards that stays under the free 1M. Then deploy the ten through
>   ingest_deploy.sh, each under its claim. That PR joins the order after your cost-record PR.
> - The proxy's bill (Logs sampling, KV): blipscope-5c, after v19 is promoted. I've sent it the brief.

**Superseded ~05:49Z by Daniel's decision on Q-LR,** verbatim (reviewer chat, pasted by Daniel). It replaces the
~05:25Z "change first" above:

> Q-LR is closed by Daniel's decision, made on your table. (a) doesn't get under 1M with per-product Workers, and (b)
> needs a proxy deploy after v19 for only ~6% headroom, which Missileer's growth will use up. Neither change is made. No
> KV move, and cref stays as it is.
> - The ten deploy as they stand, through ingest_deploy.sh, each under its own claim. Predictions first, announced
>   before.
> - After the deploy, read the whole account's R2 Class A and report the monthly run rate.
> - Daniel checks his first invoice. If Cloudflare bills the Class A overage as a whole million ($4.50 a month), it
>   comes back. If it bills proportionally (about $0.20), it stays.
> - In §7, this decision supersedes the ~05:25Z "change first".

Q-LR's table, the two designs Daniel decided between:
- **(a)** one shared R2 `latest` for all OPERA products, as #570 worded it. Each product has its own Worker since the
  code stage, so each still writes its entry, and Class A after the ten stays about 1.02-1.04 M.
- **(b)** each product's `latest` in KV, as ruled for stage 1b on 2026-11-06. Class A falls to about 0.94 M. That
  needs a KV binding on the ingests, a rebuilt forward-only rule (KV has no conditional put), and a proxy that reads KV
  first, deployed before the ingests switch.

## 8. CI for this record's PR (predicted before the push)

It changes `docs/wx-account-cost.md` and `tools/wx/account_cost_read.py`. Neither is in `workers.yml`'s or
`web-renderer.yml`'s path filter, so the three workflows that run on every PR run: `device-ids`, `firmware` and
`no-traffic-captures`.
- **Predicted: 32 check runs: 29 success, 3 skipped, 0 failed.**
- **The baseline** is #633's finished head run (`82ec36ae5ba1`): 32 = 29/3/0, the same three workflows.
- **No job-set change since.** #633 added two steps to `device-ids`, not a job.
- **Run locally first, all exit 0:** `check_device_ids.py`, `check_wifi.py`, `check_macs.py`,
  `check_binary_strings.py`, `check_no_traffic_captures.py` and `skus.py --check-readers`, over the tracked tree with
  these two files in it.
