# Blipscope military photo library — sourcing playbook

Goal: one clean, legally-solid photo per aircraft type Blipscope actually paints
— military hand-curated (Tiers 1–3), civil long tail auto-harvested (Layer 2) —
sized for the 150 px sprite pipeline, with license + credit captured in the
manifest at ingest.

## Strategy: Wikimedia Commons first, .mil for gaps

Counterintuitive but operationally right: **Wikimedia Commons is the primary
source even for US military photos.** Most official USAF/USN imagery is mirrored
there — same public-domain photos — but Commons wraps every file in
machine-readable license metadata (license, author, credit) via its API. That
means the harvest script can auto-fill the manifest instead of you hand-typing
credit lines from .mil captions. DVIDS and the service sites become the fallback
for types Commons covers poorly.

### License rules by source

| Source | License | Commercial use | Attribution | Notes |
|---|---|---|---|---|
| Commons: "PD US Air Force / Navy / Army / DoD" | Public domain | Yes | Courtesy credit (do it anyway) | The bulk of the US library |
| Commons: CC-BY | Copyright, licensed | Yes | **Required** | Main path for foreign types |
| Commons: CC-BY-SA | Licensed, share-alike | Yes, with full compliance | Required + license link + changes noted | Tier 1: avoid. Layer 2: allowed — see note below |
| UK MOD imagery marked OGL (Open Government Licence) | Licensed | Yes | Required | Covers RAF types well |
| DVIDS / af.mil / navy.mil etc. | PD **if** US-service-shot | Yes | Courtesy credit | **Credit-line test below** |
| planespotters / JetPhotos / airliners.net | Photographer copyright | No | — | Never, regardless of quality |

**The credit-line test (every photo, no exceptions):** "U.S. Air Force photo by
SSgt …" → public domain. "Photo courtesy of Lockheed Martin / Boeing / Northrop
Grumman" → contractor copyright even on a .mil page → skip. This is the single
most common trap in DoD-hosted imagery.

**Endorsement note:** PD covers copyright only. Photos on the device detail card
are factual display — fine. Building Shopify marketing heroes out of USAF
imagery edges toward implied endorsement — keep marketing art separate.

## Photo selection criteria (for a 150 px wide sprite)

- Side or front-quarter **in-flight** shot, aircraft fills ≥70% of frame width
- Clean sky or high-contrast background; no airshow crowds, no heavy haze
- Landscape orientation; survives a hard downscale (fine detail is wasted)
- No watermarks, no embedded caption text
- Consistency beats individual brilliance: an all-air-to-air library reads as a
  designed product; a mix of ramp shots and air-to-air reads as scraped

### It has to survive a SQUARE crop (2026-08-09)

The detail card is going full-bleed on a round panel, so the sprite is no longer
a 150 px landscape rectangle — it is a 240×240 disc. A landscape photo loses its
sides to that crop, and the criteria above were written for the rectangle.

Two fields go on the picksheet, both optional and both defaulting to the safe
value: **`focus`** (subject centre as `[x, y]` in 0–1, default `[0.5, 0.5]`) and
**`zoom`** (default `1.0`). They exist because a stock photo is composed for a
rectangle: apron, sky and runway are most of the pixels, and a centre crop
faithfully preserves a composition that is wrong for a 130 px disc.

> **A photo that cannot be framed usefully gets REPLACED, not cropped harder.**
>
> This is a curation standard, and it sits here next to the licence rules
> deliberately — it is the one that erodes during a bulk harvest. Cropping harder
> always *works* in the sense that it produces an image; it just produces one the
> owner cannot identify. Measured on a real batch: a global zoom tuned to one
> photo made two of three test aircraft unrecognisable, and a third could not be
> saved at any setting because the aeroplane sat low and wide in the frame. That
> third one is not a cropping problem to solve, it is a photo to drop.
>
> The test is one question, asked at the pick: **at 240 px, can you tell what
> aircraft this is?** If the answer needs a zoom setting that throws away the
> wings, find another photo. There are usually several.

A `zoom` above ~1.4 is a smell rather than an error — it usually means the
aircraft is small in the frame, which is the ≥70%-of-width criterion above
failing quietly.

### Render the card before accepting a pick — the title is not the photograph

**A pick is not vetted until you have looked at it rendered as a 240 px card.**
Reading the Commons title, the licence and the dimensions tells you nothing about
whether the image shows an aeroplane.

Measured on the first batch to use this process: **4 of 17 picks that had passed
a title-and-licence review failed the moment they were rendered.** One
(`CRUZ`) was captioned *"University Flying Club (VH-EZT) CSA PiperSport at
Jandakot Airport"* — accurate, correctly licensed, right aircraft, and the
photograph is of its **instrument panel**. Another (`C150`) was a close-up of a
fuselage side. Two more had the aircraft too small or upstaged by a larger jet
behind it.

