# Workers Logs: invocation logs off, our own lines kept (predictions before the deploy; written 2026-11-17)

**The owner's ruling (2026-11-17, about 05:27Z, relayed by the reviewer; verbatim):** "logs: off. Confirm afterwards
that our own error and breaker log lines still arrive."

**The owner's second ruling (2026-11-17, 05:47Z, relayed by the reviewer; verbatim):** "logs: neither as offered.
Cloudflare's per-request logs go off, and our own log line carries a short label instead of the device ID: the first 8
hex characters of the ID's SHA-256, no secret needed. Add a small helper that turns a device ID into its label, without
printing the ID. If that's more than a small change, do "drop device id too". Confirm our own line holds no IP address
either. logs slot: ok (Wed 12:30-15:00Z). Wednesday's chain rule: radar 10:00Z, logs ~14:00Z, route run 16:00Z; if one
fails or rolls back, the later ones wait for me."

**DRAFT. Not pushed** until the reviewer has reviewed the predictions. Nothing here deploys on merge: `workers.yml` has
no deploy step.

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

## 4. Our own line: a label, not the device id (the owner's ruling)

On main, `evt:"req"` carries `dev`, the authenticated device id, on every authenticated request
(`setDeviceAttribution()`; never the key). It is our own line, so `invocation_logs = false` alone would keep storing
it. **"No invocation event" is not "no device id in Workers Logs".**

Two options went to the owner: (i) keep `dev`, or (ii) drop it. He chose neither (above). The line now carries
**`devl`, the device LABEL**: the first 8 hex characters of SHA-256 over the normalised device id. There is no secret.
The change is small:

- **`src/devlabel.ts`** (new). `normDeviceId()` is the ONE rule for what a device id is: trimmed, lowercased, 8-32 hex.
  It moves out of `setDeviceAttribution()`, so the Worker and the helper cannot disagree. `deviceLabel()` computes the
  label with `crypto.subtle`.
- **`src/index.ts`.** One awaited line after `setDeviceAttribution()`, inside the already-async `route()`, sets
  `meta.devl`. It is failure-safe: a failure leaves the label out and never fails the request, and a missing label shows
  in (e) below. `record()` is synchronous and the digest is not, and there is no `nodejs_compat` flag; that is why the
  label is computed here rather than in `record()`.
- **`src/metrics.ts` `record()`.** The console line spreads the metric WITHOUT `dev`. The Analytics Engine point is
  unchanged: blob5 is still `dev`.
- **`scripts/device-label.ts`** (new), the helper. It reads ONE device id from stdin, never from an argument or the
  environment, and prints only its label. It never prints the id.

**A label is a PSEUDONYM, not anonymity:** device ids come from a small space, so anyone who holds the ids can recompute
every label. That fits our own logs, which is the intent.

**Other lines of ours still carry a device id**, and are outside this change until the owner rules on them:
- `enroll.ts` (two `enroll …` lines);
- `provision.ts` (`provision_mint` / `provision_refused`);
- `leaderboard.ts` (`lb_clamp`, `incoherent_row`).
They are per activation, per mint or per submission, not per request.

**No IP address in our `req` line**, read from the code at main `b4f7e511`:
- The line is `metrics.ts:229`, `{evt:"req", ...m}`. Its fields are `RequestMetric`'s (`metrics.ts:4-33`).
- No writer reads `cf-connecting-ip`, `x-forwarded-for` or any address in `request.cf`.
- `colo` is a data-centre code.
- Two fields are CLIENT-SUPPLIED text: `ep` (the path) and `model` (the raw `X-Blip-Model` header). They carry no
  address by design, but a scanner's path could be IP-SHAPED. The test below pins the line's EXACT key set, so a new
  field (an address, or `dev` coming back) turns it red.

## 5. Predictions, frozen before the deploy

**The reader is not this session's.** Every stored-log read below is the Workers Observability telemetry query, run by
the map-server session as its own registered instrument with the observability-read token it already holds. Reads
print **counts only**, never an event, a field value, a key, an id or a URL. `wrangler tail` is excluded: it reads the
live stream, which these storage settings do not govern, so it cannot fail on (a).

