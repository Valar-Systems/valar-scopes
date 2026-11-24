# #747's support page alone on production's commit: a page-only deploy, if the owner says go today (plan, predictions first)

Written 2026-11-18, BEFORE the branch exists, at the reviewer's request (~12:36Z). Nothing deploys without the owner's
word AND the reviewer's go.

**The owner's rule (2026-11-17 16:44Z, verbatim):** "privacy page: with Wed logs deploy. If that deploy slips, the page
gets its own slot the same day." The logs deploy slipped at 12:30Z (no owner answer). Production's support page still
serves the old false sentence.

**Why a branch, not main.** main's EXPECT (4a02123c) carries #744 and #736 as well, so deploying the page from main IS
the logs step. The page alone needs production's own commit plus #747's page and nothing else.

**Slot (the reviewer):** today 15:00-17:00Z, only on the owner's go by 16:00Z. That is after the gains bench, which reads
the live proxy and ends at 14:47Z, and clear of M3-9's 17:50Z T-10. No go by 16:00Z: the page rides Thursday's logs step
as planned, and this branch is discarded.

## 1. The branch: 074863fc plus #747's page, and nothing else

- **Base:** `074863fc90c156f142afbb1ce96e45736a823396`, the commit both environments' /healthz stamps.
- **Content:** #747's two files under proxy/, `proxy/pages/support.html` and `proxy/src/pages.generated.ts`, taken
  BLOB-IDENTICAL from `4a02123c`. That is `git checkout 4a02123c -- <the two>` in an LF detached checkout of the base,
  then one commit. No branch is created from main and no merge is made.
- **Why only those two, read before the branch:** #747 touched five files. Its patch applies cleanly to 074863fc for
  four, but `.github/workflows/device-ids.yml` does not apply (#746 changed it in between). Of the five, only the two
  under proxy/ are in what the proxy ships. The other three are not deployed:
  - `.github/workflows/device-ids.yml` (CI);
  - `README.md` (served by GitHub from main, where it is already live);
  - `scripts/check_privacy_text.py` (the check CI runs).
- **No other commit touched the two files** between 074863fc and #747's parent (read: only #746 touched any #747 file
  in 074863fc..13a6dab8, and that was the workflow).

**Predicted, read after the branch is made (B-rows); any other reading: no deploy.**

