# v16 release plan

**Confirmed 2026-10-07 (review): cut v16 when #376 merges; ntfy and `nmi` → `NM` go to v17.**

## The rule

- **v16 is cut when both required items are on `main` and verified**: zoom and the touch-wedge
  reboot cap.
- **"On main" means `git ls-tree origin/main` shows the change**, not that a PR is open or green
  (CLAUDE.md, *a green signal is about process*).
- **ntfy removal and `nmi` → `NM` ride along only if they are done by then.** Otherwise they move to
  v17 unchanged.
- **The coarse-location and README `/blips` privacy rows say where they stand either way.**
- **Gate added at review: an overnight soak of the SHIPPING image** (real constants, no bench
  hooks) on the bench board, graded against predictions frozen before the flash. Passed
  2026-10-08 (S1–S5 clean; graded on #376).
- **Promote is approved by Daniel**, as with v15: the cut stops at the `promote` job and reports.

## Items

| # | item | spec | v16? | status |
|---|---|---|---|---|
| 1 | Swipe-to-zoom on the radar, plus a ZOOM tag | PR #374 | **required** | **merged `b803aef`** (Z1–Z5 and sabotages a–d graded, glass done; the shipped 10-min idle return is pinned by a test and the bench override cannot compile into a shipping env) |
| 2 | Touch-wedge reboot cap + "touch unavailable" | [v15-touch-wedge-cap.md](v15-touch-wedge-cap.md) | **required** | **merged `34305d1`** (#376: W1–W5 graded, sabotages a–c plus host sabotages red then undone, glass done incl. an iPhone scan of Connect with the strip up; overnight shipping-image soak S1–S5 clean) |
| 3 | ntfy removal | not yet written | **v17** (decided at review) | not started. Needs scope: radar only, or every edition that uses ntfy (Missileer, Quakescope, Quillscope, Reelscope, Claudescope, Speedscope)? And what happens to a saved `ntfy-topic`? |
| 4 | `nmi` → `NM` | not yet written | **v17** (decided at review) | not started |
| 5 | Coarse location on the wire | PR #372 | **already shipped in v15**, not a v16 item | the `v15` tag (`4df698f`) contains `include/WireLocation.h`, and its `AircraftManager.cpp` sends every location at 2 dp (6 `wireloc::Center` call sites) |
| 6 | README `/blips` privacy line | this PR | v16 item; done here | the README says what the feed request carries: the centre at 2 dp (~1.1 km), never the stored 4-dp value; a tapped aircraft's position the same way; nothing from a local receiver; the "Use my location" position goes only to the device |

Proposed in the session, **not on this list unless added**: config-page checkboxes that save
themselves, and pre-scanning Wi-Fi networks for the setup portal.

## Known cosmetic issues (accepted)

- **The ZOOM tag can sit on top of an airport label** in the top-left corner (seen at 10 mi with
  `0G15`, 2026-10-07). The tag is drawn last, so ZOOM itself always reads; part of the airport code
  can be hidden. Accepted as-is.

## Print cards

- **Zoom line, pending v16:** *"Swipe up on the radar to zoom in; swipe down to zoom out."* It is
  recorded in [CARDS-README.md](CARDS-README.md).
- **The Canva edit happens when v16 is promoted, not before.** The 50 units ship on v15, whose
  firmware has no zoom.
