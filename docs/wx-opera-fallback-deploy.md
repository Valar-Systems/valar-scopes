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

## 6. Amendments before the run (2026-11-17, ruled by the reviewer)

The text above is left as frozen. These two change R1 and R0.1 only; everything else stands.

### 6a. R1: the deploy checkout is LF

This carries the owner's "eol: yes" (2026-11-16 15:02Z), as the proxy deploy's D0.8 does. It replaces R1's "a fresh
detached checkout of main's sha, with `npm ci` in `proxy/`":
- `git worktree add --no-checkout --detach <path> <main sha>`, then
  `git -C <path> -c core.autocrlf=false -c core.eol=lf reset --hard <main sha>`;
- a byte count of CRLF over `proxy/src` and `proxy/scripts`, with a planted control (a file of two CRLF lines). It
  covers every `.ts`, `.mts`, `.mjs`, `.js`, `.json`, `.html`, `.toml`, `.sh` and `.py` file, with `node_modules`
  excluded;
- then `npm ci` in `proxy/`.

**Predicted:** the control reads 2, both folders read 0 CRLF, `npm ci` exits 0, and porcelain is 0. Otherwise no deploy.

**Why.** `.gitattributes` begins `* text=auto`, so a default Windows checkout is CRLF, while CI builds the LF blobs.
The deployed bytes should be the bytes CI tested (`docs/proxy-deploy-2026-11-16.md`, "Why D0.8").

**Dry run, as information** (2026-11-17 00:33:54-00:34:04Z at main `0fc8c1d3`): the control read 2, 0 CRLF over 201
files, `npm ci` exit 0, porcelain 0. The run's R1 is a new checkout of its own.

### 6b. R0.1: the cut is already live; the word rests on a "still holding" read

**The cut went live on 2026-11-16.**
- The Missileer session reported it in-segment from 04:46:54Z that day, after the owner's own listening check.
- PUTs fell to about 720 an hour from 1,080, which saves about 263k a month. There has been no rollback.
- The reviewer recorded R0.1's condition as met then.

**Before the slot, R0.1 is given on a read that the cut is STILL holding.** The rule, frozen here before the read:
- **Treatment:** 2026-11-17's five full UTC hours, 04:00-09:00Z. That is a whole post-cut day; 11-16's early hours
  straddle the flip, so they are not used.
- **Control:** the same five hours on 11-14 and 11-15, both before the cut, per hour. The two-day mean is graded, and
  both days are printed.
- **Source:** R2's analytics (`r2OperationsAdaptiveGroups`), bucket `missileer-live`, with Class A as
  `tools/budget/budget_read.py`'s action set. Counts only, through the token file's analytics-read token, never
  printed.
- **LIVE:** every treatment hour is at least 250 Class A below its control mean. The cut is about 360 an hour.
- **UNTRUSTWORTHY, never a pass:**
  - an API error, or a missing or incomplete cell;
  - any hour reading 0 Class A;
  - the Class B anchor reading 0 in any treatment hour.
- **Outside the set** (some hours below the line, or treatment above control): stop and report, with the pairs.

**Who does what:**
- The read is uk-data's, on Wed 11-18 at 09:00-09:10Z. The per-hour pairs and the verdict go to the reviewer.
- The reviewer adds "checked": the Missileer session's word that the stream still serves, with no rollback since. The
  reviewer then gives R0.1's word before 09:30Z.
- If the read is UNTRUSTWORTHY or outside the set, R0.1 is not given, and the slot is Thu 10:00Z (section 2).

**The reader's dry runs** (2026-11-17 00:37:27-00:37:37Z; predictions written before the run). Information only: the read
that counts is Wednesday's.

| run | treatment vs control | predicted | read |
|---|---|---|---|
| A | 11-17 (its hours not yet complete) vs 11-14 and 11-15 | UNTRUSTWORTHY | UNTRUSTWORTHY: all five cells "not complete" |
| B | 11-16 05:00-09:00Z (after the flip) vs 11-14 and 11-15 | LIVE | LIVE: 720 in each hour against means of 1,081.0-1,084.5, so 361.0-364.5 below |
| C | 11-15 vs 11-14 and 11-13 (all before the cut) | OUTSIDE THE SET | OUTSIDE THE SET: -2.0 to 7.5 below, in all five hours |

