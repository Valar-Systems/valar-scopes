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
| `.github/workflows/account-budget.yml` | CI on any change to the four paths above: `--selftest`, then the checker (it prints the flags into the log), then `--render-check` |
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

## 6. The table (rendered by the checker, from the next commit)

(empty until the build)

## 7. Point 4, the listener read (after the budget file)

Read-only, when convenient. The measure is requests per hour for `kph/live.m3u8`, the playlist every listener reloads
about every 10 s. That needs per-object or per-path counts. R2's analytics, as read so far, give bucket and operation
type only, and the edge caches the playlist for 2 s (`public, max-age=2`), so R2 sees at most one fetch per data
centre every 2 s, however many listen. The read will say plainly what it cannot see; a zone's HTTP analytics for
`live.missileer.watch`, if the token can read them, is the first candidate.

## 8. CI for this registration (predicted before the push)

**Predicted: 32 check runs: 29 success, 3 skipped, 0 failed.**
- It changes this doc only, which is in no workflow's path filter.
- **The baseline** is #667's head run (docs only): 32 = 29/3/0.
- **The build commit adds `account-budget.yml`,** whose one job runs when its paths change. That commit is predicted at
  **33 = 30/3/0**, registered again before its push.
