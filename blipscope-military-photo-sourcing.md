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
