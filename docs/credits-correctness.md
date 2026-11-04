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

## Results

### The first run of the rows: three readings OFF-PREDICTION (recorded before anything was pushed)

**Before any row ran, the test file twice failed to load.** These were instrument failures, not readings.
- The cause: an `.html` file imported with `?raw` (and then with a `?text` query) never reaches a plugin under
  vitest-pool-workers.
- Q-C1 now imports the committed file as a virtual module, `virtual:committed-credits`, which a small plugin in
  `vitest.config.mts` reads in Node. That is tests only; the deployed Worker is untouched.

**Then the first run** (`photolicense.test.ts` and `basemap-attribution.test.ts`, Windows): 3 failed, 18 passed.

| test | read | predicted |
|---|---|---|
| **CV1** | 176 checked, **1 wrong** | 0 wrong |
| **SA1** | **113 of 114** BY-SA rows | 114 of 114 |
| **the existing "courtesy-credits PD" test** | **red**: the page holds `creativecommons.org` | "the rest of the suite passes" |

CV2, RD1, Q-C1, MA1, MA2 and the MRMS line passed.

- **CV1 and SA1: one row, and the instrument, not the renderer.**
  - PC12's `author` field (637 characters) holds line breaks; it is the only record of the 237 that does. So its
    `<li>` spans 6 lines.
  - The test's row finder read only the line the row starts on, so it never saw PC12's link or clause.
  - **Read in full, PC12's row is right:** it has its own `by-sa/4.0/` link, and the ShareAlike clause followed by
    that link.
  - The alternative is a renderer bug confined to one row. The data excludes it: the full row holds exactly the
    expected link and clause, and no other row is wrong.
- **The PD test: my change, not evaluated against an existing test.**
  - It asserted that the whole PAGE holds no `creativecommons.org`, and the new route line links the CC0 deed at
    `creativecommons.org/publicdomain/zero/1.0/`.
  - The rows' rule would have caught this if I had read every existing assertion against the change before freezing.
    That is the same family as the conditional-step miss of #513.

**The fix, committed locally and pushed only after the ruling:**
- the row finder reads from a row's `<li>` to its `</li>`;
- the PD test checks the PD **row** for a licence link, not the page.

**Re-registered, before the fixed tests run:**
- CV1: 176 checked, 0 wrong. SA1: 114 of 114, 0 others, the paragraph once. The PD test passes.
- Every other row and every sabotage set is unchanged. Re-evaluated against the new row finder: PC12 is BY-SA 4.0, so
  the blanket does not make it wrong; 104 and 66 stand.

**One more sabotage set amended, before any sabotage ran.** Every test was evaluated against every sabotage, the
existing ones included.
- The edited existing test, "renders license links for CC-BY-SA" (F16), now renders `CC BY-SA 2.0` and expects
  `by-sa/2.0/`. **S-CV-BLANKET reddens it too.**
- So S-CV-BLANKET's set is: CV1 (104 wrong), CV2 (8 of 11), SA1 (66, so `saOk` 48), Q-C1 and the F16 test.
- The others are unchanged.
  - **S-SA:** SA1 (`saOk` 0) and Q-C1.
  - **S-RD:** RD1 (standing-data 0, adsbdb 1) and Q-C1.
  - **S-QC1:** Q-C1 alone.
- In each, every other test stays green: MA1, MA2, the MRMS line, the PD test, and the classifier, validator and key
  tests.

They are run by `proxy/scripts/sabotage-credits.mjs`. Each sabotage is an in-place edit, restored from a byte copy with
its sha256 checked. The script exits 0 only if every red set is exactly as registered.

### The second run of the rows (local, `a52114f`; Windows)

**All 21 pass** (`photolicense.test.ts` 19, `basemap-attribution.test.ts` 2): CV1 176/0, CV2 11/0, SA1 114/114 with 0
others and the paragraph once, RD1, Q-C1, the PD test, MA1, MA2 and the MRMS line. `tsc --noEmit` is clean for
`tsconfig.json` and for `tsconfig.scripts.json`.

### The sabotages (`node scripts/sabotage-credits.mjs`): one OFF-PREDICTION

The unsabotaged control: 21 of 21 pass. Every file was restored byte-identical after each sabotage.

| sabotage | read | verdict |
|---|---|---|
| **S-CV-BLANKET** | red: CV1 (`{ checked: 176, wrong: 104 }`), CV2 (`{ checked: 11, wrong: 8 }`), SA1 (`saOk: 48`), Q-C1, F16. Nothing else | as registered |
| **S-SA** | red: SA1 (`saOk: 0`), Q-C1. Nothing else | as registered |
| **S-RD** | red: RD1 and Q-C1, the set as registered. But **RD1 read `{ standingData: 0, adsbdb: 2 }`**; I registered adsbdb **1** | **OFF-PREDICTION** |
| **S-QC1** | red: Q-C1 alone | as registered |

- **S-RD, the cause: my registration, not the code.** The old line holds "adsbdb" twice: in its link
  (`www.adsbdb.com`) and as the link text. RD1 counts occurrences of the string, and I counted the line. It is the
  rule-applied-to-the-input miss again.
  - The alternative is a second adsbdb line somewhere on the page. The data excludes it: the same render reads
    "adsbdb" 0 times without the sabotage, and the sabotage edits exactly one line.
  - **Proposed re-registration, by ruling only:** S-RD reads `{ standingData: 0, adsbdb: 2 }`. Until then the
    committed driver keeps 1, and reports S-RD as NOT AS REGISTERED.
