# Self-provisioning device keys

How a device that was **not** flashed by us gets a per-device cloud key.

**Status: BUILT 2026-08-12** (decided 2026-08-08, architecture revised 2026-08-12).
Pending only the Turnstile widget itself, which needs a Cloudflare API token
scoped `Account.Turnstile:Edit`, and a bench pass on the two boards.

It does not touch the assembled-board burn procedure —
[`scripts/provision-device.py`](../scripts/provision-device.py) already makes
that unattended (MAC over USB → id → key → NVS image → flash → verify against
production → log to `provisioned.csv`) and a factory-flashed unit never sees the
enrollment step at all.

**What changed on 2026-08-12, and why the in-page widget below was abandoned:**

The original plan put the Turnstile widget on the device's own settings page.
That cannot work, and the reason is worth keeping because it is not obvious:

- The device serves that page over mDNS at `http://<device-name>.local`, and the
  device name is **user-settable**. The hostname is therefore unknowable in
  advance and different on every board.
- Turnstile validates the hostname a token was solved on, and Cloudflare's own
  guidance is explicit: *"Do not allow local hostnames in production."* An
  allow-list that must accept `radar-desk.local` or a bare LAN IP is the shape of
  validation rather than validation.
- Given that the gate protects a relationship (below), a nominal check is worse
  than an honest absence of one.

So the solve happens on an origin we own — `scopes.valarsystems.com/enroll` —
and the key comes back to the device page by `postMessage`, which crosses
HTTPS → HTTP because no resource is loaded, only a message.

**Two URLs, on purpose.** The canonical page is `/blipscope/enroll`, per the
edition-namespacing convention, and that is what the popup opens: the machine
path spends no redirect hop it could fail on. The short `/enroll` is a 301 to it,
and exists because the device's own fallback text asks a customer to *type* that
URL on a phone next to an 8-hex id — every character is one they can get wrong.

This was found the hard way. Enrollment first shipped with the popup pointing at
`/enroll` and the Worker routing only `/blipscope/enroll`: the entire feature was
a 404, behind sixteen passing tests, because every test requested the path the
tests had chosen. Two checks close that gap, and neither is a transcription of
the other side's intent — `proxy/test/enroll.test.ts` pins both paths from the
Worker's side, and `proxy/scripts/smoke-prod.sh` greps the URLs out of
`src/ConfigurationWebServer.cpp` and fetches each one against the live Worker.

**THE POPUP IS A CONVENIENCE, NOT A SECURITY BOUNDARY.** The enrol page is an
ordinary HTTPS page and works perfectly well opened directly on a phone; that is
the documented path for a LAN that cannot reach Cloudflare, and it is the primary
fallback rather than a degraded one. The gate is the **solve**, not the transport.
Moving the browser does not remove it. The honest residual: this makes scripted
bulk enrollment marginally easier than a device-bound flow would — but a
device-bound flow was never available (see the hostname problem), and the
detection is the volume log, not the popup.

## Background: what already exists

Per-device auth is live in production today (`DEVICE_KEY_SECRET` confirmed set,
2026-08-08). It is stateless by design:

    deviceKey = HMAC-SHA256(DEVICE_KEY_SECRET, deviceId)      64 hex chars
    deviceId  = SHA-256(MAC ‖ LEADERBOARD_SALT)[:8]           16 hex chars

