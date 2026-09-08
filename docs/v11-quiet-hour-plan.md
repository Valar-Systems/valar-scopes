
---

## Boot-reason path — Worker half DEPLOYED 2026-09-08, live check BLOCKED

`recordBoot()` + `X-Blip-Boot`, merged as `1cd5c00` and deployed to production
(`/healthz` confirms the commit). 417 tests pass, including the pass condition
stated as an assertion: **a boot with no update available produces a reason row.**

Post-deploy regression check against live fleet traffic: 19 Worker invocations,
all `ok`, every request 200.

### What is NOT verified, and why it stops here

The pre-registered requirement was *deployed and verified live before any
firmware sends the field.* The first half is done. **The second cannot be.**

`recordBoot` is called only on the authenticated path — deliberately, matching
`recordUsage`, so an anonymous caller cannot spend the Analytics Engine budget.
Verifying it live therefore needs an authenticated request carrying the header,
and:

- the operator device key on this machine is **stale** — production returns 401.
  Discriminated rather than assumed: the `enr:dev:` row for that device is
  PRESENT, and a known-good bench device's row is PRESENT too (the control), so
  the device is enrolled and the KEY is what no longer matches. Consistent with
  the 2026-08-31 rotation;
- deriving a fresh one needs `DEVICE_KEY_SECRET`, a Worker secret that cannot be
  read back;
- and no bench board can send the header, because the firmware half does not
  exist yet — which is the whole point of doing the Worker first.

So the ordering guarantee is **partially unmet** and the options are:

1. **Issue a working device key.** Closes the live check properly, before any
   flash, exactly as specified. Daniel's to issue, by file, never through chat.
2. **Accept the live check happening when the firmware lands.** The first board
   to send the header is then simultaneously the first test of the parser. That
   is weaker — it is the arity trap's shape, with device and Worker changing
   together — but the blast radius is small: a parser that drops rows loses
   telemetry, it does not break serving, and `recordBoot` is wrapped so it
   cannot.

**Not chosen here.** Recorded so the gap is visible rather than discovered later
as a green check that never ran.

### And a note for O6

Once this path is live AND the firmware sends it, a future O6-style check needs
**no prerelease scaffold**: the reason arrives on an ordinary check-in whether or
not an update was available. The pinned-prerelease setup in the O6 plan is a
workaround for the OTA-report coupling and expires with it.