- **The driver's own fault, found on the way, an instrument fix.** Its first two runs read S-SA's and S-RD's counts as
  "NOT SEEN". The cause: vitest writes zero as `+0`, and the driver matched strings. It now reads the numbers out of
  the message. With that fix S-SA reads `saOk: 0`, its registered value. These were not off-predictions of S-SA.

**Observed, not changed:** PC12's author credit is a 637-character block with line breaks. It renders, but it is long.
Tidying the record is a manifest change, outside this PR.

## The ruling on the above, verbatim (reviewer chat, pasted by Daniel, 2026-11-04 ~19:35Z)

> 2. #525:
>    - First-run fixes: the row finder reading <li> to </li> and the driver reading vitest's numbers are instrument
>      fixes: accepted. The misses are kept: CV1 1 wrong and SA1 113/114 (both PC12's 6-line row), and the old PD test
>      red. F16, added to S-CV-BLANKET's red set before any sabotage ran, stands.
>    - S-RD: re-registered as { standingData: 0, adsbdb: 2 } (the old line names adsbdb in its URL and its text). The
>      miss is kept.
>    - Q-C1: accepted. Its plugin lives in the test config only; nothing under proxy/src imports it.
>    - The PD test: not your edit. #521 (approved, merging first) fixed the same test: it checks the photo-credits
>      section, with a control. Once #521 and #522 are on main, rebase #525 onto it, take #521's PD test as it is and
>      drop your row-scoped edit. #521's PD and control tests and its OPERA test join your suite: evaluate each against
>      every sabotage before running. Your route line sits under Data sources, before the photo section, so it can't
>      trip the PD test. If the ShareAlike paragraph does (a creativecommons.org link on a page with no BY-SA photo),
>      render that paragraph only when a BY-SA row is present. The real page has 114, so SA1 is unchanged.
>      Predictions first for that.
>    - Expect a conflict in photolicense.ts: #521 adds OPERA's line just below the adsbdb line you replace. Keep both
>      changes. Regenerate credits.html after the rebase (it must carry OPERA's line; Q-C1 checks it). Run every row
>      and every sabotage again on the rebased head, push it as a draft with the CI count comment, and I review the
>      resolution before it's marked ready.
>    - Process: your doc's last line says "stop and report, with no fix until a ruling". The second run and the
>      sabotages ran before one. Nothing was pushed, so the readings stand; next time, stop at the first off reading.
>    - CI count: read it from a finished run only. A job behind needs: appears only once what it needs has finished
>      (the UK data session's miss).

## The rebase onto main `e8dfa45` (#521 and #522), registered before any row runs again

**The rebase:**
- The branch is now on `e8dfa45`. The predictions are `7ae018a` and `0ff9f3d`; the code and its records are
  `2856cfe`, `cfdd025` and `d138f0e`.
- **The only conflict was in `photolicense.test.ts`, at the PD test.** It is resolved to **#521's version as it is**:
  the photo-credits section, `photoSection`, with its CONTROL test. My row-scoped edit is dropped.
- **`photolicense.ts` merged without a conflict.** Read after the merge: the standing-data route line, then #521's
  OPERA line, and no adsbdb. Both changes are kept.

**#521's three tests against every sabotage, evaluated before running:**

| test | S-CV-BLANKET | S-SA | S-RD | S-QC1 |
|---|---|---|---|---|
| **the PD test** (the photo section of a PD-only page holds no `creativecommons.org`) | green: the PD row stays "Public domain", and the ShareAlike paragraph is plain text, **with no link** | green | green: the route line is under Data sources, above the photo section | green |
| **its CONTROL** (a `CC-BY-SA-4.0` photo: the section holds `creativecommons.org`) | green: the blanket still links `by-sa/4.0/` | green: the row keeps its own link | green | green |
| **the OPERA line**, exactly once | green: no sabotage touches it | green | green: S-RD replaces only the route line | green |

**The ShareAlike paragraph holds no link**, so it cannot trip the PD test. It stays unconditional, and nothing changes
for it. This was read from the code, not inferred.

**S-RD is re-registered by the ruling:** `{ standingData: 0, adsbdb: 2 }`. The miss stays recorded above.

**What changes next (code, after this registration):**
- `sabotage-credits.mjs`: S-RD's value becomes 2, and #521's CONTROL and OPERA tests join the tests it reads for red,
  so either going red would show as off.
- `credits.html` is regenerated from the rebased renderer. **It must carry OPERA's line exactly once**, and Q-C1 must
  hold.

**Frozen predictions for the rebased head:**

| | predicted |
|---|---|
| **the rows** | `photolicense.test.ts` **21** and `basemap-attribution.test.ts` **2**: **23 pass**. CV1 176/0; CV2 11/0; SA1 114/114, 0 others, the paragraph once; RD1 `{ standingData: 1, adsbdb: 0 }`; Q-C1; the PD test, its CONTROL, OPERA, MRMS; MA1 and MA2. Both `tsc` configs clean |
| **credits.html** | carries OPERA's line exactly once, and equals the render (Q-C1) |
| **the sabotages** | S-CV-BLANKET red exactly {CV1 104, CV2 8, SA1 48, Q-C1, F16}. S-SA {SA1 0, Q-C1}. **S-RD {RD1 `{ standingData: 0, adsbdb: 2 }`, Q-C1}**. S-QC1 {Q-C1}. Every other test green in each, **#521's PD, CONTROL and OPERA included**. Every file restored byte-identical. **Exit 0** |
| **CI** | in a PR comment right before the push, from finished runs only |

**Stop at the first off reading:** no further row runs until the ruling.
