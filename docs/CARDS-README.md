# Print cards

**The card design lives in Canva; the cards in this repo (`docs/*card*.html`) are the fallback and the source of truth for the wording only** — `scripts/check-print-cards.mjs` asserts two sentences **exactly** in CI — the data note, and step 2's join instruction (*"Point your phone's camera at the screen and tap Join. No prompt? Join the Wi-Fi network named on the screen. After you tap Join, the setup page takes a few seconds to appear."*, since v15's Wi-Fi QR setup screen) — so Canva and the repo cannot drift on copy.

## Pending: v16 (not on any printed card yet)

- **Zoom:** *"Swipe up on the radar to zoom in; swipe down to zoom out."* Swipe-to-zoom ships in
  v16 (PR #374). The Canva edit is made **when v16 is promoted, not before**: the 50 units ship on
  v15, whose firmware has no zoom, so a card that mentioned it would describe a feature the unit
  in the box does not have.

## Pending: v17 (ships in v17; not on any card yet)

The Canva edit is made **when v17 is promoted, not before**, together with any v16 line still
pending: units shipped before then run firmware without it.

- **Long-press zoom** (`docs/v17-long-press-zoom.md`; PRs #388 and #411; replaced double-tap,
  Daniel 2026-10-09): *"Press and hold the radar to zoom in close; again to zoom back out."* While
  you hold, a band fills around the edge of the screen, not under your finger.

## Pending: next release (specs only; not built, not on any card)

Moved out of v17 on 2026-10-14: v17 ships only what was already in main.

- **Overhead card** (`docs/v17-overhead-card.md`): *"When a plane passes overhead, a card shows it
  for a few seconds. Tap it for details."* The wording depends on the default decided in that spec.
  If the card sits behind "Look up!", the line begins *"With Look up! on, ..."*.
- **What's new** (`docs/v17-whats-new.md`): *"After an update, tap 'What's new' on the radar to see
  what changed."*
