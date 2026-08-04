# Missileer: how the game face coexists with the monitoring face

Firmware/repo review for [missileer-game-design.md](missileer-game-design.md) §13
build task 2. **A review, not a decision** — findings and recommendations, with
the calls that need making flagged as such.

Scope: the existing EAM monitor is a working product. The game has to land inside
it without breaking it, and the shapes are further apart than "add some screens."

---

## 1. The blocking conflict: auto-rotation will lose the sortie for you

The monitor is a **dwell-timed carousel** ([EamManager.cpp:20-21](../src/eam/EamManager.cpp#L20-L21)):

```cpp
constexpr unsigned long AUTO_DWELL_MS   = 8000;   // 8 s per screen
constexpr unsigned long INTERACT_HOLD_MS = 30000; // pause 30 s after a touch
```

The game is a **hard real-time commitment**: T is 5–15 minutes out (§6), the
commit cutoff is T−60 s, execution lands inside a published **2-second window**
(§3 step 4), and the score is `|key − T|` **in milliseconds** (§4).

Put those together and the current shell actively defeats the game:

> Commit at T−10 min. Touch nothing for 30 s. The carousel resumes and advances
> **every 8 seconds for the next nine and a half minutes**. At T you are
> somewhere in the rotation, not on the launch face.

**This is not tunable.** Raising `INTERACT_HOLD_MS` to cover a sortie would
freeze the monitor for ten minutes after any stray touch, breaking the thing the
product already does well. The two behaviours are genuinely incompatible and need
separating, not reconciling.

**Recommendation: a committed sortie is a MODE, not a screen.** From ack to
resolution, auto-rotation is suspended outright — not delayed. `AutoRotate()`
([EamManager.cpp:230](../src/eam/EamManager.cpp#L230)) returns early while a
sortie is live.

## 2. Model it on the radar's detail card, not on a new screen

Three options considered:

| Option | Verdict |
|---|---|
| New entries in the `Screen` enum | **No.** They join the rotation, which is exactly the problem above, and `HasData()` gating has no sensible answer for "a sortie is running." |
| A separate app mode (like a product flag) | **No.** The monitor must keep running *underneath* — §4's preemption rule requires real traffic to interrupt the game, which means the feed is still live and still rendering. |
| **A modal overlay above whatever screen is current** | **Yes.** |

The overlay pattern already exists and is proven in this repo: the radar's detail
card sets `inDetail`, is drawn over any screen, and `IsRadarView()` is
`screen == Radar && !inDetail` so navigation and other behaviours can ask "is the
card up?" The game's launch face is the same shape — and the same accessor
discipline applies, which is why `DetailCardOpen()` was recently split out from
`IsRadarView()` rather than callers conflating the two.

The `Screen` enum stays exactly as it is. Nothing is added to the rotation.

## 3. Two interrupt sources with opposite priority — precedence must be stated

§4: *"new real traffic **preempts** a running sequence — real-world traffic takes
precedence. Losing your window because the world got busy is the game at its
best."*

§1 (this doc): a live sortie must suspend the carousel.

So the game suspends *automatic* navigation but must **not** suspend *event-driven*
interruption. Those are different mechanisms and the firmware needs an explicit
precedence order, or it will be decided accidentally by whichever check runs
first in `Update()`.

**Proposed order, highest first** — needs a design call, flagged as such:

1. A **new real EAM** — preempts everything, including a live sortie (§4).
2. A **live sortie** — suspends auto-rotation, auto-dim, and the ambient cycle.
3. **Manual swipe** — allowed, but must not dismiss a live sortie by accident.
4. **Auto-rotation** — lowest, and off entirely while (2) holds.

Item 3 is the sharp one: today a swipe navigates freely. During a sortie it needs
either to be blocked or to be a deliberate "leave the launch face" gesture with a
way back. **This is the one place I would not guess.**

## 4. Auto-dim will dim the screen mid-commitment

`MaybeAdjustBrightness` ([EamManager.cpp:270](../src/eam/EamManager.cpp#L270))
drops brightness at night when a location is set. Nothing knows about sorties, so
the screen can dim in the seconds before T.

Same class of problem as rotation, same fix: suspend while a sortie is live. It
is a smaller bug but a worse experience — the display going dark at T−5 s reads
as the device failing at the decisive moment.

Note the radar already has precedent for a brightness override tied to an event:
a visual alert takes over brightness and releases it on dismissal
(`lastBrightnessCheck = 0` in `DismissVisualAlert`).

## 5. Millisecond scoring has TWO uncertainty floors, not one

§13 build task 1 measures **NTP sync uncertainty** and sets scoring granularity
from it. That is necessary and not sufficient. There are two independent floors:

| Floor | Source | Measured by |
|---|---|---|
| **Clock** — how well the device knows what time it is | NTP sync + drift | `gametest-s3-128`, NTP screen |
| **Input** — how quickly the device notices the finger | touch poll cadence + frame time | `gametest-s3-128`, `poll_max_ms` |

The honest granularity is the **larger** of the two, and the input floor is not
small: the harness measured `poll_avg_ms=7, poll_max_ms=45` in a loop doing
nothing else. Under a rendering launch face it will be worse.

**Recommendation:** publish the deviation score at a granularity no finer than
`max(clock, input)` measured together, and state the figure in the UI. A
leaderboard quoting single milliseconds the fleet cannot resolve is worse than a
coarser one that is honest — and this is a competitive ranking, so somebody will
eventually try to beat it by a millisecond.

## 6. The touch path is the crew layer's actual gate

Two hardware findings already recorded in
[gametest-results-template.md](gametest-results-template.md) bear directly here:

- **The driver and the chip disagree.** Observed on the first bench run:
  `TOUCH,20578,0,1,0,0` — driver says no touch, chip says one point. `IrqCtl` is
  `EnTouch|EnChange`, so a finger that does not *move* may generate no interrupt.
  The deputy's hold is by definition a finger that does not move.
- **The commander's key-turn is a press-drag-hold arc** (§11 bezel) landing
  inside a 2-second window. A dropout at the wrong instant is a failed sortie the
  player did nothing to deserve.

If the hold test fails, **the crew layer does not exist as designed** and §8's
two-person rule needs redesigning around discrete taps rather than sustained
contact. That is why task 1 gates task 2 rather than running beside it.

## 7. What can be reused as-is

Genuinely reusable, no changes needed:

- **`SevenSegment.cpp`** — §11 wants the TODC as red 7-seg Zulu with the
  countdown to T "in the same face." That renderer already exists for the EAM
  clock screen.
- **`EamLogbook`** (NVS `eam-log`) — already tracks seen EAMs and is already
  **edge-seeded at boot** so the backlog never fires ntfy alerts. That is exactly
  the mechanism §4's "backlog seeding: solved by construction" needs, and it is
  already written. The game must reuse it so a boot-time history pull cannot
  offer sorties for EAMs whose T passed days ago (§13's "<2 min to T =
  non-scorable" rule is enforced at the same point).
- **Toast pattern** — the radar's claim/rank toasts are bounded-dwell overlays
  drawn over any screen. The game's transient confirmations are the same shape.
- **The build gating** — `FEATURE_EAM` already drops all radar TUs and
  `[filters]` already excludes siblings. The game lives in `src/eam/` and needs
  no new filter work.

## 8. Open calls for the design, not for the firmware

1. **Swipe during a live sortie** — blocked, or a deliberate leave-and-return?
   (§3 above.)
2. **Does a preempting real EAM void the committed sortie, or queue behind it?**
   §4 says real traffic takes precedence and calls losing the window "the game at
   its best" — but §10's tiered stand-down penalises a missed execution. Being
   penalised for an interruption the game itself caused needs an explicit answer.
3. **Scoring granularity** — pending the measured `max(clock, input)`.
4. **What the monitor shows underneath.** The overlay implies the carousel is
   still running behind it. Cheaper to freeze it; more alive to keep it. Cosmetic
   until it is not — a screen change behind a translucent overlay is a distraction
   at T−5 s.

---

## Summary

Nothing here blocks starting. The load-bearing conclusions:

- **The launch face is an overlay + a mode, never a rotation screen.** The
  carousel is the single biggest incompatibility, and 8 s dwell against a 10 min
  sortie is not a tuning problem.
- **Suspend auto-rotation and auto-dim for the sortie's lifetime**, the same way
  the radar's visual alert takes over brightness.
- **State the interrupt precedence explicitly** rather than letting `Update()`
  ordering decide it.
- **Scoring granularity is bounded by input latency as well as NTP** — measure
  both before publishing a millisecond leaderboard.
- **Task 1 genuinely gates this**: if a 10 s hold is not reliable, §8 needs
  redesigning, not tuning.
