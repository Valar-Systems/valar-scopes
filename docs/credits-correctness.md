# /credits says what each source's licence actually is (predictions first)

**The ruling (reviewer chat, pasted by Daniel, 2026-11-04 ~19:00Z), verbatim:**

> New, small, predictions first: the /credits correctness PR. It's wrong for devices today.
> - Route data: credit vradarserver/standing-data (CC0), and remove the adsbdb line.
> - Photos: each image's actual licence version (2.0, 3.0 or 4.0), from its own Commons record, not a blanket 4.0.
> - The ShareAlike notice, which was never built.
> - A test that reads the version from each image's own record (sabotage: a forced blanket version turns it red).

## What is on main (`ec4f0d4`), read before any change

- **The renderer gives every CC image the same version.** `LICENSE_META` in `proxy/src/photolicense.ts` maps every
  CC BY row to "CC BY 4.0" and every CC BY-SA row to "CC BY-SA 4.0", whatever the row's own licence says. It also maps
  every OGL row to "Open Government Licence v3.0".
- **Each image's own record already holds its version.** Every one of the 237 manifest rows comes from Commons. Its
  `license` is that file's `extmetadata.LicenseShortName`, taken at harvest (`proxy/scripts/harvest-commons.ts:128`).

  | the record says | rows |
  |---|---|
  | CC BY-SA 4.0 | 48 |
  | CC BY-SA 2.0 | 36 |
  | CC BY-SA 3.0 | 18 |
  | CC BY-SA 3.0 au | 12 |
  | CC BY 2.0 | 32 |
  | CC BY 4.0 | 24 |
  | CC BY 3.0 | 3 |
  | CC BY 2.5 | 2 |
  | OGL v1.0 | 1 |
  | Public domain | 54 |
  | CC0 | 7 |

  **So the page shows the wrong licence for 104 rows today:** 66 BY-SA, 37 BY and the one OGL row.
- **Beyond the ruling's "2.0, 3.0 or 4.0", from the records themselves:** 2 rows are CC BY 2.5, 12 are the Australian
  port CC BY-SA 3.0 AU (its own deed, `/by-sa/3.0/au/`), and the OGL row is v1.0, not v3.0. Each is rendered as its
  record says.
- **Production renders exactly these records.** The published `photo:manifest` (read-only, `--remote`) has the same
  237 rows, with **the licence token equal on all 237**. A deploy of the renderer fixes the live page; no KV write is
  needed.
- **Route data.** The page says "Route data from adsbdb".
  - Every route has come from the CC0 vradarserver/standing-data mirror since the 2026-08-26 cutover.
  - adsbdb's only other use, the type backfill (`enrich.ts:174`), is behind the same switch,
    `ROUTE_ADSBDB_ENABLED`. That switch is `"false"` in `[env.production.vars]`.
  - So no adsbdb data reaches a device, and its line goes.
- **The ShareAlike notice was never built** (ROADMAP B4). There are 114 BY-SA rows, and each already notes its change
  (114 of 114 have `changesNoted`). What is missing is the ShareAlike clause itself: the resized image "must remain
  available under the same license, stated on the credits page" (`blipscope-military-photo-sourcing.md:313`).
- **One existing test encodes the defect.** `photolicense.test.ts` renders an unversioned `"CC-BY-SA"` and expects
  `by-sa/4.0/`. That is a version no record gave.
- **The committed `proxy/photos/credits.html` is already stale.** Today's renderer makes 9 lines it lacks (the
  weather and basemap credits added since it was last regenerated), and it lacks none of the renderer's other lines.

## The change

1. **The licence comes from the row's own token** (`licenseDisplay` in `photolicense.ts`).
   - **CC BY and CC BY-SA:** the version, and a port if there is one, parsed from the token. "CC BY-SA 3.0 au"
     becomes the label "CC BY-SA 3.0 AU" with the link `https://creativecommons.org/licenses/by-sa/3.0/au/`.
   - **OGL vN:** "Open Government Licence vN.0", linked to `.../open-government-licence/version/N/`.
   - **A CC or OGL token with no version: the raw token, with no link.** A version is never invented, because a
     plausible wrong version is the defect being fixed. No row today has one.
   - **Public domain and CC0:** "Public domain", as before.
