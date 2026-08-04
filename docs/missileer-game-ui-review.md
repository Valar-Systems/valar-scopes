# Missileer: game UI review

Firmware/repo review for [missileer-game-design.md](missileer-game-design.md) §13
build task 2. **No code changes** — an inventory, proposed attachment points, and
the calls that need making, flagged as such.

Supersedes the earlier `missileer-ui-coexistence.md` (folded in here).

---

## 1. Shell inventory

What exists today, in [`src/eam/`](../src/eam/).

### Screens

Ten, in [EamManager.h:45](../src/eam/EamManager.h#L45):

```
Ticker, Tempo, Activity, Codewords, Abncp, MilAir, Propagation, Icbm, Reference, Clock
```

**Clock is the idle screen** — it is the initial `current` ([EamManager.h:66](../src/eam/EamManager.h#L66)) and `HasData()` returns `true` for it unconditionally, as it does for `Reference` (static help) and `Abncp` (always meaningful: airborne / none / needs-creds). Every other screen is **feed-gated** and drops out of the rotation when its feed is empty.

The Clock also carries a **rotating ambient stat** (`ambientIndex`, [EamManager.h:74](../src/eam/EamManager.h#L74)) — relevant below, because that is the line a countdown strip would compete with.

### Rotation, dwell, swipe

[EamManager.cpp:20-21](../src/eam/EamManager.cpp#L20-L21):

```cpp
constexpr unsigned long AUTO_DWELL_MS    = 8000;   // 8 s per screen
constexpr unsigned long INTERACT_HOLD_MS = 30000;  // pause 30 s after a touch
```

`AutoRotate()` ([:230](../src/eam/EamManager.cpp#L230)) returns early if either timer says so. Swipe is left → next, right → previous ([:261](../src/eam/EamManager.cpp#L261)); there is **no tap verb at all** in the shell today — touch is purely navigational.

Order and enable/disable come from **`eam-screens`, a CSV of string ids** ([:40-42](../src/eam/EamManager.cpp#L40-L42)), resolved by `idToScreen()`. Empty config = all screens, in enum order ([:62-63](../src/eam/EamManager.cpp#L62-L63)).

> **Adding enum entries is safe for saved config.** Because order is persisted as *ids*, not indices, a new entry cannot shift anyone's stored preferences. But note the two populations diverge: a user with **empty** config gets the new screen automatically; a user with an **explicit CSV** never sees it until they edit. That asymmetry needs a deliberate answer for the sortie board.

### NEW pulse

[EamManager.cpp:140-145](../src/eam/EamManager.cpp#L140-L145), on a fresh top-of-feed EAM, does **four** things:

```cpp
newPulseUntilMs = millis() + 2000;   // 2 s "NEW" pulse
current = Screen::Ticker;            // navigates
lastInteractionMs = millis();        // fakes a touch -> 30 s rotation hold
tickerScroll = 0;                    // rewinds the marquee
```

> **Correction (2026-08-04).** An earlier revision of this section said the pulse "does not
> navigate, interrupt, or extend dwell." That was wrong on all three counts, and it is the
> baseline §6.6 was decided against. The shipping monitor **already** yanks the screen to the
> ticker and pins it there for 30 s. The pulse is not a screen-agnostic flash either — it
> renders only inside `DrawTicker` ([EamScreens.cpp:62](../src/eam/EamScreens.cpp#L62)), which
> is consistent: the device jumps first, then pulses where it landed.

**The backlog is already edge-seeded.** [EamFeedClient.cpp:276](../src/eam/EamFeedClient.cpp#L276)
requires `!lastTopId.isEmpty()`, so the first poll after boot only records the top id and
raises no edge. Anything hung on this hook inherits that for free.

It remains the only place in the shell that knows "a real EAM just arrived", which makes it
the natural preempt hook (§3).

### Brightness / night-dim

`MaybeAdjustBrightness` ([:270](../src/eam/EamManager.cpp#L270)) drops brightness at night when `autoDim` is on and a location is set. Nothing can currently suppress it.

---

## 2. Attachment points

### 2a. Countdown-to-T strip — on the Clock screen

Natural fit: §11 wants the TODC as red 7-seg Zulu with the countdown "in the same face", and [`SevenSegment.cpp`](../src/eam/SevenSegment.cpp) already renders exactly that.

**Conflict to resolve:** the Clock already rotates an ambient stat in roughly the space a countdown strip wants. Options — (a) countdown *replaces* the ambient line while a sortie is pending, (b) it takes a third band and the ambient line shortens, (c) the ambient rotation freezes while counting down. **(a) is my recommendation**: the countdown *is* the ambient stat when one exists, and it keeps the layout count unchanged on a 240 px round face.

Because Clock is always in rotation, this surface is visible without the player doing anything — which is correct. It is passive information, not the game.

### 2b. Sortie board — a new `Screen` in the enum and the rotation

This belongs in the rotation, and it is the one game surface that does.

- `HasData(Screen::Sortie)` = **"is there a live or offered sortie?"** That is exactly the existing feed-gating idiom, and it means the board **auto-hides when nothing is pending** — the shell already does this for `Icbm` ("hidden when no upcoming launch"), which is the same shape.
- Append **before `COUNT`**, add an id to `idToScreen()`.
- Decide the default-visibility asymmetry noted above.

### 2c. Launch sequence — modal takeover, outside the rotation

**Entered only by a player tap**, per §1.1: *"Everything game-shaped happens only because a human chose to run their drill on it. The device never auto-animates off real traffic."* Entry is a deliberate act; nothing may enter this surface on the device's initiative.

This must **not** be a `Screen`, and the reason is arithmetic rather than taste:

> T is 5–15 minutes out (§6), the commit cutoff is T−60 s, execution lands in a published 2-second window (§3 step 4), and the score is `|key − T|` in milliseconds (§4). Meanwhile the carousel advances **every 8 s** once the 30 s touch hold lapses. Commit at T−10 min, touch nothing for 30 s, and the device cycles screens for the next nine and a half minutes, leaving you somewhere in the rotation at T.

Stretching `INTERACT_HOLD_MS` to cover a sortie is not a fix — it would freeze the monitor for ten minutes after any stray touch and break the product that already works.

**The pattern already exists in this repo**: the radar's detail card sets `inDetail`, draws over whatever screen is current, and suppresses navigation. `IsRadarView()` is `screen == Radar && !inDetail`, and `DetailCardOpen()` was recently split out precisely so callers stop conflating "which screen" with "is a modal up". Same shape here.

While the modal is up:

| Behaviour | While a sortie is live |
|---|---|
| `AutoRotate()` | **suspended outright**, not delayed |
| `MaybeAdjustBrightness` | **suspended** — a night-dim at T−5 s reads as the device failing at the decisive moment (the radar has precedent: a visual alert takes over brightness and releases it on dismissal) |
| Ambient/Clock cycling | frozen (see §4 below) |
| Swipe | **open call** — see §5 |

---

## 3. Interruption semantics

### Where the hook goes

**At [EamManager.cpp:141](../src/eam/EamManager.cpp#L141)**, where `newPulseUntilMs` is set. That is already the single place in the shell that knows a fresh EAM has landed, and it is already the "something real just happened" signal. Adding a second detection path would create two sources of truth for the same event.

### (a) New real EAM during rotation

Today: a hard jump to the ticker plus a 30 s hold (see the correction in §1). §4 says *"new real traffic **preempts** a running sequence — real-world traffic takes precedence."*

So the shipping monitor already implements the strongest available reading of "real traffic wins" for the plain rotation case. The live question is not whether to *add* an interruption — it is whether that interruption should stay a **takeover** or become a **print**. Settled in §6.6.

### (b) New real EAM during the modal sequence

The sharp case, and it is a **genuine conflict between two settled rules**:

- **§4** — real traffic preempts a running sequence; losing your window because the world got busy is "the game at its best."
- **§10** — a missed execution after commitment costs a tiered stand-down.

So the player is penalised for an interruption **the game itself caused**. That cannot be decided in the firmware; it is a design call. Options:

1. Preempt and **void** the sortie — no penalty, no score. Preserves §4's drama without §10's unfairness.
2. Preempt and **penalise** — §4 taken literally; harsh, and punishes the player for the device doing its job.
3. **Defer** the preempt to a NEW badge on the modal, full takeover only after resolution. Safest for the player, weakest for §4's premise.

**My recommendation is (1)**, and note §1.1 supports it: the device never auto-animates off real traffic, so a real EAM must never *start* anything — but interrupting is not animating, and voiding is the interpretation that keeps the human's agency intact.

### Interaction with the NEW pulse

Subsumed by the arrival banner (§6.6): the banner is the arrival cue on whatever screen is up, and the 2 s ticker-local pulse goes away with it.

---

## 4. 240×240 constraints, per surface

The panel is a **round** GC9A01: the corners do not exist, so the usable area is a circle inscribed in 240×240. Text at size 1 is 6×8 px; at size 6, 36×48.

| Surface | Verdict |
|---|---|
| **Countdown strip (Clock)** | Fits. 7-seg Zulu + one countdown line is two bands, which is what the Clock already renders. Recommend replacing the ambient line rather than adding a third. |
| **Sortie board** | **Flag: wants one-element-at-a-time.** A list of offered/committed sorties with class, T, and state is a table, and tables do not fit a round 240 px face — the corners eat the columns. Should render **one sortie per view** with a position indicator ("2 / 3"), not a scrolling list. |
| **Launch sequence** | **Flag: hard one-element-at-a-time, and it is a sequence of steps anyway.** §3's six steps map to six full-screen states. Do not attempt to show progress + current step + countdown + controls simultaneously. The countdown is the only persistent element; everything else is the current step alone. |
| **Split-knowledge minigame (§3 step 3)** | **Flag: worst case on this panel.** Six characters, of which the player keys three, under a time limit, on a round 240 px screen with a single-touch digitizer. Needs its own layout study — a keypad is not obviously feasible and the entry method may have to change (rotary select? bezel drag?). |
| **Key-turn arc (§3 step 4)** | Fits, and suits the round panel — a press-drag along the bezel is the one interaction the circular form factor actively flatters. **Gated on the hold test**: it is sustained contact, which is exactly what the driver may lose (finding (b)). |

---

## 5. `-DFEATURE_EAM_GAME` gating

Cleanest boundaries, matching how the repo already separates products:

**TU boundary — `src/eam/game/`.** Add a `[filters]` fragment mirroring `hwtest_off` / `gametest_off`:

```ini
eamgame_off = -<eam/game/>
```

Append it to the EAM env's filter, and add a `missileer-game-*` env that re-includes `+<eam/game/>` with `-DFEATURE_EAM_GAME`. This follows the established rule that a product's TUs are excluded by default and re-included by exactly one env, so the game cannot leak into the shipping monitor image.

**Screen enum.** Guard the entry inside the enum:

```cpp
enum class Screen : uint8_t {
    Ticker, ..., Reference, Clock,
#ifdef FEATURE_EAM_GAME
    Sortie,
#endif
    COUNT
};
```

Safe because order is persisted by **id string**, not index (§1). `idToScreen()` and `HasData()` need matching guards.

**Config page.** `ConfigurationWebServer.cpp` already branches per product (`#ifdef FEATURE_EAM`). The game's settings belong in a nested `#ifdef FEATURE_EAM_GAME` block *inside* the EAM branch, since the game is an EAM sub-mode rather than a sibling edition. Whatever is added must stay inside the single whole-form POST — `scripts/check-config-form.py` enforces this and will fail the build if the game section becomes its own form.

**The remote hold flag (§1.2)** is a *runtime* cloud config bit, not a build flag. It must revert a fleet already running game firmware to pure monitoring, so it cannot be `FEATURE_EAM_GAME` — it gates at the same place the modal is entered.

---

## 6. Calls — decided 2026-08-04

**All calls answered; count is zero.** §6.6 closed the last one. Two items carry forward as
implementation notes rather than open questions: the banner replaces an existing behaviour
(§6.6 note 1) and needs a ≤ 15-char config key (§6.6 note 3).

### 6.1 Preemption: suspends, never cancels

Real traffic **suspends** the sequence. The commitment stands and re-entry is allowed right
up to T. If T passes and a preemption occurred **anywhere inside the commit window**, the
miss is exempt — bird returned, no ratio hit, logged `PREEMPTED` not `FAILED`.

**No could-they-have-made-it-back adjudication**; any preemption in the window exempts. That
is the load-bearing half: the alternative is a judgement about whether the player had time
left, which is unanswerable and unappealable, and would make the fairest-sounding rule the
most arbitrary one in the game. Design doc §10 table patched to match.

### 6.2 Swipe: two regimes

| Regime | Swipe | Countdown |
|---|---|---|
| Commit → modal entry | carousel runs **free** | rides as **persistent chrome on every screen** |
| Inside the modal | **blocked** | the face itself |

Modal exits are exactly three: **completion**, **real-traffic preemption**, or a deliberate
**long-press ABORT**. Abort costs what a miss costs (stand-down + ratio) and is logged
`ABORTED` — ack is a promise, so breaking it deliberately costs what breaking it by accident
costs, and the honest label is the only difference.

> Note the chrome requirement is a real change: the countdown must render on **every** screen
> between commit and modal entry, which means a shell-level overlay rather than a per-screen
> element. Cheapest place is the same draw tail every screen already returns through.

### 6.3 Auto-dim: inhibited for the sortie's lifetime

`MaybeAdjustBrightness` gets a live-sortie inhibit — full brightness from commit to
resolution. Same shape as the radar's visual-alert brightness override.

### 6.4 `Update()` ordering

```
real-traffic intake  ->  preemption check  ->  modal input  ->  carousel
```

Real traffic always wins; the modal never starves message processing. Preempt hook stays at
the `newPulseUntilMs` assignment — one event, one source of truth.

### 6.5 Scoring granularity

`max(NTP uncertainty, key-window input latency)`, where the key window runs **arm-C-style
direct high-rate polling**. Measure the input floor *inside a focused poll window*, not the
idle product loop — **the 45 ms idle figure is the wrong floor** and would have set the
bucket an order of magnitude too coarse. The leaderboard displays the smallest bucket the
measured floor supports. Added to the results template as a fourth number.

### 6.6 Arrival banner — decided

A fresh EAM **prints a teletype-style arrival banner into a band of the current screen**:
message type, first group(s), frequency. Not gated behind `FEATURE_EAM_GAME` — this is a
monitoring improvement for all EAM devices, and it is design doc §11's *"messages print, not
pop"* arriving early. Default-on, with a config toggle.

| | |
|---|---|
| **Trigger** | the `newPulseUntilMs` assignment ([EamManager.cpp:141](../src/eam/EamManager.cpp#L141)) — the established single source of truth; the banner **subsumes** the 2 s pulse |
| **Duration** | at least one full dwell (8 s) |
| **Beneath it** | the carousel keeps running |
| **Tap** | jump to Ticker; counts as an interaction (normal 30 s hold) |
| **Swipe** | dismiss |
| **A newer EAM** | refreshes the banner |
| **Draw** | the overlay pattern (like the radar's detail card) — never joins the rotation, never navigates on its own |
| **Backlog** | edge-seeded; only messages first seen live ever print |
| **During a live sortie** | takes the band opposite the countdown chrome |
| **Inside the modal** | does not render at all — the preemption path owns real traffic there (§6.4 ordering) |

Three things the decision inherits, one of which it has to overturn:

**1. It is a removal, not just an addition.** The decision was taken against a baseline that
said the shell does not navigate on a fresh EAM. It does — `current = Screen::Ticker` plus a
30 s hold, [EamManager.cpp:140-145](../src/eam/EamManager.cpp#L140-L145). Every clause above
("carousel continues beneath", "tap → jump to Ticker", "never navigates on its own") is
incompatible with keeping that jump, and only "tap → jump" is even *meaningful* if the device
has already jumped. **Resolution applied: the banner replaces the auto-jump.** The spec is
unambiguous as a target state; it simply described a change from a baseline one revision out
of date. But this now *removes* a takeover from the shipping monitor rather than *adding* a
cue to it, which is a larger change than the one that was approved, and is called out here so
it can be reversed on sight rather than discovered on a bench.

**2. What "off" means.** The toggle reverts to **today's behaviour** — jump to ticker + 2 s
pulse — not to silence. A monitoring product should not ship a setting whose effect is
"be quieter about a real EAM", and making off ≡ status quo ante means the toggle doubles as
the fleet's rollback if the banner reads worse in the room than it does on paper.

**3. The key must be ≤ 15 characters.** `eam-arrival-banner` is 18. Form field names double as
NVS keys and NVS caps names at `NVS_KEY_NAME_MAX_SIZE - 1`; over-long keys fail **silently**
in `putString`/`isKey` — the toggle renders, appears to save, and never sticks
([ConfigurationWebServer.cpp:2614](../src/ConfigurationWebServer.cpp#L2614)). Proposed:
**`eam-arrival`** (11), consistent with `eam-colon-blink` (15, the current longest).

**Free from the existing hook:** backlog seeding. `newLatestEdge` already requires a non-empty
`lastTopId` ([EamFeedClient.cpp:276](../src/eam/EamFeedClient.cpp#L276)), so the first poll
after boot raises no edge and the backlog cannot print.

**Converges with §6.2.** The pulse renders only inside `DrawTicker` today; a banner over *any*
screen needs a shell-level overlay in the common draw tail — which is exactly the mechanism
§6.2's between-commit-and-modal countdown chrome already requires. One hook, two customers.

### 6.7 Sortie board visibility — decided

Do **not** auto-inject into an explicit `eam-screens` list; a hand-curated rotation is a user
choice. Instead:

- `HasData(Sortie)` requires `FEATURE_EAM_GAME` **and a claimed seat**, so non-players never
  see it at all.
- The config page's game section shows a one-line notice when a seat is claimed but `sortie`
  is missing from an explicit list, **quoting the exact CSV value to add**.
- Empty-config users get it automatically, as they do every other screen.

### 6.8 Split-knowledge — parked to crew scope, with constraints

Seeded so the study starts from the right place rather than from a keyboard:

1. Each seat renders and enters **only its own 3 characters** — never 6 on one panel.
2. Entry is a **rotary dial-to-character selector on the bezel arc**, not a keypad.

If the dial cannot clear 3 characters comfortably inside the 60 s enable limit, that is a
real problem and the mechanic needs rethinking rather than tuning.

## 7. Not in scope here, but load-bearing

**Scoring granularity has two floors, not one.** §13 task 1 measures NTP; that is necessary and not sufficient. Input latency is independent and not small — the harness already logged `poll_max_ms=45` in a loop doing nothing else, and it will be worse under a rendering launch face. The honest figure is `max(clock, input)`. A competitive ranking quoting single milliseconds the fleet cannot resolve is worse than a coarser honest one, because somebody will eventually try to beat it by a millisecond.

**Task 1 genuinely gates §2c and the key-turn.** The deputy's hold is by definition a finger that does not move; `IrqCtl` is `EnTouch|EnChange`; and the first bench run already showed the driver and chip disagreeing on a static contact. If the hold is unreliable on all three arms, §8's two-person rule needs redesigning around discrete taps rather than sustained contact — that is a design change, not a tuning exercise.