None of that is visible in metadata, and all of it is obvious in one contact
sheet. Generate the whole batch as cards in a grid, look at the grid, then
accept. It takes a minute for a batch of twenty and it is the only step that
inspects the thing customers actually see.

> **The general rule, of which this is one instance: a check operates on the
> artifact that ships, never on a description of it.**
>
> It is the same rule as reading the ELF instead of [platformio.ini](platformio.ini)
> to prove a build flag took, and as running `ls` on a downloaded release asset
> before grepping it — a missing file greps as a clean negative. See the standing
> practice in [CLAUDE.md](CLAUDE.md); this playbook's version of it is the contact
> sheet. A title, a licence tag and a pixel count are all descriptions. The card
> is the artifact.
>
> The tell is always the same: the check passes while the defect is present,
> because the check never touched the defective thing.

**Short type names pull non-aviation results.** Searching `Sling 2` returns baby
slings, weapon slings and Iron Age sling bullets alongside the aeroplane, and
`U21` returns under-21 football. Worse, a search for one variant is dominated by
its more common sibling — every unfiltered `Sling 2` result was a Sling **4**.
Exclude the sibling explicitly (`-"Sling 4"`) and require the aircraft word.

### Measured 2026-08-10: what the square crop actually costs, and which lever fixes what

The framing above was reasoned about. These are measurements across the whole
234-photo library, and two of them invert the intuition.

**A centre cover-crop discards about half of an airliner.** 3:2 source into a 1:1
target keeps 67% of the width, and the missing third is nose and tail. Measured:
**49% of a 777 survived, 53% of a 737-9.** A customer saw a fuselage section,
which identifies nothing. Fixed by `subjectCrop()` in `proxy/src/framing.ts`:
crop to the subject, then **grow vertically into the sky** until the box is square
enough (sky is free space, so it is spent before aeroplane), and only narrow --
clipping a wingtip -- once the sky runs out. The fill cap is **35%**, chosen by
rendering the entire library at 30/35/40 and picking against the set rather than
against the two photos that motivated the change.

**The blurred fill is a BETTER text bed than the photograph was.** The leftover
space is filled with a blurred, darkened copy of the same crop. The expectation
was that this would cost some legibility and need the scrim re-tuned. It did the
opposite. Same worst-single-pixel method, same hostile set, glyphs composited
after so text cannot flatter its own background:

| framing | worst pixel | contrast (target 4.5:1) |
|---|---|---|
| centre cover-crop (before) | 0.032 | 8.1:1 |
| subject crop, 35% cap | 0.017 | **12.0:1** |

Worst case across the hostile six under the new framing is **9.0:1**. Framing
improved legibility instead of costing it, which is worth stating plainly because
the reflex on seeing "we now composite a blurred band under the callsign" is to
assume the guarantee weakened and go looking for what broke.

**A soft photo is NOT a cap problem, and reaching for the cap will not help.**
This is the trap this section exists to prevent. The cap decides how much
aeroplane you trade for a square frame. "The aircraft is small in a big sky" is a
different quantity: once the crop is tight around a small subject, filling the
panel means UPSCALING, and the failure is softness, not size. No cap value
touches it.

Measured: median subject width **1275 px**; worst is B350 at **512 px**, which
still only needs 0.94x for the 480 panel. **Zero of 234 photos would be upscaled
at any panel size.** The lever, if it ever binds, is `THUMB_W = 1200` in
`proxy/scripts/harvest-commons.ts` -- a *download* setting. Commons originals run
4-8k wide, so there is 4-6x of headroom sitting unused.

**Related check:** `proxy/scripts/audit-photo-subject.ts` ranks the library by how
much of the square the aeroplane occupies. Its first version ranked on encoded
size and luminance stddev, which measure a flat BACKGROUND rather than a small
SUBJECT -- six of its eight worst hits were good photos and it missed the two
genuinely bad ones. Rendering the results is what caught that.

## Starter type list

Keys are ICAO type designators — **verify each against the Worker's existing
type-name table / doc8643 before keying**; codes marked (?) are from memory and
need checking. Claude Code should reconcile this list against types actually
seen in the proxy logs and the tar1090/Mictronics military-flagged database.

### Tier 1 — weekly sightings on a US scope (do these first)
- C-17 Globemaster III — `C17`
- C-130H Hercules — `C130` · C-130J — `C30J`
- KC-135R Stratotanker — `K35R`
- KC-46A Pegasus — (?)
- C-5M Super Galaxy — `C5M`
- P-8A Poseidon — `P8`
- B-52H — `B52` · B-1B — (?) · B-2A — `B2`
- F-16 — `F16` · F-15C/E — `F15` · F/A-18E/F — (?) · F-22 — `F22` · F-35 — `F35`
- A-10C — `A10`
- T-38 Talon — `T38` · T-6 Texan II — `TEX2`
- V-22 Osprey — `V22`
- UH-60/HH-60 — `H60` · CH-47 — `H47` · AH-64 — `H64`
- RC-135 — `R135` (?) · E-3 Sentry — (?) · E-6B Mercury — (?)
- U-2S — `U2`
- VC-25A (Air Force One) — `B742` per-hex override candidates

