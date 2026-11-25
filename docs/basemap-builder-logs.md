# The basemap builder's Workers Logs: (B), written, proven on staging, and left off in production

**Status: predictions first.** This PR is the first of D4's series (the ruling of 2026-11-18: "(ii), a PR on main after #696
merges, proven on staging in its own slot before D4's production deploy"). It changes one config file and adds this doc. It
deploys nothing. Its staging proof is registered separately, before its own slot.

## The choice it carries out (the owner, 2026-11-18)

- **A, in production:** the builder, `blipscope-basemap`, keeps no Workers Logs. Nothing about any request is stored there.
- **B, written and left off:** the builder's own build line only, with request (invocation) logs off, as on the proxy
  (`docs/workers-logs-invocation-off.md`). It is proven on staging first. In production it is written with `enabled = false`.
  Switching it on is that one line and a builder deploy, on the owner's word only, with its own read registered first.

## The change (`proxy/basemap.wrangler.toml` only; additions, nothing removed)

| env | before | after |
|---|---|---|
| staging | `observability` enabled, sampling 1; request logs **on** (the default) | the same, plus `[env.staging.observability.logs] invocation_logs = false` |
| production | **no** `observability` block (so nothing is stored) | `enabled = false`, sampling 1, `invocation_logs = false`: written and switched off |

Production's stored events are the same before and after: `enabled = false` stores nothing, as the absent block did.

## Read offline, before the push (no network, no account)

The config wrangler actually resolves, read with wrangler's own `unstable_readConfig` (the logs-off doc's method), with every
Cloudflare credential variable removed and the network blocked inside the reading process:

| file | staging | production |
|---|---|---|
| this PR's | `{"enabled":true,"head_sampling_rate":1,"logs":{"invocation_logs":false}}` | `{"enabled":false,"head_sampling_rate":1,"logs":{"invocation_logs":false}}` |
| main's (the control) | `{"enabled":true,"head_sampling_rate":1}` | absent |

Network attempts 0 and config warnings 0 on both reads. The control reads differently from the change, so the reader can see
it. The same reads, and two sabotaged copies (production `enabled = true`; staging `invocation_logs = true`), each failing
exactly its own check, were run on 2026-11-20 on these same file bytes, as registered.

## The staging proof (registered before its slot; after D3's END)

Red first, as the logs-off step was proven:
- **BEFORE,** with today's staging builder (request logs on): N fresh builds through the staging proxy as the test identity,
  at points no stored package answers. Predicted: the builder's request events N, and its own build lines N.
- **Deploy** the builder from this PR's head to staging only (`deploy.sh --builder staging`).
- **AFTER,** N new fresh builds. Predicted: request events **0**, exact beside the build lines' **N**; the request fields seen
  BEFORE (`url`, `content-type`) read **0**; one names-only read of the stored keys shows exactly the build line's fields.
- **Rollback:** the builder back to the version recorded before the deploy. Any control off: STOP, no verdict.

## CI for this PR, predicted before the push

- It touches `proxy/` and `docs/` only, so the same five pull-request workflows run as for #696.
- **Predicted: 39 check runs, 37 success, 2 skipped (`promote`, `version`), 0 failed,** with the same job names as #696's last
  run. It is tested on the merge into main as of the push, read from a job's own checkout log, and read twice at least 65 s apart.
- **Outside the set:** a tested base other than main at the push, or any other count, conclusion or job name: STOP, and read
  which job before anything else.

## Pins

| pin | claim | token |
|---|---|---|
| main when this was written | commit | 93ea561bc03c |
| the builder's config on main | blob of `proxy/basemap.wrangler.toml` at `93ea561bc03c` | 5e0edb988b1f |
