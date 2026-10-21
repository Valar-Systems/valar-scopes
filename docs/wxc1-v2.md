# WXC1 v2: a rain/snow type per cell (spec, predictions first)

**Status: a draft for review. Docs only, no code.** WXC1 v2 is shared work between two sessions:
- **the producer**, blipscope-5c: fetching PrecipFlag, pairing it with a radar frame, the encoder, the Worker and the cap. Its sections arrive as commits on this PR.
- **the consumer**, Weatherscope: `include/WxCrop.h` v2, its host tests, fixtures and sabotages, and the renderer.

Both sessions review every section. **Rulings come only from Daniel's reviewer chat.** The open questions in section 3 are his to rule on.

**The rules carry over from Weatherscope Stages 1 and 2:**
- predictions are frozen before the code or the measurement they predict;
- every check is proven able to fail by a sabotage;
- stop on anything off-prediction, and don't re-grade;
- downloaded MRMS files stay out of git;
- no deploys.

Facts cite `origin/main` at `fc784b8` unless a commit is named.

## 0. What is ruled

| ruling | source |
|---|---|
| Snow and rain only, no MIX. Snow is `PrecipFlag` code 3, and every other precipitation code (1, 6, 7, 10, 91, 96) is rain, including 7 (rain mixed with hail). | Daniel, 2026-10-20; `docs/weatherscope-screens.md` §4 (#425) |
| **The 15 dBZ floor stays.** No sub-15 dBZ snow is carried. | Daniel, 2026-10-20; same section |
| **The only format change is a rain/snow type per cell, from `PrecipFlag`.** | Daniel, 2026-10-20; same section |
| SeamlessHSR is Weatherscope's product. The Blipscope layer keeps the composite (`MergedReflectivityQCComposite`, "cref"); the product stays a parameter of the request. | Daniel, 2026-10-20 |
| Open this spec now as a docs-only draft PR; blipscope-5c reviews it. | Daniel, 2026-10-21 |

**Unchanged from v1:** the code nibbles (0 dry inside coverage, 1-13 for 15-75 dBZ in 5 dBZ steps, 14 no coverage, 15 missing), the run encoding, the header fields, the 32 KB cap and the refuse-whole strictness. The format reference is the header comment of `include/WxCrop.h`; the v1 wire spec is "Wire format v1" in `docs/wx-spike-2026-10.md` on `spike/wx-overlay`.

## 1. Why this is a version bump

There is no room in v1 for a type:
- **All 16 code nibbles are taken** (0, 1-13, 14, 15).
- **v1 refuses unknown flags and a nonzero reserved byte** (`WxCrop.h`: `UnknownFlags` for any flag bit but bit 0, `ReservedByte` for byte 7; added in the #429 review).

That strictness was put there for this moment. A new meaning has to bump the version rather than being silently ignored.

## 2. The format

### 2.1 Header: 32 bytes, as v1, with two differences

- **Byte 4, `version`: 2.**
- **Byte 6, `flags`:**
  - bit 0: coarsened to fit the cap (as v1);
  - **bit 1: typed.** When it is set, a type layer is always present.
  - Bits 2-7 are refused (`UnknownFlags`).
- **Unchanged:** the magic stays `WXC1`, and byte 7 stays reserved and must be 0.

**Why "typed" is its own flag** (blipscope-5c, agreed). Without it, an untyped crop (a frame or product with no `PrecipFlag`) and a typed all-rain crop look the same. With it, an all-rain typed crop is a single long run of about 3 bytes. Omitting the layer when there's no snow would save almost nothing and bring the ambiguity back.

### 2.2 Body: the code layer, then the type layer

1. **The code layer is exactly v1's.** Its runs are row-major from the north-west cell, may cross row ends, and cover exactly `rows × cols` cells:
   - one byte per run, with the code in the high nibble and a length of 1-15 in the low nibble;
   - a low nibble of 0 means a long run whose length follows as an unsigned LEB128.
2. **The type layer** is present if and only if `typed` is set. It starts at the byte right after the code layer has covered every cell. v1's no-overrun rule makes that boundary unambiguous.
   - **It covers echo cells only** (codes 1-13), in the same row-major order, skipping every cell with code 0, 14 or 15. Dry, no coverage and missing carry no type.
   - **Its runs use the same nibble + LEB128 scheme.** The nibble is the type: **0 = rain, 1 = snow**. Types 2-15 are refused.
   - **Its runs cover exactly the crop's echo-cell count.** Covering more is refused, and so is ending before it. When the crop has no echo cell the layer is empty (0 bytes), and that is still "present".
3. **Nothing follows the type layer.** A trailing byte is refused.

### 2.3 What a v2 decoder refuses

Everything v1 refuses, plus:

| input | status |
|---|---|
| `version` not 1 or 2 | `BadVersion` |
| a version-1 crop with flag bit 1 set | `UnknownFlags`. A v1 crop keeps v1's rules exactly |
| flag bits 2-7 | `UnknownFlags` |
| `typed`, but the type runs end before the echo count (including no layer at all when there is echo) | `TypeUnderrun` |
| `typed`, and the type runs cover more than the echo count | `TypeOverrun` |
| a type nibble of 2-15 | `BadType` |
| not `typed`, but bytes follow the code layer | `Overrun`, as in v1 |
| `typed`, and bytes follow the type layer | `TrailingBytes` |

**As in v1, the whole crop is validated before a single run is reported or a single cell written.** A caller never holds part of a grid.

**The v2 decoder also decodes v1.** A version-1 crop decodes exactly as today, every cell typed rain, so the consumer keeps working against an endpoint that serves v1.

### 2.4 What a v1 decoder does with a v2 crop (predicted; V2 tests it)

| the crop | the v1 decoder (today's `WxCrop.h`) |
|---|---|
| a v2 crop as specified, typed or not | `BadVersion`, refused whole, no cell written |
| a crop mislabelled `version` 1, typed | `UnknownFlags` (bit 1), refused whole |
| a crop mislabelled `version` 1, untyped, with a type layer appended | `Overrun`, refused whole |
| a crop mislabelled `version` 1, untyped, no type layer | decodes, untyped. This is a correct v1 crop |

So no v2 crop is ever partly drawn by a v1 decoder. That is the clean refusal Daniel asked for.

### 2.5 Coarsening

A coarsened block takes the code of its strongest cell, as v1 does.
- **Its type is the type of the cell that won its code.**
- **Ties need a rule.** That means equal-strength cells of different types winning the same block. See Q1 in section 3.

### 2.6 The cap, and a worst-case bound

**32 KB covers the whole v2 crop: header, codes and types together.** The producer's coarsening ladder (1, 2, 4, 8) considers both layers.

**The bound** at coarsen `c`, with `cells = ceil(rows / c) × ceil(cols / c)`:
- the code layer is at most `cells` bytes (one byte per run, at most one run per cell);
- the type layer is at most `echo ≤ cells` bytes;
- so a typed crop is at most `32 + 2 × cells` bytes. An untyped crop keeps v1's `32 + cells`.

The producer's section turns this into the largest crop each rung can always fit. **Its sabotage shows the cap check sees the type layer:** a crop that fits on codes alone and is over the cap with types must step down a rung.

### 2.7 Serving

- **The endpoint serves v1 unless the request asks for v2** (for example `&fmt=2`; the producer names it). Today's v1 fixtures and tests stay valid.
- **Nothing that decodes WXC1 has shipped.** The default flips to v2 only by a ruling.

### 2.8 Not in v2

- **No product byte.** An earlier ruling listed "WXC1 v2 (rain/snow, the product byte)". The later ruling makes the type the only change, and the URL and ETag already carry the product. Written here so nobody thinks it was forgotten.
- **No type below code 1.** The 15 dBZ floor is untouched.

## 3. Open questions, for Daniel's ruling

| | question | proposal |
|---|---|---|
| **Q1** | **The coarsening tie:** a block's strongest cells are equally strong and have different types. Which type does it get? | blipscope-5c proposes **snow wins**, so coarsening never hides snow. That is a ruling, not ours to make. |
| **Q2** | **An echo cell `PrecipFlag` doesn't call precipitation:** code 0 (no precipitation), −3 (no coverage), or no paired file for that cell. Which type does it get? | **Rain (type 0).** Snow only on positive evidence (code 3), which matches the ruled "snow is code 3, every other code is rain". Measurement 4 counts how often it happens (M4f). If no `PrecipFlag` file pairs with the frame at all, the crop is served **untyped** (flag bit 1 clear), never typed all-rain. |

> **Q2, added 2026-10-21 (Addendum 1, A1.1):** on the composite, which is the Blipscope layer, Q2's rain default would draw virga at the edge of a snowstorm in the rain ramp. M4f(composite) measures how often that happens. It goes to Daniel beside Q2 as information; it isn't a different proposal.

## 4. Measurement 4: before any encoder or decoder code

**Purpose.** Settle three things from data before the spec does:
- how `PrecipFlag` files pair in time with SeamlessHSR's and the composite's;
- whether the grids line up cell for cell;
- how sparse the type layer is.

No format choice depends on the result except the pairing rule and Q2's frequency. **Run by the consumer, predictions frozen here.**

**The sample:** change 1's 24 snowing hours at KMSP, 2024-12-02 05:53Z to 12-11 15:53Z (`docs/weatherscope-stage2.md`).
- These are the same SeamlessHSR files that run2 and the snow-ring measurement used.
- The composite files paired with them in Stage 2's run2 are also used.
- The `PrecipFlag_00.00` files are new: for each hour, every file within ±10 minutes of that hour's SeamlessHSR file.

**The instrument** is committed on its own branch before it runs (`tools/weatherscope/m4_precipflag.py`), so this PR stays docs-only. The results return here as an addendum.

**The crop:** Weatherscope's screen crop around KMSP, the 60 mi geometry of Stage 1's fixture. **No coordinates are printed or committed.**

**Two controls must pass before any number is read:**
- **C-M4a, census.** Every decoded `PrecipFlag` value is in NSSL's table (−3, 0, 1, 3, 6, 7, 10, 91, 96). A value outside it refuses the run: that is the instrument, not the data.
- **C-M4b, anchor.** Re-decoding each hour's SeamlessHSR and composite files reproduces, exactly:
  - the centre value Stage 2 recorded for that hour in run2's `results2.json` (kept outside git, with the MRMS files);
  - the histogram committed in `docs/weatherscope-stage2.md` (SeamlessHSR 7 / 12 / 1 / 3 / 1, composite 4 / 12 / 4 / 3 / 1).

**Two sabotages must turn the instrument red:**
- **decoding `PrecipFlag` with the wrong field width:** C-M4a refuses;
- **pairing each hour with the next hour's `PrecipFlag` file:** M4b reports offsets near 3600 s, outside every predicted set, so the run stops.

| | measured | predicted | what each outcome means |
|---|---|---|---|
| **M4a** | `PrecipFlag` cadence: the interval between consecutive files in each of the 24 windows | **120 s in all 24** | 120 s: pair per 2-minute frame. **Anything else: stop and decide**, and design the pairing from the measured cadence. |
| **M4b** | the nearest `PrecipFlag` file's time minus the SeamlessHSR file's, per hour | **0 s in 24/24** | (a) 0 s in 24/24: pair by identical timestamp; a missing match serves the crop untyped. (b) The same nonzero offset in 24/24: pair at that offset. (c) Varying, but all within ±120 s: pair to the nearest, with a stated maximum age. **(d) Anything else: stop and decide.** |
| **M4c** | the same, against the paired composite file | **\|Δ\| within 31-43 s in 24/24.** Stage 2 measured that spread between the composite and SeamlessHSR, and the prediction is that `PrecipFlag` runs on SeamlessHSR's clock | Inside the band: the Blipscope layer pairs across products to the nearest file, at most 60 s apart. Outside: **stop and decide** (the composite side's pairing has to be designed separately). |
| **M4d** | the grid of `PrecipFlag` against SeamlessHSR and the composite: dimensions, first point, increments, scanning mode | **identical for all three** | Identical: types pair cell for cell. Different: **stop.** v2 defines no resampling. |
| **M4e** | of the echo cells (codes 1-13) in the crop, pooled over the 24 hours: the share with `PrecipFlag` 3 (snow) | **80-100 %** on SeamlessHSR; **80-100 %** on the composite | Inside: snowing hours look like snow on the wire. Below 80 %: report it with M4f, and Daniel decides whether the type layer means what the ruling assumes. |
| **M4f** | of the echo cells, the share with no type evidence (`PrecipFlag` 0 or −3) | **≤ 5 %** on both products | Q2's frequency. Above 5 %: Q2 stops being an edge case, and its ruling is asked with this number. |
| **M4g** | of the snow cells (`PrecipFlag` 3) in the crop, the share with echo ≥ 15 dBZ | **5-40 %** on both products | The share of snow the 15 dBZ floor keeps on the wire, for the record. It changes nothing ruled. |
| **M4h** | the type layer's size under 2.2, per hour, against the code layer's | **≤ 4 bytes in every hour whose echo is all one type; otherwise ≤ 25 % of the code layer's bytes**, in 24/24 | Inside: the cap bound in 2.6 is loose in practice. Outside: the producer's ladder section gets this number before it is written. |

**Outside every row's predicted set: the instrument or the model is wrong. Stop and decide; don't write a pairing rule from it.**

> **Amended before M4 runs, by Addendum 1 (2026-10-21):**
> - M4a, M4b, M4e and M4f are replaced, and M4c is sharpened.
> - M4d gains a sabotage, and M4g gains a direction.
> - A third control, C-M4c, checks alignment.
>
> Where a row in the addendum replaces one above, the addendum's row is the one graded.

## 5. Who does what

### 5.1 The producer (blipscope-5c; sections to come as commits)

Fetching `PrecipFlag`, the pairing rule (from M4b and M4c), encoding the type layer, coarsening with both layers, the cap ladder and its sabotage, `&fmt=2`, and the Worker. **Nothing is deployed from this PR.**

#### 5.1.1 The producer's parts that don't depend on M4 (blipscope-5c, 2026-10-21; frozen before any encoder code)

**Facts cited:** `proxy/src/wx/crop.ts` and `handler.ts` on #443 at `57a677f`.
- The view box is clamped to ±2.0° (`VIEW.maxDegrees`) and padded by `PAD_CELLS` = 2.
- The ladder is 1, 2, 4, 8.
- `CAP_BYTES` is 32,768.
- The ETag is `cropEtag(product, epoch, lat2, lon2, rKm)`.

**Not in this part:** fetching `PrecipFlag`, pairing it, and serving untyped when nothing pairs. All three wait for M4b/M4c. So does coarsening's tie rule, which waits for Q1.

| | check | predicted |
|---|---|---|
| **P1** | **The ladder never runs out, typed or not** (host, exhaustive over the box sizes the handler can produce: every whole-km `r` up to the clamp, at every quantised latitude of the grid) | **The box:** the largest box is 405 or 406 cells a side. 405 is 400 + 1 + 2 × 2; one more is possible from float rounding at a half-cell boundary, and the test reports which. **At rung 4:** `cells(4)` is at most ⌈406/4⌉² = **10,404**, under the typed always-fit limit of (32,768 − 32) / 2 = 16,368 cells. So **every crop fits by rung 4 whatever its content**: rung 8 is never needed, and `wx_too_large` stays unreachable in v2 as it is in v1. **At rung 2:** `cells(2)` reaches 203² = 41,209, so rung 2 is not guaranteed, and the ladder is doing real work there. |
| **P1a** | **The bound in 2.6 is exact** for the worst content: every cell its own code run (alternating codes 1-13) and its own type run (alternating rain/snow) | The crop's size is **exactly** 32 + 2 × `cells(c)` at the rung it lands on. The same content untyped is exactly 32 + `cells(c)`. |
| **P1b** | **The cap sees the type layer** | A real query whose box has 16,368 < `cells(2)` ≤ 32,736, with the worst content: **untyped lands on rung 2, typed on rung 4**, and both are ≤ 32,768 B. **Sabotage:** the ladder's size test counts the code layer only. The typed crop is then accepted at rung 2 at 32 + 2 × `cells(2)` > 32,768 B, so the check goes red on both its rung and its size. |
| **P2** | **`fmt` is refused, never coerced** | **Absent or `1`:** the response is byte-identical to today's, and `test/wx-crop.test.ts` passes **unmodified**. **`2`:** a v2 crop. **Anything else** (`3`, empty, `2.0`, `v2`, ` 2`): **400 `bad_fmt`**. An unknown format answered as v1 would be a plausible-shaped wrong answer. **Sabotage:** `parseInt` with a default of 1. The `2.0`, `v2` and empty cases go red. |
| **P3** | **The ETag covers every input to the bytes** | v2 adds two inputs, the format and the type source: the paired `PrecipFlag` file's time, or "untyped". **(a)** For one frame and one query, the `fmt=1` and `fmt=2` ETags differ. A v1 ETag sent as `If-None-Match` on a `fmt=2` request gets **200 with a v2 body, never 304.** **(b)** One frame served untyped, then typed once its `PrecipFlag` pairs, has **two** ETags. So a device holding the untyped crop is never answered 304 indefinitely. **Sabotages:** drop the format from the ETag and (a) goes red; drop the type source and (b) goes red. |
| **P4** | **The producer's own round trip** (the floor; V5's cross-check is the strong form) | **The decoder:** a reference decoder written in the test, independent of the encoder. **The inputs:** seeded random typed crops up to 406 × 406, with run lengths at every LEB128 boundary (15/16, 127/128, 16,383/16,384). **Predicted:** the decoder recovers codes and types cell for cell, and every output is within 2.6's bound for its rung. **Sabotages:** a type layer emitted over all cells instead of echo cells (the reference decoder reports `TypeOverrun`); a type run one short at a LEB128 boundary (`TypeUnderrun`). |
| **P5** | **The coarsened type, the case Q1 doesn't cover** | A block with a unique strongest cell takes that cell's type, in both directions: a snow winner among rain cells and a rain winner among snow cells. The tie case waits for Q1, and no tie winner is written into a test before the ruling. |

**Why P3 is here.** The ETag's job is to say "these are the same bytes". v2 adds inputs to the bytes, and a 304 path that doesn't know about them is the second-path shape in CLAUDE.md, the route-cache row. Whether a `PrecipFlag` file can arrive after its radar frame is a pairing question for M4. P3 holds either way, so it's frozen now.

### 5.2 The consumer (Weatherscope): predictions for `WxCrop.h` v2

Frozen here before the decoder code:

| | check | predicted |
|---|---|---|
| **V1** | v1 unchanged | Every v1 fixture in `test/fixtures/wx/MANIFEST.json` gives the same runs and the same status under the v2 decoder. `test_wx_crop.cpp`'s v1 cases pass **unmodified**. |
| **V2** | a v1 decoder against v2 crops | Today's `WxCrop.h`, pinned by blob sha in the test, gives exactly the statuses in the 2.4 table on the v2 fixtures and their three mislabelled variants. Its callback is never called on a refused crop. |
| **V3** | v2 strictness | Each row of the 2.3 table has a case that gives its status, with zero callbacks. **Each has a sabotage in the decoder that turns that case red.** That gives at least one sabotage per new status, in `sabotage_wx_crop.py`. |
| **V4** | the cap, from the consumer side | A typed crop one byte over 32,768 is `TooLong`. The same crop untyped, under the cap, decodes. |
| **V5** | cross-checked fixtures | Every v2 fixture is written by the producer's encoder from real `PrecipFlag` and radar files. `WxCrop.h` v2 and an independent Python decoder agree on it cell for cell (codes and types), as v1's fixtures are checked against `crop.ts` and `a3_check.py`. |
| **V6** | the renderer | Snow cells are drawn from the snow ramp and rain cells from the rain ramp of the ruled palette (`WxPalette.h`). Pixel-identical to an extended `render_ref.py` on one typed fixture. **CONTROL:** the same fixture decoded untyped renders all rain. |

## 6. Order

1. **This skeleton, then blipscope-5c's review.**
2. **Measurement 4:** the instrument committed on its own branch, run, and its results recorded here.
3. **Daniel's rulings on Q1 and Q2,** with M4f's number attached to Q2.
4. **The producer's sections and the consumer's V-predictions finalised,** then each side's code on its own branch. Fixtures come from the producer's encoder.
5. **Flipping the endpoint's default to v2:** a ruling, not a step.

**Outside every step's predictions: stop and decide.**

## Addendum 1, 2026-10-21: blipscope-5c's review of measurement 4 (frozen before M4 runs)

blipscope-5c reviewed the predictions at `d2e14fc` (#451). I've taken all five changes and the directional M4g. **No `PrecipFlag` file has been listed or fetched.**

**Read for this addendum, and nothing else:** Stage 2's per-hour file times for change 1, from run2's `results2.json`. The sharpened M4c needs them.
- Every SeamlessHSR file is on a whole minute.
- The composite file is 31-43 s later in every hour. The 24 values are 33, 36, 32, 31, 34, 34, 39, 41, 33, 33, 35, 31, 33, 38, 43, 40, 36, 39, 34, 38, 38, 38, 36 and 42 s, in hour order.

### A1.1 M4e and M4f, split by product (review 1; these replace the section-4 rows)

**Why:** `PrecipFlag` is a surface type, and the composite is a column maximum. So echo aloft with nothing reaching the ground is composite echo with `PrecipFlag` 0: virga, or the leading edge of a snow shield.

| | measured, pooled over the 24 hours | predicted |
|---|---|---|
| **M4e(HSR)** | of SeamlessHSR's echo cells (codes 1-13), the share with `PrecipFlag` 3 | **80-100 %** |
| **M4e(comp)** | the same, on the composite | **60-100 %, and ≤ M4e(HSR)** |
| **M4f(HSR)** | of SeamlessHSR's echo cells, the share with `PrecipFlag` 0 or −3 | **≤ 5 %**. This is 100 % minus C-M4c's own number, so it is read only once C-M4c passes (A1.2) |
| **M4f(comp)** | the same, on the composite | **≤ 25 %, and ≥ M4f(HSR)** |

**What each outcome means:**
- **A composite value outside its band:** reported beside Q2 as the Blipscope layer's virga share. That's not a stop by itself.
- **Either direction reversed** (M4e(comp) > M4e(HSR), or M4f(comp) < M4f(HSR)): **stop.** The model of the two products is wrong.

### A1.2 C-M4c, the alignment control (review 2), read before M4e-M4h

**blipscope-5c's form:** at least 95 % of SeamlessHSR's echo cells have `PrecipFlag` > 0, with a flip/shift sabotage.
- **Taken, with one change.** That share is exactly 100 % − M4f(HSR). On its own, a failure couldn't say whether the grids are misaligned or the expectation is wrong.
- **So the control compares the true alignment with displaced ones.**

**The measure.** `A(o)` is the share of SeamlessHSR's echo cells, pooled over the 24 hours, whose `PrecipFlag` is > 0 when `PrecipFlag` is read at offset `o`. Every displaced window is read from the full grid, never wrapped. The offsets are:
- (0, 0);
- the 8 offsets of ±10 cells in rows, columns, or both;
- the crop window flipped north to south.

| outcome | meaning |
|---|---|
| **(i)** `A(0,0)` ≥ 95 % and greater than every displaced `A` | aligned, as expected: M4e-M4h are read |
| **(ii)** some displaced `A` ≥ `A(0,0)` | misaligned, or no signal: the instrument. **Stop** |
| **(iii)** `A(0,0)` is greater than every displaced `A` but below 95 % | aligned, and the expectation was wrong. **Stop and decide**; Q2's ruling gets the number |
| **(iv)** no SeamlessHSR echo cell in the sample, or anything else | the instrument is unreliable at this scale. **Stop** |

**Two sabotages, run before the real control is believed:**
- **(a) `PrecipFlag`'s full grid flipped north to south** (the scanning-mode slip M4d can't see). Predicted: not (i), with `A(0,0)` at least 20 points below the unsabotaged `A(0,0)`.
- **(b) the `PrecipFlag` window shifted 10 cells east.** Predicted: (ii), because the −10-column offset recovers the true alignment.

> **Superseded 2026-10-21 by Addendum 2 (A2.1).** `A(o)` is one-sided, so it can't tell "aligned" from "saturated". Alignment is now read from a two-sided measure that includes the ±1 offsets. `A(0,0)` ≥ 95 % stays, as the expectation bar only.

### A1.3 Missing files (review 3; these replace M4a's and M4b's outcome rules)

**M4a:**
- **120 s cadence with N gaps:** every interval is a multiple of 120 s, with 120 s as the mode. N is reported. **Predicted: N ≤ 5 % of the intervals** across the 24 windows.
- **N above 5 %:** stop and decide. Gaps would no longer be rare enough for the pairing to treat as rare.
- **An interval that isn't a multiple of 120 s:** stop, as before.

**M4b:**
- **The modal offset:** the most common per-hour offset to the nearest `PrecipFlag` file, measured first.
- **A missing hour** has no `PrecipFlag` file **at the modal offset**. It is counted as missing, never as an offset.
- **Outcome (a)** becomes: the modal offset is 0 s, and every hour that isn't missing is at 0 s. M missing hours are reported. **Predicted: M ≤ 2 of 24.**
- **M above 2:** stop and decide, because untyped crops would be common.
- **Outcomes (b)-(d)** are judged on the hours that aren't missing.

### A1.4 M4c, sharpened (review 4)

**If M4b lands in (a):** for each hour, the composite file's time minus the `PrecipFlag` file's time **equals** that hour's value in the list above, exactly. Missing hours are excluded and counted.
- **Predicted:** equal in every hour that isn't missing.
- **Any hour that differs:** stop.

**If M4b doesn't land in (a),** M4c keeps section 4's band form.

### A1.5 M4d's sabotage (review 5)

The comparison is run twice more, each time with one field of `PrecipFlag`'s decoded grid metadata perturbed:
- an increment changed by one unit;
- the scanning mode flipped.

Each must report **"different"** and name the field. If either reports "identical", M4d's comparison is not reading what it claims, and M4d isn't graded.

### A1.6 M4g's direction (the small one)

**Predicted: M4g(comp) ≥ M4g(HSR),** pooled. The bands are unchanged, 5-40 % each.
- **Pooled, not cell for cell.** The composite file is 31-43 s later and has its own QC, so a single cell can read lower than SeamlessHSR's.
- **A reversed direction:** stop.

### A1.7 The order the measurement reads things in

1. **The checks on the instrument:** C-M4a (the census), then C-M4b (the anchor).
2. **The timing and grid rows:** M4a, M4b, M4c and M4d, each with its sabotage.
3. **The alignment control:** C-M4c's sabotages, then C-M4c itself.
4. **Only after outcome (i):** M4e, M4f, M4g and M4h.

A stop at any step leaves every later row unread.

## Addendum 2, 2026-10-21: C-M4c read from a two-sided measure (blipscope-5c's re-read at `2210443`; frozen before M4 runs)

blipscope-5c's re-read found everything in Addendum 1 right except C-M4c. **No `PrecipFlag` file has been listed or fetched.**

### A2.1 The finding, and the alignment control as it now stands

**The finding, checked against A1.2's text.** `A(o)` asks only what share of echo cells land on `PrecipFlag` > 0. That makes it one-sided, and it can saturate:
- **Why it saturates.** M4g predicts that only 5-40 % of snow cells reach 15 dBZ. So in a snow shield, the `PrecipFlag` > 0 area is several times the echo area. A 10-cell shift can leave `A` at about 100 %, tying `A(0,0)`.
- **What a tie did under A1.2:**
  - a correctly aligned decode could stop as "misaligned" (outcome (ii));
  - sabotage (b) could pass on a tie whether or not the measure discriminates.
- **What ±10 can't see:** an off-by-one, which is the likeliest decode bug.

**The two masks,** per hour, over the crop:
- `P` = the cells with `PrecipFlag` > 0;
- `H` = the cells with **raw** SeamlessHSR ≥ **5 dBZ**.

The threshold is stated now, without data. Only where the agreement peaks is predicted, not its level, so 5 dBZ only sets which boundary is compared.

**The measure:** `IoU(o) = Σ|P(o) ∩ H| / Σ|P(o) ∪ H|`.
- The sums are pooled over the hours, with `P` read at offset `o` from the full grid, never wrapped.
- Values are compared **exactly**, as fractions of integer counts.

**The offsets, 17 displaced:**
- the 8 at **±1 cell** in rows, columns or both;
- the 8 at ±10 cells;
- the crop window flipped north to south.

**A boundary hour** is one whose crop holds cells both inside and outside `P`, and cells both inside and outside `H`. **K = 8 of the 24 hours**, stated now. The boundary-hour count is always reported.

**The expectation bar stays `A(0,0)` ≥ 95 %,** which is 100 % − M4f(HSR). It is read only once alignment has passed. A1.2's displaced `A` values are no longer used.

| outcome | meaning |
|---|---|
| **(i)** at least K boundary hours; `IoU(0,0)` **strictly** above all 17 displaced values; and `A(0,0)` ≥ 95 % | aligned, as expected: M4e-M4h are read |
| **(ii)** some displaced `IoU` **strictly** above `IoU(0,0)` | **misaligned:** the instrument. **Stop** |
| **(iii)** at least K boundary hours, a strict peak at (0,0), but `A(0,0)` < 95 % | aligned, and the expectation was wrong. **Stop and decide**; Q2's ruling gets the number |
| **(iv)** fewer than K boundary hours, **or a tie at the top**, or anything else | **no signal:** the instrument is unreliable at this scale. **Stop** |

### A2.2 Three sabotages, run before the real control is believed

| sabotage | predicted |
|---|---|
| **(a) `PrecipFlag`'s full grid flipped north to south** | not (i), and `IoU(0,0)` at most half its unsabotaged value |
| **(b) the `PrecipFlag` window shifted 10 cells east** | (ii), with a **strict** maximum at the offset that undoes it (10 cells west) |
| **(c) the `PrecipFlag` window shifted 1 cell east** | (ii), with a **strict** maximum at the offset that undoes it (1 cell west) |

**A sabotage that lands in (iv) is no evidence that the measure sees.** If (b) or (c) lands there, the control isn't believed, and the run stops.

**A1.7's order is unchanged.** "C-M4c" there now means A2.1, and "its sabotages" means A2.2.
