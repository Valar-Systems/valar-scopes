# v16 release plan

**Proposed 2026-10-07 (review). Daniel to confirm.**

## The rule

- **v16 is cut when both required items are on `main` and verified**: zoom and the touch-wedge
  reboot cap.
- **"On main" means `git ls-tree origin/main` shows the change**, not that a PR is open or green
  (CLAUDE.md, *a green signal is about process*).
- **ntfy removal and `nmi` → `NM` ride along only if they are done by then.** Otherwise they move to
  v17 unchanged.
- **The coarse-location and README `/blips` privacy rows say where they stand either way.**

## Items

| # | item | spec | v16? | status |
|---|---|---|---|---|
| 1 | Swipe-to-zoom on the radar, plus a ZOOM tag | PR #374 | **required** | **merged `b803aef`** (Z1–Z5 and sabotages a–d graded, glass done; the shipped 10-min idle return is pinned by a test and the bench override cannot compile into a shipping env) |
| 2 | Touch-wedge reboot cap + "touch unavailable" | [v15-touch-wedge-cap.md](v15-touch-wedge-cap.md) | **required** | spec accepted (moved from v15); build starting from `main` |
| 3 | ntfy removal | not yet written | if done by the cut, else v17 | not started. Needs scope: radar only, or every edition that uses ntfy (Missileer, Quakescope, Quillscope, Reelscope, Claudescope, Speedscope)? And what happens to a saved `ntfy-topic`? |
| 4 | `nmi` → `NM` | not yet written | if done by the cut, else v17 | not started |
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