| | read | predicted |
|---|---|---|
| B1 | `git diff --name-status 074863fc BRANCH` | exactly `M proxy/pages/support.html` and `M proxy/src/pages.generated.ts` |
| B2 | the blob sha of each at BRANCH, and at `4a02123c` | equal, both files |
| B3 | `git diff --name-status 13a6dab8 4a02123c -- proxy/` (#747's own proxy/ change) | exactly the same two files: every byte #747 ships is in the branch |
| B4 | `git diff --name-only BRANCH 4a02123c -- proxy/`: EVERY difference between the branch and what the owner approved | exactly the proxy/ files changed between 074863fc and 13a6dab8 (`git diff --name-only 074863fc 13a6dab8 -- proxy/`): #744's, #736's and #734's. None of them is either page file. Listed in full in the record |
| B5 | `node scripts/embed-pages.mjs --check` in the branch's proxy/ | exit 0: the generated module matches the page |
| B6 | the checkout: CRLF over proxy/src and proxy/scripts with the planted control; `npm ci`; porcelain over proxy/ | control 2, tree 0; exit 0; empty |

## 2. The deploy machinery at this commit, named

The branch runs 074863fc's `proxy/scripts/deploy.sh` (#747 does not touch it). The commit-pin change (#755) is not in
it, so the commit check is BY PROCEDURE (D0 below), as on Wednesday.

- **Behind main.** The tree is behind origin/main in what the proxy ships. deploy-inputs.mjs is predicted to list
  EXACTLY three commits, and nothing else:
  - `c7f73d10` (#744);
  - `00fc75d1` (#736);
  - `4a02123c` (#747: its content IS in the branch, but its commit is not an ancestor).
  The deploy therefore needs **`ALLOW_BEHIND=1`, named here explicitly.** The list the script prints must equal those
  three, or there is no deploy. Nothing in production is reverted: production runs 074863fc, which has neither #744 nor
  #736.
- **On the remote.** deploy.sh refuses a commit that exists only locally. One of these, for the reviewer's ruling:
  - (i, preferred) push the branch as `deploy/page747-on-074863fc`, with no PR, so that the commit exists off this
    machine, for the script's own provenance reason. CI's response to that push is predicted from the workflows'
    triggers before the push, and the push waits for main's CI to be idle;
  - (ii) `ALLOW_UNPUSHED=1`, named explicitly.
- **D0, printed before each upload; any mismatch means no deploy:**
  - D0-H: `git rev-parse HEAD` = BRANCH, in full;
  - D0-P: `git rev-parse HEAD^` = 074863fc, in full;
  - D0-D: porcelain over proxy/ is empty;
  - D0-S: /healthz `commit` on staging and on production is `074863fc`, each read twice at least 60 s apart;
  - D0-V: each environment's current version id is written down as its rollback target.
- **The claim** covers `proxy:staging`, then `proxy:production`. **The deployer lock** is confirmed with the reviewer
  first.

## 3. Checks, staging first, then production from the SAME commit

**Before (both environments, read-only):**
- S0: a fresh smoke, as the test identity, verdicts by name;
- B: /healthz twice, using rtref-tools/b736_healthz.py read;
- PS-0: one anonymous `GET /blipscope/support`.

**How PS reads a page.** Every run of whitespace in the served HTML is collapsed to one space before matching. The new
sentence is WRAPPED in the source (`...keeps these logs for` / `up to seven days.`, lines 544-545 of 4a02123c's
support.html), so a literal match on the raw HTML reads it ABSENT on a correct page. That would read as "#747 did not
ship" and roll back a good deploy. Read 2026-11-18 ~12:40Z: a raw grep of 4a02123c's page counts the new sentence 0
times. **The same reading applies to 7a.4's P-S on Thursday; it is reported to the reviewer for that plan.**

PS has two CONTROLS:
- **PS-L (local, before any deploy):** the same matcher run on the branch's own `proxy/pages/support.html`. It must read
  the new sentence present and the old one absent. This proves the matcher can see the new sentence in the wrapped
  source; without it, a PS that reads "absent" cannot be told from a matcher that cannot see.
- **PS-0 (live, before each deploy):** it must read the OLD sentence present ("nothing that identifies your device") and
  the new one absent ("Cloudflare keeps these logs for up to seven days"). If it does not, PS cannot fail, and the
  reading is BLIND.

**After each environment's deploy:**

| | check | predicted |
|---|---|---|
| C1 | /healthz `commit` | BRANCH's short sha, twice at least 60 s apart |
| PS | anonymous `GET /blipscope/support` | holds "Cloudflare keeps these logs for up to seven days"; does not hold "nothing that identifies your device" |
| H | /healthz against B, under the 7c rules: upstreams | 7 entries (no `adsbdb_ac`: #736 is not in the branch), equal on id, enabled, order and key set, state printed as information; `ok`, the routes block (ageDays excluded) and the OPERA values equal to B |
| C4 | smoke, whole, outside any other window | every verdict equal to S0 by name, under 7b rules 1-3 (traffic SKIP <-> per-aircraft PASS is NOT COMPARABLE, one re-run within 10 min). The page-only deploy changes no enrichment code (B1, B4), so an enrichment half still NOT COMPARABLE after the re-run is recorded BLIND and reported, not a stop. Any FAIL graded below |

Production starts only after staging reads as predicted on C1, PS, H and C4.

## 4. Outcomes

| reading | action |
|---|---|
| a B-row or a D0 row off; deploy-inputs lists other than the three | no deploy |
| PS-L or PS-0 not reading as its control requires | BLIND: no deploy |
| C1: another stamp | roll back that environment |
| PS: the old sentence served, or the new one absent | roll back that environment; report |
| H: upstreams (id, enabled, order, keys), `ok`, routes or OPERA moved | roll back that environment; report |
| C4: a verdict that PASSED at S0 FAILS now | roll back that environment; report |
| anything else | stop and decide; nothing is improvised |

**Rollback:** `node scripts/wrangler-run.mjs rollback <D0-V's id> --env <env>`, from the branch checkout's proxy/. A
rollback is never refused.

## 5. Timeline, only on the owner's go by 16:00Z and the reviewer's go

| step | about |
|---|---|
| the branch is already built and B1-B6 read (done before the go) | |
| the lock confirmed; the claim taken; D0; S0, B and PS-0 on both environments | 15:00-15:12Z |
| staging: deploy, C1, PS, H, C4 | 15:12-15:30Z |
| production: deploy, C1, PS, H, C4 | 15:30-15:50Z |
| the claim released; the report | by 16:00Z (the slot ends 17:00Z) |

---

## 6. Appended below the frozen text (sha256 e2637de9b39e covers everything above this line)

### 6a. The commit this deploy ships (the owner's rule: every deploy plan names its exact commit)

**BRANCH = `fcc33692a8ba98fd0dd53a8629303e8b8e25a213`** on local branch `deploy/page747-on-074863fc`, parent `074863fc90c156f142afbb1ce96e45736a823396`. Built 12:37:32-12:38:00Z in an LF detached checkout (the deploying session's) as section 1 says. Rulings (the reviewer, about 12:37Z): plan accepted; route (i), PUSH the branch, no PR, with CI predicted and main's CI idle; build now.

### 6b. B1-B6, read 12:38Z

| | predicted | read |
|---|---|---|
| B1 | exactly the two page files | as predicted: M proxy/pages/support.html, M proxy/src/pages.generated.ts |
| B2 | blobs equal to 4a02123c's | as predicted: 4952ca2e6128 and 6aeb86200b04, equal |
| B3 | #747's own proxy/ change = the same two files | as predicted |
| B4 | every proxy/ difference from 4a02123c = 074863fc..13a6dab8's proxy/ files, no page file | as predicted: 18 files, the lists identical, 0 page files (FEED-SOURCING.md, README.md, device-label.selftest.mts, device-label.ts, sabotage-wx-opera-fallback.py, devlabel.ts, index.ts, metrics.ts, types.ts, upstreams/adsbdb.ts, upstreams/chain.ts, six tests, wrangler.toml) |
| B5 | embed-pages --check exit 0 | as predicted: exit 0, "pages.generated.ts is current (4 page(s))" |
| B6 | CRLF control 2 / tree 0; npm ci 0; porcelain empty | as predicted: 201 files, 0 CRLF; npm ci exit 0 (disk before 29.73 GiB, after 29.53); porcelain empty |
| PS-L | branch page: new present, old absent | as predicted. The raw, uncollapsed match reads the new sentence ABSENT, which is the defect PS-L exists for. The control on 074863fc's page reads new absent, old PRESENT. A first attempt at that control read an empty file (an MSYS path slip) and is not counted |

### 6c. The go, and the steps before the push (2026-11-18)

- **The reviewer's ruling (b), ~12:45Z:** hold the push until the owner says "page: deploy today".
- **The owner, 15:02Z, to this session directly:** "page: deploy today." The owner's conditions:
  - deploy fcc33692 from this plan; push it, read CI twice, deploy staging and check it, then production from the same commit;
  - ALLOW_BEHIND=1 is allowed for this deploy only;
  - wait for the reviewer's GO and the lock;
  - **if production has not started by 16:30Z, stop; the page rides Thursday's logs deploy;**
  - **if any post-deploy check fails, roll back at once to the ids D0-V records, and tell the owner.** This is stricter than
    section 4's last row ("anything else: stop and decide"): a failed post-deploy check is a rollback, never a stop.
- **The reviewer, ~15:04Z:** GO per this plan (e2637de9b39e) and the branch; the DEPLOYER LOCK is confirmed (no session
  it coordinates deploys or merges until this session's end line); production starts by 16:30Z; finish by 17:00Z, clear
  of M3-9's 17:50Z T-10. Afterwards, Thursday's logs plan re-reads its "before" commit and its rollback target, below its
  frozen text, so that a Thursday rollback cannot bring the old sentence back.
- **Disk, 15:04:10Z:** 30.46 GiB, above 29.5. The session's spare logs checkout is kept.
- **Before the push, all read 15:04-15:07Z, from the branch checkout at fcc33692:**
  - `python scripts/skus.py --check-readers`: exit 0, stderr empty;
  - the release workflow's file name and the second release-triggered workflow's file name: 0 hits in the commit
    message, the added lines and the two whole files;
  - personal names: 0 hits in the commit message and the added lines;
  - the branch name: 0 hits.
- **CI predicted for the push: 0 runs.** Read from fcc33692's own workflows (the pushed commit's tree decides):
  - every workflow with a push trigger filters on `branches: [main]`, except print-cards.yml;
  - print-cards.yml filters on paths (`docs/*card*.html`, its check script, itself), and the push matches none of them;
  - photo-drift.yml's workflow_run needs a run to finish first.
  - main's CI, read 1 at 15:05:36Z: 0 unfinished runs repo-wide.
- **deploy-inputs pre-read (15:07:15Z), the exact check deploy.sh runs, before any claim.** It ran
  `node proxy/scripts/deploy-inputs.mjs check proxy staging` from the branch checkout, a guarded `--dry-run` that uploads nothing.
  - Exit 1, as predicted: blocking commits = **exactly the three section 2 names**: 4a02123ca992 (#747), c7f73d10d2e7
    (#744), 00fc75d154e3 (#736).
  - Noted, not blocking: #734's three proxy/ files outside the set, and 18 commits behind that touch nothing under proxy/.
  - main had moved on to e1c725a0 since 4a02123c. Its three commits after 4a02123c touch three docs/ files only, so they
    are outside the set and do not change the three.
- **Graders, with controls (15:09Z).**
  - The session's grader grades `commit` (D0-S/C1) and `h` (H) over the /healthz snapshots that section 3's B reader
    writes.
  - Its controls ran against the 12:01:49Z production snapshot: 9 of 9 as required. A stale 074863fc stamp read against
    BRANCH is NO MATCH. A flipped `enabled`, an appended adsbdb_ac, swapped entries, a flipped `ok` and a moved OPERA value
    each read OFF. A flipped `state` is information only.
  - Two runners: one for D0 and the before-reads, and one per environment for C1, PS, C4 and H. Both read the two shas
    from this file; neither is typed.

### 6d. Push, CI, claim, D0, staging (2026-11-18)

- **devids:** fcc33692 merged into main e1c725a0 (merge 28eae76f). 27 of 27 exit 0, every stderr file empty.
  - The background-process check afterwards: 0 running.
- **main's CI before the push:**
  - read 1, 15:05:36Z: 0 unfinished runs repo-wide;
  - read 2, 15:16:38Z: 0 unfinished on main.
  - Three pull_request runs from 15:10:33Z were in progress on another session's PR (blipscope-5c/wx-layer-save-count).
    They are not main's.
- **The push:** started 15:16:49Z, through the session's guarded push wrapper, in the background, registered.
  - Guards: device ids 0, wifi 0, macs 0. `* [new branch]`; the remote equals local fcc33692 (15:19:01Z).
  - The background-process check: 0 running.
- **CI after the push:**
  - read 1, 15:19:34Z: 0 runs for the branch or its sha;
  - read 2, 15:20:40Z (63 s later): 0. **As predicted.**
  - Control on both reads: the same query sees 5 runs on the 5c branch.
  - A first attempt at read 1 ran gh outside the repo, exit 1, "not a git repository". That is an instrument failure,
    not a reading; it was redone with `-R`.
- **The claim:** taken 15:20:58Z: `deploy`, covers exactly proxy:staging,proxy:production, until 16:58:58Z.
  - The no-claim control (15:09:43Z), `DEPLOY_CLAIM_CHECK_ONLY=1`: deploy.sh refused, exit 1.
  - Held, the same seam: exit 0 on both environments.
- **START line sent:** 15:21:03Z.

**D0 and before (15:21:17-15:23:01Z), all as predicted**

| row | read |
|---|---|
| D0-H / D0-P / D0-D | fcc33692... = BRANCH; 074863fc... = base; porcelain over proxy/ 0 lines |
| D0-V | staging newest deployment 2026-11-16T16:10:41Z, **4464d8ab-15e8-4970-9c7d-97e26bcda9bd** at 100 %; production 2026-11-16T16:30:42Z, **2b92e9f1-73c8-4cb3-b2b0-b0090f6102a2** at 100 %. These are the rollback targets |
| D0-S | staging 074863fc at 15:21:20Z and 15:22:34Z; production 074863fc at 15:21:58Z and 15:23:01Z |
| PS-0 | both: status 200, 33020 bytes, the new sentence absent, the old one PRESENT. The control can fail |
| S0 | staging 15:21:22-15:21:57Z, production 15:21:59-15:22:34Z: each 39 PASS, 0 FAIL, 0 SKIPPED; OPERA backup x10; stderr 2 lines, byte-identical to the 12:00Z S0's |
| B (read 2) | both: ok true; 7 upstreams (adsb_lol, adsb_lol_b, adsb_fi, adsb_fi_b, airplanes_live off, adsb_lol_route, adsbdb_route off); routes rows 628956, stale false; OPERA backup x10. Production's adsb_lol_route state was open (information) |

**Staging deploy: 15:23:31-15:24:20Z, exit 0**
- deploy-inputs: exactly the three named commits; `ALLOW_BEHIND=1` printed.
- The secret is present.
- **Version e6de3d81-68e8-4858-b8a2-e59a03858441.**
- confirm-deploy: reads 1 and 2 returned 074863fc, then it was confirmed fcc33692.

**Staging after (15:24:28-15:25:30Z)**

| check | read | grade |
|---|---|---|
| C1 | fcc33692 at 15:24:28Z and 15:25:29Z (61 s apart) | as predicted |
| PS | status 200, 33586 bytes, the new sentence PRESENT, the old one absent. The raw, uncollapsed match reads new absent | as predicted |
| H | both reads against B: as predicted. No state moved | as predicted |
| C4 | 39 PASS, 0 FAIL, 0 SKIPPED; stderr byte-identical to S0's. By name, 2 differ: `/v1/photo (via enrich pointer .../photo:C152-...)` PASS -> absent, and `/v1/photo (via enrich pointer .../photo:R22-...)` absent -> PASS | **off-prediction by name; outside the set of 7b in `docs/workers-logs-invocation-off.md`** |

**Why C4 stopped.**
- The live aircraft changed between S0 and C4. The enrich check's name carries the live hex. The session's
  verdict-by-name diff normalises that hex (a rule stated before D4, 2026-11-16 16:13Z), so the enrich check reads equal: PASS on both sides.
- The photo-pointer name carries the same aircraft's photo key, and nothing normalises it.
- The logs plan's 7b rule 2 names only SKIPPED against a per-aircraft PASS; a PASS against a PASS under another aircraft's name is
  outside it.
- No rollback row in section 4 matches: nothing that passed fails now, and C1, PS and H are as predicted. So this is
  section 4's "anything else: stop and decide".
- The owner's rollback rule is for a check that FAILS, and none did. Staging is left as deployed.
- Production is held for the reviewer's ruling; asked at 15:26:54Z, with options (a) grade as a traffic name, (b) one
  re-run within 10 min, (c) roll back staging and stop.

**An instrument contradiction, resolved before reporting.** A raw `cmp` of the enrich lines read "different" while the
tool's diff read them equal. Both were correct: the raw compare does not normalise the hex. Lines were printed with
hex-like runs masked. A control `cmp` of two identical process substitutions reads exit 0.

### 6e. The ruling, staging's re-run, production, the end (2026-11-18)

**The reviewer's ruling (b), ~15:27Z.**
- The photo-pointer check is in the enrich/photo family that 7b was written for. A per-aircraft PASS against another
  aircraft's per-aircraft PASS is rule 2's phenomenon: NOT COMPARABLE by name, never a pass.
- Not (a): extending the normaliser after seeing the reading would be choosing the rule after the reading.
- One staging re-run by 15:35Z, under 7b rules 1-3. Still not comparable means BLIND, reported, and no stop (the C4 row).
  Any FAIL means an immediate rollback.
- Then production, with the same treatment.

**Staging re-run, 15:27:45-15:28:19Z.**
- 39 PASS, 0 FAIL, 0 SKIPPED; stderr identical to S0's.
- By name: the same 2 photo-pointer names (C152 -> R22).
- **Graded:** C4 = 39 PASS, 0 FAIL, 37 names equal, 2 photo-pointer names NOT COMPARABLE (traffic). That half is BLIND,
  reported, and not a stop.
- **Staging: C1, PS and H as predicted; C4 as just graded.**
- Re-run result and production's start line sent at 15:28:29Z.

**Production deploy: 15:28:40-15:29:04Z, exit 0.**
- deploy-inputs: the same three commits; the 67-path set listing is byte-identical to staging's; ALLOW_BEHIND=1 printed.
- The secret is present. **Version e663ea72-676b-4f53-90b7-f4135f7c45dc.**
- confirm-deploy: read 1 returned 074863fc, then it was confirmed fcc33692.
- stderr carries wrangler's warning that `BASEMAP_PKG` is top-level but not on env.production. That comes from 074863fc's
  own wrangler.toml, which the branch does not touch (B1). Information only.

**Production after (15:29:13-15:30:55Z)**

| check | read | grade |
|---|---|---|
| C1 | fcc33692 at 15:29:13Z and 15:30:14Z (61 s apart) | as predicted |
| PS | status 200, 33586 bytes, the new sentence PRESENT, the old one absent | as predicted |
| H | both reads against B: as predicted. Information: entry 6 adsb_lol_route state open -> closed | as predicted |
| C4 | 15:29:15-15:29:55Z: 39 PASS, 0 FAIL, 0 SKIPPED; stderr identical to S0's; 2 photo-pointer names differ (C152 -> R22). Re-run 15:30:24-15:30:55Z: 39 PASS, 0 FAIL; 2 differ (C152 -> B39M, a third aircraft) | 0 FAIL; 37 names equal; the photo-pointer half NOT COMPARABLE (traffic), BLIND, reported, not a stop |

**The end.**
- The claim was released at 15:31:06Z; deploy and merge both free.
- The 16:25Z deadline wake was stopped. The background-process check: PASS, 0 running.
- The process-group checks: PASS for each of the three background roots (the device-id run, the push and the wake).
- Disk: 30.43 GiB. END line sent at 15:31:39Z.
- **The owner's deadline was met:** production started at 15:28:40Z, before 16:30Z.
- **No check FAILED and nothing was rolled back.**

**What each environment runs now, which is Thursday's re-read.**
- Production: fcc33692 on **e663ea72-676b-4f53-90b7-f4135f7c45dc**.
- Staging: fcc33692 on **e6de3d81-68e8-4858-b8a2-e59a03858441**.
- The D0-V ids (2b92e9f1 production, 4464d8ab staging) serve the old sentence, so they must never be a Thursday rollback
  target.
- The branch `deploy/page747-on-074863fc` stays on the remote as the deployed commit's provenance. Deleting it is the
  owner's call.
- Thursday's logs step carries this re-read in its plan's 7e (`docs/workers-logs-invocation-off.md`). The
  traffic-dependent smoke names this deploy met are listed in that plan's 7f.
