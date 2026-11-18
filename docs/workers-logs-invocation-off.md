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

### 5a. Corrections before the slot (2026-11-17, ruled by the reviewer; appended below the frozen section 5)

Section 5 above is frozen and is not edited. The reader verified that text against its instrument before any reading,
and found three places where the text, as written, could not be read. Each correction is ruled. **None changes a
prediction**: each restates how a row is read.

**C1. (f)'s two patterns, without lookarounds.** The frozen patterns use lookbehind and lookahead. An RE2-style
regex engine supports neither, so on the day (f) could come back as an HTTP 400 and be ungradable. They are replaced
by these forms:

- IPv4: `(^|[^0-9])[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}([^0-9]|$)` (already proven on the reader's API)
- IPv6: `(^|[^0-9A-Fa-f:])([0-9A-Fa-f]{1,4}:){7}[0-9A-Fa-f]{1,4}([^0-9A-Fa-f:]|$)|::`

These are looser than the frozen forms: the IPv4 one also matches `999.1.1.1`, and the IPv6 one matches any `::`.
That costs nothing where (f) is graded. The graded fields (`colo`, `cache`, `upstream`, `authPath`, `bm`, `devl`,
`fw`, `evt`) hold no dot and no colon, so neither form can fire on them. `ep` and `model` stay information.

**The rehearsal showed the frozen patterns would have FAILED SILENTLY, not loudly.** The reader's dress rehearsal (2026-11-17
07:27Z, a past 10-minute staging window, counts only, every multi-filter query AND) found the following:
- The API ACCEPTED every pattern, the frozen lookaround ones included (HTTP 200).
- On the same data, the lookaround-free IPv4 form matched 5 events. The frozen IPv4 form matched 0.

So the API's engine accepts lookarounds and does not honour them as written. (f) as frozen would have read 0 in every
window on Wednesday. That would have been caught only as BLIND, by the IPv4 plant's control. Without the plant it would
have read as a pass. One observation of 5 events.

The IPv6 form's ability to match is NOT shown before the day, because no IPv6-shaped value appeared in that window. Its
control on the day is C2's plant, and only that.

**C2. A control for the IPv6 half of (f).** The frozen plant, `GET /192.0.2.1`, controls only the IPv4 pattern, which
would leave the IPv6 pattern uncontrolled. Each window's triggering requests therefore gain ONE more anonymous request,
`GET /2001:db8::1`, an address from the documentation prefix (RFC 3849). It is answered 404, and it carries no key and
no device header. In every window, `ep`'s IPv6-shaped count must be **>= 1**. If it is not, (f)'s IPv6 half is BLIND.

**C3. (d)(ii)'s control: a FUTURE window.** "A window older than retention returns 0 keys" cannot be met: the
reader's rehearsal found names still stored for 2026-11-01. The control is now a window 24 hours AHEAD, which must
return 0 keys. That read 0 on both services when rehearsed.

**C4. (e)'s "WITHOUT an 8-hex `devl`: 0", as it is read.** The reader's API has no negated match. So the row is read
as two counts:
- (A) the `evt:"req"` lines with `authPath = "device"`;
- (B) those lines that also have a `devl` matching `^[0-9a-f]{8}$`.

The row is graded on **A - B = 0**, with A > 0 as before.

**Before the slot.** The reader's dress rehearsal sends these corrected patterns to its API against a past window, so
that each is proven to be accepted. C2's plant cannot be rehearsed on a past window, because no such request was ever
sent; its own control is the reading on the day.

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

### 7a. Amendment before the slot: #736 rides this deploy, its checks kept separate (2026-11-17, appended below 7)

Sections 1-7 above are frozen and are not edited. This section changes four things: what this deploy SHIPS, what it
CARRIES, the D0 that proves both, and the timeline. It adds #736's own checks, apart from (a)-(g). Every prediction in
sections 5 and 5a stands.

**The owner's words, 2026-11-17 18:53Z (relayed by the reviewer), verbatim, the parts that govern this deploy:** "#736:
Wednesday. Keep its checks separate from the logs checks in the plan, so a failure can be traced to one change. rule: by
procedure Wed. [...] name each of Wednesday's three commits in the plans."