**Windows.** Every window is 10 minutes long, and every count is read twice, at window close +10 min and +20 min
(ingestion lag). The two reads must agree; a count still moving between them is not a reading.
- **BEFORE:** one window per environment, both ending before the staging deploy. It holds the controls.
- **A:** staging. It opens 2 minutes after C1 (the deploy plan's stamp check) shows the new commit on `/healthz`, so
  that an old isolate's last requests fall outside it. It holds (a), (b) and (d)'s `user-agent`.
- **W2:** staging. It opens only after (a) has read 0 on both of A's reads, and holds (d)'s identity headers with (a)
  and (b) again.
- **P:** production. It opens 2 minutes after C1 on production, and holds (c) and (d).

| | after the deploy | its control, in the window BEFORE the deploy |
|---|---|---|
| **(a)** staging | 0 invocation events for `blipscope-proxy-staging`, with the triggering requests sent inside the window. The stored key is `$metadata.type`; Cloudflare's page names the value `"cf-worker-event"`, which the reader verifies before the slot (below) | the same query counts **> 0** invocation events for the same triggering requests |
| **(b)** staging | **> 0** `evt:"req"` lines with `ep = "/healthz"` from the anonymous `GET /healthz` requests sent inside the window | the same query counts **> 0** such lines |
| **(c)** production | (a)'s and (b)'s checks for `blipscope-proxy`, plus **> 0** `evt:"upstream_skip"` lines; `evt:"breaker"`/`evt:"error"` counts printed as information | **> 0** invocation events; **> 0** `evt:"req"` lines; **> 0** `evt:"upstream_skip"` lines |
| **(d)** both | 0 stored events **of ANY type** carrying any field of the REQUEST ENVELOPE: every `$workers.event.request.headers.*` field, every `$workers.event.request.cf.*` field, `$workers.event.request.search.*`, `$workers.event.search.*` and `$workers.event.request.url` (the query string). `$workers.event.request.path` and `.method` are counted as information | the same query counts **> 0** events carrying `$workers.event.request.headers.user-agent` (proving that it can see an envelope field at all) |
| **(e)** W2 and P | 0 `evt:"req"` lines carrying a `dev` field; **> 0** `evt:"req"` lines with `authPath = "device"`, and every one of them carries a `devl` of exactly 8 lowercase hex (count of those WITHOUT one: 0) | **> 0** `evt:"req"` lines carrying `dev`, in production's BEFORE window (staging has no device traffic to show it) |
| **(f)** A, W2 and P | the line's OWN fields (`source.*`): 0 `evt:"req"` lines with an IP-shaped value (IPv4 dotted quad or an IPv6 form) in any field other than `ep` and `model`. The envelope Cloudflare could attach to the same event (its request headers and `request.cf`) is graded by (d), which counts events of every type; the `ep`/`model` IP-shaped counts printed as information (client-supplied text, not the requester's address) | in the SAME window, the `ep` IP-shaped count is **>= 1**. That is the planted `GET /192.0.2.1` below, a documentation address (RFC 5737), so the matcher is shown to fire on a `req` line field |
| **(g)** W2 | the W2 request's `devl` EQUALS the helper's label for the staging test device. The reader compares it in memory and prints only `equal` / `not equal` | the same compare against the label of a DIFFERENT example id must read `not equal` |

**(e)'s `authPath = "device"` count** is what makes its zero `dev` count non-vacuous: such a line exists only for an
authenticated request. On staging, the W2 request supplies it. In production, devices supply it all the time.

**How each row is read (the reader's instrument notes, adopted).**
- **Field names.** Our line's fields are stored BARE (`evt`, `dev`, `devl`, `ep`, `authPath`, `status`, `ms`, `model`,
  `colo`, …), and filters use those names. "`source.*`" above is only how the events view nests them in an answer. The
  event-type key is `$metadata.type`.
- **(a)'s value, READ (the reader's guarded values read, 2026-11-17 06:16:46Z, window 04:00-05:00Z; exact counts, every
  multi-filter query stated AND).**
  - `$metadata.type` takes exactly two values on both services: `"cf-worker-event"` (the invocation events) and
    `"cf-worker"`.
  - Every `evt:"req"` line is `"cf-worker"`: on staging, 251 of 251, with 251 invocation events; in production, 2,194
    of 2,194.
  - So (a) counts `$metadata.type = "cf-worker-event"`, and our own lines are never among them.
  - Still open: whether a `"cf-worker"` line carries the request envelope. (d) stays across ANY type for that reason.
- **(d), two ways, both counts or names only.**
  - (i) A COUNT, across all event types, of events where ANY known name under the five prefixes exists: an OR of
    `exists` over every such name on the reader's own list (kept with its instrument, not here).
  - (ii) A KEYS read over the window: the number of names under the five prefixes must be 0. This catches a name that
    is not on the list. Its control: a window older than retention returns 0 keys, so the endpoint is shown to honour
    the window.
