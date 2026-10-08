# "TODAY" on the Stats screen (v18 or later)

**Status: spec, not built.** Depends on the time-zone fix (#379): the day boundary must be local
midnight *with* DST. Line refs at `74866aa`.

## Customer view

The Stats screen's TODAY block shows, for the local calendar day:
- **aircraft seen today** (unique);
- **busiest hour** (as today, with its count);
- **highest** (altitude + callsign);
- **fastest** (ground speed + callsign);
- **rarest type today** (the type seen today with the fewest lifetime sightings in the logbook).

It survives the nightly reboot, and resets at local midnight.

## What exists, and what is wrong with it today

`todayContacts`, `todayPeak`, `todayHourCounts[24]` and `statsDayLocal` (`AircraftManager.h:514-530`)
are **RAM-only by design** ("no flash wear; resets at local midnight and on reboot").

The header comment cites only the weekly reboot. Since v11, though, **every device reboots at
03:00 local every night**: quiet hour, #377; 14/14 nights on the always-on fleet. That causes
three defects, all already shipping:
1. Midnight-03:00 is **lost every day**. The block restarts at 03:00.
2. After the reboot, every contact still in range is **counted again as new**
   (`AircraftManager.cpp:3196-3207` counts on first merge of an icao).
3. A contact merged **before NTP sync** gets `localHour = -1` and is never counted.

There is no daily highest, fastest or rarest. The High/Fast/Near rows (`:3954-3960`) are a live
snapshot.

## Design

### State

One struct, persisted as **one NVS blob** in its own namespace `today`, which follows the
`logbook`/`touch-wd` separation:
- `localDay`;
- `hourCounts[24]` (u16);
- `highestFt` + callsign[8];
- `fastestKt` + callsign[8];
- `rarestType`[5] + its lifetime count;
- a **seen-set** of today's ICAO addresses: 3 bytes each, sorted, capped at 1,000 entries,
  ~3 KB. A busy day on COM18 saw 499 contacts.

The seen-set is what makes "unique" honest across a reboot. Without it, defect 2 survives
persistence.

### Write cadence and wear

Three triggers:
- **Hourly**, at the local hour boundary, if dirty.
- **Before every planned reboot**: the quiet-hour deferral, the preventive weekly, the touch-wedge
  rung, and OTA. Each of those paths calls one `today::FlushBeforeRestart()`. *The logbook has
  the same gap today: nothing calls `PersistNow` before `ESP.restart()`, so each planned reboot
  can lose up to 10 min of logbook. Fix both with the one hook.*
- **At local midnight**, the rollover write.

That is about **24-26 writes/day**. Two power-cut properties:
- A cut loses at most the current hour, and **undercounts, never overcounts**: the same rule as
  `usage::Store`, `include/UsageStore.h`.
- After a restart, the persisted seen-set prevents re-counting.

**Wear**, at a ~3 KB blob x 26 writes/day, about 78 KB/day of NVS entries:
- On a 20 KB NVS partition (4 usable 4 KB pages): ~5 erases per page per day, so 100k cycles is
  ~55 years.
- On the 84 KB partition (`partitions-s3-16mb-bignvs.csv`, ~20 usable pages): ~1 per day, so
  ~270 years.
- For scale: the logbook already rewrites ~4 KB every 10 min (~576 KB/day, per the partition
  file's note). This adds ~14% to that.

Measured on hardware before merging: NVS free entries before and after one simulated day.

### Rules

- **Highest/fastest** use the logbook's sanity bounds: alt <= 60,000 ft, speed <= 1,200 kt
  (`Logbook.cpp:782-802`). A garbage value is rejected, not clamped. Same rule as the "truncated
  row is a lie" entry in CLAUDE.md.
- **Rarest type** needs a type code, which cloud mode has. With the card-details source Off,
  the row is omitted, not shown as "unknown".
- **Day rollover** is at local midnight from the zone-aware clock (#379). A device whose clock is
  not synced shows nothing for TODAY rather than a day it cannot place.

## Tests (host, pure)

- **Rollover:** exactly at 00:00 local in DST and in standard time, plus the 23 h and 25 h
  transition days.
- **Persistence round-trip:** a blob written at 02:59 and read after a 03:00 reboot gives the same
  counts, with no double count.
- **Wear:** the write count for a simulated day with 1,000 contacts and 3 reboots is <= 30.
- **Bounds:** 70,000 ft is rejected, not stored as 60,000.

## Telemetry

**No new counter.** This feature shows data; it is not a feature a customer "uses". It fits the
existing eight integers unchanged.

## Predictions to freeze before code (draft)

- The block survives the 03:00 reboot: the counts after it are >= the counts before it, and the
  unique count does not jump.
- <= 30 NVS writes per day (counted from a log line per write).
- The counts reset within a minute of local midnight.

**Sabotage:** drop the seen-set, so the reboot must show a double count in the test; drop the
pre-restart flush, so the 02:59 -> 03:00 test must lose the last hour.

## Card

None. Stats already exists. No new gesture.
