# Tap-to-peek — design, locked 2026-08-28

**A card's flight, shown on the globe or arc, transiently.** Touch a detail card
whose flight has a resolvable route and the route view opens; a dwell expires or
any touch returns you to normal rotation. No config, no persistence.

**Status: DESIGN LOCKED, BUILD DEFERRED.** Every decision below is settled and
should not be reopened without a new fact. What is not settled is *when*, and
that has two gates — see [Sequencing](#sequencing).

---

## 1. The gesture is a swipe, and long-press is permanently off the table

**Entry: swipe DOWN on the detail card.**

Everything else on the card is taken:

| gesture | current meaning |
|---|---|
| tap | flip photo → data page; on page 1 or a photo-less card, close |
| swipe up | pin/track the aircraft, then close |
| swipe **down** / left / right | close |

Down is the pick because up already means *pin*, left/right carry no specific
meaning, and down does not collide with the photo-flip tap. It is repurposed from
a synonym for "close", so nothing loses a distinct behaviour.

### Why not a long-press — and why this cannot be revisited

**A hold gesture is not available on this hardware.** There is no long-press
anywhere in the firmware, and that is not an omission: the reset menu was
deliberately converted *from* a 2 s hold *to* discrete taps because

> The CST816D may report **no change interrupt under a static contact**, so a
> held finger can register as nothing at all. A destructive control whose gesture
> the panel may silently fail to see is a control that appears broken to the
> customer who needs it most.
> — [src/AircraftManager.h](../src/AircraftManager.h), the reset-menu history

Two of the four SKUs (`s3_128`, `s3_21`) use CST816. A hold would work on the
other two and fail intermittently on those, which is worse than failing
everywhere — it would present as a flaky feature rather than an absent one.

**So: do not propose press-and-hold for this or any other control on this
panel.** The constraint is the touch IC, not the design, and it does not change
until the hardware does.

---

## 2. The three-case route rule

The affordance and the face are both decided by what the route resolves to, and
the three cases want three different things:

| case | behaviour |
|---|---|
| **No route at all** (`routeOrigin`/`routeDest` empty) | **No affordance.** The swipe keeps meaning "close". A peek with nothing to draw should not be offerable. |
| **Both codes resolve to coordinates** | Globe if great-circle ≥ **4,000 km**, arc below. Same threshold as Follow (§9). |
| **Codes present, one or both unresolvable** | **Arc, never an empty globe.** |

The rule underneath, which is the part to remember:

> **The globe requires coordinates; the arc requires only strings.**

The globe has nothing to centre on without both endpoints — its basis is built
from them. The arc draws its codes from the strings and simply omits the marker,
which is the code-only degradation Follow already builds and tests. So a
resolution miss degrades to the arc rather than to a blank disc.

---

## 3. Lifecycle

- **Entry:** swipe down on a card whose flight has a route.
- **Dwell:** ~12 s. Deliberately shorter than Follow's 20 s auto-surface: this is
  transient by definition, where Follow's dwell follows a state transition the
  owner may want to study.
- **Dismissal:** **any** touch — tap or swipe — returns immediately. The timer is
  a backstop, not the primary exit.
- **Resolving:** the globe cannot draw until both endpoints resolve, and the
  fetch cannot block the loop task (85 ms frame budget; the fetch task exists for
  exactly this). So a peek opens in a *resolving* state and fills in.
- **Aircraft leaves the contact table mid-peek:** **freeze the marker** and let
  the dwell expire. Do **not** import Follow's absence machinery — the three
  absence states exist because someone is watching a specific aeroplane over
  hours. A 12 s peek has no such stake, and `SIGNAL LOST` on a glance is alarm
  without purpose.

---

## 4. Privacy — settled, and kept verbatim so it is not re-litigated

> Privacy is a non-issue here, and worth saying explicitly so it isn't
> re-litigated: the lookup is by **airport code**, for an aircraft the radar is
> already displaying and already enriching. C2's prohibition was about naming a
> *follow target* — a persisted private preference — which this isn't.

The distinction that does the work: a follow target is a **stated preference**
that exists only in the owner's config, so a request naming it discloses
something we would otherwise never know. A carded aircraft is one the device is
already fetching enrichment and a photograph for; an airport-code lookup adds no
disclosure that those requests did not already make.

---

## 5. Scope — reuse vs new

### Reuse (all currently on `feat/follow-mode`)

| piece | reuse |
|---|---|
| `include/GlobeProjection.h` | **100%.** The extraction already made the basis a per-route parameter, so an arbitrary flight's route needs no change at all. |
| `include/FollowArc.h` maths | **100%.** Great circle, bearing, progress, interpolation. |
| The 4,000 km threshold and the router shape | 100%. |
| `DrawFollowGlobeFace` / `DrawFollowArcFace` | Needs a **signature refactor**, not a rewrite — see below. |

### The signature refactor is merge cleanup, not peek work

Both faces currently read `followRouteOrigin` / `followRouteDest` / the follow
machine's state off the manager. They should take their inputs:

```cpp
void DrawRouteGlobe(BandCanvas&, const follow::Endpoint& org,
                    const follow::Endpoint& dst,
                    float acLat, float acLon, follow::State st);
```

**That is the right shape whether or not peek is ever built** — a face that reads
global-ish members is a face with one possible caller. Do it as part of Follow's
merge cleanup so peek is a caller rather than a refactor.

### New

- **An `ap:` device client.** The firmware has **no** `ap:` client today — the
  endpoint shipped 2026-08-28 is server-side only and nothing on the device calls
  it. This is the real cost: a new request kind on the shared HTTP client, a
  parse, and a small resolved-endpoint cache so repeat peeks do not refetch.
- **Peek state machine** and the gesture binding.
- **Copy** for the resolving and unresolvable states.

---

## Sequencing

Peek waits on **two** things, and only one of them is a decision:

1. **Saturday's glass session settling Follow's merge.** Every reusable piece is
   on `feat/follow-mode`. Building before that means building on an unmerged
   branch or duplicating the faces, and duplication is what this codebase keeps
   paying for.
2. **The `[follow] arc=` frame-cost reading.** The globe's per-frame cost is
   still **unmeasured** — no board was free during stage 2. Follow's globe draws
   on its own screen; a peek puts it on the **radar** path. If it is expensive,
   peek is where that shows. So the Saturday reading is an input to this
   feature's feasibility, not only to Follow's.

**The `ap:` device client is the one piece with no dependency on the glass
decision.** It can be specified now. It must not be wired up until the above
resolve.
