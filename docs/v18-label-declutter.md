# Label declutter at wide zoom (v18 or later)

**Status: spec, not built.** Issue #310 is the declutter issue the overlap counter was built
for. Line refs at `74866aa`. "AM" = `src/AircraftManager.cpp`.

## The problem, measured

Every airborne contact that is not zoom-culled gets a full label: up to 3 lines from `LabelLayout`
(AM:4930-4983). Placement is fixed at `(x+5, y+5+9k)`. Nothing moves or suppresses a label for
density.

**Baseline, from the shipping image's own counter.** That is `overlapcount::Frame`, reported in
`[perf]` as `labelOverlaps` / `labelOverlapsMax` / `labelOverlapPx`, with badges included since
v12. It comes from 773 minutes of COM18's overnight soak, 2026-10-07/08, at `view=100mi`:

| contacts on screen | minutes | overlapping label pairs per frame (mean) | max pairs | overlapped px per frame (mean) |
|---|---|---|---|---|
| 0-9 | 430 | 0.16 | 4 | 13 |
| 10-24 | 166 | 3.75 | 28 | 291 |
| **25-40** | 177 | **22.0** | **51** | **1,544** |

At a busy hour, the default view has ~22 label collisions in every frame. That is the photo
everyone has seen.

## Options

**A. Nearest-N full labels, dots for the rest.**
- Contacts are ranked by distance: `SortedAircraftByDistance`, AM:4606-4622.
- The N nearest keep their labels; the rest draw the blip only.
- Every aircraft is still **shown at its true position**; only text is withheld.
- N is chosen from the measurement below, not guessed.

**B. Fold overlapping aircraft into one marker with a count.**
- Contacts whose labels would intersect are merged into one marker, `"3"`, at their centroid.
- This removes collisions completely, but **moves information**: a folded marker sits where no
  aircraft is. A count does not say which way each aircraft is heading.

**Always labelled, under either option:** emergency, military, pinned and followed aircraft
(`isEmergencySquawk` AM:491; `IsMilitary` `SpecialAircraft.cpp:83`; `pinnedIcao`; `MatchesFollow`
AM:5619). Plus watchlist matches: the customer named them, so hiding their names would undo the
watchlist.
- They count toward N for A, so a busy military day does not unlabel everything else.
- They are never folded under B.

## Measurement before choosing

- **The on-device counter already exists**, so no new instrument is needed for the *live* number.
- **Comparing A and B fairly needs the same traffic twice.** The repo holds no recorded traffic:
  - `scripts/bench-photo-feed.py` is a static synthetic sky;
  - `bench-logs` hold only aggregate lines.
- So, first: **a bench-only traffic capture.** A `[traffic]` serial line per poll, under the
  bench-hook pattern so it cannot ship, carrying icao, lat, lon, altitude, ground speed, track and
  t. Plus **a host replay harness** that runs the real `LabelLayout` and `OverlapCount` code over
  it.
- **The captures stay on the workstation** (bench-logs is gitignored). They put aircraft around a
  home, so they never leave it; only aggregate numbers are reported. This is the standing
  coordinates rule.
- The same capture serves spec 5 (next overhead).
- **Report, per option and per zoom step** (100 / 50 / 25 mi):
  - mean and max overlapping pairs and overlapped px;
  - **labels shown**, so "zero overlaps because nothing is labelled" cannot win;
  - for B, the mean centroid displacement in px.

**Provisional lean: A.** It never misplaces an aircraft, and it is one ranked cut rather than a
geometry pass. The recommendation is made on the replay numbers, and that is a decision, not this
lean.

## Captures are workstation-only (decided at review)

Bench traffic captures locate a home, so they are **never committed**. `.gitignore` covers
`*.traffic.jsonl`, `*.traffic.log`, `*.traffic.csv` and `traffic-captures/`.
`scripts/check_no_traffic_captures.py` runs in CI (`.github/workflows/no-traffic-captures.yml`,
no path filter) and refuses those names **and** the capture's line format
(`[traffic] t=<epoch> hex=<6 hex> lat=<deg> lon=<deg> ...`) anywhere in the tree. Its selftest
plants a capture first. **The capture tool must emit exactly that line format**, so the guard
matches what the tool writes.

## Telemetry

None. Display behaviour; nothing for a customer to "use".

## Predictions (drafted when the replay exists)

- Option A at N chosen from the replay: busy-hour mean overlapping pairs from 22 to <= 2 at
  `view=100mi`, measured live on COM18 with the same `[perf]` fields. Labels shown >= N.

**Sabotage:**
- Drop the always-label rule: a military contact outside the N nearest loses its label in the
  replay.
- Make N unbounded: the overlap count returns to baseline.

## Card

None. No gesture.
