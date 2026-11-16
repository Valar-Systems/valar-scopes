# The radar deploy: the OPERA backup fallback on the ten ingest Workers (plan and predictions, before the deploy)

**The owner's go (relayed 2026-11-14):** *"Radar: deploy, after the Missileer cut is live and checked, and not in the
same hour as the route run. If Monday's proxy deploy carries the merged radar fallback code, it must stay inert until
the radar deploy."*

**The slot** (the reviewer, 2026-11-14):
- **Wed 2026-11-18, 10:00-11:00Z**, if the Missileer cut (its flip about 03:30Z) is reported live and checked by
  09:30Z;
- **else Thu 2026-11-19, 10:00-11:00Z**;
- never 15:00-18:00Z on 11-18 (the route run is at 16:00Z), and never Mon 16:00-17:00Z.

The uk-data session deploys, one Worker at a time, each under its own claim. What is deployed is `b2233f0c`'s fallback
(`docs/wx-opera-backup-fallback.md`). The proxy's half, `/healthz`'s `source`, goes out with Monday's proxy deploy and
reads `null` until this one (`docs/proxy-deploy-2026-11-16.md` section 1a, check C7). So this deploy is the ten
ingest Workers alone.

## 1. What it carries, read from git and a build at plan time

**The ten:** `blipscope-wx-ingest-<id>`, the `[env.<id>]` sections of `proxy/wx-ingest-opera.wrangler.toml`, for opuk,
opis, opib, opmt, opwe, opce, opse, opba, opsw and opsn. They were deployed 2026-11-11 15:00-16:24Z from main
`9673f7d0` (`docs/wx-opera-stage3.md`). Of the ten, only opuk is in `ROUTED` (`products.ts`), so only opuk's frames
reach a customer today. The nine build frames that serve no one until their own routing deploy.

