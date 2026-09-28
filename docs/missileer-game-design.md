# Missileer Gamification — Design Notes & Source Digest

Working notes from the design session of 2026-08-04. Companion to
[#135](https://github.com/Valar-Systems/valar-scopes/issues/135) (gamify EAM reception) and
[#136](https://github.com/Valar-Systems/valar-scopes/issues/136) (shredded-paper pack-in).
Status: brainstorm output, not a spec. Nothing here is committed.

---

## 1. Tone principles (settled)

1. **Real traffic is the engine; the player is the escalation.** The device reacts to a real
   EAM soberly — klaxon, message rendered verbatim, documentary. Everything game-shaped
   (ack, authenticate, enable, key-turn, animation, detonation) happens only because a human
   chose to run their drill on it. The device never auto-animates off real traffic.
   - Why: agency is the tone gate. If a human turned the key, the joke is on the human. If
     the device auto-plays a launch animation during a real-world crisis traffic spike, the
     joke is on the message — and someone screenshots it.
2. **Remote hold flag.** One cloud config bit reverts the whole fleet to pure monitoring.
   Ships in v1. Makes the tone call reversible.
3. **Deadpan bureaucracy is the comedic register.** The publicly depicted reality of the job
   is binders, checklists, printouts, padlocks, and a clock. Drama comes from procedure and
   the sweep toward T — not klaxons and red strobes. Post-detonation copy: "Your
   participation has been noted in your permanent record."
4. **The vague version is the better toy.** Take published skeletons, invent freely in the
   gaps, never chase fidelity. (See §10 — even the best public article gets corrected by
   veterans in its own comments. Fidelity is a treadmill; vagueness is both the legal
   posture and the design philosophy.)
5. **Exercise framing.** Synthetic/training traffic is marked the way the real world
   publicly marks it: EXERCISE EXERCISE EXERCISE. The edition carries a
   Nuclear-Companion-style disclaimer: *based on unclassified public sources; procedure
   depicted is a guess; deliberately so.*
6. **Real units' real failures are off-limits.** The wing articles document failed surety
   inspections and the 2014 proficiency-test cheating scandal. Public ≠ usable: the game
   takes real *institutions* (monthly proficiency testing, annual competition) and never
   references real incidents, named individuals, or a real unit's disciplinary history.
   Same logic as #136's box rule — a joke about a real organisation's worst day is a claim,
   not a toy.

## 2. Sourcing rule (standing, applies to everything)

Only publicly citable material: AF public affairs, NPS Minuteman NHS, declassified
material, prepublication-reviewed books, environmental-review documents, Wikipedia-level
secondary sources. "Nobody told me it was classified" is not a test — classification isn't
briefed fact-by-fact, material can be CUI, aggregation matters, NDAs outlast service.
**Veteran anecdotes (e.g. blog comment sections) are folklore, not source** — don't build
on them, don't chase their corrections. The tension comes from the timing window and the
second person, not procedural fidelity.

## 3. Core game loop

Skeleton = the six-step REACT launch procedure as published by Nuclear Companion (itself
explicitly a guess from unclassified sources — the right provenance posture).

| # | Real step (published outline) | Game port |
|---|---|---|
| 1 | Decode & authenticate EAM | Real EAM arrives → prints on screen (paper-strip animation) → device "auto-decodes" (REACT's published EWO auto-processing) → reveals message class (§5) and, for execution traffic, **T** (Zulu execution time). Player acks = commits the sortie. Authenticate visual: padlocked SAS safe, two crew locks, crack the seal. |
| 2 | Set preparatory launch procedures | Confirm/adjust war plan: target from FDM (default) or manual override (§6). Locks at T−X. |
| 3 | Cooperative enable | **Split-knowledge minigame**: system derives 6 characters; each crew member's device shows 3; each keys in their own 3 correctly within a time limit. Two-player, citable, already designed by Ford Aerospace. |
| 4 | Execute launch command (four-hand) | **Commander**: key-turn (press-drag arc on bezel, hold) at exactly T. **Deputy**: cooperative switches held through the window. All inputs within the **execution window** (§12a, open question T1). |
| 5 | Second LCC vote | Crew's execution registers a **launch vote**, visible fleet-wide. Another crew can: second it (ELC → launch proceeds), **Inhibit** it (playable verb!), or let the dead-man timer expire (launch proceeds alone). |
| 6 | Terminal countdown | **30 s**, not final (§12a, open question T2). Then flight (§7). |

## 4. Scoring (settles #135's passive-scoring objection)

- **Score the human, not the antenna.** Receptions are the antenna's achievement; acks,
  copies, and executions are the human's.
- **Ratio, not count**: readiness = committed sorties executed cleanly ÷ sorties committed.
  Ignoring a message costs nothing (kills the 0300 problem and the uptime advantage).
  Acking commits you; missing T after commitment is a logged failed execution.
- **Precision metric**: deviation from T, scored and displayed in **tenths of a second**.
  Commander scored on |key − T|, deputy on hold coverage of the window. Crew score = combined.

  > **Amended 2026-08-05, on measurement. RE-AMENDED 2026-08-11 — see §13 A.3.** This said
  > "milliseconds", provisionally, pending §13 task 1.
  >
  > ~~*The bench answer is a 75.7 ms clock floor (worst NTP correction, 6 syncs / 17 h) against
  > a 43 ms input floor, so `max(clock, input)` ≈ 76 ms and the finest honest bucket is 0.1 s.*~~
  > **SUPERSEDED.** The 75.7 ms figure came from a session contaminated by the harness freeze; a
  > cleaner, longer one measured **198.5 ms** worst. The clock floor is **199 ms** and the bucket
  > **0.2 s**. Evidence: `test/fixtures/ntp-corrections-2026-08.json` (valar-eam-feed).
  >
  > §13's rule — display the smallest bucket the measured floor supports — is what executes
  > here, and it is the rule that moved the number, not a new rule.
  > See [gametest-results-2026-08-05.md](gametest-results-2026-08-05.md).
  >
  > 1. **Single-execution deviation: 0.2 s buckets.** The board says so plainly and footnotes
  >    the 199 ms clock floor as the reason. The honesty is on-brand; a spec-sheet caveat is
  >    period-correct. At 0.1 s the device was sorting players by margins smaller than its own
  >    error — two sorties a bucket apart were not measurably different and the display said
  >    they were. That is ranking noise, not scoring.
  > 2. **Aggregates may display finer.** Quantization error averages down, so a monthly average
  >    over N launches has effective precision ~0.1/√N s: the proficiency-cycle ladder may show
  >    hundredths on season averages while single launches show tenths. Stated on the board as
  >    *"single sorties in tenths; cycle averages sharpen with volume."*
  > 3. **Applies wherever deviation renders** — device VOTE REGISTERED screen (`+0.1 s`
  >    format), credits lines, `/log`, duels.
  >
  > This kills the beat-a-record-by-a-fake-millisecond failure mode — a ranking quoting digits
  > the fleet cannot resolve is worse than a coarser honest one, because someone eventually
  > tries to beat it by one — while keeping the ladder competitive. 20 buckets across the 2 s
  > window *will* tie on single nights at 50 players; the month is the real ladder.
- **Deviation → miss distance.** Timing error maps to impact offset at the target (300 ms
  late ≈ 300 m off). Flight animation ends at the offset point. Leaderboard stat: miss
  distance ("CEP"), lower is better. Published benchmark to beat: Minuteman III CEP ≈ 800 ft
  / 240 m.

  **Curve — LINEAR (RATIFIED 2026-08-06).** §13's "start linear, small deadzone" is the
  ruling. Deviation maps straight to distance at a fixed slope, with a deadzone at the
  measurement floor and a cap at the window edge:

  ```
  miss = 1000 m per second of |dev|      ( = 100 m per 0.1 s )
         floored at 50 m inside the deadzone   (|dev| ≤ 0.1 s — see ruling 3)
         capped  at 900 m                      (binds above 0.9 s — see ruling 1)
  ```

  | `\|dev\|` | Scores | Displays |
  |---|---|---|
  | ≤ 0.1 s | **50 m** (see ruling 3) | **"SHACK"** — direct hit |
  | 0.3 s | **300 m** | the distance |
  | 0.9 s | **900 m** | the distance |
  | 1.0 s (window edge) | **900 m** — capped | the distance |

  The 100 m per 0.1 s slope is not a free parameter — it *is* the bullet's own 300 ms ≈ 300 m,
  so the curve and the flavour text agree by construction. The deadzone is the §13 "small
  deadzone" and it is exactly the measurement floor: 0.1 s is what the fleet can resolve
  (§4 amendment), so forgiving the first bucket is the same statement as not claiming
  precision we do not have.

  > **What this replaced, and why it is worth a paragraph.** The previous table read
  > `miss = 100 m × (b − 1)` over the bucket index — a **step** function — directly beneath the
  > sentence claiming the slope *is* "300 ms ≈ 300 m". It is not: at `b = 3` that formula gives
  > **200 m**. The section asserted a slope and specified a staircase, and the two had been
  > sitting a line apart since 2026-08-05.
  >
  > The give-away is ruling 3 below, which derives the 50 m deadzone score from
  > `E[|dev|] = 0.05 s` "at the 100 m-per-bucket slope". That derivation **only works on the
  > linear curve** — under the step formula, 0.05 s falls in bucket 1 and scores 0 m, and the
  > ruling would be deriving 50 from a curve that says 0. Ruling 3 was linear all along.
  >
  > The same two-formulas split had propagated into `valar-eam-feed`'s `game/config.ts`, where
  > `maxMissM`'s comment carried the step arithmetic (`b ≤ 10 → 900 m`) while its sibling
  > constants carried the slope. Both are now corrected, and the implementation
  > (`src/game/scoring.ts` there) is linear. **The curve exists in exactly one place —
  > `GAME_SCORE_*` config — so the device and the website cannot disagree about what a sortie
  > scored.**

  **Rulings on the three flags (DECIDED 2026-08-05).**

  1. **CAP = 900 m.** Deviation only exists *inside a valid execution window*, and the window is
     **±1 s around T** (2 s total width; §12a, open question T1). A key landing outside it is a FAILED execution
     with no flight at all, per §3 — so `|dev| ≤ 1.0 s` **by construction**, and the 1,900 m
     reading this ruling was written against assumed a deviation the game can never record.
     Board flavour line stays honest: *"worst valid shot ≈ 4× published CEP."*

     **Amended by the 2026-08-06 linear ratification:** on the linear curve the window edge
     reaches **1,000 m**, so 900 m is a **cap that binds** above 0.9 s rather than a maximum
     that falls out of the arithmetic. That distinction is not pedantry — a constant that can
     never bind is a constant somebody eventually deletes as dead, and this one is doing work.
  2. **CEP = MEDIAN.** The ladder stat is the **median** miss distance, which is what CEP
     actually means — the radius containing 50 % of impacts. A mean may appear as a secondary
     stat but **never under the CEP label**. Comparison against the published 240 m is then
     statistics-honest rather than a category error.
  3. **SHACK scores 50 m, displays SHACK.** 50 m is not an arbitrary tiebreak: it is the
     **expected miss of a shot known only to be inside the first bucket**. Uniform over ±0.1 s
     gives `E[|dev|] = 0.05 s`, which at the 1000 m/s slope is exactly 50 m. The deadzone
     scores its expected value; the display keeps the reward. The player still sees a bullseye;
     the ladder still separates.

     **A SHACK never scores 0 m.** Recorded explicitly because "direct hit = zero miss" is the
     obvious reading of the display and would undo this ruling by reflex: at 0 m every SHACK
     ties exactly, which is the collapse ruling 4 then has to solve all over again — and the
     mean would stop separating the top of the board too, not just the median.

  4. **Rank on the mean; headline the median (DECIDED 2026-08-05).** Rulings 2 and 3 are each
     right and they collide: if every SHACK scores *exactly* 50 m, any player who SHACKs more
     than half their launches has a median of **exactly 50 m** — as does every other such
     player, so the tie moved from 0 m to 50 m rather than going away. Ruling 3 de-ties the
     *mean* (60 % and 80 % SHACK rates give different averages) but cannot de-tie a *median*,
     which only reads which bucket the middle sample falls in — and above 50 % that is always
     the deadzone. So the two statistics split jobs:

     | | Stat | Role |
     |---|---|---|
     | **Rank** | **mean miss distance** (deviation-derived, SHACK floored at its 50 m expected value) | the §4 sharpening stat — separates crowded tops at `0.1/√N`, and **every launch counts, including the bad ones** |
     | **Headline** | **CEP = median miss**, labelled as median | compared against the published 240 m. **Never used for rank** |

     The mean's outlier sensitivity is **a feature in a ranking**: consistency is the skill
     being measured, and a stat that lets you discard your worst nights measures something
     else. It is a bug in a headline, which is why the headline is the median.

     Board presentation makes the split legible in one line:

     ```
     RANKED BY AVG MISS · CEP SHOWN IS MEDIAN (50% RADIUS)
     ```

  > **General principle, recorded so it survives us.** **Ranking stats and headline stats have
  > different jobs.** Ranking wants **sensitivity** — it must move when performance moves, and
  > it must not saturate at the top where the contest actually is. Headlines want
  > **robustness** — they are read once, compared to an outside number, and must not be
  > swingable by one bad night.
  >
  > Collapsing the two into one number is exactly how the 50 m tie happened: a median was asked
  > to rank, and medians do not rank — they describe. Any future stat added to a board should
  > be asked which of the two jobs it is doing, and if the answer is "both", it is about to
  > have this bug.
- Backlog seeding: solved by construction — animations require a live human in the loop, so
  history renders as a stack of already-printed message strips.
- Dwell/interruption: new real traffic **preempts** a running sequence ("real-world traffic
  takes precedence"). Losing your window because the world got busy is the game at its best.

## 5. Message taxonomy (from REACT's published RMP classes)

Synthetic decode layer assigns each real message a class. Rarity distribution is a design
knob (weights TBD; derive deterministically from message hash so the fleet agrees).

| Class | Real meaning (published) | Game meaning | Rarity |
|---|---|---|---|
| **NAM** | Non-Action Message | Nothing happens. True to life. The driest joke in the game. | Common |
| **FDM** | Force Direction Message (targeting) | Assigns/updates your war plan target coordinates. Solves "who picks the target" — manual entry becomes optional override. | Uncommon |
| **EAM w/ execution** | Launch execution order | Carries T. The launch event. | Rare |

The real EAM structure publicly includes a timing plan / launch delay time (fratricide
deconfliction) — the "message tells you when to launch" mechanic is the real message shape.

### EVERY MESSAGE GETS WORKED

The basis is documented practice, not a game convenience: the **RMP processes all traffic**
(Nuclear Companion), and the **HVC links the squadron** (Wikipedia, LCC). So every message is
worked, not just the rare ones that carry a T.

1. **Banner tap runs the decoder** — the ritual beat. A short decode animation, then the class
   reveal. The tap is the whole point: it is the player choosing to work the message, which is
   what §1.1 requires of everything game-shaped.
2. **Unattended messages auto-decode** after a few minutes, so a device nobody is watching
   still keeps a complete log.
3. **T is anchored to the message timestamp regardless** of when it was decoded — so working a
   message late never moves the launch window, and auto-decode cannot advantage or disadvantage
   anyone.
4. **CONFIRM COPY** (one hold) posts to the **squadron receipt board**: `COPY CONFIRMED · 6/10`.
   The number is the squadron, and watching it fill is the ambient social beat of a quiet night.

**Scoring is additive only** — "watch days" accumulate, with a **daily cap**. Working traffic
can earn, never penalise. This is deliberately not a streak: a streak punishes absence, and
§4's whole objection to passive scoring is that the device must never turn into an attendance
sheet. The cap is what stops a bot-like operator from farming a quiet week.

### RV count — server config, default 1

The number of RVs a sortie carries is a **server-side config value, default 1**. Single-RV
loading has been documented since 16 Jun 2014 (minutemanmissile.com), and post-New-START
**upload readiness** was reported in 2026 (TWZ, CSIS) — so both the default and the ability to
raise it are grounded, and the game can follow the real posture by changing one number rather
than shipping a build.

**Arrival rate & weight tuning.** Community sources are qualitative only: EAMs are "a very
common broadcast type" (priyom.org), "heard daily, 7 days a week, 365 days a year"
(mt-milcom) — call it single-digit-to-dozens per day, bursty, with exercise spikes and
quiet stretches. **Fleet telemetry is the authoritative rate — tune decode weights from
observed arrivals, not forum lore.** Target: execution events ≈ once/day at ~20 msgs/day
observed (≈5% weight); FDMs ~10–15%. Two required handlings: (1) **dedupe repeats** — the
same EAM string is rebroadcast; hash-triggering must fire once per unique message;
(2) **storm damping** — a cooldown floor / dynamic down-weight on execution decodes after
each launch event, so exercise-week traffic spikes don't inflate launch night into launch
week. Droughts stay droughts (that's fishing); storms get clipped.

## 6. Timing, T, and targeting

- **T derivation**: deterministic from message content (hash → offset), anchored to message
  timestamp rounded to a Zulu boundary, not local arrival (feed latency varies). No cloud
  needed for solo play; **the whole fleet computes the same T** → every execution EAM is a
  fleet-wide simultaneous event, rankable that night.
- **T offset (DECIDED 2026-08-04)**: hash-derived, mostly **5–15 min** after decode — room
  to ack, pair, run the enable minigame, and face the retarget-or-launch dilemma — with a
  rare **~2-min "snap execution"** tier to keep everyone honest. Distribution weights TBD in
  playtest.
- **Retargeting cost gradient** (published): MRT mode (small azimuth change) = fast;
  CEP mode (big swing, platform realign) = 15–30 min (not final: §12a, open question T3). Staying near your
  FDM-assigned target is cheap; wrenching across the map costs clock. Targeting locks at
  T−X because a retarget can't complete inside the window.
- **Launch origin**: player selects wing → squadron → flight/capsule (e.g. "90th MW →
  Golf-01"). All locations public (Sentinel EIS, NPS, Wikipedia LF lists). Origin matters:
  real great-circle geometry gives different flight times to the same target.
- **v1 targeting scope (DECIDED 2026-08-04)**: **FDM-assigned targets only.** No coordinate
  entry UI in v1 — the message assigns the target, the taxonomy provides the mechanism, and
  the hardest round-screen UX problem leaves the critical path. Manual entry (rotary dials
  first candidate, companion page fallback) ships later as the override; the MRT/CEP cost
  gradient still plays in v1 via successive FDMs moving your war plan.
- **Guidance drift & alignment (solo upkeep loop)** — from the guidance-system history
  (minutemanmissile.com): the inertial platform's gyros spin continuously on alert and the
  platform must be kept aligned to its reference azimuth; drift degrades accuracy. Game:
  each sortie has a slow **guidance drift** stat that adds baseline error to its miss
  distance unless the player runs a short alignment ritual between messages. Gives solo
  players a skillful idle-time loop, deepens CEP scoring (a well-kept flight out-shoots a
  neglected one at equal timing skill), and gives device uptime an in-fiction home
  ("gyros spinning") **without scoring uptime** — flavor only, per §4.

## 6b. Targeting tiers & publication policy (DECIDED)

**The product picks what the PUBLIC RECORD shows, not what the player aims at.** On-device
targeting is the player's own sandbox — the precedent is NUKEMAP, which has let anyone place a
detonation anywhere for over a decade as a public-education tool. What Missileer controls is
its own published surfaces, and that is where the policy lives.

**Real-adversary targeting is never a feature.** Not a tier, not an unlock, not an easter egg.

| Tier | Target source | Public record shows |
|---|---|---|
| **1 · FREE-FIRE** (post-v1) | manual coordinates | **Private.** `/log` renders range class + land/water only — never the coordinates |
| **2 · DUELS** | opponent's self-declared 4-char Maidenhead grid | **Full public credits** — both callsigns, deviation, outcome |
| **3 · EVENTS** | community-nominated, fleet-voted | **Codenamed scored windows**, fully public |
| **v1** | FDM-assigned **Broad Ocean Area** | Public; BOA under the 1994 detargeting posture |

- **Tier 1 free-fire** exists because the sandbox is the player's business; the log's silence
  is what keeps the *product* out of it. Range class + land/water is enough to score and to
  narrate, and carries nothing worth publishing.
- **Tier 2 duels** are opt-in station-vs-station. The target is the opponent's **self-declared
  4-character Maidenhead grid** — the ham-radio convention, ~1° × 2° and deliberately coarse,
  which is a locator rather than an address and is the reason it can be public at all. Launch
  is from a **real wing LF**, so great-circle geometry stays honest; **score scales with
  great-circle range** (the long shot is the hard shot).
  **Defense = a GMD interceptor**: scarce, a midcourse timing minigame, with the **published
  ~50% test record used as the odds**. Hits are **cosmetic and logged, never destructive** —
  a duel can never cost the victim inventory, because a game where losing costs you your
  ability to play stops being played.
- **Tier 3 events** are the community's: nominated, fleet-voted, codenamed, and time-boxed.
- **v1 ships the BOA** — the Broad Ocean Area is the published practice target under the 1994
  detargeting posture, which means the game's default aim point is the one the real force
  uses, and it is ocean.

## 7. Flight & impact

- Real time. Intercontinental ≈ 30 min (public figure). Launch → device returns to
  monitoring → ntfy push at impact. The dead time is the bit.
- Animation script = the published Minuteman III MIRV flight sequence (8 phases:
  1st-stage sep ~60 s, shroud eject, 3rd-stage ~120 s, post-boost ~180 s, RV/chaff/decoy
  deploy, burst). Public numbers, free choreography.

### FLIGHT DIRECTOR — real-time flights, time-division views

The flight is ~30 real minutes and the device is a monitor for most of it. So the flight is
**time-divided**, and each division gets the treatment its phase deserves:

| Phase | Wall-clock | Treatment |
|---|---|---|
| **Boost → RV release** | ~T+0–4 min | **Full-screen cinematic**, at the *true published* T+ marks. **Participants only** |
| **Midcourse** | ~T+4 min → T−90 s | **Monitoring resumes**, with flight chrome over it (the shared overlay mechanism). Tap for the map |
| **Terminal** | **REAL T−90 s** | **Re-escalates: REENTRY view** — plasma descent, penaids burning through |
| **Detonation** | wall-clock impact second | Full-screen |

The device goes back to being a monitor in the middle because that is what the real dead time
*is*, and because a 26-minute animation nobody watches is worse than a 4-minute one everybody
does. Terminal re-escalation at the real T−90 s is what makes the return feel earned.

> **Match-cut rule.** Ascent ends by shrinking the vehicle to a single dot; the map opens with
> that same dot. One continuous object across a cut between two entirely different renderers —
> it is the cheapest possible way to make two views read as one flight, and it stops working
> the instant either side redraws the dot differently.

### Time-of-flight & range — minimum-energy Lambert

Ballistic TOF from great-circle distance `d`, minimum-energy (the honest first-order model,
and it needs no launch-angle input):

```
RE = 6371 km            MU = 398600.4418 km³/s²
th   = d / RE                       (central angle, radians)
c    = 2 · RE · sin(th / 2)         (chord)
s    = RE + c / 2                   (semiperimeter)
a    = s / 2                        (minimum-energy semi-major axis)
beta = 2 · asin( sqrt( (s − c) / s ) )
TOF  = sqrt(a³ / MU) · ( (π − beta) + sin(beta) )
```

Validates against the published figures: **6,700 km → 24.9 min · 9,700 → 31.6 · 14,000 → 38.5**.
The ~30 min intercontinental figure in the bullet above falls out of it rather than being
asserted.

**All tiers validate `d ≤ 14,000 km`** (the §12 range cap). The map **renders the reachable
boundary**, so the constraint is visible rather than an error message after the fact.

> Consequence worth keeping as a **discoverable**, not a documented rule: the southern Indian
> Ocean is unreachable from CONUS — it is roughly antipodal, and a minimum-energy ballistic
> shot cannot get there. Players who go looking for the one place they cannot hit should find
> it themselves.
- **Payload select before launch — the tone escape hatch. STATUS: OPEN.** Originally: absurd
  payloads only — 2,000 lb of shredded paper (→ #136: the pack-in is *recovered payload
  debris*), glitter, a formal apology; the moment the warhead is shredded paper, the feature
  reads toy, not sim. **Shredded paper was removed by the §11 locked art direction**, which is
  too committed to absorb a joke payload mid-sequence. The tone valve it provided still has to
  come from somewhere — that is the open part, not whether one is needed.

## 8. Roles & the two-person rule

- **Capsule = two seats (RESTRUCTURED 2026-08-04).** A capsule (LCC) has 2 crew positions —
  MCCC + DMCCC, the real structure — so **the crew and the vote are separate mechanisms**:
  the four-hand execution (enable 3+3, switches, key, execution window) happens **inside one
  capsule between its two seats** (the two real workstations = the two devices — the enable
  minigame's "half the characters on each workstation's VDU" is now literal); the **vote**
  happens **between capsules** of a squadron (§9). Topology: seat → capsule (2 people,
  standing crew, shared 10 birds) → squadron (5 capsules, 10 people, vote network + party
  line) → wing (3 squadrons) → fleet.
- **Placement flips from scarcity to concentration**: 45 capsules × 2 = **90 seats vs ~50
  owners**. Placement UI lists **capsules with an open seat first** ("Golf-01 needs a
  deputy") before offering to open a new capsule — every filled seat is an instant standing
  crew; a solo owner is a "crew wanted" listing with a story. Self-crewing = one owner
  holding both seats of their capsule, publicly attributed. Full squadron salvo is now a
  **10-person** event — rarer and better.
- **Target hardware (DECIDED 2026-08-05): the S3 1.28"** (GC9A01 240×240 round +
  **CST816T**, the Blipscope default board — variant `s3_128.h`, touch bus verified). The
  S3 1.46" SPD2010 AMOLED (`missileer-s3-146` env, which currently exists in the repo) is
  the upgrade SKU down the road. Consequences: a `missileer-s3-128` env must be created
  (variant header already exists; CLAUDE.md's add-a-SKU recipe applies), and **game
  screens must fit 240×240**.
- **⚠ CST816 auto-sleep vs the hold gesture:** the repo's own bench history (C3 1.28"
  retirement + wedge program, platformio.ini) implicates the **CST816's auto-sleep
  engine** in both observed touch-wedge classes. A multi-second static hold is the most
  auto-sleep-shaped input the game asks for — the deputy-gesture prototype is now aimed at
  a *documented* failure mode, not a hypothetical. Bench firmware must test holds with
  auto-sleep in its current config AND held off.
- Also already built in `src/eam/`: the 7-segment Zulu clock (Clock = idle screen), the
  10-screen rotation/swipe shell, `Msg.id` dedupe, the single-worker feed poll pattern,
  `LeaderboardId`, the `eam-` OTA channel.
- Hardware truth: CST816 = single touch → one press-hold per human → the honest port is
  two devices. REACT's published config (1 launch key + 3 cooperative launch switches,
  four-hand) makes the roles **asymmetric**:
  - **Commander (MCCC)** — the key. Precision role. Sits left (published convention).
  - **Deputy (DMCCC)** — the switches. Steadiness role. Sits right. LEP is publicly part of
    the deputy's workstation.
  - Exact control split beyond that is folklore → invent freely.
- Era note: two keys = pre-1994 CDB (the movie image). Key + switches = REACT. **Committed:
  REACT era** — better asymmetry, and the citation is the Wikipedia article.
- Solo failure is mechanical, not thematic: no deputy holding switches → key-turn at T does
  nothing. "Second officer not on station." Never fake a partner.
- **Solo mode (DECIDED 2026-08-04): solo vote + dead-man timer.** A solo player runs the
  full drill alone (their one press-hold = a degraded single-operator execution); a clean
  execution registers a **lone launch vote** that pends on the dead-man timer. If no crew
  seconds it and nobody inhibits, the launch proceeds — slow, lonely, and real. Crew launch
  is instant and prestigious. This is the published one-vote-plus-timer mechanic doing the
  game-balance work; the fiction balances solo vs crew, no artificial nerf needed. Solo
  launches share the main ladder (no split leaderboards at 50 units); ALCS fiction shelved.
- **Async countersign fallback**: off-schedule launch order sits pending until another owner
  countersigns within hours. Turns "both online now" into "someone, today." NTP-simultaneous
  crew launch stays the prestige version. Build in the same release as pairing.
- Pairs that complete a joint launch become a **standing crew** (persistent, named, crew
  leaderboard). At 50 units, few sticky bonds > ambient matchmaking.

## 9. Fleet, teams, progression

- **Delicious coincidence: REACT was deployed to exactly 50 LCCs. The fleet is ~50 units.**
  The game fleet can map 1:1 onto the real REACT force. Use this everywhere.
- **Teams = the three wings** (setup choice, permanent-ish): 90th "Mighty Ninety"
  (F.E. Warren), 91st "Roughriders" (Minot), 341st (Malmstrom). ~17 per team at current
  fleet size — the granularity where a 50-unit leaderboard feels alive. Monthly
  wing-vs-wing readiness competition.
- **Wing assignment (DECIDED 2026-08-04): player choice with current wing populations
  visible at setup.** Chosen identity builds attachment; visible counts self-balance well
  enough at this fleet size. Revisit only if one wing hollows out.
- **Squadron nicknames are public and glorious** (Wikipedia): 319th "Screaming Eagles",
  740th "Vulgar Vultures", 741st "Gravelhaulers", 742nd "Wolf Pack", 12th "Red Dawgs",
  490th "Farsiders", 10th "First Aces". Free team identity.
- **Capsule-picker data structure confirmed** (90th MW article): each wing = 3 missile
  squadrons (90th: 319th/320th/321st) × 5 flights = **15 flights per wing, lettered
  A→O, 45 fleet-wide** — matching the 45-MAF constant in §12. Picker: wing → squadron →
  flight letter. The 90th's field spans ~9,600 sq mi across Wyoming/Nebraska/Colorado —
  tri-state capsule coordinates are on the map for real.
- **90th-specific flavor** (public): F.E. Warren is the first operational ICBM base
  (Atlas-D, 1960) and hosts 20 AF HQ; the Peacekeeper (LGM-118) served **only** at the
  90th (400th MS, retired 2005) → candidate wing-exclusive cosmetic/easter egg;
  Quebec-01 preserved on base (reference-photo target, add to §15 museum list).
- **Huey overflights** (20 AF article: 582nd Helicopter Group, UH-1N, missile-field
  security support): occasional tiny helicopter sprite crossing the idle map screen.
  Pure flavor, zero mechanics, exactly the right amount of alive.
- **Squadron = 5 crews sharing a party line.** Published: the Hardened Voice Channel links
  the five LCCs of a squadron, party-line, no privacy. Game: squadron-of-5 chat/ping channel
  ("HVC"). Right-sized social unit for a small fleet.
- **HVC presence — ambient callsign chirps.** The party line carries small presence lines:
  `HVC · WOLFPACK: COPY 4181`. Two rules make this a feature rather than surveillance:
  - **Fired ONLY by affirmative acts** — a confirmed copy, a vote, a commit, a launch.
    **Never by passive telemetry.** Presence is something a player *did*, never something the
    device observed about them.

    > **This rule is load-bearing twice, and it is the same rule both times.** It is
    > [CLAUDE.md](../CLAUDE.md)'s *"Non-goal: behavioural telemetry (settled 2026-08-02)"*
    > applied to the social layer — the repo-wide line is "a consequence of serving the device,
    > not a measurement of the person", and here it reads "a consequence of an action you took,
    > not a measurement of who is at their desk."
    >
    > It is also **squadron trust**: a party line that reports presence you did not announce is
    > surveillance wearing a game's clothes, and the squadron is the one place in this design
    > where other real people are watching.
    >
    > Recorded explicitly because the softening is so easy and so plausible — "just show who is
    > online" is one line of code, is what every other product does, and would quietly convert
    > an ethics commitment into a presence indicator. Anyone proposing it should have to argue
    > with the non-goal by name.
  - **Each callsign gets a stable 6×6 glyph**, hash-derived from the callsign, so squadron-mates
    become recognisable at a glance on a 240 px face where a name would not fit.

  Placements: the **commit-wait** screen shows squadron-mates on the same T (the strongest one —
  you can see you are not alone in the window); **pending votes** show who is on watch; the
  **Clock ambient rotation** carries the quiet version; the **receipt board** lists confirmations
  in arrival order.
- **Self-declared grid locator (optional).** A player may publish a **4-character Maidenhead
  grid** as their station locator. **Absent = not duelable** — no locator, no Tier 2, no prompt,
  no penalty. Opting in is the entire consent mechanism for §6b Tier 2, which is why it is a
  field the player fills rather than anything derived from the device.
- **Rank progression with public structure**: PLCC crew → Squadron Command Post → Wing/
  Alternate Command Post. Published authorities map to game powers: SCP can execute for a
  failed flight and **countermand** launches in its squadron; WCP for the wing. Veteran
  players earn inhibit/execute-on-behalf authority. (Also the inhibit-balancing lever:
  inhibits gated by rank + scarcity, one per message, so one griefer can't ruin launch night.)
- **Top of the ladder: Twentieth Air Force** — the numbered air force commanding all three
  missile wings (HQ at F.E. Warren, under AFGSC; "America's ICBM team"). Use it as the
  in-fiction **Higher Authority**: decoded traffic is branded as coming from 20 AF, and the
  rank ladder tops out there (crew → SCP → WCP → 20 AF staff, cosmetic). Skip the WWII
  lineage entirely — the toy doesn't need it and the tone rule (§1) says leave it alone.
- **Wing competition has a directly citable model: Olympic Arena / the Blanchard Trophy.**
  The 341st article names both — SAC's annual missile competition and its "most coveted
  prize" (341st won in 1976, '86, '90, '91). So the annual fleet-wide championship is an
  Olympic-Arena-shaped event with an invented trophy name (real structure, invented name,
  per §2). The Omaha Trophy (90th accolades) is the secondary reference point.
- **Season cadence — proposed answer (closes open question, pending sign-off):** the real
  institutions provide it. Individual ladders reset on a **monthly proficiency-test cycle**
  (monthly launch-officer proficiency testing is a documented institution); the annual
  Olympic-Arena-style championship crowns the year. Monthly rhythm for individuals, annual
  for wings.
- **Wing identity kits for the selection screen** (public mottoes + emblems): 341st
  *"Pax Orbis per Arma Aerea"* (World Peace Through Air Strength) at Malmstrom; 91st
  "Roughriders," *"Poised for Peace,"* at Minot — where the 5th Bomb Wing's B-52s share
  the base (the dual-nuclear-base bragging right is public); 90th "Mighty Ninety" at
  F.E. Warren with 20 AF HQ. Three distinct flavors of swagger, all citable.
- **Malmstrom heritage cosmetics** (341st article): first Minuteman wing ever (first
  LGM-30A arrived July 1962, emplaced at **Alpha-9**); the only wing that fielded the
  Minuteman IA; hosted America's **1,000th Minuteman** (completion-of-deployment
  milestone); once ran four squadrons / 200 silos (564th MS, 1966–2008). Wing-exclusive
  easter eggs, same slot as the 90th's Peacekeeper history. "Rivet MILE" (Minuteman
  Integrated Life Extension, 341st lead unit) is the real name-shape for the sortie
  maintenance/regen cycle — invent a variant.
- **Votes are squadron-scoped (ADDED 2026-08-04 — supersedes "visible fleet-wide" voting).**
  In the real system the voting network is the squadron: only the 5 LCCs of your squadron
  can second, inhibit, or leave your vote to the dead-man timer. Game consequences:
  - **The vote network and the HVC party line are the same five people** — the squadron
    becomes the fundamental social unit (classic 5-player party size).
  - **Inhibit griefing is solved structurally**: your inhibitor is one of four known
    squadron-mates on a shared party line; attribution + proximity self-polices. §13's
    inhibit economics shrinks to "pick a timer length."
  - **Placement system** (superseded by §8's two-seat capsule structure): 3 wings × 3
    squadrons × 5 capsules × 2 seats = **90 seats** vs ~50-unit fleet. Choose wing → fill
    an open seat in an existing capsule (preferred; instant standing crew) or open a new
    capsule. Squadron = **10 people** in 5 capsules. **Expansion valve with a citation:
    reactivate the 564th** (Malmstrom's real fourth squadron, 1966–2008) if the fleet ever
    outgrows the structure.
  - **Squadron pool**: 5 × 10 = 50 birds collectively; squadron readiness = the monthly
    competitive stat. Leaderboard tiers: crew/capsule → squadron → wing.
  - **Prestige ladder from the published two-LCC rule**: crew launch < squadron launch
    (two crews, same squadron, same T) < **full squadron salvo** (all 5 capsules clean on
    one T — the once-ever, fleet-witnessed, framed-screenshot event).
  - **SCP as rotating honor**: one Squadron Command Post per squadron, held monthly by the
    squadron's top proficiency scorer; carries countermand authority (published basis).
    Replaces grind-rank progression at squadron level.
  - Witnessing stays fleet-wide (§ above) — everyone sees every launch; only voting
    *agency* is squadron-scoped. Fleet-shared T keeps execution nights communal; each
    squadron resolves its own votes inside the shared moment.
- Vote → inhibit → dead-man timer (published triad) is the squadron co-op layer. Remaining
  balancing = timer length only (§13).
- **Fleet witnessing (DECIDED 2026-08-04): every launch is a shared, ambient event.** When
  a vote goes terminal, all devices show the track on their map + a status line ("LAUNCH IN
  PROGRESS — GOLF-01 / 90 MW"); impact draws the ring fleet-wide at the same moment.
  Full-screen is for participants only; spectators tap-to-watch (opt-in klaxon setting).
  Same interruption-budget logic as the real-traffic rule: nobody's screen gets hijacked
  because someone else played. At ~1 launch/day this shared moment is the core social
  payoff of a 50-unit fleet.
- **Attribution (DECIDED 2026-08-04): names on everything — crew, seconder, inhibitor.**
  Accountability is what the two-person rule is about; the fiction demands a record.
  Bureaucratic credits format: "LAUNCH 0347Z — GOLF-01 — MAVERICK / GOOSE — SECONDED:
  WOLFPACK — DEVIATION: 43 MS." Callsigns only (owner-chosen handle on `LeaderboardId`),
  never real names. Inhibits are attributed, not anonymous — anonymity shields the one
  griefer; attribution + the party line self-polices. Inhibiting requires picking a reason
  from a deadpan dropdown ("targeting discrepancy," "safety of flight," "paperwork
  incomplete"); the reason appears in the credits. The permanent record is the game.
- Scheduled fleet exercises (invented two-word codenames — VIGILANT HAMSTER) demoted to
  secondary events: onboarding cohorts, thin-traffic weeks, guaranteed-pairing nights. The
  real EAM is the primary bell — it hits every device within seconds, free, on the USAF's
  schedule. Practice/training mode runs on EXERCISE-marked synthetic traffic only.
- Solo-mode fiction if wanted: ALCS ("Looking Glass" airborne missileers, E-6B) is the
  published survivable-launch fallback — a citable frame for playing without a ground crew.

## 9b. Web presence — scopes.valarsystems.com (added 2026-08-04)

Server exists; Pi (SDR) records the HFGCS audio. One site, three surfaces, one pipeline.

**Architecture note (2026-08-05, corrected after repo review):** Missileer's backend is
the separate **`valar-eam-feed`** repo — a **Render service**
(`https://valar-eam-feed.onrender.com`, the firmware's `EAM_FEED_BASE`), *not* a
Cloudflare Worker like Blipscope's proxy. The device already polls its normalized
endpoints (`/eam/latest`, `/eam/skykings`, `/eam/tempo`, `/eam/stats`, `/eam/codewords`,
`/propagation`, `/launches/icbm`). Getting `/missileer/*` onto scopes.valarsystems.com
therefore needs a routing hop — Worker proxy to Render, a Cloudflare origin/route rule,
or Worker-served pages calling the Render API — **open, delegated to the backend prompt's
discovery step**. Still true: build on the new convention from day one (no legacy paths),
and inherit Blipscope's metrics pattern (exact-match route allow-list).

**URL convention (DECIDED 2026-08-04): edition-namespaced paths.** `/leaderboard` was
already Blipscope's — the fix is a standard template every edition inherits:
`scopes.valarsystems.com/{edition}/{surface}`. Blipscope migrates to
`/blipscope/leaderboard` with a **301 from the old `/leaderboard`** (existing firmware and
links keep working). Root `/` becomes the hub page listing all scopes. API mirrors it:
`/api/v1/{edition}/…`. Bonus this unlocks: one owner identity across editions — the same
callsign on Blipscope and Missileer, and an owner profile page aggregating their scopes
(a cross-edition "service record").

- **/missileer/log — the permanent record (centerpiece).** Public launch log: credits line
  per launch (time, capsule, crew callsigns, seconded/inhibited + reason, deviation, miss
  distance). **Each entry links to the audio of the real EAM that triggered it** — game
  event and source transmission, one click apart. Live "LAUNCH IN PROGRESS" banner + map
  track while a vote is terminal (shareable URL).
  **Rendering is keyed off `target_class` per the §6b publication policy** — the enum
  `{BOA, STATION, EVENT, FREEFIRE}` **ships in v1**, even though only `BOA` is reachable in v1.
  Shipping the discriminator early is the point: the log format never has to be migrated when
  free-fire lands, and there is no version of the schema in which a free-fire coordinate could
  be rendered because the renderer never had a branch for it.
- **/missileer/leaderboard.** Individual (monthly proficiency cycle: avg deviation — a *cycle
  average*, so it may render hundredths per the §4 amendment; single sorties stay in tenths — CEP,
  readiness ratio), crew board, wing standings (annual championship), cycle history. Reuses
  fleet auth + `LeaderboardId` from the radar edition.
  **Rank is by mean miss distance; the displayed CEP is the median** (§4 ruling 4), and the
  board says so in one line rather than leaving a reader to assume the number they are sorted
  by is the number they are shown:

  ```
  RANKED BY AVG MISS · CEP SHOWN IS MEDIAN (50% RADIUS)
  ```
- **/missileer/archive.** Every recorded EAM: timestamp, duration, playback, decode class,
  cross-links to resulting log entries. Stands alone as an SWL-community resource (audio
  EAM archives are scarce; text logs aren't) → organic discovery traffic from non-owners.
- **Pipeline (DECIDED 2026-08-04):** Pi records **squelch-triggered clips** (pad a few
  seconds each side; watch for mis-set squelch clipping message starts) → per-transmission
  Opus + metadata → server ingest → (a) archive, (b) fleet event feed. **The ingest event
  doubles as the fleet-wide witnessing push (§9)** — one pipeline serves both. Storage
  trivial (~100 MB/month at 20 msgs/day).
- **Event feed (DECIDED 2026-08-04): SSE + ntfy.** Server-sent events from the scopes
  server carry live vote state, witnessing tracks, and the web banner; ntfy keeps impact
  pings and owner alerts. One-directional is all this needs; revisit WebSocket only if
  devices ever send interactive traffic.
- **Device tie-in (optional):** tap a printed strip in history → QR/short-link to that
  message's archive page. On-device audio playback only if the speaker hardware merits it;
  the web is the listening surface.
- **/missileer/sources — public provenance.** A walkthrough of the launch sequence, **beat by
  beat, each beat citing the source it came from**. Rules: public-domain and CC material is
  **embedded**; commercial video is **linked, never rehosted**; and anything invented is
  **explicitly marked as an invention**.
  This is §2 made externally checkable rather than internally promised. It also converts the
  project's most awkward question — "is any of this real?" — into its best page, and gives the
  answer a permanent URL instead of a paragraph in a README.
- **Republication posture:** same practice as WebSDR live streams and long-running YouTube
  EAM archives — traffic is broadcast in the clear worldwide, and EAM content is itself
  ciphertext. Consistent with §2.

## 10. Sortie economy

- Published structure: flight = 1 LCC + 10 LFs → **your capsule controls 10 missiles,
  shared between its two seats** (the crew shares one inventory — reinforces the
  partnership). Squadron pool: 5 × 10 = 50.
- **Inventory outcomes (DECIDED 2026-08-04) — tiered by fault, not flat:**
  | Outcome | Log label | Inventory effect | Other cost |
  |---|---|---|---|
  | Launched | `LAUNCHED` | Expended → regen queue | — (you got the launch + score) |
  | Missed after commit | `FAILED` | **24 h maintenance stand-down**, then returns | Readiness ratio hit + public log entry (the real deterrent) |
  | Aborted deliberately | `ABORTED` | **24 h maintenance stand-down**, then returns | Same as FAILED — ack is a promise, and breaking it on purpose costs what breaking it by accident costs. The honest label is the only difference. |
  | Preempted by real traffic | `PREEMPTED` | Returned immediately | **None** — see the preemption rule below |
  | Inhibited | `INHIBITED` | Returned immediately | None (griefing never costs the victim a bird) |
- **Preemption rule (DECIDED 2026-08-04).** Real traffic **suspends** the sequence, it does
  not cancel it: the commitment stands and re-entry is allowed right up to T. If T passes
  and a preemption occurred **anywhere inside the commit window**, the miss is exempt — bird
  returned, no ratio hit, logged `PREEMPTED` rather than `FAILED`.
  **No could-they-have-made-it-back adjudication.** Any preemption in the window exempts,
  full stop. The alternative is a judgement call about whether the player had enough time
  left, which is unanswerable, unappealable, and would make the fairest-sounding rule the
  most arbitrary one in the game. A player who is interrupted must never have to argue.
- **Regen rate (DECIDED 2026-08-04): 1 bird / 3 days** — a full rack rebuilds in ~a month,
  matching the monthly proficiency cycle. A launch spends ~3 days of capacity; a miss's
  stand-down is a third of that. (Real-world flavor: F.E. Warren publicly retargeted up to
  22 missiles/month as routine maintenance.)
- Sortie status ladder (published/status-code folklore, safe at label level): Strategic
  Alert → Enabled → Launch Commanded → Launch in Progress → CES (Committed Executed
  Sortie). This is the 10-sortie status board — and a squadron-wide sortie board is
  literally REACT's advertised feature.

### Tunables — set, not proposed

Gathered here because they were decided across three sessions and were only recorded in §13's
decision table; a number that governs the economy belongs where the economy is specified.

| Tunable | Value |
|---|---|
| Miss after commit | **24 h stand-down** |
| Regen rate | **1 bird / 3 days** |
| Dead-man timer | **10 min** |
| Enable minigame limit | **60 s** |
| Ack / commit cutoff | **T−60 s** |

**Gesture spec (bench-measured 2026-08-05 — see
[gametest-results-2026-08-05.md](gametest-results-2026-08-05.md)):**

- **≥100 ms rejoin window.** Contact is debounced, never sampled raw. Every dropout measured
  across three arms was ≤56 ms, so 100 ms clears the worst by better than 2×.
- **Driver source** (`getTouch()`), **not** the chip's `TouchNum` register. Polling the register
  directly measured *worse* — it was the losing arm by 30 points.
- **Auto-sleep off** — worth ~20 points of clean-hold rate.
- **LATE-START tolerance.** The gesture waits for a clean contact-begin and **never fails the
  attempt** because the first touch did not register. Two contacts landing simultaneously can
  register nothing at all on this single-point controller, so a palm arriving with the thumb
  must cost a moment, not a bird.

## 11. Visual & UI spec (art bible = released USAF imagery)

| Element | Source | Port |
|---|---|---|
| **TODC clock** | Published: Time-of-Day Clock, upper center bay, red 7-segment, spec'd drift figure (not final: §12a, open question T4), survives power loss | Idle face: red 7-seg Zulu on black. Countdown to T in same face. (NTP quietly beats the nuclear-hardened spec — README joke.) |
| **Paper-strip printer** | Center console in crew photos | Messages *print*, teletype pacing, character by character; history = scrollable stack of printed strips |
| **Padlocks / SAS safe** | Photo shelf padlocks; "SAS safe with two crew locks" (Wikipedia gallery) | Authenticate step visual: locked compartment, two locks, crack the seal. Depict the padlock, never the procedure. |
| **Fisheye capsule** | The classic crew photo is a round image | Round display = porthole into the LCC: console arc hugging bezel, clock top, printer center |
| **Palette** | Photos | 7-seg red (time), paper white (traffic), checklist green (interactive), binder tabs / brass padlock (accents), **amber = training** |
| **Seating** | Published | Commander left, deputy right — pairing screen convention |
| **Floppy disk** | Published: FDD archives the crew log | Log/session export icon |
| **Retro-tech flavor** | VAX 810, trackball, Ada, drum-memory ancestry | "Armageddon with a floppy disk and a trackball" — the aesthetic is the joke |

**AMBER is the fourth accent, and it means one thing only: training / EXERCISE.** Never
decoration, never a second warning colour. It is load-bearing precisely because it is reserved —
a player must be able to tell a drill from the real thing at a glance and from across a desk,
and a colour that appears anywhere else stops carrying that.

Physical capsule flavor (published, for boot screens/easter eggs): 4.5 ft concrete walls,
blast door, room suspended as a pendulum on four shock isolators, EMP shielding, sand-filled
escape tunnel, 2006 "Netlink" internet upgrade (cite when explaining why a nuclear capsule
aesthetic has Wi-Fi).

### Animation art direction (LOCKED)

Reference: the **Northrop Grumman 2007 flight-sequence video**, used as the beat sheet. The
whole ascent plays as a **script over a sinking Earth limb** — the horizon dropping away is what
sells altitude on a screen with no other scale cue.

**Separations are AXIAL** — flash plus an anamorphic streak at the joint, embers, and the spent
stage receding *along the flight line* rather than tumbling off sideways. And each separation
carries the full **STAGING BEAT**:

```
burnout  ->  sep  ->  ~1 s coast (exposed, UNLIT bell)  ->  IGNITION  ->  burn
```

The one-second unlit coast is the whole beat. It is the pause where a real vehicle is committed
and not yet accelerating, and cutting it — going straight from separation to a lit engine —
removes the only moment in the ascent with any suspense in it.

| Beat | Direction |
|---|---|
| **Liftoff** | **the one shot from the ground.** A fixed side-on silo camera. It opens on the **Launcher Closure Door** — 110 tons of reinforced concrete and steel, 3.5 ft thick — where a **steel locking pin retracts**, then a **ballistic gas generator** shoves the slab sideways on steel tracks and **clean off the frame**. Two-stage motion, because that is what makes an opening read as a *mechanism* rather than a drawer; **slow enough to see 110 tons move** — ~1.8 s on the bench, 2.3 s at true speed, after two passes at half that read as a panel snapping aside; sideways rather than hinged, because a sliding lid shoves clear through the debris a near-miss dumps on the surface while a hinged one lifts into it and jams. **All of this is T-minus, not T-plus** — the beat runs T-4 → T+10 and its pre-roll is subtracted from the T+ clock, so the door can take as long as it needs without moving a single published mark downstream. The gas generator is not drawn. The most recognisable piece of hardware on a missile field, and absent from both the NG animation and the preview, which each open on a hole that is simply already there. **Ignition follows the door and happens inside the tube** — the first-stage motor lights once the closure has cleared the path, with the vehicle still fully below grade. Slab and closure are sized off the vehicle in the same frame (`kSegments` gives stage 1 an 11 px body for a 5.5 ft airframe, so 1 px = 0.5 ft). Then **fire shoots straight up out of the silo while the missile is still inside it**: a vertical jet, not a pool, and in four consecutive frames of real launch footage the vehicle is not visible at all. It appears **coming out of the top of the fireball** — so the fire is drawn *over* the vehicle, which is the difference between a silhouette sliding out of a slot and something emerging from fire. Above it a smoke **COLUMN**, not a ground bank. The column is the subject and the missile is the small thing riding it; the smoke rises far more than it drifts. Camera shake decays over ~2.2 s. **The launch is deliberately slow** — the vehicle is in frame for ~4.7 s of a 9 s beat — because it is the moment nobody should miss, and at the look target's own pace it was visible for 1.5 s and easy to miss entirely. *Where the look target's ground bank and the NG video's column disagree, the video wins: the spec names the video, and `docs/reference/*` fills in where the spec is silent rather than overruling the source the spec cites.* It is **its own beat**, not a camera split inside stage 1 — burying it there would average its cost into a beat with 50 s of cheap chase-cam, put the most iconic moment in the sequence out of reach of tap/swipe/hold, and force a refactor the first time §7 wants liftoff separately gated. *Added 2026-08-06.* |
| **The cut out of liftoff** | **a cut on ABSENCE, not a match cut.** The match-cut rule below governs ascent→map and matches on *shape*. This one matches on nothing: the ground camera holds until the vehicle has left frame (~39% into the beat; the rest is smoke) and the cut happens because the subject departed. What carries continuity instead is that the vehicle is **the same object at the same size in the same paint** on both sides — the pad is not a second colour scheme, it is the airframe under a daylight exposure that fades out across the opening quarter of stage 1. A colour change on the same object across an instant cut reads as a *different object*, which is the one trap this beat sets. |
| **Shroud** | **one piece.** The aeroshell leaves whole, forward and to the side, tumbling away on its own arc with a separation-motor flare. *Corrected 2026-08-06. This row said "clamshell halves", which came from a third-party animation (AiTelly) that this section does not cite. **The NG 2007 video named at the top of this section shows a single shroud at T+121**, and so does the MM3 MIRV-path diagram (item E). Both of the sources we actually rely on agree; the row was wrong.* |
| **Post-boost** | blue porcupine RCS |
| **PSRE pitch-over** | continues the arc **nose-down** — downrange velocity is conserved, so the RV releases *in the direction of travel*. Getting this backwards is the tell that an animation was drawn rather than reasoned |
| **RV release** | **SILENT.** No ordnance, no bang. The quietest moment in the sequence is the one that matters most |
| **Bus** | **backs away** under retro thrust |
| **Penaids** | deploy from the backing bus |
| **Reentry** | decoy streaks burning out |
| **Detonation** | **Plumbbob Hood / Upshot-Knothole Badger** fire palette — full-screen, cooling to rust |

**The reference is authoritative for WHAT and WHEN, never for WHETHER IT CAN BE SEEN**
(standing rule, 2026-08-06). The beat sheet above is ported from a preview authored on a
240×240 canvas *displayed at 480 CSS px* on a bright laptop — every size and contrast
decision in it was made at 2× magnification in a viewing condition the product never has.
Choreography, beat timing and palette intent port faithfully. **Legibility at 240 px across
~32 mm of glass at desk distance is a device-side judgment and it overrides the reference.**

Already caught, both faithfully ported and both wrong on glass: the RV is ~14 px of geometry
(**1.9 mm** — below the size at which a viewer can tell what the object is) drawn at ~4%
luminance on black. *"The reference does it this way"* is not a defence for something
invisible on the panel. Note also that "shrink the vehicle to a single dot" above is about
the **end** of the ascent and the match cut, where being a dot is the point — it is not a
licence for the vehicle to be unreadable through the whole of midcourse.

**Shredded paper is removed.** The §7 payload-select escape hatch is **OPEN** as a result: the
tone valve it provided has to come from somewhere, and this direction is too committed to
absorb a joke payload mid-sequence.

## 12. Published constants (cite, don't invent)

**Game timings are not in this table.** They are ours, measured from our own recordings or
stated as our own design choices, and live in §12a (ruled 2026-09-28).

| Constant | Value | Source |
|---|---|---|
| Enable minigame | 6 chars, 3+3 split (the time limit is ours: §13, 60 s) | Nuclear Companion |
| MM III range | 8,700 mi / 14,000 km (targeting range cap) | Wikipedia LGM-30 |
| Terminal speed | Mach 23 | Wikipedia LGM-30 |
| MM III CEP | ~800 ft / 240 m (miss-distance benchmark) | Wikipedia LGM-30 |
| Retarget, CEP mode | 15–30 min realign — **TODO: not final, §12a T3 (proposed: drop)** | Nuclear Companion |
| Retarget, whole force (REACT) | 10–12 min — **TODO: not final, §12a T3 (proposed: drop)** | Nuclear Companion |
| Flight sequence | stage 1 ~60 s, stage 3 ~120 s, post-boost ~180 s | Wikipedia LGM-30 sidebar |
| Force structure | 3 wings, 400 missiles, 450 silos, 45 MAFs, 15/wing; flight = 1 LCC + 10 LF; squadron = 5 flights; **REACT fleet = 50 LCCs** | Wikipedia ×2, Nuclear Companion |
| Separation | LFs ≥3 mi apart, ≥3 mi from LCC | Wikipedia LGM-30 |
| Alert tour | 24 h | Wikipedia / photos |
| TODC drift spec | ≤1 s / 24 h — **TODO: not final, §12a T4 (proposed: drop)** | Nuclear Companion |

Invented freely (public record is silent — per rule §2): T offset distribution, ack window,
inhibit economics, deviation→distance curve, message-class weights, sortie regen rate,
choreography of the four-hand split, exercise codenames, payload roster.

## 12a. Timings (ruled 2026-09-28)

> **Ruling (Daniel, 2026-09-28):** *"Game timings: replace with our own numbers measured from
> our own recordings, documented in the game design doc with the clips they came from.
> Nothing from Nuclear Companion."*

Every game timing is one of three things, and the table says which:

- **MEASURED**: a quantity our own station recorded, with the messages and clips below.
- **OURS**: a design choice, decided in a session and recorded in §10/§13. It is not
  presented as a measurement of anything.
- **OPEN**: no source yet. The value stays in code so nothing moves by accident, but it is
  **not final**, and it carries a `TODO` naming the question (T1 to T4 below).

### What the game's timings are

| Constant | Value | Where | What it drives in play | Status |
|---|---|---|---|---|
| Execution window | 2 s | device `Config::window_us`, server `timing.executionWindowS` | How far from T a key turn may land and still count (§4's cap depends on it) | **OPEN, T1** |
| Terminal countdown | 30 s | device `Config::terminal_us` only | The wait between a resolved vote and the drill completing | **OPEN, T2** |
| T offset | normal 300–900 s (90 %), snap 90–150 s (10 %) | server `timing.tOffset` | How long after the message T falls | OURS (§6, 2026-08-04) |
| Ack / commit cutoff | T−60 s | server `ackCutoffS` | Last moment a drill can be committed | OURS (§13) |
| Late copy | 120 s | server `lateCopyNonScorableS` | A message arriving later than T−120 s is not offered | OURS (§13) |
| Enable limit | 60 s | server `enableLimitS` | Time allowed for the enable step | OURS (§13) |
| Dead-man timer | 10 min | server `deadManS` | How long a lone vote pends before it proceeds | OURS (§13) |
| Auto-decode | 300 s | server `autoDecodeS` | An untouched banner shows its class after this | OURS (§5) |
| Stand-down / regen | 24 h / 3 days | server `economy` | Sortie economy | OURS (§10) |
| Storm cooldown | 6 h, ×0.25 | server `stormDamping` | Served, unused in v1 | OURS (§5) |
| Paper-strip print | 1.2 s | device `Config::print_us` | The print animation | OURS (cosmetic) |
| Key hold / rejoin | 300 ms / 250 ms | device `KeyTurnParams` | Gesture debounce | Bench (§13) |
| Scoring bucket / clock floor | 0.2 s / 199 ms | server `scoring` | Deviation display and miss distance | Bench (§4, §13 A.3) |
| Retarget times | 10–12 min, 15–30 min | this doc only (§6, §12) | Nothing in code | **OPEN, T3** |
| Clock drift figure | ≤1 s / 24 h | this doc only (§11, §12) | Flavour text only | **OPEN, T4** |

The execution window and the terminal countdown were the two numbers in code that the game
took from Nuclear Companion. Neither is kept as a sourced constant: both are marked OPEN,
in code and here, until Daniel rules on T1 and T2. The retarget times and the drift figure
were also taken from it; nothing in code uses them, and they are OPEN as T3 and T4.

### What our recordings measure

Source: the station's own word timestamps (whisper large-v3, `--dtw`), already on disk in
`E:\eam\full\*.segments.json`, one per message archive clip. No model was run for this.
Only messages the station graded `message` are used. Words become characters and anchors
through the station's own vocabulary (`pi-agent/nato_snap.py`, exact matches only), and
Whisper loop collapse (three or more tokens on one timestamp) is never measured. Where a
message has two cuts of the same audio, only one is used (the archive clip, else the
longest). Reproduce with `python3 scripts/game_timings.py --root /mnt/e/eam` in
valar-eam-feed; this table is its run over `messages.json` built 2026-09-28T18:20:54Z.
Word timestamps are Whisper's DTW estimates, so any single interval is good to a few
tenths of a second; read the medians.

| Quantity | How it is measured | n | Median | Range | p10–p90 |
|---|---|---|---|---|---|
| **Character slot** | one spoken character to the next, adjacent words only | 31 messages (1,547 pairs) | **1.7 s** | 1.22–2.20 s (per-message medians) | 1.34–2.02 s |
| **Standby cycle** | one "standby" to the next in the opening (a preamble reading plus "standby") | 24 cycles, 13 messages | **12.3 s** | 9.1–14.9 s | 11.1–13.9 s |
| **Standby phase** | first preamble character to MESSAGE FOLLOWS | 13 messages | **33.0 s** | 26.4–39.5 s | 27.5–39.0 s |
| **One reading** | MESSAGE FOLLOWS to I SAY AGAIN | 8 messages | **52.6 s** | 39.5–95.9 s | — |
| Second receiver | the same broadcast's clip start on the other receiver | 31 clip pairs, 17 messages | 0 s | 0–4 s | 0–2 s |
| Rebroadcast | one transmission of a message to its next, one receiver | 32 intervals, 9 messages | 30.2 min | 6.4–91 min | 10.2–57.2 min |

Notes on the last three rows. **One reading** has only 8 messages because Whisper rarely
hears both anchors in one cut; the 95.9 s is 56MIE4 (`M20260925T181819Z`), whose I SAY
AGAIN is probably a later one, so read the median, not the range. The **second receiver**
is not a delay: both receivers hear the same transmission at the same moment, and the
0–4 s is the detector's trigger and the clip id's one-second resolution. It is not a game
quantity. The **rebroadcast** interval includes transmissions we missed (the 60 and 91 min
intervals are two and three cycles); no constant uses it.

### Open questions for Daniel

**T1. The execution window (now 2 s).** As Nuclear Companion framed it, the window is a
crew concurrency tolerance, and our recordings cannot show anything on the crew side. The
nearest thing they do show, cleanly, is the **character slot: 1.7 s**, the time the reader
gives one character. Options:

1. **Adopt 1.7 s** as a design choice ("land the key inside one character's time"). This
   moves more than one number: the window's half-width becomes 0.85 s, so §4's linear
   curve tops out at 850 m and the 900 m cap (ruling 1) never binds; 1.7 s is 8.5 scoring
   buckets, not a whole number; and the device fixture and host test pin 2 s. Each needs
   its own ruling.
2. **Own 2 s** as a design number and say so. It sits at the slow end of the character
   slot (p90 of the per-message medians is 2.02 s), but that was noticed after the fact,
   so it would be a choice, not a measurement.
3. **Drop the window framing.** Scoring already uses the device's measured deviation, so
   only the accept/fail bound would need a new basis.

Found while measuring, and it needs a ruling whichever option is chosen: **the device and
the server do not agree on the window's shape.** The device accepts `[T, T+2 s]`
(`DrillMachine::ResolveTurn`: a turn before T aborts as "keyed before the window"). The
server accepts `T ± 1 s` (`votes.ts`, `halfWindowMs`), and §4's ruling 1 says `±1 s`. A
key 1.5 s after T commits on the device, then fails on the server; a key 0.5 s before T
is valid on the server but the device aborts it. Not changed here.

**T2. The terminal countdown (now 30 s).** It is launch side, which our recordings cannot
show. The nearest thing on the air is the broadcast's own wait before a message, the
**standby phase: 33 s** (13 messages, 26–40 s): the preamble read three times, each
followed by "standby", then MESSAGE FOLLOWS. Options: **adopt 33 s** as a design choice
(device only; nothing on the server or in scoring moves), **own 30 s** as a design number,
or **drop the countdown** and go straight to §7's flight.

**T3. Retarget times (15–30 min, 10–12 min; §6 and §12).** Taken from Nuclear Companion.
Nothing in code uses them (v1 is FDM-assigned targets only). They are targeting, which is
outside the editorial line, and our recordings cannot show them. Proposed: **drop them**.

**T4. The clock drift figure (≤1 s / 24 h; §11 and §12).** Taken from Nuclear Companion.
Flavour text only. Proposed: **drop it**; the game already has its own measured clock
figure, the 199 ms floor (§4).

### Evidence: the messages and clips

"Clip" is the archive clip (`pushed.json`, served at `/missileer/archive`) whose word
timestamps were measured; "Archive" is its archive id. Slot is the median character slot,
with the number of pairs. Standby cycle, phase and reading are in seconds; — means not
heard cleanly enough to measure.

| Message | Archive | Clip | Preamble | Slot s (pairs) | Standby cycle | Phase | Reading |
|---|---|---|---|---|---|---|---|
| `M20260922T010500Z` | eam-6b7a259f | `desk-M20260922T010500Z-kph-010449-183s-p2` | ACH53Z | 1.98 (33) | — | — | — |
| `M20260922T010904Z` | eam-d4e4a141 | `desk-M20260922T010904Z-kph-010901-53s` | ACBBAW | 1.54 (9) | — | — | — |
| `M20260922T150813Z` | eam-86d80b13 | `desk-M20260922T150813Z-kph-150743-230s-p2` | QHUDS2 | 2.10 (37) | — | — | — |
| `M20260922T160115Z` | eam-f27e2d58 | `desk-M20260922T160115Z-iceland-160045-429s-p2` | ACC6GX | 1.98 (88) | — | — | — |
| `M20260923T050256Z` | eam-31a26f7f | `desk-M20260923T050256Z-kph-050249-136s-p2` | ACRGIQ | 1.80 (56) | — | — | — |
| `M20260923T173253Z` | eam-356f1a01 | `desk-M20260923T173253Z-iceland-173253-126s` | AC4AUL | 1.54 (52) | 12.9, 11.2 | 31.9 | 50.8 |
| `M20260923T190247Z` | eam-36183461 | `desk-M20260923T190247Z-kph-190217-158s-p2` | ACRUZR | 1.22 (39) | — | — | — |
| `M20260923T193249Z` | eam-8e5774ca | `desk-M20260923T193249Z-kph-193244-139s-p2` | ACU6NQ | 1.54 (51) | — | — | — |
| `M20260923T193554Z` | eam-212a3f98 | `desk-M20260923T193554Z-iceland-193524-243s-p2` | VQTMAK | 1.64 (35) | 11.2 | 33.0 | — |
| `M20260923T193928Z` | eam-4fb93c48 | `desk-M20260923T193928Z-iceland-193924-38s-p2` | ACHAVK | 1.56 (10) | — | — | — |
| `M20260923T203321Z` | eam-edf34881 | `desk-M20260923T203321Z-kph-203251-132s-p2` | AC7VGQ | 1.56 (33) | 11.2, 11.3 | 32.0 | 52.9 |
| `M20260923T203822Z` | eam-1c188907 | `desk-M20260923T203822Z-iceland-203819-104s` | ACNA3M | 1.55 (32) | 11.2, 11.1 | 32.2 | 52.3 |
| `M20260923T214732Z` | eam-c93611e3 | `desk-M20260923T214732Z-kph-214732-96s` | ACZAA5 | 1.42 (37) | 11.7 | 27.5 | — |
| `M20260924T021943Z` | eam-50a93ef8 | `desk-M20260924T021943Z-kph-021943-186s-p2` | ACV3KE | 1.78 (41) | — | — | — |
| `M20260924T035208Z` | eam-484f63ba | `desk-M20260924T035208Z-kph-035138-228s-p2` | AC3QFD | 1.72 (35) | — | — | — |
| `M20260924T090156Z` | eam-c1286db5 | `desk-M20260924T090156Z-kph-090126-216s-p2` | QHCPBK | 1.70 (53) | — | — | — |
| `M20260924T091810Z` | eam-8e55b9b0 | `desk-M20260924T091810Z-iceland-091740-142s-p2` | QHMUSD | 1.82 (18) | — | — | — |
| `M20260924T093155Z` | eam-028fb7df | `desk-M20260924T093155Z-kph-093125-250s-p2` | QHSI7J | 1.68 (65) | 12.7, 12.3 | 36.2 | 57.2 |
| `M20260924T153003Z` | eam-52a62382 | `desk-M20260924T153003Z-iceland-163317-179s-p2` | QHGCGK | 1.62 (49) | 13.0, 13.9 | 39.5 | 62.8 |
| `M20260924T154010Z` | eam-e987fde1 | `desk-M20260924T154010Z-kph-153944-186s-p2` | 56YJ2I | 1.26 (22) | — | — | — |
| `M20260924T170328Z` | eam-ed75798a | `desk-M20260924T170328Z-iceland-170325-208s-p2` | QHJY7J | 1.74 (54) | 11.6, 12.5 | 28.6 | — |
| `M20260924T211959Z` | eam-103c0101 | `desk-M20260924T211959Z-kph-211956-173s-p2` | QHAWC5 | 1.82 (61) | 14.7, 14.9 | 39.0 | — |
| `M20260925T143430Z` | eam-66387c6e | `desk-M20260925T143430Z-kph-143400-225s-p2` | 56ZF26 | 1.80 (44) | 11.7, 12.4 | 34.6 | — |
| `M20260925T181819Z` | eam-2d82e0de | `desk-M20260925T181819Z-kph-181749-253s-p2` | 56MIE4 | 1.40 (89) | — | — | 95.9 |
| `M20260925T195946Z` | eam-2d82e0de | `desk-M20260925T195946Z-kph-195916-205s-p2` | 56MIE4 | 1.34 (57) | — | — | — |
| `M20260926T035338Z` | (not pushed) | `desk-M20260926T035338Z-kph-035308-371s-p2` | 563HBW | 1.73 (100) | — | — | — |
| `M20260926T130220Z` | eam-c1cce232 | `desk-M20260926T130220Z-kph-130150-299s-p2` | QH54V4 | 2.02 (64) | 13.7, 13.8 | 37.4 | — |
| `M20260926T133512Z` | eam-2122b29c | `desk-M20260926T133512Z-kph-133442-213s-p2` | VQR5NV | 1.92 (53) | 12.2, 13.4 | 36.2 | 44.9 |
| `M20260926T135341Z` | eam-f68719e2 | `desk-M20260926T135341Z-kph-135311-255s-p2` | 56VK2V | 2.09 (60) | — | — | — |
| `M20260926T215637Z` | eam-487597f2 | `desk-M20260926T215637Z-kph-215607-188s-p2` | QHRSNB | 1.28 (68) | 9.1, 9.2 | 26.4 | 39.5 |
| `M20260927T130152Z` | eam-d8c099b0 | `desk-M20260927T130152Z-kph-130122-221s-p2` | QHSNJF | 1.68 (67) | — | — | — |
| `M20260927T131754Z` | eam-c8731e05 | `desk-M20260927T131754Z-kph-131724-237s-p2` | QHIXMC | 2.20 (35) | — | — | — |

ACBBAW's cut has 9 pairs and is left out of the per-message slot median (the floor is 10).

**Second receiver** (the clip pairs are in the script's `--out` JSON): M20260922T150813Z,
M20260922T160115Z, M20260923T190247Z, M20260923T193249Z, M20260923T193928Z,
M20260923T203321Z, M20260923T214732Z, M20260924T021943Z, M20260924T090156Z,
M20260924T091810Z, M20260924T093155Z, M20260924T153003Z, M20260924T154010Z,
M20260924T170328Z, M20260924T170650Z, M20260924T184310Z, M20260924T211959Z.
**Rebroadcast**: M20260922T010500Z, M20260923T190247Z, M20260924T035208Z,
M20260924T153003Z, M20260924T154010Z, M20260924T170328Z, M20260924T170650Z,
M20260925T000043Z, M20260926T135341Z.

## 13. Decisions & remaining work

### Decided 2026-08-05 (walkthrough session)

| Decision | Call |
|---|---|
| Ack/commit cutoff | **T−60 s** (snaps stay playable for the quick) |
| Miss cost | **Tiered stand-down** (see §10 table) |
| Dead-man timer | **10 minutes** |
| Enable time limit | **60 seconds** |
| Regen rate | **1 bird / 3 days** |
| Seasons | **Monthly proficiency cycle + annual championship** (SCP rotates monthly; impact map wipes annually) |
| Onboarding | **Certification required** — one full drill on EXERCISE traffic before first live commitment. **Built from existing parts, no bespoke training animation**: the live sequence in EXERCISE dress (amber, per §11) + instruction chrome + a game card on the Reference screen + an on-device attract-mode demo |
| Impact map | **Seasonal accumulation** — strikes persist all season, wiped at championship |
| Late copy | **<2 min to T = non-scorable** for that device (no offer, no penalty) |
| Event feed | **SSE + ntfy** (§9b) |
| Audio capture | **Squelch-triggered clips** (§9b) |
| State authority | Server-authoritative votes/launches/records (default adopted) |
| Callsigns | Length/charset limits + light profanity filter (default adopted) |
| Self-crewing | Allowed, publicly attributed (default adopted) |
| Augmentee | Allowed, logged as AUGMENTEE in credits (default adopted) |

### Measured on the bench — touch report interval and the 10 s hold (2026-09-24)

**Board:** the Missileer board, a Kit S3 1.28" with a CST816T, USB serial `90706931E9D8`.
**Firmware:** valar-scopes `feat/game-client` @ `325cb69`, built with `-DKEYTOUCH_BENCH -UFEATURE_USB_OPEN`.
**Estimator:** INT-driven only, with no timer reads. The ISR stamps each falling edge of the
chip's INT line with `esp_timer`, and the interval is edge to edge while the finger is down
(`src/eam/KeyTouchSampler.cpp`, `src/game/TouchCadence.h`).

**The chip, as shipped** (read at boot):
- `IrqCtl` (0xFA) = `0x20`: change-only, one pulse on press and one on release. The firmware
  now writes `0x60` (EnTouch|EnChange, one pulse per report) and reads it back.
- `NorScanPer` (0xEE) = 1.
- `AutoReset` (0xFB) = 50 s and `LongPressTime` (0xFC) = 60 s.
- `DisAutoSleep` (0xFE) = 1.

**Report interval** (Daniel: bezel drags with lifts, then holds; 5,357 intervals over two windows):

| window | intervals | mean | min | p95 | max | silences | drops |
|---|---|---|---|---|---|---|---|
| 1 | 1,173 | 13.68 ms | 13.09 ms | 14.00 ms | 17.93 ms | 0 | 0 |
| 2 | 4,184 | 13.75 ms | 13.09 ms | 15.00 ms | 34.49 ms | 0 | 0 |

**The panel reports every ~13.7 ms (about 73 Hz).** That is the resolution a key turn is
scored at on this board, replacing the ~48 ms render pass it was read at before. Each I2C read
of a report took 408–416 µs on average, 2.6 ms at worst.

**The 10 s hold: held.** One touch lasted **10,228 ms** with no release inside it and no
silences. That covers every candidate for the breaks seen in bench run 1:
- **AutoReset: ruled out by the register.** It is 50 s, and LongPressTime 60 s, both far past a
  10 s hold. No change is needed in the game build.
- **HID typing: ruled out for run 1's first break.** That break (17:29:10.911, ~0.2 s) came
  1.5 s before any typing started. Run 1's samples never stalled while it typed (worst gaps
  20.16 and 20.00 ms). Run 2 had the long press compiled out and held for 10.2 s.
- **Phantom releases from timer polling: the remaining explanation for run 1.** Run 1 read the
  chip on a 20 ms timer (the chip was in change-only mode, so there were no reports to read
  on), which is the between-report read the variant notes record as returning zero contacts
  mid-touch. Reading only on the chip's report signal, run 2 recorded the 10.2 s touch whole.

**Not yet attributed:** three gaps between touches in run 2 were shorter than 250 ms (107, 214
and 226 ms, at 18:34:02–18:34:10 UTC), and run 1's break was ~0.2 s. Whether those were
Daniel lifting mid-drag or the panel dropping the finger is not in this log; it needs Daniel's
account of those seconds, or a run with a timed, known lift.

**Release debounce: provisional 250 ms, BOUND, NOT MEASURED** (ruled 2026-09-25).
`KeyTurnParams::rejoin_us` (`src/game/KeyTurn.h`) is 250 ms, up from 100 ms: a lift shorter
than that is a dropout, not a release. It is a bound chosen to cover run 2's unattributed gaps
(107, 214 and 226 ms), not a number measured from any run. The data that will replace it is the
release log (`[keytouch] release: held=… gap=…`, every release). A debounce shorter than a real
lift misses nothing; one longer than a real lift merges two strokes.

**The test that replaces it, in two parts.** Daniel runs both on a `KEYTOUCH_BENCH` build:
- **Run A:** 30 s of continuous drag with no lifts. Every release in it is a dropout.
- **Run B:** ten deliberate lifts. Every one is a real lift.

Before each run the bench build prints a serial prompt. Daniel answers `A` or `B`, and `E`
ends the run. Every release and window line is tagged with its run (`run=A#1`, `run=B#2`), and
the end line gives the run's release count. The constant goes between run A's longest
dropout and run B's shortest lift. **It is not set from bench run 1's data** (the timer-polling
run above), and until both runs are in, 250 ms stands, marked as a bound.

### Remaining — build tasks, not decisions

1. **Deputy hold gesture prototype** (gates crew layer): test firmware — draw switch, 10 s
   hold, log touch events, report dropout rate. Same build also measures **NTP sync
   accuracy** (sets honest scoring granularity for the deviation leaderboard).
2. **Firmware UI review** — how the game face coexists with the current monitoring face.
   **Done 2026-08-04: [missileer-game-ui-review.md](missileer-game-ui-review.md).** Headline:
   the dwell-timed carousel (8 s/screen, 30 s touch hold) actively defeats a 5–15 min sortie,
   so the launch face must be an **overlay + a mode**, never a rotation screen; auto-rotation
   and auto-dim suspend for the sortie's lifetime; interrupt precedence needs stating
   explicitly; and scoring granularity is floored by **input latency as well as NTP**. Four
   open design calls are listed there, incl. whether a preempting real EAM voids a committed
   sortie (§4 vs §10).
3. **Blipscope URL migration** (prompt delivered 2026-08-04) — lands before Missileer web
   surfaces.

### Remaining — content & playtest tunables

~~Deviation→distance curve~~ — **RATIFIED LINEAR 2026-08-06** (§4). "Start linear, small
deadzone" was this line; it is now the specification and the implementation, and the constants
live in `valar-eam-feed`'s `GAME_SCORE_*` config so tuning it is not a build. Still open:
message-class weights (tune from fleet telemetry, §5) · payload roster (§7 escape hatch) ·
codename wordlists · achievements · certification drill script · inhibit-reason dropdown list.

### Superseded — original open-questions list (for the record)

Settled 2026-08-04 (details inline): T offset (5–15 min + rare ~2-min snaps) · solo mode
(lone vote + dead-man timer, shared ladder) · v1 targeting (FDM-assigned only) · wing
assignment (choice, populations shown) · fleet witnessing (ambient, participants
full-screen) · attribution (callsigns on crew/seconder/inhibitor + reason dropdown) ·
URL convention (/{edition}/{surface}, Blipscope 301).

### A. Gates the v1 solo build

1. **Ack/commit cutoff** — proposed: commitment allowed from decode until **T−60 s**;
   snaps demand an instant decision by design.
2. **Missed execution consumes the sortie?** — proposed: **yes**. Ack is optional; don't
   commit what you can't execute. Makes commitment dramatic.
3. **NTP sync vs ms scoring** — **REOPENED 2026-08-11.** Partly re-closed; one defect remains.

   - ~~*CLOSED 2026-08-05. Measured at 75.7 ms (worst correction, 6 syncs / 17 h) — above the
     ±10–50 ms this anticipated. Granularity set to 0.1 s per the amendment above. Input floor
     (43 ms) is not binding.*~~ **SUPERSEDED.** Retained because a wrong number somebody acted
     on is part of the record.
   - **The clock premise was wrong — now closed.** 75.7 ms came from 5 corrections over 17.1 h
     in a session whose `poll_max_ms` was 20042 (the TX-ring freeze). A cleaner, longer session
     — same board, same statistic, same 3 h cadence, `poll_max_ms` 45 — recorded 8 corrections
     with a worst of **198.5 ms**, and *seven of the eight exceed 75.7*. It was not a floor; it
     was a small sample's lucky low. Clock floor is now **199 ms** and the deviation bucket
     **0.2 s**. Evidence is committed at `test/fixtures/ntp-corrections-2026-08.json` in
     valar-eam-feed — do not restate the numbers in prose here, where they can drift.
   - **The arm comparison was never re-measured — STILL OPEN.** Arms B and C ran exactly once,
     on the *broken* build (session 2026-08-05 0734, before `4c504f2` fixed the swipe/arm
     unreachability). That is evidence about a build we deliberately replaced. The 26.8 h
     corrected session ran arm A only (`B runs 0`, `C runs 0`) because cycling the arm needs a
     tap above y=50 and none of its 14 touch samples landed there. **A.3 does not close on the
     clock alone.** A forced-arm bench path now exists (`-DGAMETEST_FORCE_ARM`, serial a/b/c) so
     B and C can be driven deterministically; runs from it log `forced,1` and must be reported
     as forced — fine for measuring the arm, not fine for measuring how often a user reaches it.
4. **Feed latency fairness** — proposed: message arriving with **<2 min to T** is
   non-scorable for that device (no commitment offered; no penalty, no temptation).
5. **State authority** — proposed: server-authoritative votes/launches/records; devices
   report events; trust the 50-unit fleet, revisit at scale.
6. **UI coexistence with current monitoring face** — needs a firmware/repo review, not a
   design decision.

### B. Gates the crew layer

7. **Deputy hold gesture** — the known one. Prototype first; a flaky multi-second hold on
   the CST816 poisons the whole crew mechanic. Test firmware: draw switch, 10 s hold,
   log touch events, report dropout rate. The number makes the decision.
8. **Crew formation flow** — largely resolved by the two-seat capsule structure (§8):
   your capsule-mate IS your standing crew, default partner on mutual ack. Remaining
   design: cross-capsule fill-in when your capsule-mate is absent (an "augmentee" from
   the squadron takes the empty seat for one launch — proposed: allowed, logged in the
   credits as AUGMENTEE); seat/role swap rules within a capsule.
9. **Self-crewing** (one owner, two devices) — proposed: **allowed and publicly
   attributed** — the same callsign twice in the credits is the joke, and the fleet sees it.
10. **Dead-man timer length** — inhibit economics largely dissolved by squadron-scoped
    voting (§9): the five-person vote network self-polices via attribution + party line.
    Remaining proposals: timer **10 min**; inhibited sortie **returned** to inventory;
    reason always logged; one inhibit per capsule per message as a backstop.
11. **Enable minigame time limit** — proposed: **60 s**.

### C. Gates the web launch

12. **Event channel** — ntfy vs WebSocket/SSE on the scopes server. Proposed: SSE for live
    vote/witness state; ntfy stays for impact pings & alerts.
13. **Audio pipeline** — squelch-trigger vs continuous+trim on the Pi; clip format (Opus);
    retention (keep all — storage trivial).
14. **Callsign rules** — public pages ⇒ length/charset limits + light profanity filter.
    Boring, mandatory.
15. **Blipscope URL migration lands first** (prompt delivered 2026-08-04).

### D. Content & tunables (non-blocking)

16. Season cadence — proposed in §9 (monthly proficiency + annual championship);
    **awaiting sign-off**.
17. ~~Deviation→distance curve~~ **CLOSED 2026-08-06 — linear, ratified (§4).** Remaining:
    payload roster · codename wordlists · achievements.
18. **Onboarding gate** — proposed: short in-fiction **crew certification** (practice
    drill on EXERCISE traffic) required before first live commitment.
19. **Persistent impacts** (new idea, 2026-08-04) — proposed: impact markers accumulate on
    the shared fleet map through the season (the world map slowly fills with
    shredded-paper strikes — the season's collective artifact); wiped at championship
    reset.

## 14. Backlog mapping

**#135 (gamify EAM reception):**
- Passive-scoring objection → resolved: ratio scoring + ack-as-commitment + deviation
  metric (§4). Leaderboard scores humans, not antennas. Record **votes**, not launches, in
  the event schema from day one (renaming later is a migration).
- Tone product call → resolved: player-escalates principle + remote hold flag (§1).
- Backlog seeding → resolved by construction (§4).
- Dwell/interruption → resolved: real traffic preempts; it's a mechanic (§4).

**#136 (shredded-paper pack-in):**
- New tie-in: paper = *recovered payload debris* from the shredded-paper warhead (§7).
- Constraint stands: invented unit names only; the box must obviously read as a joke;
  candidate: the edition disclaimer (§1.5) printed on the box.
- Adjacent pack-in idea: perforated "authenticator card" deck (break-the-seal ritual,
  invented codes).

**Suggested new issues:** core loop (§3) · scoring (§4) · message taxonomy + T derivation
(§5–6) · crew pairing & roles (§8) · fleet vote/inhibit layer (§9) · visual spec (§11) ·
deputy-gesture hardware prototype (§13.3).

## 15. Source register

| Source | What it gave |
|---|---|
| USAF missile combat crew photo (public domain) | Art bible: TODC clock, printer, padlocks, binders, fisheye composition, seating, tone register |
| Nuclear Companion, *"REACT: Armageddon with a floppy disk and trackball!"* (Paul Dent, 2023) | Six-step loop skeleton; split-knowledge enable (6 chars, 3+3); NAM/FDM/EAM taxonomy; vote/inhibit/dead-man triad; MRT vs CEP retarget; console components (VDU/RMP/WSP/LEP/LCP/CLS/OID/TODC/FDD); the disclaimer template. Comments = folklore, not source. **No game timing comes from this source** (ruled 2026-09-28, §12a); the retarget times and drift figure it gave are open (T3, T4). |
| Wikipedia, *Rapid Execution and Combat Targeting System* | 1 key + 3 cooperative switches; auto EWO processing; retarget times; REACT era choice |
| Wikipedia, *Missile launch control center* | LCC hierarchy (ACP/SCP/PLCC) + countermand authority (rank system); HVC party line (squadron social unit); capsule physical details; SAS safe w/ two crew locks; 2-person crew; Netlink |
| Wikipedia, *LGM-30 Minuteman* | Specs table (range/speed/CEP); MIRV flight sequence; wing/squadron structure + nicknames; ALCS/Looking Glass; ERCS; surviving museum sites (Delta-01/-09, Oscar-Zero, November-33, Quebec-One — reference-photo targets); Sentinel timeline |
| Wikipedia, *Twentieth Air Force* | Org umbrella over the three wings → Higher Authority fiction + rank-ladder top; 582nd Helicopter Group (UH-1N field security) → idle-screen flavor. WWII lineage deliberately not used. |
| Wikipedia, *90th Missile Wing* | 3 squadrons × 5 flights = 15 flights/wing (A→O), confirms 45-MAF structure → capsule-picker schema; tri-state field ~9,600 sq mi; first-ICBM-base lineage; Peacekeeper exclusivity (400th MS) → wing cosmetic; Omaha Trophy accolades → wing-trophy shape; Quebec-01 on-base |
| Wikipedia, *341st Missile Wing* | **Olympic Arena + Blanchard Trophy** (wing-competition model); motto *Pax Orbis per Arma Aerea*; first-Minuteman-wing heritage (Alpha-9, 1962; 1,000th missile; only LGM-30A wing; 564th MS fourth-squadron era); Rivet MILE (maintenance-cycle name shape); monthly proficiency testing as institution. Notable-incidents section is explicitly NOT used (tone rule §1.6). |
| Wikipedia, *91st Missile Wing* | "Roughriders" / *Poised for Peace* identity kit; 740th/741st/742nd structure confirmed; Minot dual-nuclear-base flavor (5th Bomb Wing B-52s share the base) |
| minutemanmissile.com, *Missile Guidance System* | Guidance lineage (NS-10Q/D-17B drum computer → NS-17/D-37C → NS-20/D-37D → GRP); gyros spin continuously on alert; platform alignment vs drift → alignment/upkeep mechanic (§6); retro-computing flavor. Enthusiast secondary source — same tier as Nuclear Companion. |
| NPS Minuteman Missile NHS + state historic sites | Standing citable references; key-turn shape (turn-and-hold); future reference-photo source |
| priyom.org HFGCS page + mt-milcom EAM primer | EAM arrival-rate characterization ("very common," daily 24/7/365, ~30-char strings); repeats/rebroadcast behavior → dedupe requirement (§5) |
| **Northrop Grumman 2007 Minuteman III flight-sequence video** | The §11 locked beat sheet: ascent over a sinking Earth limb, axial separations, staging beat (burnout→sep→coast→ignition), clamshell shroud, PSRE pitch-over, silent RV release, bus backing away. **Commercial — linked from /missileer/sources, never rehosted** |
| **AiTelly flight-sequence explainer** | Secondary corroboration of the staging order and post-boost behaviour; same tier as Nuclear Companion (enthusiast) |
| **Wikipedia, MIRV-path diagram** | The bus/RV release geometry that makes "releases in the direction of travel" checkable rather than asserted |
| **minutemanmissile.com, MIRVs/RVs page** | Single-RV loading documented since 16 Jun 2014 → §5 RV-count default of 1 |
| **TWZ + CSIS, post-New-START upload-readiness reporting (2026)** | Why RV count is a server config rather than a constant — the real posture can change without a firmware build |
| **AEC test photography — Plumbbob Hood, Upshot-Knothole Badger** | Detonation fire palette (§11). Public-domain US government imagery → embeddable on /missileer/sources |
| **1994 detargeting agreement + Glory Trip / Broad Ocean Area practice targets** | §6b v1 default: the game's aim point is the real force's practice aim point, and it is ocean. **Glory Trip** is the published name of the operational test-launch programme those shots fly under — the same GT series the backend already tracks (`valar-eam-feed`, `data/glory-trips.json` → `/launches/icbm`), so the game's v1 target and the launch feed the device already polls cite one programme |
| **NUKEMAP (Alex Wellerstein)** | Precedent for the §6b posture — a decade of public arbitrary-target placement as an education tool; the product picks what it *publishes*, not what the player aims at |
| **GMD interception test record (~50 %)** | §6b Tier 2 defense odds — the published record used as the actual probability, so the least believable part of duels is the sourced part |
| **Minimum-energy Lambert time-of-flight** | §7 TOF model; validates 6,700 km → 24.9 min, 9,700 → 31.6, 14,000 → 38.5, so the "≈30 min" figure is derived rather than asserted |
