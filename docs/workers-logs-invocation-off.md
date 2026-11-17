# Workers Logs: invocation logs off, our own lines kept (predictions before the deploy; written 2026-11-17)

**The owner's ruling (2026-11-17, about 05:27Z, relayed by the reviewer; verbatim):** "logs: off. Confirm afterwards
that our own error and breaker log lines still arrive."

**DRAFT. Not pushed.** It waits for the owner's word on section 4, (i) or (ii). The slot in section 7 needs the owner's
OK through the reviewer. Nothing here deploys on merge: `workers.yml` has no deploy step.

## 1. The setting, from Cloudflare's documentation

Cloudflare's Workers Logs page (developers.cloudflare.com/workers/observability/logs/workers-logs/, "Last updated Nov
12, 2026") describes two kinds of stored log among others. Verbatim:

- "Each Workers invocation returns a single invocation log that contains details such as the Request, Response, and
  related metadata. These invocation logs can be identified by the field `$cloudflare.$metadata.type =
  "cf-worker-event"`."
- "Invocation logs can be disabled in wrangler by adding the `invocation_logs = false` configuration." The TOML form is
  `[observability.logs]` and `invocation_logs = false`.
- "Logs include invocation logs, custom logs, errors, and uncaught exceptions."
- "Any `console.log` statements within your Worker will be visible in Workers Logs."

The page does not say that turning invocation logs off removes custom (`console.log`) logs. It does not say they stay,
either. **So prediction (b) is a real test and not a formality.**

The wrangler this repo pins (4.35.0) knows the key. Its `config-schema.json` has `Observability.logs.invocation_logs`, a
boolean described as "Set to false to disable invocation logs", and `cli.js` reads it.

## 2. The change

`proxy/wrangler.toml` gains one subtable per environment. Nothing else in the file changes. Staging carries it so that
staging proves it first:

```toml
[env.staging.observability.logs]
invocation_logs = false

[env.production.observability.logs]
invocation_logs = false
```

`enabled = true` and `head_sampling_rate` are unchanged in both environments. **Production samples 100%**
(`head_sampling_rate = 1.0`, "FULL logging during the pilot"), and so does staging. The `0.05` in the comment above
staging's block is the old production figure, not a current one.

**Read from the resolved config, not from the file.** wrangler 4.35.0's own `unstable_readConfig` reads both
environments. The reader is the session's `obs_config.mjs`; it runs with no network and no account, and wrangler's debug
log goes to a temp dir that it removes.

| toml | staging `observability` | production `observability` |
|---|---|---|
| main `b4f7e511` (the control: it can read a block with no `logs`) | `{"enabled":true,"head_sampling_rate":1}` | `{"enabled":true,"head_sampling_rate":1}` |
| this branch | `{"enabled":true,"head_sampling_rate":1,"logs":{"invocation_logs":false}}` | the same |

Both reads were predicted before they ran, and both read as predicted. The config warnings wrangler prints are the
same for both files ("unsafe" fields, and the top-level `BASEMAP_PKG` R2 binding), so they predate this change.

## 3. What stays: our own lines

The Worker's own lines are `console.log` lines, so they are custom logs and are not invocation logs:

- `evt:"req"`, one per request, from `metrics.ts` `record()`. It runs on every request, including an anonymous
  `GET /healthz`.
- `evt:"upstream"`, one per upstream attempt.
- `evt:"upstream_skip"`, a source not called and why: `disabled_by_config`, `breaker_open` or `hold_4xx`.
- `evt:"breaker"`, which fires only on a breaker transition.
- `evt:"error"`, on a 500.

The owner's condition names the error and breaker lines. Neither can be triggered harmlessly: a breaker opens on
upstream failures, and an error is a 500. So:

- (b) uses the `evt:"req"` line of an anonymous `GET /healthz`. It is the same `console.log` path, it is triggered
  without a key, and it sends no identity.
- (c) adds the production count of `evt:"upstream_skip"` lines. That line fires in the normal course of production
  traffic.
- `evt:"breaker"` and `evt:"error"` are counted as information, never graded. A window can honestly hold zero of
  either.

## 4. What the change does NOT do (the owner's ruling, pending)

`evt:"req"` carries `dev`, the authenticated device id, on every authenticated request (`setDeviceAttribution()`;
never the key). It is our own line, so `invocation_logs = false` keeps storing it. **"No invocation event" is
therefore not "no device id in Workers Logs".**

- **(i)** This PR as scoped. The owner accepts `dev` in our `req` line.
- **(ii)** Also drop `dev` (and `fw`?) from the console line only, keeping them in the Analytics Engine point. This is a
  code change, with its own tests and red-first registration, outside this PR's present scope.

The owner's word: _pending._

## 5. Predictions, frozen before the deploy