C is the control that the line can fail. The Class B anchor read 5,032-6,074 an hour wherever it was read.

### 6c. Wednesday's chain rule (the owner, 2026-11-17 05:47Z; appended below the frozen text)

**The owner's words, verbatim:** "Wednesday's chain rule: radar 10:00Z, logs ~14:00Z, route run 16:00Z; if one fails
or rolls back, the later ones wait for me."

**This deploy is the FIRST in the chain**, so it has no chain precondition of its own. R0.1 and the rest of section 2
are unchanged. What it adds is the hand-off:

- **The radar deploy FAILS** (once a deploy command has run, from R2 on: an outcome of section 5 that is not "all as
  predicted", or a stop at any later step), **or ANY of the ten Workers is rolled back** (section 4). Then the logs slot (12:30-15:00Z,
  `docs/workers-logs-invocation-off.md`) and the route run (16:00Z, `docs/route-weekly-code.md` section 4e) **wait for
  the owner**. The reviewer is told at once, with the step and its reading. Neither later step starts on anyone else's
  word.
- **The radar deploy reads as predicted, with no rollback.** The reviewer is told, with the time, and the logs slot may
  start.
- **The radar deploy does not run at all** because its own precondition held it (no R0.1 word, so it moves to Thursday
  10:00Z under section 2; nothing is deployed and nothing changes in production). **The reviewer's reading, 2026-11-17,
  which the owner may override:** that is neither a failure nor a rollback, so the later steps go ahead on their own
  preconditions. The owner is told at that moment.
- **A deploy that STARTED** (any deploy command run) and then did not complete as predicted counts as a FAILURE: the
  later steps wait for the owner.

### 6d. The commit this deploy ships, and its D0 by procedure (2026-11-17, appended below 6c)

**The owner's words, 2026-11-17 18:53Z (relayed by the reviewer), verbatim, the parts that govern this deploy:** "rule:
by procedure Wed. One gap to close today: the 10:00Z radar step deploys the proxy too. With #744 and #736 on main, does
that deploy carry them? If yes, the logs step's "before" readings must be taken before 10:00Z, or the order changes.
Tell me which, and name each of Wednesday's three commits in the plans."

