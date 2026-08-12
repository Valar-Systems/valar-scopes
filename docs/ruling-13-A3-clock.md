# RULING — §13 A.3, clock floor and deviation bucket (2026-08-11)

**Verdict: A.3 reopens. `bucketS` 0.1 → 0.2. `clockFloorMs` 76 → 199. Land the
constant and the corrected guard together, plus the fixture, in one PR.**

---

## 1. The bucket

Widen it. The reasoning is not "198 > 76" — it is what the old pair asserted
about the device.

A 0.1 s display bucket is a claim: *deviations that differ by more than 100 ms
are different, and we will show them as different.* The clock underneath that
claim can be off by 198 ms. So the device has been sorting players by margins
smaller than its own error. Two sorties a bucket apart were not measurably
different; the display said they were. **That is not scoring, that is ranking
noise.**

Set `bucketS = 0.2`. Set `clockFloorMs = 199` (⌈198.504⌉). The standing ruling
in the state file — *"Deviation displays in 0.1 s buckets per sortie; hundredths
only on cycle averages"* — is amended to 0.2 s buckets. Hundredths on cycle
averages survives unchanged: an average over many sorties can legitimately
resolve finer than any single one.

**On the statistic.** 198.5 is the running max over 8 corrections, not a draw
from 5. It supersedes 75.7 rather than competing with it — same board, same
statistic, same 3 h cadence, and a *cleaner* session (`poll_max_ms,45` vs
`20042`), so it cannot be dismissed as a worse measurement. Seven of eight
exceed 75.7; only 51.3 ms falls below. 75.7 was not a floor, it was a small
sample's lucky low.

Max is the right statistic here and a quantile is not. A p95 computed from 8
points is arithmetic dressed as inference — it would interpolate between the two
largest values and hand back a number with no sampling basis. With 8 samples the
only defensible conservative choice is the largest one observed, stated together
with the fact that it is 8 samples.

**On the margin.** 200 ≥ 199 satisfies the invariant by 1 ms, and the running
max over 8 samples has plainly not converged — a ninth correction could exceed
200. Do not pad the bucket to hide that. Handle it in §3 instead: the fixture
freezes what we have measured, and a runtime alarm reports when the world
exceeds it. Widening to 0.25 to buy headroom would be inventing evidence we do
not have in order to avoid hearing about it later.

---

## 2. The guard

The existing assertion is the defect, not the constant:

```
assert.ok(bucketMs >= body.scoring.clockFloorMs)   // 100 >= 76 ✓
```

It reads the floor out of the same object it is validating. Any floor the server
publishes will be consistent with any bucket the server publishes, because the
server picked both. **A number validated against itself.** It would have passed
identically with a floor of 5 ms or 5000 ms. This is the week's failure family
in its purest form and it belongs in the ledger as its own entry, alongside the
stubbed leak that kept 22 tests green and the conventions guard that matched
nothing.

Replace it with two assertions anchored outside the object under test:

```
const measuredMaxMs = Math.max(...fixture.corrections.map(c => Math.abs(c.adjust_us))) / 1000
assert.ok(body.scoring.clockFloorMs >= measuredMaxMs)   // floor covers the evidence
assert.ok(bucketMs >= body.scoring.clockFloorMs)        // display no finer than the floor
```

The first is the one that was missing. The second only means something once the
first exists.

Add a negative control in the same file — a fixture whose max exceeds the
published floor must fail the first assertion. A guard that has never been
observed to fail is not known to be a guard; that is exactly how the conventions
test shipped.

---

## 3. The fixture, and the alarm

**Commit the ledger extract. Endorsed as recommended.** `bench-logs/gametest-*.log`
is gitignored, so the entire evidentiary basis for a fleet-visible scoring
constant currently exists on one laptop. That is the same shape as the
un-uploaded reject clips and the MM3 screenshots: evidence that cannot be
re-examined by anyone who wasn't there.

Extract the eight `NTP,sync` lines to `test/fixtures/ntp-corrections-2026-08.json`
with a header block recording board id, session timestamp, poll cadence,
`poll_max_ms`, and firmware build. Provenance is part of the fixture — a bare
list of eight integers is not re-derivable and cannot be compared against a
future run. Reference the fixture from the doc; do not restate the numbers in
prose where they can drift.

**Then add the runtime alarm.** When a live correction exceeds `clockFloorMs`,
say so in the heartbeat, in the register of `*** RECEIVER SILENT ***` — the
condition announces itself rather than waiting to be queried. Deliberately *not*
a test failure: the test asserts the build is consistent with the evidence we
hold (deterministic, never goes red on its own), while the alarm reports that
the world has outrun that evidence (observational). Conflating them gives you a
red build caused by weather.

Re-rule trigger, stated now so it isn't argued later: **three alarms, or any
single correction above 250 ms.** At that point extend the fixture and re-open
this ruling.

While you're there: the 0855 log has printed `uncertainty_ms,198` in every
heartbeat for 26 hours against a doc asserting 75.7. A log and a doc disagreed
continuously, in the open, and neither complained. Log that in the ledger too.

---

## 4. What A.3 actually needs, stated sharply

Your correction is right and worth making sharper still: **the corrected
instrument never collected a B or C run.** Arms B and C ran once, on the broken
build (`REG,arm,B_sleep_armed…honoured,1`, session 2026-08-05 0734). That run is
not evidence about the corrected build — it is evidence about a build we
deliberately replaced.

So A.3 has two independent defects, and this ruling closes only the first:

1. **The clock premise was wrong** — closed here.
2. **The arm comparison was never re-measured** — still open. The 26.8 h
   corrected session ran arm A only (`B runs 0`, `C runs 0`) because cycling the
   arm needs a tap above y=50 and no sample landed there.

Do not close A.3 on (1) alone. And do not wait for a random tap: 26.8 hours of
organic use produced zero, so the expected wait is unbounded. Add a bench-only
forced-arm path (a build-flag command, not a UI affordance) so B and C can be
driven deterministically. Note in the report that the runs were forced — a
forced arm is fine for measuring the arm, and would not be fine for measuring
how often users reach it.

---

## 5. Timing

Land it now, before enrollment. Every displayed deviation changes, which is
exactly why this cannot wait: `GAME_ENROLLMENT_OPEN=false` means there are no
scores in flight and nothing to invalidate. The same change after enrollment
re-buckets live sorties under people who already read their numbers.

Stamp existing bench sortie records with the bucket in force when they were
taken, or discard them. Do not mix 0.1 s and 0.2 s records in one table.

Update the doc in place with a struck-through *superseded* note and the pointer
to the fixture — the house pattern, same as the receiver-limited antenna verdict.
Do not delete 75.7. A wrong number that someone acted on is part of the record.
