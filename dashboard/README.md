# Blipscope Cloud — fleet dashboard

An Access-gated console for the fleet: per-device usage, firmware rollout, OTA
outcomes, enrichment gaps, and one-click revocation.

**This is a separate Worker from the one devices talk to**, on a separate
hostname, with a separate deploy. That is the whole point. The device Worker
serves every board every few seconds and has, by design, no admin surface — the
revocation feature was deliberately built as *a command you run*, not *a page
that exists*. Putting a console on the same script would undo that: its routes
would live on the hostname devices hit, and a bug in a rendering path or a query
would sit in the same isolate as the thing keeping 50 screens alive.

Nothing in this directory is on the device serving path. The only change the
device Worker needed was to its telemetry (see *What made this possible*).

---

## What it shows

| Page | Answers |
|---|---|
| **Fleet** | A **triage list** first -- enrolled but silent 7 days, last boot a crash/watchdog/brownout, OTA not ok, enrolled but never requested, render drift not CLEAN -- each a count with the ids behind it. Then which devices are alive, how much each is used, error rate, staleness. Revoke / restore. |
| **Device** (`/device/<id>`) | One unit: model, firmware and its transitions, first/last seen, boot and OTA history, requests and error rate, usage counters, Revoke / Restore. Every device id on every page links here; an unknown id is a 404. |
| **Funnel** | Setup: enrolment -> first `/blips` -> first card open (first photo fetch), per device and as medians, with who is stuck at each stage. |
| **Upstreams** | Fleet vs upstream: per upstream, requests, the device-facing error rate, and p50/p95 latency, worst first. |
| **Firmware** | Who is on which version, per model — *did that OTA actually land?* |
| **OTA** | Every update attempt with its result, naming the exact unit that failed. |
| **Enrichment gaps** | What the fleet looked up that we couldn't answer, ranked by real demand. |
| **Usage** | Per device, over 7 or 30 days: card opens, switches to each screen, logbook claims, whether Follow is set, uptime, report count -- and which **enrolled devices made no request at all**. |

`/fleet.json` and `/usage.json` mirror the tables for piping elsewhere. Every SQL statement any page issues is run against the live engine by `npm run smoke:analytics` (and in CI); vitest cannot see what the engine rejects.

### Requests are not attention

This is the one thing to be clear-eyed about. **A device polls on a timer whether
or not anyone is in the room**, so `requests` measures *uptime*, not engagement.

The honest interaction number is **card opens**. The Usage page shows the
device's own count from its hourly usage report. The Fleet page's **Cards** is the
older proxy for it: the firmware fetches a photo once per aircraft when a
**detail card is opened**, which only happens on a tap -- so it is a lower bound
(a reopened card, or an aircraft with no stock photo, fetches nothing).
`/v1/enrich` is background work the device does on its own and is **not**
interaction. **Reports** on the Usage page are not attention either: a device sends
one each hour it is on, watched or not.

There is a second signal that exists but is not reported: the poll cadence is
itself touch-derived (the device polls fast for 10 minutes after a touch, slower
otherwise), so request *rate* encodes roughly how many hours a day someone was
present. Reading that back out requires each model's three cadences and produces
a number that looks precise and isn't — so it is deliberately left out. If you
want it properly, the device should report it (see *What this cannot tell you*).

---

## Putting Access in front

The Worker **verifies the Access JWT itself** rather than trusting that Access is
in front of it. Access protects a *hostname*; it does not protect a Worker. A
workers.dev subdomain left enabled, a second custom domain, or a policy edited to
"Bypass" by mistake all reach the code directly. Verifying the assertion means
the only way in is a token Access actually signed, whatever the routing looks
like. `workers_dev = false` is set on every environment as well — defence in
depth means not offering the second door either.

Everything in [src/access.ts](src/access.ts) **fails closed**, which is the exact
opposite of the device Worker's revocation check and for the opposite reason:
there, an infrastructure blip must not take the fleet off the air; here, an
infrastructure blip must not hand out an admin surface.

**A Worker deployed before its Access config is set serves nobody.** With
`ACCESS_TEAM_DOMAIN` or `ACCESS_AUD` missing, every request is a 403. That is
intentional and it is tested.

### Setup

1. **Zero Trust → Access → Applications → Add** a self-hosted application for
   `fleet.valarsystems.com`.
2. Policy: *Allow* → *Emails* → your address. (Not "Everyone", not "Bypass".)
3. Copy the application's **Application Audience (AUD) tag**.
4. Put the AUD and your team domain into `[env.production.vars]` in
   [wrangler.toml](wrangler.toml) (done 2026-09-24 for `fleet.valarsystems.com`;
   the AUD is an identifier, not a secret):

   ```toml
   ACCESS_TEAM_DOMAIN = "yourteam.cloudflareaccess.com"
   ACCESS_AUD         = "<the AUD tag>"
   ```

5. Optionally set `ACCESS_ALLOWED_EMAILS` as a belt-and-braces list on top of the
   policy — it costs nothing and catches a policy widened by accident.

### The analytics token

```sh
npx wrangler secret put CF_API_TOKEN --env production
```

Create it at **My Profile → API Tokens → Create Token → Custom**, with
**Account · Account Analytics · Read** and *nothing else*. It is a read
credential for telemetry, not an account key; scope it that narrowly. It never
touches the device Worker.

### Deploy

```sh
cd dashboard
npm install
npm test
npm run deploy:production
```