**Their inputs, read from a build.** An esbuild bundle of `src/wx/ingest-opera-worker.ts` at `77acc072` (packages
external) has 9 inputs, all under `src/wx/`: `grid`, `ingest-opera-worker`, `ingest-worker`, `ingest`, `laea`, `mrms`,
`opera`, `products` and `resample`. It imports no package. So a deploy's inputs are those 9 files, the toml, and
`proxy/package-lock.json` (wrangler's version).

**The carried list,** from `git log 9673f7d0..<main>` over those inputs (main `77acc072`, read 2026-11-14 ~15:48Z):

| commit | PR | what changes for the ten |
|---|---|---|
| `03632992` | #643 | `products.ts`: a comment on `ROUTED` only |
| `05746dec` | W4 | `products.ts`: shsr's MRMS row appended after the OPERA rows. An OPERA Worker builds only the product its `PRODUCT` var names, so nothing changes for the ten |
| `b2233f0c` | #720 | `ingest.ts`: the fallback (one source per check, recorded per frame and in `latest`), and `OPERA_UA` gaining the contact address |

The toml and the lockfile are unchanged since `9673f7d0`. **On the day the list is read again** (R0.2). A commit that
touches the ten's inputs and is not in this table stops the deploy until it is planned.

## 2. On the day: the order

| step | when | what | predicted |
|---|---|---|---|
| R0.1 | 09:30Z | the reviewer's word on the Missileer cut | live and checked: the Wed slot holds. Otherwise the slot is Thu 10:00Z, and every step below moves by a day |
| R0.2 | before 10:00Z | `git ls-remote` main, and the carried list from section 1 | the three commits above, or more that touch none of the ten's inputs. Anything else: stop |
| R0.3 | before 10:00Z | the rollback targets: each Worker's `deployments list --json` through the helper, version ids only | the ids read 2026-11-14 (below). A different id means someone deployed since: write it down, and report before going on |
| R0.4 | before 10:00Z | `/healthz` on production, twice, at least 60 s apart, counts only | 10 OPERA products. Each `source` is `null` if Monday's deploy stands (C7), or the key is absent if it was rolled back. Each product's `frameAgeS` is written down |
| R0.5 | before 10:00Z | the primary and the backup: today's and yesterday's `OPERA/COMP/` listing on each, counts only (the same read-only listing as the fallback record's section 1, 11-14 08:05:07Z) | the primary empty (as since 11-11 about 20:10Z), the backup non-empty. **If the primary lists frames,** that is PR-RET's branch: the first frames may read `"primary"`, which is legal, and is recorded |
| R0.6 | before 10:00Z | the baseline: `smoke-prod.sh` against production, as the test identity, whole output to a file | each verdict saved by name; the OPERA lines end `source=null` (or `source=-`) |
| R1 | 10:00Z | a fresh detached checkout of main's sha, with `npm ci` in `proxy/` | clean; `proxy/` equal to main's |
| R2 | per Worker | its claim `wx-ingest-<id>:production`, then `BLIPSCOPE_SESSION=uk-data bash tools/wx/ingest_deploy.sh wx-ingest-opera.wrangler.toml <id>` | stage 3's S3-1: exit 0; one version at 100 %; the bindings exactly `WX` -> blipscope-wx, `PRODUCT` = `<id>` and `WX_INGEST` = on; `cpu_ms` 12000; the deploy's `schedule: * * * * *` |
| R3 | per Worker, by its deploy end + 900 s | the first storing check, from `/healthz` and `tools/wx/latest_check.py <id>` | `/healthz` `wx.<id>.source` is `"backup"` (or `"primary"` on R0.5's branch), `frameAgeS` at most the product's `maxAgeS`, and `stale` false, read twice at least 60 s apart. `latest_check.py` exits 0 |
| R4 | per Worker | `logver.py blipscope-wx-ingest-<id> <deploy end> <R3's read>`, counts only | every storing line is the new version's; no `wx_ingest_error` line |
| R5 | after R3 and R4 | the claim released | |

**The order:**
- **opuk first, alone.** The nine start only after opuk's R3 and R4 read as predicted. opuk is the one product a
  customer sees, so its return is the deploy's purpose, and it is graded first.
- **Then the nine, back to back** (opis, opib, opmt, opwe, opce, opse, opba, opsw, opsn). Each is deployed under its own
  claim, and each is graded at its own deploy end + 900 s. They are not gated one on the next as stage 3 was.
  - **Why:** stage 3's serial gating took 96 minutes for ten (15:00-16:36Z), which does not fit the hour. Each Worker
    is its own invocation with its own CPU, so a back-to-back deploy shares nothing but the backup host. That is about
    ten checks a minute for a few minutes, against stage 3's about three a minute.
  - **The alternative**, serial gating as stage 3, would end about 11:40Z and overrun the slot. It is named so the
    reviewer can choose it instead.

**The 900 s limit** (ruled 2026-11-14) is stage 3's S3-2 rule, measured on these ten. On 11-11 the first storing checks
came +298 to +747 s after each deploy. A 600 s limit would have rolled back two healthy deploys (opmt at +643 s and
opuk at +747 s, the latter during an OPERA pause).

**The rollback targets** (read 2026-11-14, from each Worker's newest deployment; re-read at R0.3):

| product | version at 100 % | deployed |
|---|---|---|
| opuk | `7c828e17-cb21-4ae3-b213-c2aa0b3e1ba6` | 11-11 16:23:31Z |
| opis | `69ea0cff-bd06-469b-98b9-727b36b1e359` | 11-11 15:39:20Z |
| opib | `4de3159a-04ed-47c6-b295-47c96980e711` | 11-11 15:31:48Z |
| opmt | `915dfa07-30ae-4abe-b63d-cadd1668782a` | 11-11 15:00:13Z |
| opwe | `5e42945a-e696-4904-9594-f0c138b7eedc` | 11-11 15:59:50Z |
| opce | `166b5974-3655-4ea5-88d6-98b354113dcc` | 11-11 15:24:18Z |
| opse | `54babaab-a02d-4fb8-84e1-9f0bb6e04098` | 11-11 15:45:49Z |
| opba | `b8dd7c8f-497d-4fc7-a065-4bd1a28d2345` | 11-11 15:52:20Z |
| opsw | `aaf12881-e4d6-45ab-a35d-f85e006f605b` | 11-11 15:13:44Z |
| opsn | `7139b24a-6d41-4feb-8633-578862a60994` | 11-11 16:07:24Z |

## 3. The checks after all ten

- **C-R1.** `/healthz`: all 10 OPERA products read `source` `"backup"` (or `"primary"`), `stale` false, twice at least
  60 s apart.
- **C-R2.** `smoke-prod.sh`, whole: the OPERA lines end `source=backup`. Every verdict equals R0.6's baseline, except
  stale WARN lines that clear (information, not verdicts).
- **C-R3, T_R** (`docs/wx-opera-code-stage.md` section 4.1, as re-registered in the fallback record, section 4): the
  first hour in which opuk stores a frame newer than 1794427800, from either source. It is this deploy's hour, recorded
  with its source. The moved 7-day rows start from it.
- **C-R4.** The contact address in `OPERA_UA` reaches both hosts from the first check. That is printed, not graded: we
  cannot see their logs.
- **Cost:** the ten's builds resume. The figures come only from the budget PR's rendered output (`docs/account-budget.md`
  section 9, built after 11-15T00Z), in its scenarios with the fallback. They model a start at 11-17 00:00Z, and this
  slot is later, so November can only read lower than those scenarios.

## 4. The rollback

- **opuk:** `tools/wx/md_rollback.sh "<the row>"` with `OPUK_ROLLBACK_TO` set to R0.3's opuk id. It logs itself in
  `rollbacks.log`.
- **The nine:** `node proxy/scripts/wrangler-run.mjs rollback <R0.3's id> -c wx-ingest-opera.wrangler.toml -e <id> -m
  "<the row>" -y`, from `proxy/`, then a labelled line in `rollbacks.log` by hand.
- A rollback is never refused. After one, `deployments list` shows the old id at 100 %, read twice.
- **What a rollback returns to:** the 11-11 code, which lists only the primary. While the primary is empty, that is the
  dark state of today, not a new failure.

## 5. Outcomes

| reads | action |
|---|---|
| every step as predicted, all ten | done. Report, with T_R's hour and each product's source |
| R0.1: the cut not reported live and checked | the Thu slot |
| R0.2: an unplanned change to the ten's inputs | no deploy until it is planned |
| R0.3: an id differs | write it down and report before deploying |
| R2: S3-1 not met | roll that Worker back at once; stop the rest; report |
| R3: no storing check by deploy end + 900 s | roll that Worker back; stop the Workers not yet deployed; report. Those already reading as predicted keep running |
| R4, or any read: an error line naming a host | roll that Worker back at once, whatever the time; stop the rest; report |
| a `source` other than `"backup"` or `"primary"`, or `null` after a storing check | stop; roll that Worker back; decide |
| anything else | stop; roll back what changed; decide |
