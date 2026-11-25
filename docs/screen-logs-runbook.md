# The screen service's logs: 10% sampled, and full logs for ONE day when chasing a fault

The screen service (`blipscope-screen`, `proxy/screen.wrangler.toml`) keeps Workers Logs at **10% sampling** in production, with invocation logs off. The owner ruled this on 2026-11-25 ("screen service logs: sample 10%"), on two conditions:

1. **Every request and every error is still counted, unsampled,** in the service's own Analytics Engine dataset. Logs are a sample; the counts are the record.
2. **There is a written way to go to full logs for a day** when chasing a fault. This page is that way.

**Status:** the 10% rate and its check (`scripts/check-screen.ts`, P-O1) are in place. The unsampled counter, the full-logs deploy target, `scripts/deploy-screen.sh`, `/health`'s `logs` field and the watch workflow all land with the service's **phase 1**. Until then, nothing is deployed to turn up.

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

1. **On.** Deploy the full-logs target, with an end time no more than 24 hours ahead:

   ```
   bash scripts/deploy-screen.sh production-fulllogs --until <ISO UTC time, at most 24 h ahead>
   ```

   - `[env.production-fulllogs]` is the SAME script name, `blipscope-screen`, with `head_sampling_rate = 1.0`.
   - The deploy stamps the end time into the bundle (`FULL_LOGS_UNTIL`). It refuses an end time more than 24 h ahead, or one in the past.
   - Then read `/health`: `logs.rate` must be `1.0` and `logs.until` the time you gave.
2. **Chase the fault.** Logs are kept for Workers Logs' usual retention.
3. **Off: THE STEP THAT ENDS THE DAY.** At or before the end time, deploy the normal target:

   ```
   bash scripts/deploy-screen.sh production
   ```

   Then read `/health`: `logs.rate` must be `0.1` and `logs.until` `null`.

## Why full logs cannot silently stay on

**The act** that ends the day is step 3. **The check** that it happened is a scheduled workflow, `.github/workflows/screen-logs-watch.yml`:
- it runs every 3 hours, and on demand; its triggers are `schedule` and `workflow_dispatch` only, with `permissions: {}`;
- it makes ONE anonymous GET of the service's public `/health`, read by `proxy/scripts/screen-logs-verdict.mjs`.

| `/health` says | verdict | the workflow |
|---|---|---|
| rate 0.1, `until` null | GREEN | passes |
| rate 1.0, `until` still ahead | GREEN (a full-logs day in progress) | passes |
| rate 1.0, `until` in the PAST | **RED: "full logs past their day"** | fails |
| rate 1.0, no `until` | **RED: "full logs with no until"** (a bundle the deploy script did not build) | fails |
| unreachable, not 200, or no `logs` field | **BLIND** | fails, with its own message |

- **Its selftest runs in every PR's CI**, on canned answers: an end time an hour ago must read red, and one an hour ahead must read green.
- **How a red run reaches a person** (the account notified of a failed scheduled run) is to be read from GitHub's documentation when the workflow is built. It is then proven once, by a manual run against a stub answering "an hour ago" whose notification is seen arriving.