#### 7a.1 The commit this deploy ships

**EXPECT = `4a02123ca992031d60bd9592e3084c9ccf34041f`**, #747's merge commit. Both environments deploy exactly this
commit, from one detached LF checkout of it (`-c core.autocrlf=false -c core.eol=lf`), and no branch is created or moved.

#### 7a.2 What it carries: every commit from `074863fc` (both environments' `/healthz` stamp) to EXPECT

Read 2026-11-17 19:46Z with `git log --first-parent 074863fc..EXPECT`. Each commit's files under `proxy/`:

| commit | PR | under `proxy/` | in the proxy's input set? | its checks |
|---|---|---|---|---|
| `4a02123c` | #747 | `pages/support.html`, `src/pages.generated.ts` | **yes** (the served page) | 7a.5, P-S |
| `13a6dab8` | #746 | none | no | none |
| `6beb4310` | #745 | none | no | none |
| `c7f73d10` | #744 | `wrangler.toml`, `src/devlabel.ts`, `src/index.ts`, `src/metrics.ts`, two scripts, one test | **yes** (this plan's change) | sections 5, 5a, 6 |
| `1695645d` | #743 | none | no | none |
| `b4f7e511` | #742 | none | no | none |
| `4dcf2be3` | #739 | none | no | none |
| `6164ac93` | #741 | none | no | none |
| `97c96b4c` | #740 | none | no | none |
| `5c45a67c` | #738 | none | no | none |
| `0fc8c1d3` | #735 | none | no | none |
| `8ffe6641` | #737 (#736's plan) | none | no | none |
| `00fc75d1` | #736 | `wrangler.toml`, `src/types.ts`, `src/upstreams/adsbdb.ts`, `src/upstreams/chain.ts`, `README.md`, four tests | **yes** | 7a.4 |
| `3815844f` | #734 | `FEED-SOURCING.md`, one script, one test | no (#736's plan's bundle check: none is an input) | none (C5 below) |
| `3741501b`, `8e83ec83`, `4e9ee56e`, `e152fd30`, `425468f1` | #731, #733, #732, #730, #729 | none | no | none |

**Three changes are in the bundle:** #744 (the logs), #736 (adsbdb off) and #747 (the support page's words).
Production's behaviour is predicted unchanged by #736 (its plan's own premise). #747 changes the page's text only.

#### 7a.3 D0, by procedure: printed, and any mismatch means no deploy

The owner ruled "by procedure" for Wednesday: until the deploy scripts check it themselves (the commit-pin change, not
merged before this chain closes), the deployer PRINTS each of these, in the slot, before the first upload:

