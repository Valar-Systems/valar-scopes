# The fetch path stops with the loop healthy — second sighting

**Open.** First seen 2026-07-09 (*"fetches silent 22 min, loop healthy, task never
dequeued"*), closed 2026-07-21 as **not reproduced, never root-caused**, with telemetry
deliberately left in. Second sighting **2026-08-17** on board `.55`, caught by that
telemetry. See [ROADMAP.md](ROADMAP.md) `s3-128-overnight-slowdown`.

## The observation

```
10:48:51  [health] ... tlsOk=1 rej=0  tls=2/115  interval=5000ms
10:49:21  [health] ... tlsOk=1 rej=0  tls=2/115  interval=5000ms  DATA STALE
```

`tls=H/R` is **identical across 30 s**: no handshakes, no reuses, **no HTTP of any kind**,
on a 5-second poll interval. Meanwhile the loop is fine — frame times normal, touch
watchdog clean, no reboot.

**It is not heap.** `tlsOk=1` says a handshake-sized block was available at that instant
and `rej=0` says the gate had never once refused. This was written up as heap twice before
the code was checked; `heaphealth::CanHandshake()` gates only enrichment
([:4077](src/AircraftManager.cpp#L4077), [:5361](src/AircraftManager.cpp#L5361)) and
**never the position fetch**, so heap pressure cannot produce this.

**It is not the key.** No `HTTP 401` lines, and nothing had been rotated.

## Why it outranks the heap fragmentation found the same afternoon

The fragmentation is **bounded**: gates are enrichment-only, so the radar keeps drawing,
cards degrade, and a smaller radius mitigates it. This has no such bound — the radar
itself stops, and the device shows the full stale ladder with nothing wrong that any
existing check reports.

## What the next occurrence must distinguish

Three different bugs produce the same silence. The telemetry separates them; this table
exists so it is *read* rather than reasoned out live.

`[soak-state] inFlight=.. inDetail=.. screen=.. reqQ=.. resQ=.. fetchAge=..s touchIdle=..s enrich=.. task=..`
with `task`: **0**=Running **1**=Ready **2**=Blocked **3**=Suspended **4**=Deleted, plus
the bracket `[fetch] task: req kind=N @T` (**dequeued**) and `[fetch] task: done ok=..`
(**completed**).

| # | Bug | `reqQ` | `inFlight` | `task: req` | `task: done` | `task=` |
|---|---|---|---|---|---|---|
| **A** | **Never enqueued** — the loop never asked | 0 | **0** | none new | n/a | 2 (idle on empty queue) |
| **A′** | **`inFlight` stuck true** — loop believes a fetch is outstanding, so it never posts another | 0 | **1** | none new | none pending | 2 |
| **B** | **Enqueued, never dequeued** — task not servicing the queue | **≥1, persisting** | 1 | none new | n/a | **2 or 3 with work queued** |
| **C** | **Dequeued, never completed** — stuck inside the request | 0 | 1 | **present, unmatched** | **missing** | 2 (blocked in socket) |

Reading rules:

- **`fetchAge` climbing past ~3× the poll interval is the trigger** to start reading the
  rest. It is the only field that says "this is a stall" rather than "this is a quiet moment".
- **A vs A′ is the whole loop-side question** and hinges on one field. A means the
  scheduler decided not to ask (look at `inDetail`, `enrich`, the interval logic). A′ means
  a result was lost and the flag never cleared — which is self-sustaining, because
  `inFlight` is what suppresses the next request. A′ is the one that matches 2026-07-09's
  *"task never dequeued"* wording without requiring the task to be broken at all.
- **C is identified by an unmatched bracket**, which is exactly why the pair was added:
  `req` without `done` localises the hang inside the HTTP call, implicating a missing or
  ineffective socket timeout rather than anything in the queue machinery.
- **B is the only one that indicts the task itself.** Queued work with the task Blocked or
  Suspended is the case where `eTaskGetState` earns its place in the line.

## Running it

`.55` on `-DFETCH_TRACE` — the `SOAK_TEST` instrumentation **without** `SoakHarness`,
which synthesises taps ([:3706](src/AircraftManager.cpp#L3706)) and would inject the exact
variable that was first, wrongly, blamed for the same afternoon's heap curve. Verify the
image, not the ini: `bash scripts/check-fetchtrace.sh` (production backend present,
staging absent, traces compiled in, harness absent).

- `.55` sits **genuinely idle**. No taps, no config-page loads.
- **`.32` stays pinned and untouched** as the negative control, on the shipping image.
- Any cross-over test (pushing `.32` to `.55`'s 100 km / 40-aircraft sky) comes **last**,
  because it consumes the control.

## What is NOT yet known

- Whether the stall is self-limiting. The 2026-08-17 instance recovered without
  intervention; 2026-07-09's ran 22 minutes. Neither was observed to wedge permanently.
- Whether it correlates with enrichment volume. `.55` does ~4× `.32`'s enrichment and is
  the board that stalls — suggestive, **not** tested. The cross-over is the test that makes
  sky size the variable rather than board identity.
- Whether the heap fragmentation and the stall are the same defect. They appeared on the
  same board on the same afternoon, which is exactly the coincidence that invites merging
  two bugs into one wrong story. Treated as separate until something links them —
  see [heap-fragmentation-2026-08-17.md](heap-fragmentation-2026-08-17.md).

## Run log

**2026-08-17, 12 minutes idle on `-DFETCH_TRACE`, production backend — NO STALL.**

A clean negative, recorded because the baseline is what the next occurrence gets compared
against. ~140 request/response pairs, **every `req` matched by a `done`**, all
`ok=1 http=200`, 66–1075 ms. Across every `[soak-state]` line: `reqQ=0 resQ=0 inFlight=0
task=2`, and `fetchAge` never exceeded 5 s. None of A / A′ / B / C fired.

One 15 s gap at 11:58:44 is **not** a stall: `touchIdle` crosses 600 s there and the poll
interval changes 5 s → 15 s exactly as `idleAfter=600s` specifies.

So the instrumentation is proven to emit what it should, and the fetch pipeline is proven
healthy when it is not stalling. 12 minutes is a thin window against something that
historically ran 22 minutes and then did not recur for six weeks; the board stays on this
image.