Then open `https://fleet.valarsystems.com`. Access will challenge you.

---

## Revocation

The **Revoke** button writes the same aggregate KV entry (`cfg:revoked`) the
device Worker already honours, in the same tolerant format. The documented
`wrangler` procedure in
[`proxy/src/revocation.ts`](../proxy/src/revocation.ts) still works and the two
cannot fight — the dashboard writes a commented, one-id-per-line file that a
human can edit by hand.

It also fixes the failure that bit us on the feature's first live test: an inline
`wrangler kv key put` value containing a newline is **truncated at the first
line, silently, in the dangerous direction** — the write "succeeds", the id never
lands, and the device you meant to cut off keeps working. A read-modify-write
against a parsed set cannot reproduce that.

Two properties are pinned by tests rather than argued:

- **Nothing that isn't a device id can enter the list.** A truncated id can't
  become a prefix match; `*`, `all`, an empty string and a bare comment are all
  dropped rather than matched on.
- **No single operation can deny the whole fleet.** Junk already in the entry is
  dropped on the next write rather than propagated.

A revoked device fails the same way a network outage does — 401s, the stale
ladder, then an empty screen — verified on hardware. It does not wedge, and
restoring it brings it back within ~60 s with no user action.

Revocations are logged with the operator's email. It is the only mutating path in
the product; the audit trail shouldn't depend on anyone remembering to look.

---

## What made this possible

The device Worker's Analytics Engine points previously carried no device
dimension at all, so *no* per-device question could be answered. Two changes in
[`proxy/src/metrics.ts`](../proxy/src/metrics.ts):

- **`dev` and `fw` appended as blob5/blob6**, and only ever on the device-key
  path. `X-Blip-Device` is a device-supplied string on an unauthenticated edge;
  recording it before the key check would let anyone write arbitrary ids into the
  dataset. That wouldn't break serving, but it would quietly make this page a
  fiction — and a fleet view you can't trust is worse than none. Shared-key
  requests aggregate as *unattributed* rather than being guessed at.
- **The endpoint collapsed to a bounded route.** `ep` was the raw pathname, so
  `/v1/enrich/<hex>` and `/v1/photo/<key>` made index1 effectively unbounded —
  one index value per airframe, plus one per URL any scanner ever probed. That
  degrades the aggregates Analytics Engine exists to accelerate, and it made
  "how many cards did this device open?" unanswerable, because every row was its
  own group.

Both are **appends**, never insertions: points are queried by blob position and
retained for three months, so shifting blob1–blob4 would silently rewrite the
meaning of everything already stored.

### Counting

Successful cache HIT/STALE points are sampled 1:10 with the correction carried in
`double4`. Every count here is `SUM(double4)`, **never** `count(*)` — getting it
wrong under-reports the busiest devices by 10×, which are exactly the ones worth
looking at. There is a test that asserts it.

---

## Usage counters, and what this still cannot tell you

**The Usage page reads the hourly usage index** (`proxy/src/metrics.ts`
`recordUsage`, index `usage`): eight integers per report -- card opens, switches to
Radar / List / Stats / Follow, logbook claims, a Follow-configured flag, and hours
since boot. Six are deltas and are summed; the flag and the uptime gauge are read
at the latest report and **never summed** (summing an hours-since-boot gauge
invents weeks). **Enrolled but silent** is the enrolment ledger (`enr:dev:*`)
minus every device that made any request in the window -- *not* the ledger's
`lastAt`, which is the last enrolment, and a working unit enrols once.

Worth knowing before it gets used to make product decisions:

- **What a screen or card was used ON.** The counts say THAT Radar was opened or a
  card was tapped, never which aircraft, callsign or follow target.
- **Swipes and zoom changes** beyond the four screen-switch counters.
- **Anything about a local-receiver user.** They don't talk to the proxy at all.
- **Who a device belongs to.** The id is a hash of the MAC; `provisioned.csv` is
  the registry that maps it to a unit.

### Screen-usage telemetry: decided NO on 2026-08-02, REVERSED on 2026-08-27

**The reversal is Daniel's, deliberate, and recorded in the root
[CLAUDE.md](../CLAUDE.md) ("Usage telemetry: counts yes, subjects never") and the
main README's Privacy & telemetry section.** The line moved from *collect nothing*
to *count THAT a feature was used, never WHAT it was used on*, and it is enforced
by construction (an integers-only payload, asserted on both sides of the wire),
not by prose. The 2026-08-02 reasons, and what became of each:

1. **Behavioural data about what happens in someone's home** -- STILL OUT. That is
   the half of the old line that survived: which aircraft, which callsign, which
   follow target, and any per-event timestamp stay off the wire.
2. **"Emailing ten owners beats instrumenting them"** -- the reason that LOST. It
   does not scale past ten, and at fleet scale a feature nobody wanted and a
   feature nobody could find look the same.
3. **"Blipscope doesn't track how you use it"** -- no longer a sentence we can
   say, so the published one changed with it: the device counts THAT a feature was
   used, never WHAT it was used on. The main
   [README's Privacy & telemetry section](../README.md#privacy--telemetry) says so,
   including that it is a change. Widening what is collected changes that section
   in the same commit, or the change is not done.

**Cards** on the Fleet page is still not a counter we added; it is a photo fetch
the device has to make to draw the card at all. The Usage page's card-opens count
IS a counter, and it is one of the eight integers the revised policy allows.
