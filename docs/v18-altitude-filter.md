# Altitude filter (v18 or later)

**Status: spec, not built.** Line refs at `74866aa`.

## Customer view

A config setting, **"Hide aircraft above ___ ft"**, off by default (blank). When set:
- Radar and List hide contacts above that altitude.
- An honest count says how many are hidden: **"N hidden above X ft"**.

## What is never hidden

**Emergency, military, pinned and followed aircraft** always show, whatever their altitude.

| class | how it is detected (code) | caveat |
|---|---|---|
| emergency | `isEmergencySquawk` (`AircraftManager.cpp:491-494`) | **The cloud feed carries no squawk** (`CloudFeed.cpp:168`; the wire row at `proxy/src/schema.ts:89` has none). So "never hide emergencies" protects only OpenSky and local-receiver users. Stated, not implied. |
| military | `SpecialAircraft::IsMilitary` (`SpecialAircraft.cpp:83-94`) | -- |
| pinned | `pinnedIcao` (`AircraftManager.h:235`) | -- |
| followed | `MatchesFollow` (`AircraftManager.cpp:5619-5635`) | -- |

## One predicate, every path

The filter is **one function**, `HiddenByAltitude(const TrackedAircraft&)`, and every path that
walks contacts goes through it. The research found **five**:
1. the radar draw loop, next to `ZoomCulled`;
2. the radar tap hit-test (`:9078-9102`, which mirrors `ZoomCulled`);
3. `SortedAircraftByDistance` (`:4606-4622`), which serves both the List draw and the List
   hit-test (`:9127-9128`);
4. the Stats counts;
5. the NEAR/HIGH/FAST selection (`:3495-3512`), which today runs *before* zoom culling. HIGH
   must not name an aircraft the customer cannot see.

**A test enumerates the five** and fails if any walks contacts without the predicate. This is
CLAUDE.md's *second path* entry: the radar hit-test already mirrors `ZoomCulled` by hand, which
is exactly how a sixth path would miss it.

## Unknown altitude

The cloud maps the "no altitude" sentinel to **0** (`CloudFeed.cpp:139`), which would pass any
"below X" test as if on the ground.
- **Recommendation:** treat a contact with no altitude as *shown*, never hidden.
- Count it nowhere special. Hiding an aircraft for lacking a number would hide it for a feed gap,
  not for its altitude.

## The honest count

- **List:** the header line `"<n> tracked"` (`:3752-3754`) becomes `"<n> tracked · <h> hidden
  above <X> ft"`.
- **Radar:** the same count in the Stats screen's aircraft-count row. The radar face has no free
  row, and the zoom tag already owns the top-left.
- Precedent: `JoinDiagScreen.h:251-258`'s `"+N more not shown"`.
- **Never hide silently.** A filter that removes contacts without saying so reads as an empty sky,
  and CLAUDE.md's *the anonymous endpoint measures the throttle* entry is about exactly that.

## Should the overhead alert and card respect it?

**Recommendation: yes.**
- The customer who sets "hide above 10,000 ft" has asked for low traffic. An airliner at 37,000 ft
  passing "overhead" (the test is horizontal distance only, 3 km by default: `IsOverhead`,
  `:10086-10094`, with no altitude term) is precisely the noise they removed.
- Emergencies and military still alert, by the rule above.

**The counter-argument,** recorded: "overhead" is about *direction*, not altitude. Some people
want to look up at a contrail. That customer leaves the filter off, which is the default.

## Config

- The key is `alt-hide-ft`. Blank means off.
- Number input, 1,000-60,000, step 500, in the radar's altitude unit.
- **Default off is right:** a filter that hides real aircraft must be chosen, never inherited.
- A blank default also needs **no `ConfigMigration` entry** (CLAUDE.md: a default only reaches
  keys never saved; blank is the absence).

## Telemetry

**Proposal:** one boolean-as-integer, "altitude filter set" (0/1), in the usage report. It would
replace nothing, so it is a **ninth integer**, which changes `usage::Format`, the Worker's
`recordUsage` shape check, and the disclosures (`README.md` Privacy, `proxy/pages/support.html`)
in the same commit.

**Not recommended for v18.** The question it answers ("does anyone use it?") can wait for a
batch of counters. Listed so it is a decision, not an omission.

## Predictions to freeze before code (draft)

- With X set, every contact above X is absent from Radar, List, hit-tests, Stats and
  NEAR/HIGH/FAST.
- The count equals the number absent.
- A military contact above X is shown.

**Sabotage:**
- Bypass the predicate in the List hit-test: the enumeration test fails.
- Hide unknown-altitude contacts: the unknown-altitude test fails.

## Card

A config setting, not a gesture: no card line.
