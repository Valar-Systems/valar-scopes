# Print cards

**The card design lives in Canva; the cards in this repo (`docs/*card*.html`) are the fallback and the source of truth for the wording only** — `scripts/check-print-cards.mjs` asserts two sentences **exactly** in CI — the data note, and step 2's join instruction (*"Point your phone's camera at the screen and tap Join. No prompt? Join the Wi-Fi network named on the screen. After you tap Join, the setup page takes a few seconds to appear."*, since v15's Wi-Fi QR setup screen) — so Canva and the repo cannot drift on copy.

## Pending: v16 (not on any printed card yet)

- **Zoom:** *"Swipe up on the radar to zoom in; swipe down to zoom out."* Swipe-to-zoom ships in
  v16 (PR #374). The Canva edit is made **when v16 is promoted, not before**: the 50 units ship on
  v15, whose firmware has no zoom, so a card that mentioned it would describe a feature the unit
  in the box does not have.

## Pending: v17 (specs only; not built, not on any card)

The Canva edit for these is made **once, when the release carrying them is promoted**, together
with any v16 line still pending.

- **Double-tap zoom** (`docs/v17-double-tap-zoom.md`): *"Double-tap an empty spot on the radar to
  zoom in close; double-tap again to zoom back out."*
- **Overhead card** (`docs/v17-overhead-card.md`): *"When a plane passes overhead, a card shows it
  for a few seconds. Tap it for details."* The wording depends on the default decided in that spec.
  If the card sits behind "Look up!", the line begins *"With Look up! on, ..."*.
- **What's new** (`docs/v17-whats-new.md`): *"After an update, tap 'What's new' on the radar to see
  what changed."*
