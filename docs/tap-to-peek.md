# Tap-to-follow — design, locked 2026-08-28

> **This was "tap-to-peek" for about an hour.** The transient-peek design (dwell,
> auto-close, freeze-on-loss) is superseded: the swipe does not open a temporary
> view, it **sets the aircraft as the follow target for the session**. What
> follows from there is Follow exactly as built. The old lifecycle is kept at the
> bottom under [Superseded](#superseded) because the reasoning that killed it is
> worth not re-deriving.

**Swipe down on a card and that flight becomes the followed flight, until it
lands or you dismiss it.** Session-only: it is **never written to NVS**, so the
config page remains the only persistent path and C2's privacy line is untouched.

**Status: DESIGN LOCKED, BUILD IN PROGRESS 2026-08-28.** Every decision below is
settled. The display question (rotation vs a locked screen) was the last open
one and is answered in §3. Nothing merges: this rides the same glass gate as
Follow.

---

## 1. The gesture is a swipe, and long-press is permanently off the table

**Entry: swipe DOWN on the detail card.**

Everything else on the card is taken (full map:
[gestures.md](gestures.md)):

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

> **AMENDED 2026-08-30, and one rule below is REVERSED.** The three cases and
> the rule underneath them survive intact; what changed is that the route no
> longer gates the *gesture*, only the *face*, and the 4,000 km threshold is
> gone (#274). The superseded row is kept below rather than deleted, because
> the argument that produced it is sound and will be re-derived by somebody
> otherwise.

The face is decided by what the route resolves to, and the three cases want
three different things:

| case | face |
|---|---|
| **Both codes place** | **Globe.** At whatever scale the route needs — the 4,000 km threshold was deleted in #274. |
| **Codes present, one or both unplaceable** | **Arc, never an empty globe.** |
| **No destination at all** (`routeOrigin`/`routeDest` empty) | **Local face.** Rings around home, and a track. |

The rule underneath, which is the part to remember:

> **The globe requires coordinates; the arc requires only strings; the local
> face requires neither.**

The globe has nothing to centre on without both endpoints — its basis is built
from them. The arc draws its codes from the strings and simply omits the marker,
which is the code-only degradation Follow already builds and tests. So a
resolution miss degrades to the arc rather than to a blank disc.

**The arc is the CODE-ONLY face, not the unrouted face**, and that distinction
is load-bearing. An earlier reading of this rule was binary — routed or not —
and would have sent an unplaceable pair to the local face. That throws away work
already judged on glass (ZQX → QZY under ROUTE ONLY was made to read as
deliberate rather than broken) *and* loses the codes, which are the most
informative true thing the device holds about that flight.

### The reversal: every card is swipeable

**The original rule was: no route, no affordance.** The swipe kept meaning
"close" on a route-less card, on the argument that

> A peek with nothing to draw should not be offerable. An affordance that
> sometimes does nothing teaches people it does nothing.

**That argument is sound and it was aimed at the wrong target.** A route-less
contact is not a flight with nothing to draw — it is a flight whose picture is
the **local face**, which is the face this product shipped first (§1.1) and the
one most contacts on a domestic scope deserve. Withholding the gesture there
meant the feature silently refused the commonest aircraft on the screen, which
teaches "it does nothing" *faster* than an occasional weak result would.

So the route chooses the face and no longer gates the gesture. Exactly one
combination is genuinely empty:

| | |
|---|---|
| no route | no arc (needs codes), no globe (needs coordinates) |
| **and** no location | no local face either — its rings, its HOME marker and its whole auto-scale are built around a point the device has not been told |

**That one declines visibly**, which is what the original rule actually wanted:

```
NO LOCATION SET
Set one to follow this flight.
```

Amber, not red (§6: an explained state, not a fault). The copy names the fix
rather than the mechanism — the customer cannot give the flight a route, and can
give the device a location in half a minute — and the second line is scoped to
**this flight** on purpose, because a routed flight follows perfectly well
without a location and "you cannot follow anything" would be false.

### Known behaviour: the trail is empty at the moment of the swipe

The local face is rings plus a **track**, and the track starts at the gesture —
so the first frame after a swipe onto a route-less contact has a bearing arrow
and rings and no path behind it. This is **accepted, not a defect**: the trail
fills as the aircraft moves, and rings with a bearing arrow are already a true
picture of where it is. Recorded here so it is not re-diagnosed as a bug, and
so nobody designs a backfill for it — the trail is what the device has actually
observed since it was asked to watch, and inventing history for it would make
the one honest thing on that face dishonest.

---

## 3. Lifecycle — a session follow target

- **Entry:** swipe down on **any** card. The aircraft becomes the follow target
  **for this session only**. (Was "a card whose flight has a route" — see the
  reversal in §2.)
- **Persistence: none.** Never written to NVS. A reboot forgets it and the
  configured target (if any) resumes. The config page stays the only persistent
  path, which is what keeps C2 untouched: the device still never *stores* a
  target it was not explicitly given.
- **From there it is Follow as built.** Absence states and dead reckoning on
  contact loss, the face by §2's three-case rule, the post-flight card on landing.
  No dwell, no auto-close, no freeze-and-expire — those existed only because a
  12 s peek had no stake, and a session follow does.
- **Dismissal:** swipe down on the follow face clears the session target and the
  configured one (if any) resumes.
- **Resolving:** the globe cannot draw until both endpoints resolve, and the
  fetch cannot block the loop task (85 ms frame budget; the fetch task exists for
  exactly this). So the face opens in a *resolving* state and fills in.

### The display question, settled: rotation, plus a route strip

**Follow stays in rotation per §13.3.** A locked screen delivers "on screen the
entire way" literally and costs the radar for the whole flight — fifteen-plus
hours on a DEN→DEL — and it contradicts a settled principle: *"Follow gets a
screen; it never gets THE screen."* That rule exists because a followed aircraft
airborne while something rare passes overhead must not steal the display.

So the flight is visible continuously **on the radar**, without taking it:

- the **followed-contact ring**, which already exists, stays; and
- a **persistent route strip** on the radar face — origin → destination with
  along-track progress — drawn for as long as a followed flight has a route.

That is the literal reading of "on screen the entire way" satisfied on the screen
the owner is already looking at, at the cost of one strip rather than the whole
display.

**The strip's draw cost is inside the same instrumentation as the faces**
(`[follow] arc=`), because it lands on the RADAR path where the 85 ms budget is
provisional and the globe's cost is still unmeasured. Saturday produces a number
that includes it, not an impression.

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
| `DrawRouteArc` / `DrawRouteGlobe` | **100% now.** The signature refactor below already landed. |

### The signature refactor — DONE 2026-08-28 (`b640490`)

Both faces read `followRouteOrigin` / `followRouteDest` / `followTarget` off the
manager, which gave each exactly one possible caller. They now take a
`RouteView`, and `FollowRouteView()` is the single place that knows Follow's
members feed them — so a session-set target supplies its own view rather than
mutating Follow's state to borrow its renderer.

Done as merge cleanup rather than as this feature's work, because **it is the
right shape whether or not any of this gets built**. The privacy guard's anchor
control fired on the rename and confirmed the two faces no longer touch
`followTarget` at all — a shrunk surface checked rather than assumed.

### New

- **An `ap:` device client.** The firmware has **no** `ap:` client today — the
  endpoint shipped 2026-08-28 is server-side only and nothing on the device calls
  it. This is the real cost: a new request kind on the shared HTTP client, a
  parse, and a small resolved-endpoint cache so repeat peeks do not refetch.
- **Session target + gesture binding.** Much smaller than the peek state machine
  it replaces: the gesture sets a target, and Follow's existing machinery does
  the rest. An effective-target accessor (session overrides configured) is most
  of it.
- **The radar route strip**, instrumented with the faces.
- **Copy** for the resolving and unresolvable states.

---

## Sequencing

Nothing here merges before Follow does — it is all on `feat/follow-mode` and
rides the same glass gate. The one open **measurement**:

1. ~~Saturday's glass session settling Follow's merge~~ — build proceeds on the
   branch; the merge gate is unchanged.
2. **The `[follow] arc=` frame-cost reading.** The globe's per-frame cost is
   still **unmeasured** — no board was free during stage 2. Follow's globe draws
   on its own screen; a peek puts it on the **radar** path. If it is expensive,
   peek is where that shows. So the Saturday reading is an input to this
   feature's feasibility, not only to Follow's.

**The `ap:` device client is the one piece with no dependency on the glass
decision.** It can be specified now. It must not be wired up until the above
resolve.

---

## Superseded

The original transient-peek lifecycle: ~12 s dwell, any-touch dismissal, marker
frozen if the aircraft left the contact table mid-peek, and explicitly **no**
absence machinery — on the reasoning that the three absence states exist for
someone watching one aeroplane over hours, so `SIGNAL LOST` on a 12 s glance is
alarm without purpose.

That reasoning was right about a peek and is exactly why the session design is
better: it does not need the exemption. A session follow **has** the stake the
peek lacked, so the absence states and dead reckoning are appropriate rather than
alarming, and the special case dissolves. Recorded so the exemption is not
reintroduced along with a lifecycle that no longer exists.
