# The starved-seconds counter

Ruled 2026-10-20 and again 2026-10-21 after the #245 soak (#433): **one integer, starved seconds per hour, in
the usage report, with the disclosure updated in the same commit.** It is how the fleet's TLS starvation
becomes visible: a v18 soak on E2:08 found 0 episodes in 8.5 h, while 6E:64 starved on v17. One board-night
can't tell rare from fixed, and today the Worker sees only boots and requests, which cannot show a
minute-scale episode (#433).

This document freezes the design and the predictions **before any code**.

## What changes

**Device (`include/UsageReport.h`, `include/UsageStore.h`, `AircraftManager`):**
- `usage::Counters` gains `starvedSeconds`, a monotonic count of seconds the starvation watch has spent in
  its **starved** state. That state runs from the `ENRICHMENT STARVED` edge to the `ENRICHMENT RECOVERED`
  edge, the same span whose length the device already prints as `RECOVERED after Ns`.
- The report carries its **delta** like every other counter, so each hourly report says how many seconds
  of that hour were starved.
- It is appended as the **ninth** field, after `uptimeHours`. `FIELD_COUNT` goes 8 → 9 and `MAX_LEN` stays
  96: the worst case is 8 × 10 digits + 1 + 8 commas = 89.
- It accrues on the loop task's existing health tick: wrap-safe elapsed `millis()` between ticks, while
  starved.
- It persists **only when a report is taken**, like every other counter, as two more NVS keys (`t_starve`,
  `r_starve`). There is no new write path, and it still comes to 24 writes a day.
- The rule ("how many seconds does this tick add") is a pure function, host-tested. The I/O stays in
  `AircraftManager`.

**Worker (`proxy/src/metrics.ts`, `recordUsage`):**
- It accepts **8 fields (the fleet today, firmware ≤ 18) or 9**, and refuses 7 and 10.
- A 9-field report writes `starvedSeconds` as a ninth double. **Every usage row also gets its arity as a
  blob** (`"u8"` / `"u9"`).
- **Why the arity blob:** an 8-field row has no ninth value, and Analytics Engine reads a missing double as
  0. Without a marker, "this firmware doesn't measure it" would look exactly like "0 seconds starved", and
  CLAUDE.md is plain that silence is not a reading. Readers select `u9` rows for starvation and never infer
  a 0 from a `u8` row.

**Disclosure, in the same commit:**
- `README.md` "Privacy & telemetry" changes "eight numbers" to "nine", and names the new one: how many
  seconds in that hour the device couldn't open a secure connection, a health measure that says nothing
  about you.
- `proxy/pages/support.html` gets the same sentence, with the embedded module regenerated.

**Order, both Daniel's:** the Worker that accepts 8 or 9 is **deployed before** any firmware that sends 9
is **released**. A 9-field report reaching today's Worker is dropped by its `{7}` check, so in that order
nothing is lost. Daniel is told before the deploy is needed.

## Predictions (frozen before any code)

Each check has a sabotage that must turn it red.

**SC1. The format (host, device).** `usage::Format` emits **9** fields, digits and commas only. The
existing "no identity can reach the wire" assertions still hold, with their control. A worst-case report
(every u32 at its maximum) is ≤ `MAX_LEN`.
- *Sabotage:* appending the field as text (e.g. `"s=12"`) must fail the digits-and-commas assertion.

**SC2. The accrual rule (host, device).**
- Driven tick by tick from the policy's own state, `starvedSeconds` grows by **exactly** the elapsed
  seconds of every tick interval spent starved, and by 0 otherwise. That includes a `millis()` wrap mid-episode.
- Replaying the 6E:64 episode shape (a STARVED edge, recovery at 150 s) accrues **150 ± one tick (30 s)**,
  matching the device's own `RECOVERED after 150s`.
- *Sabotages:* counting **ticks** instead of seconds must fail; counting only the edge (one tick) must fail.

**SC3. The Worker (vitest).**
- 8-field reports are accepted and written as `u8` with 8 doubles.
- 9-field reports are accepted and written as `u9` with the ninth double = `starvedSeconds`.
- 7 and 10 fields are refused.
- The existing shape checks (digits and commas, ≤ 128 chars, u32 range, the 0/1 flag) are unchanged.
- *Sabotages:*
  - accepting only 9 must fail the 8-field case (the fleet's rows would be dropped);
  - writing an 8-field row without its `u8` marker must fail the gap-versus-zero assertion.

**SC4. The two sides agree (host, cross-side).** The device test reads the Worker's accepted arities **from
`proxy/src/metrics.ts`** (the other side of the contract) and asserts the device's `FIELD_COUNT` is among
them. *Sabotage:* `FIELD_COUNT` 10 must fail it.

**SC5. The disclosure says it (host, CI).**
- A check reads `README.md` and `proxy/pages/support.html` and requires both to name the starved-seconds
  number, and the README to say "nine numbers".
- `embed-pages.mjs --check` passes.
- *Sabotage:* the sentence removed from `support.html` must fail.

**SC6. On the bench (E2:08), with predictions re-stated in the PR before the flash.**
- **Healthy:** ≥ 2 h on the release build. Every hourly usage report the board prints carries
  `starvedSeconds` **0**.
- **Forced:** a **bench-only** env makes `CanAllocate` refuse for a fixed span. Its marker is covered by
  `check-no-bench-hooks`, so it can never ship. The next report's `starvedSeconds` must equal the device's
  own `RECOVERED after Ns` for that episode **± 30 s**. Two instruments, one number.
- No other board is flashed. E2:08 is flashed with the key-safe app-only flash, so NVS is never touched.

**SC7. After Daniel's deploy (production, read-only).** `u8` rows keep arriving from the fleet. The old
firmware's reports are still accepted, which is the property the order exists for. Read twice, ≥ 10 min
apart.

**Outside the set:** stop and report. Code, tests and bars are not edited to make a reading come true.

## Results: host, Worker and build (2026-10-21, before any bench run)

Graded on `ad88f0b`. Every frozen bar below was met, and every sabotage turned its check red.

| check | reading | verdict |
|---|---|---|
| SC1 format | 9 fields, digits and commas; worst case within `MAX_LEN` | as predicted |
| S-SC1 `s=%u` | the digits-and-commas assertion red (4 FAIL lines) | as predicted |
| SC2 accrual | healthy hour 0; the 6E:64 shape accrues **150 = RECOVERED 150**; jitter + a `millis()` wrap, 18 episodes, counter exceeds the sum of RECOVERED floors by 8 s (< 1 s per episode) | as predicted |
| S-SC2a ticks, S-SC2b edge only | SC2 red, each | as predicted |
| SC3 Worker | 8 → `u8` with 8 doubles, 9 → `u9` with the ninth double; 7 and 10 refused; shape checks unchanged | as predicted |
| S-SC3a accept only 9 | the 8-field case red | as predicted |
| S-SC3b no arity marker | the gap-versus-zero assertion red, plus the two cases that also assert the marker (the plant dropped it from both arities) | as predicted |
| SC4 cross-side | `FIELD_COUNT` 9 is in the Worker's `USAGE_FIELD_COUNTS = [8, 9]`, read from `metrics.ts` | as predicted |
| S-SC4 `FIELD_COUNT` 10 | SC4 red | as predicted |
| SC5 disclosure | README says "nine numbers" (derived from `FIELD_COUNT`, previous count absent), and README and `support.html` both carry the same sentence; `embed-pages.mjs --check` current | as predicted |
| S-SC5 sentence removed from a copy of `support.html` | exactly one FAIL line: the `support.html` check | as predicted |

**Where SC5's check lives.** In the host usage test, beside SC4, not in vitest: the proxy suite runs in the
Workers pool, which has no `node:fs`, so it cannot read `README.md`. The test takes `README.md` and
`support.html` as arguments from `run.sh` and reports BLIND (exit 2) if it cannot read them.

**Suites and build.**
- `test/host/run.sh`: ALL HOST TESTS PASSED (usage report 44 checks, 0 failures).
- Worker: 43 files, 580 tests passed; `tsc --noEmit` clean.
- `pio run -e blipscope-s3-128` succeeds.
- `check-no-bench-hooks.sh` on that image: no bench hook, with `[bench-force-starve]` among the 4 markers checked and the anchor present.
- `--selftest` passes.

**Still to run:** SC6 on E2:08, with its predictions re-stated in the PR before the flash, and SC7 after
Daniel's deploy.
