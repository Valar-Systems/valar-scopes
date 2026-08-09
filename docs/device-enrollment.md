# Self-provisioning device keys

How a device that was **not** flashed by us gets a per-device cloud key.

**Status: planned, not built (decided 2026-08-08).** This is for kit and DIY
buyers, who do not exist yet. Nothing on the pilot path depends on it, and it
does not touch the assembled-board burn procedure — [`scripts/provision-device.py`](../scripts/provision-device.py)
already makes that unattended (MAC over USB → id → key → NVS image → flash →
verify against production → log to `provisioned.csv`).

**What happens until then:** DIY buyers get a key emailed with their order.
Manual, fine at this volume, and it yields a customer↔key mapping that
`provisioned.csv` — which only knows boards we flashed — structurally cannot.

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

## Open question — deliberately not answered here

**What is a self-enrolled key actually worth to an abuser?**

This is the number that decides whether the minimum abuse controls are adequate
or whether a harder gate is mandatory, and it is not knowable from the code. It
depends on upstream economics: `adsb.lol` positions is a real per-IP rate limit
we are already working around, so "how many free rate-limit buckets does it take
to hurt us" has a concrete answer — we just don't have it yet.

Revisit when there is real fleet traffic to measure against. **Do not resolve it
by guessing**; a guessed threshold here would be indistinguishable from a
measured one in six months, and would be quoted as though it had been measured.
