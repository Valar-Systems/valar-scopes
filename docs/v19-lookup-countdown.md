# "Look up" countdown (v19)

**Status: predictions only. No code yet.** Branch `feat/v19-lookup-countdown`, off main `217d7b5`. Target release v19. It is not merged until v18 has been cut, and only on the reviewer's "approved to merge".

**Board:** 6E:64 (USB serial `907069326E64`, currently COM6). Daniel said on 2026-10-20 to use COM6 for any flashing; this replaces the brief's "COM16 only". E2:08 stays free for Daniel's v18 bench session. Before the first flash, 6E:64's firmware image, settings and logbook export are saved to files, and the board goes back on that image at the end.

## Rules this implements (from the brief)

- **Qualifies:**
  - an airborne contact with a valid track and a ground speed of at least 50 kt;
  - its closest approach to home is within `lookup-dist` and within the next 3 minutes;
  - closest approach is predicted as a straight line from the dead-reckoned position (`GetDisplayPosition`), ground speed and track;
  - the geometry is flat-earth with cos(lat), like `IsOverhead`: **111.0 km per degree, longitude scaled by cos(home latitude)**. The same constants keep the countdown's "within lookup-dist" in step with `IsOverhead` at the hand-off;
  - the prediction must hold for 3 consecutive updates before a countdown appears.
- **One target at a time:** the soonest. Switch only to a contact that passes at least 15 s sooner.
- **Status line:**
  - `LOOK UP IN m:ss`, then `TYPE · DIR · N FISTS UP`;
  - time in 5 s steps from 3:00 down to 1:00, then whole seconds;
  - type is the card's short name, else the ICAO type code, else omitted;
  - direction is an 8-point compass bearing from home to the closest-approach point, true north, whatever radar-up is set to;
  - a ring on the target and a tick on the outer ring at that bearing.
- **Hand-off:** at closest approach, or when `IsOverhead()` turns true, the line reads `LOOK UP NOW · DIR · N FISTS UP`. The existing Look up! alert is unchanged. The line clears once the contact is moving away.
- **Fists:**
  - fists = elevation at closest approach / 10°, rounded; below 5° reads `LOW`, and 80° or more reads `STRAIGHT UP`;
  - height = (baro altitude + vertical rate × time, never below 0) − home elevation;
  - home elevation = the field elevation of the nearest airport within 25 km, from `/airports` (Part B);
  - without a home elevation: fists only at or above 15,000 ft MSL, and direction only below that.
- **Setting:** "Count down before it passes", under Look up! on the config page. It works only when Look up! is on, and defaults to on.

## Gaps in the spec: provisional rules, for a ruling

The website will copy the device, so each of these has to be one exact rule.

| # | gap | provisional rule (used in the tests below) |
|---|---|---|
| G1 | direction when the closest approach is directly overhead (distance ≈ 0, so no bearing exists) | **`STRAIGHT UP` omits the direction**: `787 · STRAIGHT UP`. The tick and the ring still show |
| G2 | which way the 5 s steps and whole seconds round | **up (ceiling)**, like a normal countdown: 104 s reads `1:45`, 59.4 s reads `1:00`, 58.9 s reads `0:59` |
| G3 | how fists round at an exact half (45° = 4.5) | **half up**: 45° → 5 fists. Pinned because JavaScript and C++ round halves differently from Python (CLAUDE.md) |
| G4 | compass sector edges | half-open, lower edge included: N is [337.5°, 22.5°), NE is [22.5°, 67.5°), and so on |
| G5 | without a home elevation and at or above 15,000 ft | height = altitude MSL (home elevation taken as 0). Error at a 5,434 ft field, 36,000 ft and 3 km: 74.7° against 72.2°, the same 7 fists |
| G6 | what "3 consecutive updates" counts | feed updates (one per cloud blips response), not draw frames. At the cloud cadence that is tens of seconds, which **delays a countdown that first qualifies late**. This feeds A2's miss rate |

## A1: the math. Predictions, frozen before any code

**Reference ("known answers"), independent of the device's formula:**
- exact spherical geometry, R = 6371 km;
- each case picks a closest-approach (CPA) point at a chosen bearing and distance from home, and a chosen time t\*;
- the aircraft's position at t = 0 is that point moved **back along the great circle** by ground speed × t\*. Its course is perpendicular to the line to home at the CPA point, which on a sphere makes that point the true closest approach;
- the reported track is the great-circle course at the t = 0 position;
- elevation = atan2(height, surface distance at CPA).

So the test measures how far the device's flat-earth prediction is from the truth, not the formula against itself.

**Why a great circle and not a flat straight line:** an aircraft holding a track for 3 minutes flies a great circle. A reference built in the device's own flat plane would only test the code against itself. This is stricter than the brief's "straight-line tracks", and it is why C10 below is predicted to miss.

**Home sites:** public airports, each checked by computation to be ≥ 150 km from every coordinate in our docs and tests (`forbidden.py`, which prints only verdicts). Sydney was refused by that check, and Santiago is used instead. `lookup-dist` is 5.0 km in every case.

