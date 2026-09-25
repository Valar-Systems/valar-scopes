# Worker-minted provisioning keys

**Decision (2026-09-25, Daniel): option B.** The bench never holds `DEVICE_KEY_SECRET`.
A factory board's key is minted by the device Worker, which already holds the secret, on
request from an authenticated bench.

## Why

Since 2026-08-31 **no person holds the production `DEVICE_KEY_SECRET`**. The #266 split
generated it inside a session straight into a scratch file, put it, and deleted the file
the same day. The old flow (`provision-device.py` deriving `HMAC(secret, id)` on the bench)
therefore cannot mint a valid key at all. Rotating to a value a person holds would break
every device key in the fleet, and it would recreate the copy that went missing. Minting in
the Worker removes the need for the copy.

## The route

`POST /blipscope/provision` on the **device Worker**. It sits beside `/blipscope/enroll` and
above the device-auth gate, because the board being provisioned has no key yet.

```
Request   X-Provision-Token: <token>
          { "mac": "90:70:69:xx:xx:xx" }
200       { "deviceId": "<16 hex>", "key": "<64 hex>", "mintsToday": n }
```

Checks run in this order. Each refusal is one JSON error, and none of them mints:

| # | check | refusal |
|---|---|---|
| 1 | method is POST | 405 (the Worker's method gate) |
| 2 | `X-Provision-Token` equals the `PROVISION_TOKEN` secret, compared in **constant time** over SHA-256 digests, so length leaks nothing | **403 `forbidden`**. This includes an unset secret, which fails closed |
| 3 | `DEVICE_KEY_SECRET` is configured | 503 `not_configured` |
| 4 | `mac` is six octets, unicast, and not all-zero or all-FF | 400 `bad_mac` |
| 5 | the derived id is not on the fake-id allowlist (production) | 403 `fake_device_id`, the same guard as every other route |
| 6 | the id is not revoked | 403 `revoked`, the same rule as enrolment |
| 7 | fewer than **120** mints today (UTC) | **429 `daily_cap`**, with the cap and the reset time |

**Token.** `PROVISION_TOKEN` is a Worker secret that Daniel generates and keeps in his
password manager. The bench reads it from `%USERPROFILE%\.config\valar-flasher\provision-token`,
and the provisioner prints only its **presence** (a boolean). **Staging has its own
`PROVISION_TOKEN`**: there's no exemption path, and staging is reached only with the staging
token.

**Derivation.** The Worker recomputes exactly what firmware and `provision-device.py`
compute:
- `deviceId = SHA-256(raw 6 MAC bytes ‖ LEADERBOARD_SALT)[:8]` as hex;
- `key = HMAC-SHA256(DEVICE_KEY_SECRET, deviceId)` as hex.

The salt is **generated** into the Worker from `include/DeviceIdentity.h`
(`embed-device-salt.mjs`, `--check` in CI), so firmware and Worker can't drift apart
unseen. The bench recomputes `deviceId` from the MAC and **refuses a mismatch**. That's the
runtime half of the same check.

**Cap.** A KV day counter, `prov:day:<yyyy-mm-dd>`, with a 40-day TTL, the enrolment
pattern. KV has no atomic increment, so the cap is **best effort**: parallel mints (at most
8, the hub size) can overshoot by that many. It limits a leaked token's damage per day; it is
not an exact meter.

**Record: every mint and every refusal is logged.**
- **Structured log:** `{"evt":"provision_mint" | "provision_refused", "reason", "mac_hash", "device", "operator":"bench", "mints_today"}`.
- **Analytics point:** `blobs: ["provision", mac_hash, "bench", deviceId]`, `doubles: [mintsToday]`, own index.
- **`mac_hash`** is `HMAC-SHA256(DEVICE_KEY_SECRET, "mac:" + mac)`, first 32 hex. It's
  keyed, because a plain hash of a MAC can be reversed by enumeration.
- **The token is never logged.**

## The bench side

- **`provision_one.py`** mints through the route instead of deriving. It sends the MAC, checks
  the returned `deviceId` against its own derivation, writes and proves the NVS key, then
  verifies the key with `/v1/config` (200) as before. `DEVICE_KEY_SECRET` is **not read
  anywhere on the bench**. `provision-device.py` and `provision-batch.py` mint the same way.
  `device_key()` stays in `provision-device.py` for one purpose only: generating the
  equivalence fixture.
- **valar-flasher bench mode** is gated by the **token file**, not an environment variable. It
  refuses in one sentence if the file is absent or empty.

## Tests

- **Equivalence, committed.** `scripts/mint-fixture.py` runs `provision-device.py`'s
  `device_id()` and `device_key()` on fake MACs with a test secret and writes
  `proxy/test/fixtures/mint-equivalence.json` (`--check` in CI). The Worker must return
  identical ids and keys for those MACs.
- **Equivalence, live, once, after deploy.**
  - Mint for provisioned row 1's MAC. It must return the device id recorded for that row, and
    its key must authenticate as that device (`/v1/config` 200): the same key the unit already
    uses, with no re-provisioning.
  - Run outside the repo: it involves a real id.
- **Refusals:** wrong token, missing token, and an unset `PROVISION_TOKEN` each give 403. At
  the cap, 429. A fake id gives 403. A bad MAC gives 400. The token never appears in a log line.
- **Sabotage:** remove the token check, and the forbidden tests go red.

## Runbook

`docs/bench-key-rotation.md` states that the secret is **Worker-only by design**: nobody holds
a copy, and a rotation is the recovery if the Worker's copy is ever lost.