| D0 | prints | predicted | on a mismatch |
|---|---|---|---|
| D0-H | `git rev-parse HEAD` in the deploy checkout | EXPECT, in full | no deploy |
| D0-A | `git ls-remote` for main = M; `git merge-base --is-ancestor EXPECT M` | exit 0 | no deploy |
| D0-B | `git log --format='%h %s' EXPECT..M -- proxy/ scripts/session-claim.py` (the proxy's inputs and machinery, read wide) | empty. A later commit touching only docs or other paths outside these is a NOTE and is printed as one | any commit listed here: no deploy until this plan names it (a merge after the plan) |
| D0-D | `git status --porcelain -- proxy/` in the deploy checkout | empty | no deploy |
| D0-S | `/healthz` `commit` on staging and production, each read twice at least 60 s apart | `074863fc` on both | another stamp means someone deployed since: no deploy; re-list 7a.2 from that stamp |

`deploy.sh`'s own behind check reads `EXPECT..origin/main` too, so D0-B and the script agree. A deploy is never run with
`ALLOW_BEHIND=1`.

#### 7a.4 #736's checks, kept apart from (a)-(g)

These are #736's own plan's checks (`docs/proxy-deploy-2026-11-17.md`, section 3), unchanged in what they grade. That
plan's Tuesday slot was not used: the owner chose Wednesday. Its drafted override (#749) was closed unmerged, and
nothing in it applies here.

| step | when | what | predicted |
|---|---|---|---|
| **#736-B** (the before-read) | 12:10-12:25Z, before the BEFORE windows | `/healthz` on both environments, twice, at least 60 s apart: `upstreams` by id, order, `enabled`, `state` and key set; the routes block; the OPERA products' `source` values | 7 `upstreams` entries, `adsbdb_ac` absent; written down as the comparison |
| **#736-S0** (the smoke baseline) | 12:00-12:28Z, before the BEFORE windows | `smoke-prod.sh` against production, and with `BASE=<staging>` against staging, as the test identity; whole output to a file | each run's verdicts saved by name |
| **#736-C2** | after each environment's C1, BEFORE that environment's next logs window opens | `/healthz` `upstreams`, read twice | 8 entries; entries 1-7 equal to #736-B; entry 8 exactly `{"id":"adsbdb_ac","enabled":false,"state":"closed","reason":"licence"}` |
| **#736-C3** | each deploy's own output | the two variables | `ROUTE_ADSBDB_ENABLED` and `TYPE_ADSBDB_ENABLED` both `"false"`, or "not observable" (never passed) |
| **#736-C5** | with #736-C2 | `ok`, the routes block, the OPERA values | equal to #736-B (`ageDays` not compared) |
| **#736-C4** (the smoke) | staging: after W2's second read; production: after P's second read. Never inside A, W2 or P | `smoke-prod.sh`, whole | every verdict equal to #736-S0 by name |

**Why the smoke stays outside the windows.** It runs as the test identity, so inside a window it would add
device-authenticated request lines and enter (e)'s and W2's counts. The `/healthz` reads add no device line, but they
are kept out of the windows too, so that every count in a window comes from that window's own triggering requests.

**#747's one check, P-S:** after each environment's C1 and before its next window, one anonymous `GET /blipscope/support`.
It must hold "Cloudflare keeps these logs for up to seven days" and must not hold "nothing that identifies your device".

#### 7a.5 Outcomes: each failure names the change it implicates

One deploy carries all three changes, so **a rollback reverts all three**. The row says which change the evidence
points at, so that the report, and any re-plan, separate them.

| reading | implicates | action |
|---|---|---|
| any of section 6's rows | **#744** (the logs) | as section 6 says |
| #736-C2 reads 7 entries, or no `adsbdb_ac` | the build is not EXPECT's: C1 fails too | roll back that environment |
| #736-C2's `adsbdb_ac` reads `enabled: true`, or has no `reason` | **#736**: the licence switch is on in that environment | **roll back at once**; report to the reviewer and the owner |
| #736-C2's entries 1-7 differ in `id`, `enabled`, order or keys | **#736** (a change nobody predicted in the upstream chain) | roll back; report |
| #736-C3 "not observable" | none (the output's format) | the deploy stands on C1, #736-C2, #736-C4 and #736-C5; report |
| #736-C4: a smoke verdict passed in #736-S0 and fails now | **the deploy**. The report names the verdict, and its check's subject says which change it touches: routes or enrichment point at #736; nothing in #744 or #747 changes a smoke-graded behaviour | roll back that environment; report |
| #736-C5: the routes block or the OPERA values moved | **none predicted** (#734's check: no change may move them) | roll back; report |
| P-S: the old sentence is served, or the new one is absent | **#747** (the page did not ship, or the wrong build did) | roll back that environment; report |
| anything else | outside the set | stop and decide; nothing is improvised |

#### 7a.6 The timeline, with #736 and #747 added (section 7's table stands except as written here)

| time (Z) | step |
|---|---|
| by 12:00 | the deploy checkout at EXPECT (LF, detached); D0-H, D0-D printed |
| 12:00-12:28 | #736-S0 (smoke, both environments); #736-B (`/healthz`, both, twice) |
| 12:30-12:40 | the BEFORE windows, both environments, as section 7. Reads 12:50 and 13:00 |
| 13:00 | the deployer lock confirmed; D0-A, D0-B and D0-S printed; the staging deploy; C1 |
| after C1 | #736-C2/C3/C5 and P-S on staging (about 3 min). **Window A then opens 2 minutes after the last of them**, not 2 minutes after C1 |
| about 13:08-13:40 | window A and its two reads; then W2 and its two reads, as section 7 |
| after W2's second read | #736-C4 on staging (smoke) |
| not before 14:00, about 14:15 | only if staging read as predicted, on (a)-(g) AND #736-C2-C5 AND P-S: the production deploy; C1; #736-C2/C3/C5 and P-S on production; window P opens 2 minutes after the last of them |
| after P's second read | #736-C4 on production (smoke) |
| by about 14:55 | the deploy claim released; the record (section 9) names each change's checks separately |

**If the slot cannot fit** (the 15:00Z end is reached with production not started), production does not start. The owner's
same-day rule for #747 then applies: its page gets its own slot the same day.

#### 7a.7 Wednesday's three commits, named

- the 10:00Z radar step: the commit its own checklist's amendment names (`docs/wx-opera-fallback-deploy.md`);
- this step: **`4a02123ca992031d60bd9592e3084c9ccf34041f`**;
- the 16:00Z route run: the commit of the tool it runs, named in its own checklist (`docs/route-weekly-code.md`).

### 7b-7f. Rulings and re-reads after Wednesday, for Thursday's step (2026-11-18, appended below 7a)

Sections 1-7a stand except as written here. **Wednesday's step did not run.**
- The radar step's C-R2 held the chain, under the owner's chain rule: if one step fails or rolls back, the later ones
  wait for the owner.
- No owner answer came by 12:30Z. The owner then moved this step to **Thursday 2026-11-19, 12:30-15:00Z, with Friday as
  the fallback**. 7a.6's clock times stand, one day later.
- **#747's page shipped alone on Wednesday**, under the owner's same-day rule (7a.6's last paragraph). Section 7e says
  what that changes here.

Each subsection below was ruled by the reviewer before the reading it governs, and each names when.

#### 7b. #736-C4's comparison rule (ruled about 11:12Z, before any S0; widened at 15:27Z)

**Why.** #736-C4 grades "every verdict equal to #736-S0 by name". But some of the smoke's checks are named after the
aircraft over its test tile:
- an empty tile prints SKIPPED names;
- an aircraft present prints per-aircraft PASS names.

That made the radar step's C-R2 read OFF on Wednesday with nothing wrong. Enrichment is #736's subject, so the text as
written has two failures:
- **a false OFF**, when traffic changes between S0 and C4;
- **worse, a hidden failure**: a PASS at S0 against a traffic SKIP at C4 means the enrich check is simply not compared,
  and a broken enrichment can hide behind the SKIP.

**The rule.**
1. A FAIL anywhere is graded as 7a.5 says.
2. A name in the traffic-dependent family (7f's T1-T3) that differs between S0 and C4 is **NOT COMPARABLE**. It is
   printed as information and is never a pass. This covers two cases:
   - a SKIPPED name on one side and its per-aircraft counterpart on the other;
   - one aircraft's per-aircraft PASS against another aircraft's per-aircraft PASS. This case was widened at 15:27Z, from
     the page-only deploy's C4 below. The reviewer declined to widen the normaliser instead, because that would choose
     the rule after the reading.
3. If the family is NOT COMPARABLE at C4, the smoke is re-run ONCE, at most 10 minutes later and still outside every
   window, and graded on that run.
4. If it is still NOT COMPARABLE, #736-C4's enrichment half is recorded **BLIND**, and **#736's verdict is NOT PROVEN:
   stop and report.** Nothing is rolled back by reflex; the owner decides whether to keep or roll back. The other names
   are graded as written. But "the other names graded" never adds up to "#736 passed" while its subject is blind.
5. **A supplementary row exists only if an existing tool gives one:** a probe that does not depend on traffic (a fixed
   public aircraft hex, through the same test identity, no new tool, no token on a command line). Read at about 11:13Z:
   **none exists**, so none is added. The smoke takes its enrich hex only from the live `/v1/blips` response, with no
   override. The other callers of `/v1/enrich` are an Analytics Engine aggregate, a callsign lookup inside the route
   ingest, and a local replay, and using any of them would be a new tool.

The owner's terms are unchanged: #736's checks stay separate from the logs checks.

#### 7c. #736-C2's grading of breaker state (ruled about 12:03Z, before C2)

- C2 grades entries 1-7 on **id, enabled, order and key set**, which is 7a.5's own list.
- Their **state is printed as information, never graded**. A breaker's state is live upstream health, not something
  #736 changes. On Wednesday, two of production's breakers went from open to closed between two reads 85 s apart.
- Entry 8 (`adsbdb_ac`) is graded exactly as written, **state included**:
  `{"id":"adsbdb_ac","enabled":false,"state":"closed","reason":"licence"}`.

#### 7d. P-S, as it is read (ruled about 12:37Z)

- **The defect.** P-S requires the page to hold "Cloudflare keeps these logs for up to seven days". That sentence is
  wrapped across two lines in the HTML source of `proxy/pages/support.html`. So a literal match on the raw HTML reads it
  ABSENT on a correct page; a raw grep of `4a02123c`'s page counts it 0 times. As written, P-S would implicate #747 and
  roll back a good deploy.
- **The reading:** every run of whitespace in the served HTML is collapsed to one space before matching. The old
  sentence ("nothing that identifies your device") is matched the same way.
- **Its control, PS-L, before the step:** the same matcher, run on the deploy checkout's own
  `proxy/pages/support.html`, must read the new sentence present and the old one absent.
  - On `4a02123c`'s page it reads exactly that, while the raw match reads the new sentence absent.
  - On `074863fc`'s page it reads new absent, old present.
  - P-S is graded only after PS-L reads as required in that day's checkout.

#### 7e. The "before" commit and the rollback targets, re-read after Wednesday's page-only deploy

**What moved.** At 15:24Z (staging) and 15:29Z (production) on Wednesday, the page-only deploy shipped
**`fcc33692a8ba98fd0dd53a8629303e8b8e25a213`** to both environments. That is `074863fc` plus #747's two files under
`proxy/`, taken byte-identical from `4a02123c`. It is on the branch `deploy/page747-on-074863fc`, and is not an ancestor
of main.
- Its checks: C1, the page check and the `/healthz` comparison read as predicted on both environments. The smoke had 0
  FAIL, and its photo-pointer half was NOT COMPARABLE after one re-run (7b rule 2), so it was recorded BLIND, which is
  not a stop for a page-only deploy.

| env | `/healthz` commit now | version now | the version before (serves the OLD sentence) |
|---|---|---|---|
| production | `fcc33692` | `e663ea72-676b-4f53-90b7-f4135f7c45dc` | `2b92e9f1-73c8-4cb3-b2b0-b0090f6102a2` |
| staging | `fcc33692` | `e6de3d81-68e8-4858-b8a2-e59a03858441` | `4464d8ab-15e8-4970-9c7d-97e26bcda9bd` |

**What changes for Thursday:**
1. **D0-S predicts `fcc33692`** on both environments, not `074863fc`. 7a.3's rule for another stamp says to re-list 7a.2
   from it.
   - `git log --first-parent fcc33692..EXPECT` lists the same 19 commits as from `074863fc`, because fcc33692's only
     parent is `074863fc`. So 7a.2's table stands.
   - #747's row is the one difference: its content is already live, so on Thursday P-S checks that the page STAYS right.
2. **The rollback targets are the versions D0 reads on Thursday**, read fresh and never copied from the table above.
   They are predicted to be `e663ea72` (production) and `e6de3d81` (staging) unless something else deploys first.
   - **`2b92e9f1` and `4464d8ab` are never a rollback target:** they serve "nothing that identifies your device".
   - A rollback target is acceptable only if it is the version serving the new page. PS-0, read before the step, must
     show the new sentence present and the old one absent on both environments.
3. **P-S after the step** is graded as "the new sentence stays present and the old one stays absent". PS-L still runs
   first, as 7d says.
4. **D0-B**, read at about 15:45Z on Wednesday over `EXPECT..main` (`f9e66b14`), is **empty**. The five commits since
   EXPECT touch neither `proxy/` nor `scripts/session-claim.py`: three plan amendments (#751, #752, #753), #756 and
   #690. They are on main, outside the input set.
5. **The scheduled photo-drift run is named in every main-CI read.**
   - photo-drift's daily `schedule` run (cron 06:17) fires 7-9 h late, between about 13:29Z and 15:57Z. Thursday's slot
     is inside that band.
   - It attaches to main's head and adds 2 check-runs (drift, photo-drift).
   - So any prediction of main's check-run count, and any "main's CI finished or idle" gate, names it: in the
     prediction if it has landed, as information if it is in progress.
   - It is identified by workflow and event, never by count alone. It is never read as off, and never as an
     unfinished run of this step's.

#### 7f. The smoke checks whose name or verdict follows live traffic (the owner's condition, 2026-11-18 15:02Z)

**Source.** Read from `proxy/scripts/smoke-prod.sh` at main `f9e66b14`; its blob is the same as fcc33692's, i.e. what is
deployed now. It is re-read at Thursday's EXPECT, and if the blob differs, this list is re-derived before S0.
- The list comes from the script, not from Wednesday's outputs, because a check that happened to read the same twice
  would not show up in a diff.
- Exactly these four checks read a `/v1/blips` response:

| # | name(s) as printed | input | what follows traffic |
|---|---|---|---|
| T1 | `/v1/enrich/<hex> (live hex from the blips response)` PASS, or `/v1/enrich` SKIPPED | the first row of the test tile's `/v1/blips` | the NAME (the hex), and PASS against SKIPPED (an empty tile, or `/v1/blips` not 200) |
| T2 | `/v1/photo (via enrich pointer <photo key>)` PASS, or `/v1/photo` SKIPPED | the photo pointer from T1's enrich answer | the NAME (the aircraft type's photo key), and PASS against SKIPPED (no hex, or a type with no stock photo). On Wednesday the page-only deploy's six smoke runs (S0, C4 and one re-run, on each environment) printed three different types (C152, R22, B39M), all PASS |
| T3 | `/v1/enrich photo variant gate (FW 6 vs FW 7)` and its `#2`, or `photo variant gate` SKIPPED, or `photo variant gate (FW 7 square)` SKIPPED | the first of the test tile's first 12 hexes that resolves a photo | whether it runs, which SKIP prints, and whether the FW 7 half runs (that depends on the TYPE having a square ingested). When it runs, its verdict is graded on that type's library row |
| T4 | `upstream chain (freshness + both relays serving)`, plus a second result under the same header when its empty-sky branch fires | `/v1/blips` over six fixed probe tiles, NOT the test tile | the VERDICT: FAIL "EMPTY SKY" when the probes see fewer than 10 aircraft in all, or fewer than 2 non-empty tiles. Its name is fixed |

The test tile's own `/v1/blips` check grades the status only (an empty tile still answers 200), and its name is built
from constants, so it does not follow traffic. No other check reads `/v1/blips`.

**Grading (ruled by the reviewer at about 16:00Z, before Thursday's S0):**
- **T1-T3 are the name family of 7b rule 2.** A name that differs between S0 and C4 is NOT COMPARABLE, never a pass. A
  FAIL inside them is still a FAIL (rule 1), whatever the aircraft.
- **T4 is named as traffic-dependent in its VERDICT, and graded as written, outside the name family.** Its FAIL is the
  alarm for a throttled upstream, which answers 200 with an empty array and a current clock, and catching that is what
  T4 exists for. Rule 1 applies, and its second result appears only with that FAIL.
  - Beside T4's verdict, at S0 and at C4, the report prints the aircraft count and the non-empty-tile count T4 saw.

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
