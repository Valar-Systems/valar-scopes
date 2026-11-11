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
