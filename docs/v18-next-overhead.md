# "Next overhead" prediction (v18 or later)

**Status: spec, not built.** Line refs at `74866aa`. "AM" = `src/AircraftManager.cpp`.

## Customer view

**"Next overhead: <callsign> in N min"**, shown when some aircraft is predicted to pass within the
overhead distance in under 10 minutes. Where it is shown (Stats line or Radar) is decided **after**
the accuracy is measured.

## Math

**Closest point of approach, straight-line, in a local flat frame** around the home position.
That frame is fine at <= 10 min x <= 600 kt (~100 nm). The same flat-earth convention as
`IsOverhead` (AM:10086-10094) and `PredictPosition` (`TrackedAircraft.h:304-332`).

- Position `p` (east, north, km) from home; velocity `v` from `trueTrack` and `velocity`.
- `t* = -(p·v)/|v|²`; `d* = |p + v·t*|`.
- **Predict only when:**
  - `0 < t* <= 600 s`;
  - `d* <= overheadKm` (`lookup-dist`, default 3 km; the same threshold as "overhead", so the
    prediction and the alert agree);
  - and the inputs are trustworthy (below).
- **The soonest qualifying `t*` wins.** Ties go to the smaller `d*`.

## Honesty rules

- **Turns.** Track change is read from the trail, which already exists: 60 reported fixes, max age
  90 s (`TrackedAircraft.h:156-164`). If the heading across the trail changes more than X deg in
  the last 60 s, there is **no prediction** for that aircraft; X is set from the measurement.
  There is no turn-rate model; a turning aircraft is simply not predicted.
- **Descents and climbs.** "Overhead" is horizontal only, so a descent does not change `d*`.
  - An aircraft descending into an airport inside the circle will usually **land** before its
    `t*`, which is a false prediction.
  - Rule: if `|verticalRate| > 1,500 fpm` **and** altitude < 5,000 ft, no prediction. The
    thresholds are set from the measurement.
- **Bad inputs.** The cloud maps a missing track to **0**, which is identical to due north
  (`CloudFeed.cpp:146`), and a missing speed to 0. A contact with track 0 **and** no trail
  heading evidence is not predicted. Speed < 40 kt is not predicted (the same floor as
  `FollowArc::MinutesToArrival`).
- **No seconds.** Whole minutes only:
  - `"in N min"` for N >= 1, rounded **up**, so it never promises sooner than the math;
  - `"in under a minute"` below that.
  - No countdown that ticks per second. Precision the data cannot support is not shown (CLAUDE.md:
    *when you decline to state a number, assert it cannot come back*). A test asserts the string
    has no seconds field.
- **It disappears when invalidated:** a turn, the contact lost, or `t*` passing without the pass
  happening.

## Accuracy, measured before deciding where it goes

- **Uses spec 3's bench traffic capture**, which stays on the workstation (coordinates rule).
- For every sample where a prediction would be shown, record the predicted `(t*, d*)`, then find
  the **actual** closest approach in the subsequent recorded track.
- **Report:**
  - error in time (actual − predicted) and in distance, **by horizon** (0-2, 2-5, 5-10 min);
  - the **false-prediction rate**: predicted within the circle, actual closest approach outside it;
  - the **miss rate**: an actual pass inside the circle that was never predicted >= 1 min ahead.
- **Placement decision:**
  - **Radar**, if the 2-5 min horizon is within ±1 min for >= 80% and false predictions are <= 10%.
  - **Stats**, if it is worse but useful.
  - **Not shipped**, if false predictions exceed 25%. A prediction wrong one time in four teaches
    people to ignore it.

## Telemetry

None proposed. If it ships on Radar, the overhead card's counter already measures the event it
predicts.

## Predictions (drafted after the capture)

- Time error within ±1 min at the 2-5 min horizon for >= 80%, measured on the replay, then
  spot-checked live.

**Sabotage:**
- Remove the turn rule: the false-prediction rate rises on the replay.
- Show seconds: the no-seconds test fails.

## Card

None until the placement is decided. A Radar line explains itself.