| case | home | CPA bearing, distance | t\* | gs | course at CPA | altitude now, vertical rate | expected |
|---|---|---|---|---|---|---|---|
| C1 crossing overhead | DEN (5,434 ft) | –, 0 km | 120 s | 450 kt | 270 | 38,000 ft, 0 | `2:00`, `STRAIGHT UP` (no direction, G1) |
| C2 offset | PHX (1,135 ft) | 045, 2.0 km | 90 s | 300 | 135 | 11,135, 0 | 56.7° → **6** fists, **NE** |
| C3 offset, high | DEN | 270, 3.0 km | 150 s | 480 | 000 | 37,000, 0 | 72.7° → **7**, **W** |
| C4 receding | PHX | 090, 2.0 km | −60 s | 300 | 000 | 10,000, 0 | **no countdown** |
| C5 slow, out | PHX | 000, 1.0 km | 100 s | 45 | 090 | 3,000, 0 | **no countdown** (< 50 kt) |
| C6 slow, in | PHX | 180, 1.5 km | 170 s | 55 | 090 | 2,135, 0 | `2:50`, 11.5° → **1**, **S** |
| C7 climbing | PHX | 135, 2.5 km | 120 s | 250 | 045 | 5,000, +2,000 fpm | at CPA 9,000 ft: 43.8° → **4**, **SE** |
| C8 descending | DEN | 315, 2.0 km | 120 s | 180 | 045 | 12,000, −1,500 fpm | at CPA 9,000 ft: 28.5° → **3**, **NW** |
| C8b descending, clamped | PHX | 315, 2.0 km | 90 s | 180 | 045 | 2,000, −2,000 fpm | altitude clamps to 0; height below home → **LOW** |
| C9 high latitude | FAI (439 ft) | 090, 3.0 km | 150 s | 450 | 000 | 30,439, 0 | 71.8° → **7**, **E** |
| C10 very high latitude | LYR (88 ft) | 200, 4.0 km | 120 s | 400 | 290 | 20,088, 0 | 56.7° → **6**, **S** |
| C11 southern hemisphere | SCL (1,555 ft) | 225, 2.5 km | 100 s | 350 | 315 | 11,555, 0 | 50.6° → **5**, **SW** |
| C12 equator (sabotage control) | SIN (22 ft) | 000, 2.0 km | 120 s | 400 | 090 | 10,022, 0 | 56.7° → **6**, **N** |
| C13 too late | PHX | 000, 2.0 km | 200 s | 400 | 090 | 10,000, 0 | **no countdown** (> 3 min) |
| C14 too far | PHX | 000, 5.5 km | 100 s | 400 | 090 | 10,000, 0 | **no countdown** (> lookup-dist) |
| C15 no home elevation, low | PHX, elevation unknown | 090, 2.0 km | 100 s | 300 | 000 | 12,000, 0 | **E**, no fists (< 15,000 ft) |
| C16 no home elevation, high | DEN, elevation unknown | 090, 3.0 km | 100 s | 450 | 000 | 36,000, 0 | **7** fists (G5), no direction |

**Bars** (brief): time within 0.5 s, bearing within 1°, elevation within 0.5°, on every timed case. The discrete outputs (time string, compass point, fists or LOW / STRAIGHT UP, countdown or none) must equal the expected column exactly.

**Predicted:**
- **Every bar met on every case except C10.**
  - Max time error ≈ 0.3 s. IsOverhead's 111.0 km per degree is 0.18 % short of the sphere's 111.19, and 0.18 % of 170 s is 0.31 s. This is a known bias, kept on purpose so the countdown and IsOverhead agree at the hand-off.
  - Max bearing error ≤ 0.3°, max elevation error ≤ 0.2°.
  - Every discrete output exact on the other 16 cases.
- **C10 (78° N, a 25 km path heading mostly west) is predicted to miss at least one bar:** time 0.5-1.5 s, bearing 1-4°, elevation 0.5-2.5°.
  - Over 25 km at that latitude, the flat-earth straight line and the true great circle drift about 0.2 km apart (s² · tan(lat) / 2R). The one cos(home lat) scale is also off by ~1 % at the aircraft's own latitude.
  - Below ~65° N, or on paths that run north-south, the same terms stay inside the bars. C9 is a north-south path at 65° N.
  - **As the bar is written, a C10 miss makes A1 a FAIL.** It needs a ruling: accept a stated high-latitude limit, drop C10 as outside the market, or use great-circle math.

**Sabotage: drop the cos(lat) scaling.** Predicted:
- **outside a bar on 11 cases: C1, C2, C3, C6, C7, C8, C8b, C9, C10, C11, C16**:
  - by time where the path runs east-west (C1, C6; and C2, C7, C8, C8b, C11 through their east-west part);
  - by elevation where the pass is due east or west and the path runs north-south (C3, C16), because only the distance changes;
  - by the output where the distance crosses `lookup-dist` (C9 becomes 7.05 km, so no countdown);
- **still passing on 2 cases:**
  - **C12**, at the equator (cos 1.36° = 0.9997);
  - **C15**, where the path runs north-south and the pass is due east. Only the distance changes, and C15 shows no fists;
- C12 and C15 are the controls: they show the sabotage is the cause, not something else.

The no-countdown cases (C4, C5, C13, C14) are not graded under the sabotage.

**Also host-tested under A1, with exact expected strings:**
- time formatting: 180, 177, 104, 61, 60, 59.4, 58.9, 30, 1 and 0.2 s, read as `3:00`, `3:00`, `1:45`, `1:05`, `1:00`, `1:00`, `0:59`, `0:30`, `0:01` and `0:01` (G2);
- compass edges: 22.4° → N, 22.5° → NE, 337.5° → N (G4);
- fists: 4.9° → LOW, 5.0° → 1, 45.0° → 5, 79.9° → 8, 80.0° → STRAIGHT UP (G3).

Predicted 100 % exact.

**Outside the set:**
- a case other than C10 off its bar with cos(lat) in place;
- **C10 inside every bar** (my error model is wrong);
- a discrete output on the other 16 cases that differs from its expected column;
- under the sabotage, C12 or C15 failing, or any of the 11 staying in bar.

In any of these: stop and report.

## A2-A6

Predictions for A2-A5 are frozen in this file before each runs: the two recorded sites (≥ 2 h each, through 6E:64's normal cloud path), the replay, stability, screen and off-means-off. A6 is `docs/countdown-spec.md`, written last, from the rules as ruled.
