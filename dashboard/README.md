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
| **Fleet** | Which devices are alive, how much each is used, error rate, staleness. Revoke / restore. |
| **Firmware** | Who is on which version, per model — *did that OTA actually land?* |
| **OTA** | Every update attempt with its result, naming the exact unit that failed. |
| **Enrichment gaps** | What the fleet looked up that we couldn't answer, ranked by real demand. |

`/fleet.json` mirrors the fleet table for piping elsewhere.

### Requests are not attention

This is the one thing to be clear-eyed about. **A device polls on a timer whether
or not anyone is in the room**, so `requests` measures *uptime*, not engagement.

The honest interaction number is **Cards**: the firmware fetches a photo exactly
once per aircraft when a **detail card is opened**, and that only happens on a
tap. `/v1/enrich` is background work the device does on its own and is **not**
interaction.

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
   [wrangler.toml](wrangler.toml), replacing both `REPLACE_ME`s:

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

## What this cannot tell you

Worth knowing before it gets used to make product decisions:

- **Which screen someone uses.** Radar vs List vs Stats, swipes, zoom changes —
  none of it reaches the cloud.
- **Whether the device is on but ignored.** A board on a shelf and a board being
  watched look identical apart from the Cards column.
- **Anything about a local-receiver user.** They don't talk to the proxy at all.
- **Who a device belongs to.** The id is a hash of the MAC; `provisioned.csv` is
  the registry that maps it to a unit.

### Screen-usage telemetry is a NON-GOAL, not a backlog item

**Decided 2026-08-02. Do not propose adding it.**

The first two above are closeable — the device already makes a request we could
hang interaction counters on, and it would be a small change. We are not going to,
and the reason is worth writing down so this doesn't get re-litigated every time
someone notices the gap:

1. **It is behavioural data from a device in someone's home.** A screen glanced at
   over morning coffee is not our business, and no product question here is worth
   that trade.
2. **At this scale it wouldn't even work well.** At 50 units, emailing ten owners
   answers "how do people use this?" better than instrumenting all of them — with
   more nuance, and with the *why* attached.
3. **"Blipscope doesn't track how you use it" is a sentence worth being able to
   keep saying**, and it is only true while it stays entirely true.

This is stated on the customer-facing side too, in the main
[README's Privacy & telemetry section](../README.md#privacy--telemetry) — so it is
a published commitment, not an internal preference that could quietly lapse.

**Cards is not an exception to this.** It isn't a counter we added; it is a
photo fetch the device has to make to draw the card at all. If a future change
ever made photos local, that column would simply disappear rather than being
replaced with a reporting call.
