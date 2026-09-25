# Print cards

**The card design lives in Canva; the cards in this repo (`docs/*card*.html`) are the fallback and the source of truth for the wording only** — `scripts/check-print-cards.mjs` asserts two sentences **exactly** in CI — the data note, and step 2's join instruction (*"Point your phone's camera at the screen and tap Join. No prompt? Join the Wi-Fi network named on the screen."*, since v15's Wi-Fi QR setup screen) — so Canva and the repo cannot drift on copy.
