# Session claims: who is deploying, who is merging (a design, predictions first)

**A design for review. No code exists yet.** Until it is approved and in place, sessions merge under Q-M1 alone and announce deploys
through Daniel (reviewer, 2026-11-07 ~16:46Z).

**The problem, on the record:** merges race on main (#583 missed four slots, #587 needed three attempts, #596 three), and on
2026-11-07 a dev deploy landed inside a staging slot because the slot's "one deployer" check was a single read.

## The requirements, verbatim (reviewer chat, pasted by Daniel, ~16:46Z)

> - A local file on Daniel's PC, outside any git tree, that every session on the machine can read and write (beside the board
>   registry in ~/.config/blipscope/). Not on main, for exactly the reason you give.
> - Two claims, not one: a deploy claim (Workers and env) and a merge claim (the PR). A three-hour M run holds the deploy claim and
>   must not stop merges.
> - A claim holds the session, what it covers, its start and its expected end, all in UTC. Taking one is atomic (exclusive create),
>   so two sessions can't both hold it. The owner releases it after its read-back or squash-tree check. An expired claim is
>   reported, never overwritten.
> - deploy.sh refuses unless the caller holds the deploy claim, and the merge driver refuses unless it holds the merge claim. A
>   selftest plants another session's claim and an expired one.

From ~16:33Z: claim main before the final re-read of its CI, merge, compare the squash tree, and release right after. A claim you
didn't write means you wait, not race. Q-M1 is unchanged; this only orders the sessions.

## The design

**One small tool, `scripts/session-claim.py`,** built on what `scripts/board-port.py` already does for board reservations: exclusive
create, an unreadable claim counts as held, and only the holder releases.

| | |
|---|---|
| **Files** | `~/.config/blipscope/claims/deploy.json` and `merge.json`, one holder each. `BLIPSCOPE_CLAIMS_DIR` overrides the folder, for the selftest only |
| **A claim holds** | `session`, `covers`, `since`, `until` (UTC), and `host` |
| **Who is asking** | `BLIPSCOPE_SESSION`, set per shell. Unset is refused, never a default |
| **`take <claim> --covers ... --until ...`** | exclusive create. 0 taken; 3 held by another session, naming it; 4 expired or unreadable, naming it |
| **`check <claim> --covers ...`** | 0 only if the caller holds it, it covers what is asked, and it hasn't expired |
| **`extend <claim> --until ...`** | the holder only, and only before it expires |
| **`release <claim>`** | the holder only. An expired claim can still be released by its holder |
| **`show`** | prints both claims and their state |

- **What "covers" means:** the deploy claim lists `worker:env` pairs, such as `proxy:staging,builder:staging`. There is one deployer
  at a time, so the list says what the holder may deploy, and it doesn't let a second session in. The merge claim names the PR.
- **An expired claim:** nothing overwrites it, so everyone is stopped until its holder releases it or a person deletes the file.
  The tool has no override.

**`deploy.sh`** checks the claim first, before any other work, for the Worker and env it was asked to deploy.

**The merge driver** moves to `scripts/merge-by-rule.sh` with `ci-wait.sh`, so there is one copy, which `docs/basemap-a1/instruments/`
holds today. After main's CI has finished it takes the merge claim, does the final re-read, merges, compares the squash tree, and
releases on every exit path. The claim lasts 10 minutes, since those steps take under two. Another session's claim means it waits,
polled, capped at 15 minutes.

**`session-claim.py` counts as deploy machinery** under Q-DB1, so a checkout behind on it blocks a deploy.

## What it can't stop

**Only `deploy.sh` and the merge driver refuse.** A file can't stop a command that never reads it, and these paths don't:

| path | today |
|---|---|
| `wrangler secret put` | K1's own secret, and `rotate-staging-provision-token.sh` |
| the wx-ingest, opuk and dev Workers | deployed by hand with `wrangler`; no script |
| the dashboard | `npm run deploy:production` |
| `wrangler rollback`, `wrangler versions deploy` | by hand |
| merges by any other route | `gh pr merge` by hand, or GitHub's web page. Q-M1's own re-read stays the backstop |

The 02:53:38Z deploy that started this was on the second row, so `deploy.sh` alone would not have refused it.

**Q-CL1, recommended:** a Claude Code `PreToolUse` hook that refuses those commands unless the session holds the claim. It fires by
itself, on every path, for every session on this machine. It changes Daniel's settings, so it needs his OK.

## Checks, registered before any code

`--selftest` runs these against a temporary folder. `deploy.sh`'s and the driver's rows use a stub `npx` and a stub `gh` that count
their calls, so nothing deploys or merges.

| row | check | predicted |
|---|---|---|
| **CL1** | take when free | exit 0; all five fields present; both times UTC |
| **CL2** | **another session's claim planted,** then take | exit 3, naming the holder; the file's bytes unchanged |
| **CL3** | 20 takes at once | **exactly 1** exit 0, 19 exit 3 |
| **CL4** | check | holder 0; another session 3; no claim 3; `BLIPSCOPE_SESSION` unset 2 |
| **CL5** | a claim on `proxy:staging`, check `builder:staging` | exit 3 |
| **CL6** | **an expired claim planted,** then check and take, as holder and as another session | exit 4 each, naming it; the file's bytes unchanged |
| **CL7** | release | another session 3, file unchanged; holder 0, file gone |
| **CL8** | an empty file, and one that isn't JSON | exit 4; the file's bytes unchanged |
| **CL9** | deploy claim held, take the merge claim | exit 0: the two are independent |
| **CL10** | `deploy.sh` with no claim, another's, an expired one, and session unset | refuses each time, with **0** stub calls. **Control:** the holder reaches the stub, 1 call |
| **CL11** | the merge driver, the same four | refuses each time, with **0** `gh pr merge` calls. **Control:** the holder reaches it, 1 call, and the claim is gone afterwards |

| plant | what it does | predicted failing set |
|---|---|---|
| **S-CL-NOEXCL** | a plain write in place of exclusive create | **{CL2, CL3, CL6, CL8}**: `take` never sees an existing file |
| **S-CL-EXPIRY** | `take` treats an expired claim as free | **{CL6}** |
| **S-CL-NOOWNER** | any session can release | **{CL7}** |
| **S-CL-NOCOVER** | `check` ignores `covers` | **{CL5}** |
| **S-CL-DEPLOY** | `deploy.sh`'s check removed | **{CL10}** |
| **S-CL-MERGE** | the driver's take removed | **{CL11}** |

**Anything else: stop and report.**

**Where it runs:** the selftest and plants as steps in an existing CI job, so the job set doesn't change. The claims live on Windows
and CI runs Linux, where file locking differs, so the selftest also runs on this machine before the PR.

**The reader:** `deploy.sh` and the driver print `show` on every refusal, and each slot's close prints it into the record.

## Open, for the reviewer

- **Q-CL1:** the hook above, for Daniel.
- **Q-CL2:** the merge claim as ruled gives order, not fairness. A session waiting on main's CI still restarts its wait if another
  session merges first, which is what cost #596 two attempts. Holding the claim across the CI wait would prevent that, at 15-25
  minutes a merge with everyone else waiting. **I recommend keeping it as ruled,** and counting how often a merge loses its slot.
- **Q-CL3:** the order. I'd build this after the combined slot and the ocean proposal: about a day, with its own PR.

**CI for this PR (docs-only), predicted: 32 check runs: 29 success, 3 skipped (`promote`, `ship-alert`, `version`), 0 failed.** The
baseline is #606's finished docs-only run (32 = 29/3), which postdates `caf63cc9`, the last change to the set of jobs.
