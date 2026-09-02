# #245 — TLS buffers on the internal heap vs in PSRAM

**A matched two-board A/B, 152.9 h, ended 2026-08-31.** This is the evidence for
making `-DBLIPSCOPE_TLS_PSRAM` the shipping default and a launch item.

## What was run

| | control | treatment |
|---|---|---|
| board | COM119 | COM16 |
| env | `blipscope-s3-128` | `blipscope-s3-128-tlspsram` |
| difference | mbedTLS allocates from the internal heap | allocations ≥ 4,096 B go to PSRAM |

Both s3-128, both `fw=v8`, both `features=cloud` against production. **The env was
read off each log's `[build] env=` banner, not from a port→role table** — COM
numbers re-enumerate and that table goes stale silently.

## Elapsed, as measured

`2026-08-24T17:23Z → 2026-08-31T02:19Z` = **152.9 h** (control) / **152.7 h**
(treatment).

Stated from the capture rather than from when the boards were first powered.
Earlier soak logs exist (`com119-heap-soak-2026-08-21`,
`fresh-boot-acceptance-2026-08-23T*`), but the **matched** two-arm capture with
both envs running side by side begins on the 24th, and that is the window this
document is about.

Two independent clocks were computed and required to agree before the figure was
quoted: the device's own `[perf]` UTC stamps, and the capture's wall clock
reconstructed by counting midnight rollovers (the capture stamps `HH:MM:SS` with
no date). They agree to 0.3 h.

## Result

| | control | treatment |
|---|---|---|
| aborts / backtraces | **4** | **0** |
| reboots during run | 2, both `abort()` | 1, after a Wi-Fi `ASSOC_LEAVE` — recovery, not a crash |
| `allocFail` | **2,658** | **0** |
| `hardFail` | 3 | **0** |
| enrichment starvation | **312 lines, longest 45,932 s (12.8 h)** | **0** |
| **OTA checks completed** | **3 of 8** | **7 of 7** |
| largest block, median | 9,716 B | 14,324 B |
| largest block, min | **820 B** | 11,764 B |
| samples below one handshake (16,717 B) | **94.03 %** | 67.07 % |
| heap free, median | 35,252 B | 63,276 B |
| frame p95, median | 46.0 ms | 52.0 ms |

## The result that decides it is the OTA row

Not the heap figures. **The control arm completed 3 of 8 update checks.** The
check needs the same contiguous block the enrichment could not get, so a
fragmented unit loses the one remote path by which it could be repaired.

That reclassifies this from a performance issue to a launch blocker. The
alternative is shipping ~50 units carrying a failure mode that disables their own
update mechanism — a unit that reaches this state cannot be fixed by shipping a
fix.

## What this does NOT establish

**The +6 ms frame cost is confounded and must not be quoted as the cost of PSRAM
TLS.** The control arm was starved for 94 % of its samples, so it was doing less
work: it was refusing the enrichment the treatment arm was performing. **A sick
board doing less work is not a faster board.** This A/B cannot separate PSRAM
access latency from the healthy board actually doing its job, and the difference
between those two readings is the whole of #271.

Written out because this is exactly the kind of confound that gets quoted as fact
once it is in a table.

Also not established:

- **The enrichment axis.** #271 remains open and blocked. The frame-gate constants
  in `AircraftManager.cpp` were fitted on a heap-starved board with enrichment
  nearly idle; this run confirms that description was accurate but does not
  re-fit them. `env:blipscope-s3-128-tlsheap` exists so the comparison can be run
  in the other direction when a board is free.
- **Replication.** One board per arm, one hardware revision, one firmware version.

## Two artefacts of the measurement, recorded so they are not invisible

- **One health line of 17,317 on the treatment arm was truncated on the wire**
  (0.006 %). Excluded and counted, not repaired. The parser's internal anchor —
  every line carrying the health prefix must parse in full — is what surfaced it;
  without that it would have been a silently dropped sample.
- **Six multi-second frames on the treatment arm** (max 5,020 ms), each
  immediately after `[ota] channel=s3-128`: the daily update check blocking the
  loop task, once per day over 6.4 days. Pre-existing and unrelated to #245, and
  now more interesting than it was, because that check is also the repair path.
  Filed separately.

## Reproducing the read

Snapshots (`md5`): `ab-com119.log` `a6c045e90e622a340e55ae4442d26e07`,
`ab-com16.log` `91117ee08b02e7e6b261df914fba45a1`. Captures were **stopped
first**, then the files confirmed stable across two reads, then copied, then
parsed — a live-appending log cannot be anchored against.
