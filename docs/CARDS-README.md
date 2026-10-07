# Print cards

**The card design lives in Canva; the cards in this repo (`docs/*card*.html`) are the fallback and the source of truth for the wording only** — `scripts/check-print-cards.mjs` asserts two sentences **exactly** in CI — the data note, and step 2's join instruction (*"Point your phone's camera at the screen and tap Join. No prompt? Join the Wi-Fi network named on the screen. After you tap Join, the setup page takes a few seconds to appear."*, since v15's Wi-Fi QR setup screen) — so Canva and the repo cannot drift on copy.

## Pending: v16 (not on any printed card yet)

- **Zoom:** *"Swipe up on the radar to zoom in; swipe down to zoom out."* Swipe-to-zoom ships in
  v16 (PR #374). The Canva edit is made **when v16 is promoted, not before**: the 50 units ship on
  v15, whose firmware has no zoom, so a card that mentioned it would describe a feature the unit
  in the box does not have.
