# The screen service's logs: 10% sampled, and full logs for ONE day when chasing a fault

The screen service (`blipscope-screen`, `proxy/screen.wrangler.toml`) keeps Workers Logs at **10% sampling** in production, with invocation logs off. The owner ruled this on 2026-11-25 ("screen service logs: sample 10%"), on two conditions:

1. **Every request and every error is still counted, unsampled,** in the service's own Analytics Engine dataset. Logs are a sample; the counts are the record.
2. **There is a written way to go to full logs for a day** when chasing a fault. This page is that way.

**Status:** phase 1's logs part is built and NOT deployed. In place:
- the 10% rate and its check (`scripts/check-screen.ts`, P-O1);
- the unsampled counter (`src/screen/metrics.ts`, bound as `SCREEN_METRICS` in every env, P-O4);
- the full-logs env `[env.production-fulllogs]` (P-O3);
- `/health`'s `logs` field (P-O2);
- the deploy path, `scripts/deploy.sh --screen`, with its pre-deploy check (`scripts/screen-deploy-check.mjs`);
- the watch workflow, dispatch only.

The service has no route yet, so there is nothing to read `/health` from. The custom domain, the first staging deploy and the watch's schedule come next, the schedule in the same PR as the domain (P-W).

## What holds the 10% in place

- **P-O1** reads `proxy/screen.wrangler.toml` on every CI run. It requires:
  - production observability enabled;
  - `head_sampling_rate` exactly 0.1;
  - `invocation_logs = false` in every env;
  - no env above 0.1 except `staging`, `production` and `production-fulllogs`.
- Its selftest puts production back to 1.0 in a temporary copy and must see the check fail.
- **Never change sampling in the Cloudflare dashboard.** It changes the live script behind git's back, and the next deploy silently undoes it.

## A full-logs day

**Who and when.** The owner or the orchestrator decides that a fault needs full logs. The deploy is the map server's, under the deployer lock, like every production deploy.

**The deploy path** is `proxy/scripts/deploy.sh --screen`: the proxy's own guards, run for the screen service. It needs:
- the deploy claim: **`screen:production`** for both production targets, because the full-logs env IS the production script;
- `EXPECT_SHA`, the full sha the plan names;
- a clean `proxy/`, with no `ALLOW_DIRTY` escape, since the stamp must be a commit;
- the commit on the remote;
- deploy-inputs' behind-check, target `screen`.

Before the upload, `scripts/screen-deploy-check.mjs` builds the env's own dry-run bundle. It refuses unless:
- the bundle holds the counter's write exactly once (P-M2);
- the `--until` rules hold (P-U);
- the end you gave survives into the bundle (P-S).

1. **On.** Deploy the full-logs target, with an end time no more than 24 hours ahead:

   ```
   EXPECT_SHA=<the plan's sha> bash proxy/scripts/deploy.sh --screen production-fulllogs --until <ISO UTC time, at most 24 h ahead>
   ```

   - `[env.production-fulllogs]` is the SAME script name, `blipscope-screen`, with `head_sampling_rate = 1.0`.
   - The deploy stamps the end time into the bundle (`FULL_LOGS_UNTIL`). It refuses an end time more than 24 h ahead, one in the past, a missing one, and an `--until` on any other env.
   - Then read `/health`: `logs.rate` must be `1` and `logs.until` the time you gave.
2. **Chase the fault.** Logs are kept for Workers Logs' usual retention.
3. **Off: THE STEP THAT ENDS THE DAY.** At or before the end time, deploy the normal target:

   ```
   EXPECT_SHA=<the plan's sha> bash proxy/scripts/deploy.sh --screen production
   ```

   Then read `/health`: `logs.rate` must be `0.1` and `logs.until` `null`.

## Why full logs cannot silently stay on

**The act** that ends the day is step 3. **The check** that it happened is a workflow, `.github/workflows/screen-logs-watch.yml`:
- **Today it runs on demand only** (`workflow_dispatch`, inputs `url`, or `body` to grade a given answer with no network).
- **The schedule, every 3 hours, arrives in the same PR as the service's custom domain:** until then there is nothing to read.
  - `scripts/check-screen.ts`'s P-W refuses a production route without the schedule, and a schedule without the route.
- **Its only permission is `contents: read`,** to check out the one script.
  - The registration said `permissions: {}`. A dispatch-only workflow runs only from the default branch, so whether checkout works under `{}` could not be proven before merge.
  - On a public repo, `contents: read` grants nothing the public does not already have.
- **It makes ONE anonymous GET** of the service's public `/health`, read by `proxy/scripts/screen-logs-verdict.mjs`.

| `/health` says | verdict | the workflow |
|---|---|---|
| rate 0.1, `until` null | GREEN | passes |
| rate 1.0, `until` still ahead | GREEN (a full-logs day in progress) | passes |
| rate 1.0, `until` in the PAST | **RED: "full logs past their day"** | fails |
| rate 1.0, no `until` | **RED: "full logs with no until"** (a bundle the deploy script did not build) | fails |
| unreachable, not 200, no `logs` field, `rate` null (a bundle nothing stamped), or any shape not named above | **BLIND** | fails, with its own message |

- **Its selftest runs in every PR's CI** (workers.yml), on canned answers.
  - An end time an hour ago must read red, and one an hour ahead must read green.
  - Then a copy comparing the end the wrong way round must flip exactly those two cases.
- **How a red run reaches a person** (the account notified of a failed scheduled run) is read from GitHub's documentation in the PR that adds the schedule. It is proven once there, by a run grading a `body` whose end was an hour ago, whose notification is seen arriving.