### Tier 2 — monthly/regional
- C-40, C-32, C-21, C-12, C-146, C-27J
- E-4B, E-11A (BD700 airframe), E-7 Wedgetail (B738 airframe)
- EC/HC/MC/WC-130 variants (reuse C-130 base shot or one special)
- CH-53E/K, UH-1Y, MH-65 (USCG), MH-60T (USCG)
- T-1A, T-45, F-5 (aggressors)

### Tier 3 — allied & customer-market types
- A400M Atlas — `A400`
- Eurofighter Typhoon — `EUFI` · Rafale — `RFAL` · Gripen — (?)
- A330 MRTT / RAF Voyager — `A332`/`A333` airframes
- C-295 — `C295` · CN-235 — (?)
- P-1 and C-2 (Japan), KC-390 (Brazil) — (?)
- NH90, AW101 Merlin — (?)
- Il-76 — `IL76` · An-124 — `A124` (freight-military gray zone, high wow factor)

Long tail falls back to silhouettes — but Layer 2 below shrinks the long tail
to nearly nothing.

## Layer 2 — the civil long tail, auto-harvested via Wikidata

The military tiers above are hand-curated. Everything else Blipscope paints —
airliners, bizjets, GA singles, helicopters — comes from an automated layer:

1. **Target list from reality:** every ICAO type code seen in the proxy logs
   (plus the tar1090/Mictronics DB for expected types), ranked by frequency.
2. **Wikidata resolution:** resolve each code to its Wikidata entity via the
   ICAO aircraft-type-designator property, then take **P18 (image)** — the
   community-chosen canonical photo for the type, the same image Wikipedia's
   infobox shows. P18 files live on Commons (free-licensed by definition);
   pull extmetadata for license/author as usual.
3. **No P18 or a bad one:** fall back to Commons search ranked by the Quality
   image / Featured picture assessment categories, landscape orientation.
4. **License filter for this layer:** PD family, CC-BY, OGL, **and CC-BY-SA**
   (revised — see note). NC and ND remain hard-rejected.
5. **Spot-check, not full curation:** pick sheet ordered by traffic frequency;
   human eyes on the top ~100, sampling below. Auto-picked photos are flagged
   in the manifest so the detail card can label them "representative photo."
6. **Re-harvest quarterly:** Wikidata curation improves for free.

Silhouettes remain the final fallback for true exotics. Any auto-pick can be
overridden later by dropping a better image on the upload script (operator-
livery keys for flagship airlines are a future nicety, not v1).

**CC-BY-SA note (revised from first draft):** share-alike binds the *image* —
the resized derivative must remain available under the same license, stated on
the credits page with changes noted — it does not relicense the product it's
embedded in. That's the standard community reading and is manageable at scale
via the manifest-generated credits page. Not legal advice; Tier 1 stays SA-free
regardless, so the flagship military library is maximally clean either way.

## Harvest workflow (hand to Claude Code)

1. **Candidate harvest script:** for each type in the list, query the Commons
   API (`action=query`, `generator=search` or category members,
   `prop=imageinfo`, `iiprop=url|extmetadata`). Filter
   `LicenseShortName` ∈ {Public domain / PD-USGov family, CC BY (any version),
   OGL} for the military tiers; Layer 2 additionally accepts CC-BY-SA. Always
   reject NC, ND, and anything ambiguous. The same harvest/ingest machinery
   runs both layers — only the curation step differs (full pick vs spot-check).
2. **Emit a pick sheet:** one HTML page per tier — thumbnails, license badge,
   author — human clicks the winner per type. Curation stays human; plumbing
   doesn't.
3. **Ingest:** the existing upload script consumes the pick, resizes to the
   150 px baseline-JPEG sprite spec, writes `photo:<TYPE>` to KV, and writes the
   manifest row (key, source URL, author, license, credit line) auto-filled from
   extmetadata. Script refuses ingest with empty license fields.
4. **Attribution page:** generate `photo-credits` (static, served by the Worker
   or on valarsystems.com) from the manifest; link it from the device config
   page. This satisfies CC-BY and OGL attribution in one place and is good
   manners for the PD shots.
5. **Gaps:** whatever Commons couldn't fill, pull from DVIDS/af.mil manually —
   credit-line test — and ingest with hand-entered manifest fields.

Deliberately out of scope for v1: drones/UAVs (rarely on ADS-B usefully),
one-off exotics, per-hex celebrity airframes beyond a handful of favorites.