2. **The ShareAlike notice.**
   - **Each BY-SA row** ends: "this adapted image is available under the same licence, CC BY-SA *v*", with its own
     version and link.
   - **The photo section** adds one paragraph stating the ShareAlike term once.
   - ROADMAP B4 is marked built.
3. **Route data:** "Route data from vradarserver/standing-data, released into the public domain under CC0 1.0". It
   links the repository and the CC0 deed, worded as `support.html:433` already words it. The adsbdb line is removed.
4. **The tests,** in `proxy/test/photolicense.test.ts`.
5. **`proxy/photos/credits.html` regenerated** from the manifest with the new renderer. That also carries in the 9
   earlier-missing lines.

**Beyond the ruling, named so it can be struck:**
- **Q-C1:** a test that the committed `credits.html` equals the render of the committed manifest, so it cannot drift
  again unseen. Its sabotage: one line of the file edited.
- **Not changed, reported:** CC BY 4.0 also asks that changes be indicated. BY rows carry no `changesNoted`; the photo
  section's note "Images are resized for the device display" covers all of them at once. This is left as it is.

## Predictions

| | check | predicted |
|---|---|---|
| **CV1** | each licensed row of the **real** manifest: its version and port, parsed by the test itself from the row's own `license`, against that row's label and link on the rendered page | **176 rows checked** (114 BY-SA, 61 BY, 1 OGL), **0 wrong**. No row carries any other licence's link |
| **CV2** | the control fixture: one row each of BY 2.0, 2.5, 3.0 and 4.0; BY-SA 2.0, 3.0, 3.0 AU and 4.0; OGL v1.0 and v3.0. Plus an unversioned `CC-BY-SA` | the 10 rows each carry their own link and label. The unversioned row shows its raw token with **no link** |
| **SA1** | the ShareAlike notice | **114 of 114** BY-SA rows carry it, naming their own licence; **0** other rows do; the paragraph appears **exactly once** |
| **RD1** | the data sources | the standing-data line with its CC0 1.0 link **exactly once**; "adsbdb" **0 times** on the page |
| **Q-C1** | the committed `credits.html` against the render | equal |
| **the rest** | MA1, MA2, the MRMS line, and the rest of the proxy suite | pass. The one changed expectation is the old blanket test, which now expects no link for the unversioned token |
| **after merge** | the proxy deployed to production, the carried commits listed first; then the live `/credits`, fetched | each licence label occurs as often as the manifest's rows hold it (the 9 in the table above); "adsbdb" 0 times; standing-data once; the ShareAlike notice on 114 rows |

**The sabotages, each run once and observed red.** Amended before any code existed (the first table, in
`5325216`, listed only each sabotage's target row). Every row was then evaluated against every sabotage:
- Q-C1 compares the committed file with the render, so every renderer sabotage reddens it.
- SA1's clause names the row's own label, so the blanket reddens it as well.
- CV2 counts its unversioned row too: 11 rows.

| sabotage | turns red (and only these) |
|---|---|
| **S-CV-BLANKET:** every CC row forced to 4.0 with no port, the unversioned row included (the old behaviour), and OGL to v3.0 | **CV1: exactly 104 rows wrong** (66 BY-SA, 37 BY, 1 OGL). **CV2: 8 of 11 wrong**: all but BY 4.0, BY-SA 4.0 and OGL v3.0. **SA1: 66 rows** naming the wrong licence. **Q-C1** |
| **S-SA:** the per-row ShareAlike clause removed, the paragraph kept | **SA1: 114 rows missing it. Q-C1** |
| **S-RD:** the adsbdb route line put back in place of the standing-data line | **RD1:** "adsbdb" once and standing-data 0 times. **Q-C1** |
| **S-QC1:** one line of `credits.html` edited | **Q-C1** only |

In each, MA1, MA2 and the MRMS line stay green.

**CI:** the job count is predicted in a PR comment right before the push, read from the last finished run with every
step's `if:` evaluated per leg, and from main as it is at that moment.

**Disk:** C: has under 30 GB free (8.6 GB at 19:05Z), so no large build runs. These are one test file's runs and one
regeneration.

**Outside the set:** stop and report, with no fix until a ruling.