- **(f)'s two patterns**, applied only to the STRING fields of `evt:"req"` lines (`ep`, `model`, `colo`, `cache`,
  `upstream`, `authPath`, `bm`, `devl`, `fw`, `evt`). The IPv6 pattern needs `::` or exactly 8 groups, so a time such
  as `12:30:45` cannot match:
  - IPv4: `(?<![0-9.])(?:(?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])\.){3}(?:25[0-5]|2[0-4][0-9]|1[0-9][0-9]|[1-9]?[0-9])(?![0-9.])`
  - IPv6: `(?<![0-9A-Fa-f:])(?:(?:[0-9A-Fa-f]{1,4}:){7}[0-9A-Fa-f]{1,4}|(?:[0-9A-Fa-f]{1,4}(?::[0-9A-Fa-f]{1,4}){0,6})?::(?:[0-9A-Fa-f]{1,4}(?::[0-9A-Fa-f]{1,4}){0,6})?)(?![0-9A-Fa-f:])`
- **A rehearsal before the day.** The reader may run its instrument once against a fixed past window, counts only (the
  reviewer's OK), so that every filter and control is shown to read > 0 where it should BEFORE the slot.

**The W2 request, for the reader to anchor on:**
- one `GET /v1/config` to the staging origin, carrying the staging test identity;
- its time is printed by the sending tool to the second (`W2 request at <UTC>`) and recorded in section 9;
- expected status **200**;
- its `evt:"req"` line has `ep = "/v1/config"`, `status = 200`, `authPath = "device"` and a `devl`.

**The non-vacuity control for (d).** A zero count means nothing in a window that held no request carrying the field.
- **`user-agent`** is carried by this session's own triggering requests, so it is non-vacuous in every window. It is
  graded on staging and on production.
- **The two identity headers:**
  - **Production:** the window must also hold **> 0** `evt:"req"` lines with `authPath = "device"`. Devices send those
    all the time. Production grades the identity headers.
  - **Staging** has little device traffic, so this session sends **one request carrying the staging test identity**
    (ruled by the reviewer, 2026-11-17). It goes into a SECOND staging window, W2, that opens only **after** (a) has
    read 0 on both reads. (a) is read after window A closes, so the request cannot go into A itself. Its time is
    recorded in section 9. The two identity headers are then graded on staging, in W2, before production is touched.
    If (a) is not 0, there is no W2 and no such request: stop and report.

**The triggering requests**, sent inside each window by this session. None carries a key or a device header:
- 5 anonymous `GET /healthz`, at least 30 s apart;
- ONE anonymous `GET /192.0.2.1`, answered 404, as (f)'s plant.

The counts are stated so that the reader can match them: (b)'s after-count is **at least 5** on staging, and `ep`'s
IP-shaped count is **at least 1** in every window.

**What is not yet known, and how it will be known before the slot.** Cloudflare's page does not say whether a console
event carries the request envelope. The reader may establish it by event type (`$metadata.type`) before Wednesday,
names only, if the reviewer OKs it. If console events carry envelope fields today, (d)'s prediction becomes "> 0"
BEFORE the slot, by a correction written below this section. The owner then decides whether the slot goes ahead.

**Predicted:**
- (a) 0;
- (b) at least 5;
- (c) as (a) and (b), plus `upstream_skip` above 0;
- (d) 0;
- (e) 0 `dev`, with every device-authenticated line carrying an 8-hex `devl`;
- (f) 0;
- (g) `equal`, with its control `not equal`;
- every control above 0.

## 6. Outcomes, decided before the reading

| reading | meaning | action |
|---|---|---|
| all as predicted, every control above 0 | invocation logs are off, our lines arrive, and no request-header field is stored | record it; production follows staging in the same slot |
| (a) > 0 after, its control > 0 | the setting did not take: invocation events are still stored | no rollback (no worse than before); stop and report |
| (b) or (c)'s `req`/`upstream_skip` = 0 after, its control > 0 | our own lines stopped arriving, which fails the owner's condition | **roll back that environment at once**; report |
| (d) > 0 after, (a) = 0 | Cloudflare attaches the request envelope to our console events too, so `invocation_logs = false` alone does not remove it | no rollback (no worse than before); stop and report to the owner, whose call the next step is |
| (e) `dev` > 0 after | the line still carries the id: the code did not take | no rollback (no worse than before); stop and report |
| (e) device-authenticated lines without an 8-hex `devl` > 0 | the label failed (the failure-safe path) | no rollback; stop and report |
| (f) an IP-shaped value in the line's own fields other than `ep`/`model` | an address reached our line | stop and report to the owner |
| (g) `not equal`, its control `not equal` | the helper and the Worker disagree on the label | stop and report |
| (g)'s control reads `equal` | the compare cannot tell two labels apart | **BLIND** |
| any control = 0 | the reader cannot see what it would need to see | **BLIND**: no verdict; stop |
| anything else, or the two reads disagree | outside the set | stop and decide; nothing is improvised |

Rollback is through the deploy plan's section 4 form (`node scripts/wrangler-run.mjs rollback <id> --env <env>`), to
the ids read at that slot's D0.5. A rollback is never refused.

**Information, never graded.** Stored Workers Logs volume should fall by about the invocation events' share, at 100%
sampling about one event per request. The account budget reads it at its next scheduled read.

## 7. The slot (the owner's OK, 05:47Z: "logs slot: ok (Wed 12:30-15:00Z)")

**Wednesday 2026-11-18, 12:30-15:00Z.** That is clear of the 10:00Z radar deploy, and it leaves the 16:00Z route run
over an hour of margin. It is not the 2026-11-17 23:00Z deploy of #736, which stays exactly as approved. (A
13:00-14:00Z slot was proposed first. It cannot hold W2, which must wait for A's two reads.)

**WEDNESDAY'S CHAIN RULE (the owner, 05:47Z): radar 10:00Z, then logs at about 14:00Z, then the route run at 16:00Z.
If one fails or rolls back, the LATER ones wait for him.**
- This slot starts only if the radar deploy did not fail and was not rolled back. If it fails or rolls back, the route
  run waits for the owner.
- **The reviewer's reading, 2026-11-17, which the owner may override.** A step that does not run because its own
  precondition held it (the radar moved to Thursday for want of R0.1's word: nothing deployed, nothing changed) is
  neither a failure nor a rollback. The later steps then go ahead on their own preconditions, and the owner is told.
- A step that STARTED (any deploy command run) and did not complete as predicted is a FAILURE.
- The same rule stands in the radar checklist (`docs/wx-opera-fallback-deploy.md` 6c) and the route checklist
  (`docs/route-weekly-code.md` 4f, T0.0).

| time (Z) | step |
|---|---|
| 12:30-12:40 | BEFORE windows, both environments: read-only, plus the anonymous `GET /healthz` requests. Reads 12:50 and 13:00. Any control at 0 is BLIND, and nothing deploys |
| 13:00 | the deploy plan's D0 checks, re-listing the carried commits from `/healthz`'s stamp to main; the deployer lock confirmed; the staging deploy; C1 |
| about 13:05-13:35 | window A, then its two reads: (a), (b), (d)'s `user-agent` |
| about 13:36-14:05 | W2, only if (a) = 0 on both reads; the one staging-identity request; its two reads |
| not before 14:00, about 14:05 | only if staging read as predicted: the production deploy, C1, window P, its two reads: (c), (d) |
| by about 14:50 | the deploy claim released; the record below |

Each deploy goes through `proxy/scripts/deploy.sh`, under the deploy claim, with the deployer lock confirmed with the
reviewer first.

## 8. The label code, red first

**Registration (this commit): the tests and a STUB `src/devlabel.ts`, both of whose functions throw.**
- `test/device-label.test.ts` has DL1-DL6.
- `scripts/device-label.selftest.mts` has H1-H6 and S-DLECHO.
- The workers workflow's proxy job gets one step that runs the selftest.
- EXAMPLE ids only: `0000000000000000`, `0123456789abcdef`, and the suite's fake `TEST_DEVICE`. Their labels
  (`fcdb4b42`, `9f9f5111`, `b82efc46`) were computed outside this code, with Python's `hashlib`, and checked against
  Node's `crypto.subtle`.

**Predicted at registration, before running:**

| row | predicted | why |
|---|---|---|
| DL1, DL2, DL3 | RED: the stub's "registration stub" error | the functions do not exist yet |
| DL4 | RED: the line's keys read `colo, dev, devl, ep, evt, fw, model, ms, status` | `record()` still spreads `dev` |
| DL5 | RED: actual `[200, [authPath, colo, dev, ep, evt, model, ms, status], undefined]` | still `dev`, and no `devl` |
| DL6 | GREEN | the control: an anonymous line is unchanged |
| every other row of the proxy suite | GREEN | nothing else changed |
| the selftest | `H1-H6` FAIL ("does not exist"), `S-DLECHO` FAIL (BLIND: the anchor is not there); `SELFTEST: 2 row(s) off`, exit 1 | there is no helper yet |
| the proxy typechecks (`typecheck`, `typecheck:scripts`) | clean | the stub has the final signatures |

The fetchMock cascade does not apply: no DL row mocks a fetch, and `/v1/config` and `/healthz` make none.

**Predicted at the code commit:** DL1-DL6 GREEN; every other proxy row GREEN, including `metrics.test.ts`'s
`setDeviceAttribution` rows, which now run through `normDeviceId()`; the selftest `SELFTEST PASSED`, with S-DLECHO caught
on at least H1 and H6; both typechecks clean.

**Two plants after the code, each restored from a byte copy and checked by sha256:**

| plant | change | predicted red |
|---|---|---|
| S-DEV | `record()` spreads `...m` again, putting `dev` back on the line | exactly {DL4, DL5} |
| S-NEWFIELD | `index.ts` adds `(meta as unknown as Record<string, unknown>).ip = "192.0.2.1"`, a documentation address, beside `meta.authPath` | exactly {DL5} (DL6's anonymous request never reaches it, and DL4 builds its own metric) |

**The registration, as read (2026-11-17).**
- **Typecheck: OFF, a MISS, recorded as read.**
  - `typecheck` exited 2 against a predicted clean, with one line:
    `test/device-label.test.ts(51,107): error TS2304: Cannot find name 'Env'`.
  - The cause was DL4's `as unknown as Env`, written without the type's import. `Env` is exported from
    `src/types.ts`, and every other test imports it.
  - Excluded alternatives:
    - main failing already: the only error line is in the new file;
    - the stub's signatures: it is a missing name, not a mismatch;
    - the toolchain: `typecheck:scripts`, with the same tsc, is clean.
  - The fix, ruled by the reviewer (test scaffolding, no row logic): `import type { Env } from "../src/types";`.
    `typecheck` re-run once: clean, exit 0, **as predicted.**
- **`typecheck:scripts`:** clean, exit 0. **As predicted.**
- **The selftest:** "FAIL H1-H6: … does not exist", "FAIL S-DLECHO: … (BLIND …)", "SELFTEST: 2 row(s) off", exit 1,
  and no sabotage file left behind. **As predicted.**
- **The whole proxy suite** (05:57:58Z, 89.6 s, one run): 5 failed, 1,110 passed, 1 skipped. **As predicted:**
  - DL1-DL3 red on the stub's error;
  - DL4 red, its keys `colo, dev, devl, ep, evt, fw, model, ms, status` (the only difference is `dev`);
  - DL5 red, actual `[200, [authPath, colo, dev, ep, evt, model, ms, status], undefined]`;
  - DL6 green;
  - every other row green.
  - The one skip is MD2, main's own `it.skipIf` (it runs only with its binding).
  - The import fix landed while this run was in flight. It is a type-only import, erased at run time, so the rows are
    the same either way.

**The code, as read (2026-11-17).**
- **The whole proxy suite** (06:06:25Z, 114.5 s, one run): 1,115 passed, 1 skipped (MD2, as before), 0 failed, with
  DL1-DL6 green. **As predicted.**
- **The selftest:** H1-H6 PASS; S-DLECHO PASS, because the echoing copy fails H1, H2 and H6; "SELFTEST PASSED", exit 0,
  and no sabotage file left behind. **As predicted.**
- **`typecheck` and `typecheck:scripts`:** both clean, exit 0. **As predicted.**

**The plants, as read (2026-11-17).** Each was planted by a driver that asserts its needle occurs exactly once and that
the file changed, then restored from a byte copy with its sha256 checked; the tree was clean after each.
- **S-DEV** (`record()` spreads `...m` again): the whole suite read 2 failed, 1,113 passed, 1 skipped. The failures are
  exactly {DL4, DL5}. Restored: `0b73187b664d` == the original. **As predicted.**
- **S-NEWFIELD** (`index.ts` sets an extra `ip = "192.0.2.1"` beside `authPath`): the whole suite read 1 failed, 1,114
  passed, 1 skipped. The failure is exactly {DL5}. Restored: `2db6ff7a14e6` == the original. **As predicted.**
  - Its FIRST run (06:20Z) was an INSTRUMENT FAILURE, not a reading. The workers pool crashed on `read ECONNRESET` with
    0 rows in all 81 files, because TIME_WAIT stood at 4,599 against this repo's rule of under 2,000 before a run; it
    had been re-read before the first whole-suite run only.
  - The fix, ruled by the reviewer: wait until TIME_WAIT is under 2,000 on two reads 60 s apart, then run once.
    TIME_WAIT is now re-read before every whole-suite run. The re-run (06:38Z) gave the reading above, after reads of
    23, 22 and 32.

## 9. Record

_Filled in after the slot, below this line, without editing anything above it._