The Worker holds one secret and recomputes the expected key per request — no key
database ([`proxy/src/deviceauth.ts`](../proxy/src/deviceauth.ts)). A device-authed
request gets two things a shared-key request does not: the leaderboard's
**verified** tier, and its own `dev:<id>` rate-limit bucket instead of sharing the
fleet's ([`index.ts:47`](../proxy/src/index.ts#L47),
[`ratelimit.ts:24`](../proxy/src/ratelimit.ts#L24)). Revocation is per-id, one
aggregate KV entry, hand-edited by an operator
([`revocation.ts`](../proxy/src/revocation.ts)).

## The plan: enroll in the browser, with Turnstile

The user is already on the device's config page during setup. Put a Cloudflare
Turnstile challenge **there**, have the page call the Worker's enrollment
endpoint, and write the returned key back to the device through the config POST
it already handles.

**Why this one and not the others.** Every other option fails on the same
question — *what proves a request came from a person who bought a device?* An
ESP32 cannot solve a challenge, and the Worker cannot tell an ESP32 from `curl`.
The config page is the only place in the entire system where a human is provably
present. So the proof gets taken where it actually exists, rather than
approximated somewhere it doesn't.

That collapses the trust problem instead of routing around it: a
browser-enrolled key can be **fully** trusted, so there is no second-class tier
to invent, no split in the derivation, and no `deviceAuthed` caveat on the
leaderboard.

It is also the cheapest of the three by a distance — the firmware already knows
how to store a key it is handed, so there is essentially no firmware work.

| | |
|---|---|
| Worker: endpoint + Turnstile verify + KV enrollment ledger + caps | ~half a day |
| Config page: challenge widget + POST + error states | ~half a day |
| Firmware | ~none — reuses the existing key-save path |

## Rejected: open enrollment (device asks, no gate)

The device POSTs its id on first boot and gets a key back. **Rejected, and this
is the argument to re-read before anyone proposes it again.**

**It is a key oracle, not a mint.** Because the key is a pure function of the id
and nothing else, an endpoint that returns `HMAC(secret, id)` for a submitted id
returns a valid key for *any* id. There is no allowlist for a made-up id to fail
against, because the stateless design has no list at all. "Enrollment" is not a
grant of anything — it is the server performing the one computation an attacker
cannot.

Three consequences, none of which rate limiting touches:

- **An abuser needs one key, once.** Any limit loose enough for a real device to
  enroll is loose enough for them. Throttling an oracle only slows down someone
  who needs it repeatedly.
- **Revocation stops working.** It is by id, and ids would be attacker-chosen:
  revoke, and they enroll a new one. Our move is a hand-edited `wrangler kv put`;
  theirs is a `curl`. That asymmetry loses on a long enough timeline, and the
  side that has to stay awake for it is us.
- **It is an upgrade, not merely a bypass.** Extracting the shared key from
  open-source firmware is already possible; what this would add is a *private*
  rate-limit bucket and the verified leaderboard tier, mintable in bulk,
  programmatically.

The fix that would make it survivable — deriving self-enrolled keys from a
different input (`HMAC(secret, "self|" + id)`) so the server can tell the tiers
apart statelessly, plus a KV ledger, plus enrollment-specific IP limits, plus a
global daily cap — is more machinery than the Turnstile version, and it buys a
*worse* result: a permanent second-class tier that can never back a verified
leaderboard entry. More work, less trust.

## Rejected: order-code gate

A single-use claim code on the card or in the order email, entered once at setup.
Sound on the security merits — it keeps the endpoint effectively authenticated,
makes revocation stick (burn the code too), and would give a real registry.

Rejected on cost, not on principle: it needs Shopify fulfilment plumbing to mint
and deliver codes, and it creates a lost-code support path that lands on us
forever. It also does not remove a manual step — it moves ours onto the customer.
The emailed-key stopgap gets the registry benefit today with none of the
plumbing.

## Constraint that must survive into the implementation

**Enrollment writes `cloud-key-fac`, never `cloud-key`.**

`cloud-key` is the user-editable override; `cloud-key-fac` is the read-only
factory identity, never rendered and never writable from the web UI. The device
falls back from the first to the second
([`AircraftManager.cpp:790-799`](../src/AircraftManager.cpp#L790-L799)), which is
what makes *"clear the box and save"* the documented customer repair for a
mangled key rather than an unrecoverable act.

Write an enrolled key into `cloud-key` and that repair becomes the injury: it
wipes the only copy, falls back to an empty `-fac`, and the device is
unauthenticated with no way back. Write it into `-fac` and the repair keeps
working exactly as documented, for enrolled and factory devices alike.

## ~~Open question~~ ANSWERED 2026-08-12 — and the answer changes what "tight" means

**What is a self-enrolled key actually worth to an abuser?**

~~*This is the number that decides whether the minimum abuse controls are adequate
or whether a harder gate is mandatory, and it is not knowable from the code. It
depends on upstream economics: `adsb.lol` positions is a real per-IP rate limit
we are already working around, so "how many free rate-limit buckets does it take
to hurt us" has a concrete answer — we just don't have it yet. Revisit when there
is real fleet traffic to measure against.*~~

**The premise was wrong, not just unmeasured.** It assumed the thing at risk was
*capacity* — a quota that a farm of self-enrolled keys could exhaust. Samuli
granted sponsored access with **leeway rather than a hard ceiling**, so there is
no quota to exhaust. What a farm of keys would actually do is make us the source
of the traffic he sees.

**So the gate protects a RELATIONSHIP, not a rate limit.** That is a different
engineering brief, and it resolves the question in a direction no traffic
measurement would have reached:

- **A threshold was never the deliverable.** There is no number of keys that is
  "safe" and one more that is not. The failure is qualitative and social, and it
  arrives as a conversation rather than as a 429.
- **The gate must be REAL, not proportionate.** With a quota you can argue that a
  cheap check is adequate because the loss is bounded and recoverable. Here it
  is neither, so: **server-side siteverify, always. A client-only check is not a
  weaker version of this design, it is a different design that does not work** —
  the browser can be skipped entirely, and an unverified token is decoration.
- **Volume must be OBSERVED, not inferred.** Log enrollment volume so abuse is
  something we *see* rather than something we deduce afterwards from someone
  else's complaint. This is the part a rate limit would have given us for free
  and leeway does not: with no ceiling to hit, nothing else will ever tell us.
  Enrollments per day, per IP, per ASN, and the siteverify hostname.

**Do not reopen this as a rate-limit question.** The next person to look will
reasonably ask "how many keys can we afford to leak" and will find no number,
because the question does not have one. The cost is measured in a relationship
with the operator whose data the whole cloud feed depends on, and the correct
posture toward that is not a threshold — it is a working gate and an honest log.

Note that this cuts the other way too: **it is not an argument for a harder gate
than Turnstile.** An order-code gate protects the relationship no better than a
working challenge does, and it costs the customer a step. What matters is that
the challenge is genuinely verified and that we can see the volume.

---

# What shipped (2026-08-12)

## The security payoff, stated precisely

It is tempting to record this as *"we removed the shared key from the firmware"*.
**That would be recording something that never happened.** Verified against the
built artifact rather than the ini: `[cloud] prod` injects only
`FEATURE_CLOUD_FEED` and `CLOUD_FEED_BASE`, no env defines `CLOUD_FEED_KEY`, it
falls back to `""` in `src/CloudFeed.h`, and the shipped
`blipscope-s3-146` image contains no key string. (The one occurrence in
`platformio.ini` is a comment showing how to add one locally.) A public firmware
image has always yielded no credential.

What this work actually achieves:

> **We ended server-side acceptance of any non-per-device credential.**

After `BLIP_KEYS` is removed, the only thing that authenticates is
`HMAC(DEVICE_KEY_SECRET, deviceId)` presented with its matching `X-Blip-Device`.
That key is per-board, individually revocable, and attributable in the dataset. A
key leaked by any route — a screenshot, a shared bench build, a pasted value in a
support thread — stops being useful to anyone but the board it names.

An attacker holding a public image gets the base URL and the header names.
Neither is secret, and neither gets them data.

## Confirming WHICH credential a board is using

While both the shared key and per-device keys are valid, **"the board still
works" cannot distinguish them.** An enrolled board could be authenticating on
the old shared key and nothing would say so until removal broke it. That is a
check that cannot fail, so there is now a discriminator:

- **`X-Blip-Auth: device`** on every response to a per-device-authenticated
  request; **`X-Blip-Auth: shared`** on the shared-key path; **absent** when no
  auth ran, so absence is meaningful too.
- It is derived from the same field that gates metric attribution, so the header
  and the dataset cannot drift apart.

Two ways to read it, and the bench should use the first:

```sh
# LIVE, from anything that can reach the proxy as the device does.
curl -sS -D- -o/dev/null https://scopes.valarsystems.com/api/v1/blipscope/config \
  -H "X-Blip-Key: <the board's key>" -H "X-Blip-Device: <its device id>" | grep -i x-blip-auth
```

- **In the dataset:** `m.dev` (blob5) is populated **only** on the device path —
  `setDeviceAttribution()` returns early when `deviceAuthed` is false. So a board
  whose device id appears in `blob5` authenticated per-device; traffic with an
  empty `blob5` is shared-key. This is structural, not conventional: the shared
  path cannot populate that field.

**Do not remove `BLIP_KEYS` until both bench boards report `device`.** The whole
point of the sequencing is that the net comes away only after the replacement is
observed holding.

## Reflashing, editions, and what is actually lost

- **The device id survives everything.** `SHA-256(efuse MAC ‖ LEADERBOARD_SALT)[:8]`
  — read from efuse, not NVS, and the salt is uniform across all editions (no env
  overrides it). Same board, same id, same key, forever.
- **Enrollment is idempotent.** Re-enrolling re-derives; it cannot issue a
  different key. A reflashed board gets `200 already_enrolled` and the ledger logs
  the repeat rather than blocking it.
- **Revocation survives a reflash**, and is asserted rather than argued —
  `proxy/test/enroll.test.ts` checks refusal at enrollment *and* at the auth
  boundary, with a control proving the same id authenticates when not revoked.
- **Editions do not share a key, because only one edition has one.** `cloud-key`
  appears in four files, all radar; nothing under `src/eam/` references a key, a
  device id, or the proxy. Cross-edition auth is *absent* rather than intended or
  accidental. The key identifies a **board, not a firmware**, and the edition slug
  enters the derivation nowhere — deliberately, since putting it in would mean
  re-enrolling on every edition switch and would break revocation.
- **The mDNS hostname DOES change with the edition.** `DeviceIdentity::Name()`
  embeds the product name, so `Blipscope-A1B2C3.local` becomes
  `Missileer-A1B2C3.local` after a reflash. The bookmark breaks, and nobody would
  connect that to a firmware switch. Say so in the DIY docs.
- **What a full erase actually costs.** Leaderboard standing is server-side under
  `lb:dev:<id>` and survives — and counts are monotonic (`mono()` takes
  `Math.max`), so a freshly wiped board submitting zeros **cannot** ratchet the row
  down. The real loss is the local `logbook` NVS namespace: the on-device lifelist
  and seen history. The framing that matters to a customer:

  > *Reflashing keeps your leaderboard standing. A full erase loses the on-device
  > logbook — download it first.*

  Export exists (`/logbook.json?download=1`); restore is the gap, and it is cheap.

## The partition-table trap

Partition tables are per-env. Every `*-s3-146` edition uses `default_16MB.csv`, so
NVS stays put across an edition switch and nothing is lost. `blipscope-s3-128`
uses `partitions-s3-16mb-bignvs.csv` and today has no sibling edition — but adding
`missileer-s3-128` with the stock table would **relocate the NVS partition**,
making the logbook, location, WiFi credentials and enrolled key all unreachable at
once. It would present as *"my Blipscope forgot everything"*, pointing at whatever
the customer last touched and never at a line in an `.ini`.

Guarded now rather than when someone adds that env: `Logbook::reportCapacity()`
checks the entry count the **running** partition granted against what the build
expects (`-DBLIPSCOPE_EXPECT_BIG_NVS`), and says so loudly. Guarded on the
artifact, not the intent — a flag asserting what the ini meant would restate the
thing that is wrong.