**The gap: this step carries neither #744 nor #736.** It deploys the ten ingest Workers alone, through
`tools/wx/ingest_deploy.sh` (the opening of this plan). It does not deploy the proxy. Read 2026-11-17 19:49:17Z, with
`git log` over the ten's inputs (section 1: the 9 `src/wx` files, `proxy/wx-ingest-opera.wrangler.toml` and
`proxy/package-lock.json`):
- `77acc072..4a02123c`: empty;
- `c7f73d10` (#744), `00fc75d1` (#736), `4a02123c` (#747) and `13a6dab8` (#746): each touches 0 of them;
- `9673f7d0..4a02123c`: exactly section 1's three commits (`03632992`, `05746dec`, `b2233f0c`);
- the deploy machinery (`tools/wx/ingest_deploy.sh`, `proxy/scripts/wrangler-run.mjs`, `scripts/session-claim.py`):
  unchanged since `77acc072`.

So the logs step's BEFORE readings stay where its plan puts them, and the order is unchanged.

**The commit: EXPECT = `4a02123ca992031d60bd9592e3084c9ccf34041f`** (main, read 2026-11-17 19:46Z). R1's LF checkout
(6a) is made at EXPECT, not at whatever main is at 10:00Z:
- `git worktree add --no-checkout --detach <path> EXPECT`, then
  `git -C <path> -c core.autocrlf=false -c core.eol=lf reset --hard EXPECT`;
- 6a's CRLF count and `npm ci` as written.

No branch is created or moved.

**D0, by procedure.** The owner ruled "by procedure" for Wednesday: the deploy scripts do not yet check this themselves
(the commit-pin change is not merged before the chain closes). So the deployer PRINTS each of these after R1 and before
the first R2. Any mismatch means no deploy:

| D0 | prints | predicted |
|---|---|---|
| D0-H | `git rev-parse HEAD` in R1's checkout | EXPECT, in full |
| D0-A | `git ls-remote` for main = M; `git merge-base --is-ancestor EXPECT M` | exit 0 |
| D0-B | `git log --format='%h %s' EXPECT..M --` over the ten's inputs and the machinery above | empty. A later commit touching other paths is not listed by this read; it is a note, and changes nothing here |
| D0-D | `git status --porcelain -- proxy/ tools/wx/` in R1's checkout, after `npm ci` | empty |

A commit listed by D0-B (a merge after this plan that touches what the ten ship) means no deploy until it is planned,
as R0.2 already says.

**Wednesday's three commits, named:**
- this step: `4a02123ca992031d60bd9592e3084c9ccf34041f`;
- the logs step: `4a02123ca992031d60bd9592e3084c9ccf34041f` (`docs/workers-logs-invocation-off.md` 7a);
- the route run: the commit of the tool it runs, named in `docs/route-weekly-code.md`.

### 6e. The claim's shape (ruled by the reviewer 2026-11-18 ~09:06Z; recorded 09:06:14Z, before anything was taken)

**The frozen text (section 2, "The order"):** "Each is deployed under its own claim, and each is graded at its own deploy
end + 900 s."

**Why it cannot run literally.** `scripts/session-claim.py` holds ONE `deploy` claim, with a covers list and one holder.
Taking it again while it is held answers "already yours" and leaves the covers unchanged. So nine separate claims cannot
overlap, and the back-to-back order needs them to.

**The correction, as ruled.**
- "Its own claim", in the plan's intent, means a claim that NAMES that Worker. `ingest_deploy.sh` checks exactly that:
  `check deploy --covers wx-ingest-<id>:production`, on every deploy.
- **(1) opuk alone:** take `deploy --covers wx-ingest-opuk:production`, deploy, grade R3 and R4, release.
- **(2) the nine:** ONE claim whose covers are exactly the nine `wx-ingest-<id>:production`. Its covers list is printed
  after taking it: the nine and only the nine. Deploy back to back, grade each at its own deploy end + 900 s, and
  release after the last grade.
- **Nothing else changes:** the stop rules (section 5), the predictions, EXPECT `4a02123c`, and the rollback table are
  as frozen. A failed grade that stops the remaining deploys still stops them.

**The lock:** the reviewer confirmed it at about 09:06Z: no coordinated session had a deploy or merge booked from 10:00Z
to 12:30Z. `session-claim.py show` at 09:05:00Z read deploy free and merge free.

## 7. Record: the run on Wednesday 2026-11-18 (appended after the run; nothing above is edited)

### 7a. The run

#### Pre-checks (all before 10:00Z)

| step | read at | result |
|---|---|---|
| R0.1 | 09:00:28-09:00:39Z | LIVE, exit 0: 341.5-364.0 below the control mean in all five hours. CHECKED by the reviewer with the Missileer session at 09:01:21Z. R0.1 given |
| R0.2 | 09:02:02Z | main `e1c725a0`; EXPECT an ancestor. `9673f7d0..main` over the ten's inputs = exactly `03632992`, `05746dec`, `b2233f0c`. `EXPECT..main` over the inputs and the machinery: empty |
| R0.3 | 09:02:11-20Z | all ten ids as the plan's table: 10 read, 0 differ, 0 blind |
| R0.4 | 09:02:44Z and 09:04:30Z (106 s apart) | production commit `074863fc`; 10 present; `source`, `frameAgeS` and `stale` null x10 (no `latest` record). /healthz carries no `maxAgeS`, so R3 uses products.ts at EXPECT: 1800 s for every OPERA row. A read at 09:03:35Z, 51 s after the first, is not counted |
| R0.5 | 09:03:21Z | primary: 0 keys under COMP for yesterday and today. Backup: 1,318 keys and 396 DBZH files, the newest 8.4 min old; range read 206, TIFF magic |
| R0.6 | 09:03:54-09:04:19Z | smoke, production, whole, as the test identity, exit 0: 38 verdicts (35 PASS, 0 FAIL, 3 SKIPPED); OPERA lines `source=null` x10. Run from a checkout at `0fc8c1d3`, whose smoke script, wrangler helper and claim script equal EXPECT's |

#### Instrument controls before R1 (each must be able to fail)

| instrument | control | read |
|---|---|---|
| R3's /healthz poller (the deploying session's) | deploy end 1000 s in the past, production | exit 1, OFF MISS at the first read (09:08:44Z) |
| the same poller | an unreachable base (control only) | exit 3, BLIND after three failed reads (09:08:58-09:09:38Z) |
| `tools/wx/latest_check.py` (R3's second half) | opuk today, no latest record | exit 1: 0 frames, latest HTTPError, not listed (09:09:50Z) |
| `tools/wx/logver.py` (R4) | opuk 08:39:50-09:09:50Z | exit 0 at ABR 1: 88 events, 7c828e17:no-frame 59, 7c828e17:invocation 29 (the pre-state) |
| `tools/wx/md_rollback.sh --dry`, `OPUK_ROLLBACK_TO=7c828e17-...` | read-only | DRY PASS: target 7c828e17 in the -e opuk Worker; opuk runs 7c828e17 at 100 %; control top level 10007 |

**R4, pre-registered.**
- R4 passes when every stored or backfilled key in logver.py's by_version_status is the new version's 8-char prefix.
- A key with status None may be a wx_ingest_error line (a product, no status): stop and inspect it before going on.
- Old-version no-frame and invocation lines inside the propagation window are legal.

#### The deploys

- **R1 and D0 (10:00:29-10:00:45Z): as predicted.**
  - An LF checkout at EXPECT: CRLF control 2, tree 0 over 204 files.
  - npm ci exit 0; its stderr held the allow-scripts note for sharp and npm's update notice only.
  - D0-H = EXPECT; D0-A main e1c725a0, is-ancestor 0; D0-B empty; D0-D empty.
- **No-claim control (10:01:00Z):** ingest_deploy.sh refused before any deploy ran (exit 4 from the driver). opuk still
  ran 7c828e17 at 100 %.
- **Claim (1)** taken at 10:01:07Z, covering wx-ingest-opuk:production, until 10:41:07Z.

| product | deploy end | version | S3-1 (R2) | R3 first qualifying | R3 second | latest_check | R4 logver | claim |
|---|---|---|---|---|---|---|---|---|
| opuk | 10:01:27Z | c68afa72-e575-401a-8a04-7fba5650b6e4 | as predicted (exit 0; 100 %; PRODUCT=opuk, WX=blipscope-wx, WX_INGEST=on; cpu_ms 12000; cron ok) | 10:02:38Z, +72 s, backup, frameAgeS 459, stale false | 10:03:40Z (+61 s), backup, 521, false | exit 0: latest 09:55:00Z listed, 3 frames | ABR 1, 10:01:27-10:03:40Z: c68afa72 stored 1, backfilled 2, invocation 1; 7c828e17 invocation 1, no-frame 1; no status-less line | released 10:04:25Z |

- **Claim (2)** taken at 10:04:50Z, until 10:49:50Z. Covers printed: 9, exactly
  wx-ingest-{opis,opib,opmt,opwe,opce,opse,opba,opsw,opsn}:production (set-equal check True).
- **The nine** were deployed back to back, 10:05:02-10:06:03Z. Each S3-1 read as predicted: exit 0; 100 %; PRODUCT = itself,
  WX=blipscope-wx, WX_INGEST=on; cpu_ms 12000; cron ok.

| product | deploy end | version | R3 first qualifying | R3 second | latest_check | R4 logver |
|---|---|---|---|---|---|---|
| opis | 10:05:08Z | 93edb8cc-d11b-4f09-ac32-c71d8b910048 | 10:06:14Z, +67 s, backup, 375, false | 10:07:16Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: 93edb8cc stored 1, backfilled 2, invocation 1; 69ea0cff invocation 1, no-frame 1; no status-less line |
| opib | 10:05:15Z | 258008c7-66e5-448c-8ae9-06bdacd8aacb | 10:06:14Z, +60 s, backup, 375, false | 10:07:16Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: 258008c7 stored 1, backfilled 2, invocation 1; 4de3159a invocation 1, no-frame 1; no status-less line |
| opmt | 10:05:22Z | 46dd29b8-fdd8-41a8-845a-5697f0ad536e | 10:06:14Z, +53 s, backup, 375, false | 10:07:16Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: 46dd29b8 stored 1, backfilled 2, invocation 1; no status-less line |
| opwe | 10:05:29Z | c93ad8e9-39d2-4998-a3eb-9aee2b6bda84 | 10:06:35Z, +66 s, backup, 396, false | 10:07:36Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: c93ad8e9 stored 1, backfilled 2, invocation 1; 5e42945a invocation 1, no-frame 1; no status-less line |
| opce | 10:05:36Z | 839fdc5a-c944-4da4-8ab3-f70c9d53c51c | 10:06:14Z, +39 s, backup, 375, false | 10:07:16Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: 839fdc5a stored 1, backfilled 2, invocation 1; no status-less line |
| opse | 10:05:42Z | deb8da85-dc47-4881-9329-c6454ee86f81 | 10:06:35Z, +53 s, backup, 396, false | 10:07:36Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: deb8da85 stored 1, backfilled 2, invocation 1; no status-less line |
| opba | 10:05:49Z | d4e14a16-9045-4a66-8ea9-7006be730792 | 10:06:14Z, +26 s, backup, 375, false | 10:07:16Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: d4e14a16 stored 1, backfilled 2, invocation 1; no status-less line |
| opsw | 10:05:56Z | cf72bb10-b2a9-4bd3-9afe-ee1a635c8c55 | 10:06:14Z, +19 s, backup, 375, false | 10:07:16Z (+62 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: cf72bb10 stored 1, backfilled 2, invocation 1; no status-less line |
| opsn | 10:06:03Z | 1c87e610-3c0b-4438-ac55-bb76aa8d6423 | 10:06:55Z, +53 s, backup, 416, false | 10:07:57Z (+61 s) | exit 0: latest 10:00:00Z listed, 3 frames | ABR 1: 1c87e610 stored 1, backfilled 1, invocation 1; 7139b24a invocation 1, no-frame 1; no status-less line |

- Claim (2) released 10:08:56Z.

#### Section 3, after all ten

- **C-R1: PASS.** /healthz at 10:09:09Z and 10:10:30Z (81 s apart): all 10 source "backup", stale false, frameAgeS 550 at
  the first (the 10:00:00Z frame). A read at 10:10:09Z was exactly 60 s apart by whole-second stamps, so it is not the
  one counted.
- **C-R2: OUTSIDE THE SET; stopped and reported at 10:10:34Z.**
  - The read: smoke, production, whole, as the test identity, from the EXPECT checkout, 10:09:09-10:09:32Z, exit 0.
    39 verdicts, all PASS; OPERA lines source=backup x10, as predicted.
  - By name against R0.6, 7 names differ:
    - SKIPPED at the baseline and now absent: `/v1/enrich`, `/v1/photo`, `photo variant gate`;
    - new, and PASS: `/v1/enrich/<live hex>`, `photo variant gate (FW 6 vs FW 7)` x2, `/v1/photo` via the enrich pointer.
  - The prediction allowed only stale WARN lines that clear.
  - **The cause, read: traffic.** The smoke's test tile returned n:0 at R0.6 and n:1 now; the smoke's SKIP text says
    this is not a proxy fault.
  - **The alternative, that the deploy changed enrich or photo, is excluded:** the proxy commit read 074863fc in both
    runs, and the ten serve neither endpoint.
  - No rollback was taken, and a ruling was asked (section 5's last row and 6c).
- **C-R3:** T_R = the 2026-11-18 10:00Z hour, source backup.
  - opuk's first frames newer than 1794427800 (2026-11-11T20:10:00Z) were stored and backfilled by c68afa72 at about
    10:02Z. Before the deploy: latest absent, 0 frames (09:09:50Z).
  - The named gap: latest went absent at some point after 11-11, so its absence alone does not exclude an earlier
    store. A Workers Logs read over 11-11 20:10Z..11-18 10:01Z would; it was offered, and was read below.
- **C-R4 (printed, not graded):** OPERA_UA at EXPECT carries the repository URL and the support address, sent from the
  first check. We cannot see the hosts' logs.

**A slip, disclosed at 10:10:34Z.** A raw text diff of the two smoke outputs was printed to the deploying session's tool
output while C-R2 was read. It held the smoke's fixed test coordinates and one enrich body; nothing left the session.
Since then, smoke runs are compared by verdict name only.

#### The reviewer's ruling on C-R2 (received about 10:12Z; acknowledged 10:12:23Z)

1. C-R2 is recorded OFF, outside the set: the radar step's first off. Cause: traffic. The alternative (the deploy
   changed enrich or photo) is excluded. It is not re-read as a pass.
2. No rollback. The state is held exactly as it is: no further deploys, nothing rolled back.
   - Section 5's "roll back what changed" exists to undo the cause of an off reading, and nothing the deploy changed
     caused this one.
   - That reading of the frozen text goes to the owner for confirmation, with the reviewer's recommendation to keep.
3. 6c applies: the logs step (12:30Z) and the route run (16:00Z) wait for the owner.
   - A go before 12:30Z means the logs step runs at its time.
   - A later go runs it only if the whole step still fits inside 12:30-15:00Z; otherwise it is re-booked.
   - The route run waits in the same way.
4. Meanwhile, read-only and counts only:
   - (a) /healthz for the ten at about 10:40Z and 11:10Z;
   - (b) a Workers Logs read for C-R3's named gap, 11-11 20:10Z to 11-18 10:01Z: stored and backfilled counts by version
     per Worker, no bodies.
5. The slip is recorded and is not repeated anywhere.

#### Held-state reads and C-R3's gap (read-only, per the ruling)

**(b), the controls, 10:12-10:13Z:** opuk 10:01:00-10:04:00Z in 60 s chunks. Cut 10:01:30Z: STORED (c68afa72 stored 1,
backfilled 2 after the cut). Cut 10:04:00Z: EARLY. Both at ABR 1.

**(b), the full read, 10:13:31-10:39:22Z.**
- Ten Workers, 11-11 20:10Z..11-18 10:01Z, 27 six-hour chunks each, 0 API errors.
- **BLIND for all ten:** sampled chunks, ABR [1, 10]. opba also had one chunk at ABR 100000 with 0 events.
- Information only: 0 stored or backfilled lines at or after 11-11 20:30Z on any Worker. There is 1 old-version stored
  line before 20:30Z on opuk, opis, opwe, opsw and opsn.
- A re-read of only the sampled chunks, in 1 h and then 15 min pieces, was proposed at 10:40:27Z.

**(a) /healthz 10:40:22Z:** all 10 "backup", stale false, frameAgeS 323 (the 10:35:00Z frame).

**The re-read of (b), re-registered at 10:41:37Z, before any re-read.**
- **The gap:** the first run kept only the SET of ABR levels per Worker, not which chunk had which. So the sampled chunks
  could not be selected from the record. That limitation of the instrument was stated before the re-read.
- **Method B:**
  - re-query each 6 h chunk with the same query; an ABR-1 answer's counts stand as read;
  - a chunk above ABR 1 is split into 1 h pieces;
  - a 1 h piece still above ABR 1 is split into 15 min pieces;
  - a 15 min piece still above ABR 1 leaves that Worker BLIND, with no further split.
  - Every count then comes from an ABR-1 read in one run.
- **Its predicted cost, for all ten (HTTP calls):**
  - the 6 h pass: 270 queries, at most 540 calls with paging;
  - the 1 h splits: at most 6 times the sampled chunks, about 900-1,000 calls; worst case 1,620;
  - 15 min splits: not expected;
  - **hard cap: 2,500 calls.**
  - The ruling below narrowed the read to opuk alone, with its own cap.
- **Its rules:** the verdict rule is unchanged (CLEAN / EARLY / STORED / BLIND; early cut 11-11 20:30Z; counts only).
  An API error or a 429 makes the Worker BLIND and stops the run. A BLIND after this second configuration is reported
  as is, with no third.

**RULED (the reviewer, about 10:43Z): opuk alone, under method B, with a cap of 300 calls.**
- C-R3's T_R is opuk's hour, so opuk's read answers the question. The nine stay recorded as BLIND, with their
  information lines as they are.
- ABR is printed per chunk and per piece.
- If the owner's go arrived mid-read and the logs step needed the session, the deploy would come first: the read would
  stop and opuk would stay BLIND.

**Predicted for opuk, before the run.**
- 27 six-hour queries: at most 54 calls with paging.
- Plus 6 per sampled chunk, about 96.
- No 15 min splits.
- In all about 125-150 calls, under the cap.
- Verdict CLEAN or EARLY. STORED (any storing line at or after 11-11 20:30Z) would be outside the set.

**The controls, 10:42:50-10:42:53Z, each as predicted:**
- (i) opuk 10:01-10:04Z, cut 10:01:30Z: STORED, exit 1.
- (ii) the same with a forced split (control only): split at 6 h and at 1 h, still STORED from the 15 min piece.
- (iii) a cap of 0: STOP before the first call, BLIND, exit 3.
- Corrected before it ran: (iii) was first written as cap 1, which the one-call window would have allowed, so nothing
  would have stopped.

**opuk, method B, 10:43:00-10:46:48Z: EARLY, inside the predicted set.**
- 81 calls of the 300 cap, 0 errors.
- 27 six-hour chunks: 18 at ABR 1 (1,076-1,080 events; 333 in the last, partial chunk). 9 at ABR 10, split into 54
  one-hour pieces, all ABR 1 (176-180 events each). 0 fifteen-minute splits.
- 72 ABR-1 pieces, 28,381 events.
- 0 stored or backfilled lines at or after 11-11 20:30Z; 1 stored line (7c828e17) between 20:10Z and 20:30Z. That can
  only be the 20:10:00Z frame itself: the primary's last key was OPERA@20261111T2010, every OPERA latest read 1794427800
  afterwards, and the old version lists only the primary.
- **So T_R stands: the 2026-11-18 10:00Z hour, source backup.**
- A prediction miss: 81 calls against 125-150, because fewer chunks were sampled than in the first run.
- The nine stay BLIND, as ruled.

**(a) /healthz 11:10:23Z:** all 10 "backup", stale false, frameAgeS 325 (the 11:05:00Z frame).

### 7b. The owner's word on the held state (2026-11-18 15:02Z, relayed by the reviewer)

**The owner's words, verbatim:** "radar: keep. No rollback. Keep the smoke-test count on the record as a miss, and
before Thursday have the smoke test's prediction name the checks that depend on live traffic."

- **The radar deploy is KEPT, and that is final.** All ten Workers stay on the versions in 7a's tables, and no rollback
  is taken.
- **C-R2 stays recorded as a MISS** (7a). It is not re-graded.
- **The traffic-dependent smoke checks are named for the next smoke prediction.** That is in
  `docs/workers-logs-invocation-off.md` 7b and 7f: T1-T3 are the name family, NOT COMPARABLE when their names differ; T4
  is named for its verdict and graded as written.
- The logs step and the route run were re-booked by the owner: logs Thursday 2026-11-19 12:30-15:00Z (Friday
  fallback), and the route run Friday 2026-11-20 10:00Z. The route run is re-registered against a new upstream in
  `docs/route-weekly-code.md` 4h and 4i.