**The reader is not this session's.** Every stored-log read below is the Workers Observability telemetry query, run by
the map-server session as its own registered instrument with the observability-read token it already holds. Reads
print **counts only**, never an event, a field value, a key, an id or a URL. `wrangler tail` is excluded: it reads the
live stream, which these storage settings do not govern, so it cannot fail on (a).

**Windows.**
- Each deploy's window opens 2 minutes after C1 (the deploy plan's stamp check) shows the new commit on `/healthz`, so
  that an old isolate's last requests fall outside it.
- It closes 10 minutes later.
- Each count is read twice, at window close +10 min and +20 min (ingestion lag). The two reads must agree; a count still
  moving between them is not a reading.
- The control windows are the same length, ending before the deploy.

| | after the deploy | its control, in the window BEFORE the deploy |
|---|---|---|
| **(a)** staging | 0 invocation events (`$cloudflare.$metadata.type = "cf-worker-event"`) for `blipscope-proxy-staging`, with the triggering requests sent inside the window | the same query counts **> 0** invocation events for the same triggering requests |
| **(b)** staging | **> 0** `evt:"req"` lines with `ep = "/healthz"` from the anonymous `GET /healthz` requests sent inside the window | the same query counts **> 0** such lines |
| **(c)** production | (a)'s and (b)'s checks for `blipscope-proxy`, plus **> 0** `evt:"upstream_skip"` lines; `evt:"breaker"`/`evt:"error"` counts printed as information | **> 0** invocation events; **> 0** `evt:"req"` lines; **> 0** `evt:"upstream_skip"` lines |
| **(d)** both | 0 stored events carrying ANY request-header field (`user-agent`, `x-blip-model`, `x-blip-key`, `x-blip-device`) | the same query counts **> 0** events carrying the `user-agent` field (proving that it can see a request-header field at all) |

**The non-vacuity control for (d).** A zero count means nothing in a window that held no request carrying the field.
- **`user-agent`** is carried by this session's own triggering requests, so it is non-vacuous in every window. It is
  graded on staging and on production.
- **The two identity headers:**
  - **Production:** the window must also hold **> 0** `evt:"req"` lines with a non-empty `dev` (under (i)), or with
    `authPath = "device"` (under (ii)). Devices send those all the time. Production grades the identity headers.
  - **Staging** has little device traffic. Whether ONE request carrying the test identity is sent there, and only
    **after** (a) has read 0, is the reviewer's to rule. Without it, staging's identity-header counts are information
    only.

The triggering requests are 5 anonymous `GET /healthz`, at least 30 s apart, sent inside each window by this session.
They carry no key and no device header. The count is stated so that the reader can match it: (b)'s after-count is
**at least 5** on staging.

**Predicted: (a) 0, (b) at least 5, (c) as (a) and (b) plus upstream_skip above 0, (d) 0, with every control above 0.**

## 6. Outcomes, decided before the reading

| reading | meaning | action |
|---|---|---|
| all as predicted, every control above 0 | invocation logs are off, our lines arrive, and no request-header field is stored | record it; production follows staging in the same slot |
| (a) > 0 after, its control > 0 | the setting did not take: invocation events are still stored | no rollback (no worse than before); stop and report |
| (b) or (c)'s `req`/`upstream_skip` = 0 after, its control > 0 | our own lines stopped arriving, which fails the owner's condition | **roll back that environment at once**; report |
| (d) > 0 after, (a) = 0 | header fields are stored somewhere other than the invocation event | no rollback (no worse than before); stop and report to the owner |
| any control = 0 | the reader cannot see what it would need to see | **BLIND**: no verdict; stop |
| anything else, or the two reads disagree | outside the set | stop and decide; nothing is improvised |

Rollback is through the deploy plan's section 4 form (`node scripts/wrangler-run.mjs rollback <id> --env <env>`), to
the ids read at that slot's D0.5. A rollback is never refused.

**Information, never graded.** Stored Workers Logs volume should fall by about the invocation events' share, at 100%
sampling about one event per request. The account budget reads it at its next scheduled read.

## 7. The slot (proposed; needs the owner's OK through the reviewer)

**Wednesday 2026-11-18, 13:00-14:00Z.** That is clear of the 10:00Z radar deploy and the 16:00Z route run, and it is
not tonight's 23:00Z deploy of #736, which stays exactly as approved. The slot's steps:
- the deploy plan's D0 checks, re-listing the carried commits from `/healthz`'s stamp to main;
- the staging deploy, then (a), (b) and (d) on staging;
- only if staging reads as predicted, the production deploy, then (c) and (d) on production.

Each deploy goes through `proxy/scripts/deploy.sh`, under the deploy claim, with the deployer lock confirmed with the
reviewer first.

## 8. Record

_Filled in after the slot, below this line, without editing anything above it._
