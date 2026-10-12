# WC-135R Constant Phoenix: the three airframes, with cited ICAO hexes

Ruled 2026-10-12 (Daniel): *"WC-135R: our own hex list from public sources, cited."*

This is that list. Every row has two kinds of source: one that says the **serial is a
WC-135R**, and registries that say **which ICAO 24-bit address belongs to that serial**.
Nothing here is inferred, and a hex no source states would have been left out. None were.

**Where this should live is an open question** (end of this page). Nothing in the repo
reads it yet.

## The list

| USAF serial | ICAO hex | type in the registries | WC-135R since |
|---|---|---|---|
| 64-14836 | `AE048D` | `W135` (tar1090-db, hexdb.io); adsbdb still says `K35R` | July 2022 (first) |
| 64-14831 | `AE07C6` | `W135` (tar1090-db, hexdb.io); adsbdb still says `K35R` | May 2023 (second) |
| 64-14829 | `AE037F` | `W135` (tar1090-db, hexdb.io); adsbdb still says `K35R` | December 2023 (third, final) |

Three, and only three: the Air Force converted three KC-135Rs, and the third arrival
completed the fleet (sources below).

## Sources: the serial is a WC-135R

- **64-14836**
  - The War Zone, "Air Force's First 'New' Constant Phoenix Nuke-Sniffing Jet Has Flown":
    *"The KC-135R, with the serial number 64-14836, now converted into the WC-135R Constant
    Phoenix configuration"*.
    <https://www.twz.com/air-forces-first-wc-135r-constant-phoenix-nuke-sniffing-jet-has-flown>
  - Wikipedia, "Boeing WC-135 Constant Phoenix": *"the first of three planned WC-135R aircraft
    (serial number 64-14836)"*.
    <https://en.wikipedia.org/wiki/Boeing_WC-135_Constant_Phoenix>
  - Offutt AFB, "Team Offutt welcomes first WC-135R to its fleet" (July 2022).
    <https://www.offutt.af.mil/News/Article/3090736/team-offutt-welcomes-first-wc-135r-to-its-fleet/>
- **64-14831**
  - Offutt AFB, "Second WC-135R arrives at Offutt" (May 2023).
    <https://www.offutt.af.mil/News/Article-Display/Article/3394549/second-wc-135r-arrives-at-offutt/>
  - Wikipedia (same page): *"The other two were delivered in May 2023 (tail number 64-14831)
    and December 2023 (tail number 64-14829)"*.
- **64-14829**
  - Air & Space Forces Magazine, "WC-135 'Nuke-Sniffer' Fleet Upgrades Are Complete, USAF Says":
    *"The third R-model, tail number 64-14829, was initially delivered to the Air Force in
    1964"*; *"The new fanjet aircraft arrived at Offutt Air Force Base, Neb., on Dec. 4."*
    <https://www.airandspaceforces.com/air-force-wc-135-nuke-sniffer/>
  - Offutt AFB, "Third, final WC-135R Constant Phoenix arrives" (December 2023).
    <https://www.offutt.af.mil/News/Article-Display/Article/3618085/third-final-wc-135r-constant-phoenix-arrives/>
  - Wikipedia (same page, quoted above).

The offutt.af.mil pages return HTTP 403 to a scripted fetch, so their wording was not read
directly; they are listed because they are the primary source, and each serial is also
quoted above from a page that was read.

## Sources: the hex belongs to the serial

Three registries, read 2026-10-12. They agree on every hex.

- **tar1090-db** (wiedehopf; the Mictronics/readsb aircraft database that readsb-based
  trackers load for registration and type). Pinned commit
  `17679e257733ad808a417cf4a5d52f3b00238887`, file `db/A.js` (gzip-compressed JSON; keys drop
  the shard's leading `A`):
  <https://github.com/wiedehopf/tar1090-db/blob/17679e257733ad808a417cf4a5d52f3b00238887/db/A.js>
  ```
  "E048D":["64-14836","W135","10","Boeing WC-135R Constant Phoenix"]
  "E07C6":["64-14831","W135","10","BOEING WC-135R"]
  "E037F":["64-14829","W135","10","BOEING WC-135R"]
  ```
- **hexdb.io**, by hex:
  - <https://hexdb.io/api/v1/aircraft/AE048D>: `"Registration": "64-14836"`, `"ICAOTypeCode": "W135"`, `"Type": "WC-135R"`
  - <https://hexdb.io/api/v1/aircraft/AE07C6>: `"Registration": "64-14831"`, `"ICAOTypeCode": "W135"`, `"Type": "WC-135R"`
  - <https://hexdb.io/api/v1/aircraft/AE037F>: `"Registration": "64-14829"`, `"ICAOTypeCode": "W135"`, `"Type": "WC-135R"`
- **adsbdb**, by serial:
  - <https://api.adsbdb.com/v0/aircraft/64-14836>: `"mode_s": "AE048D"`
  - <https://api.adsbdb.com/v0/aircraft/64-14831>: `"mode_s": "AE07C6"`
  - <https://api.adsbdb.com/v0/aircraft/64-14829>: `"mode_s": "AE037F"`

  adsbdb types all three `"KC-135R"` / `K35R`, their type before conversion. Its hexes agree
  with the other two; its type is stale. So match these airframes by **hex**, not by a type
  code: a list keyed on `K35R` would miss them on tar1090-db data, and one keyed on `W135`
  would miss them on adsbdb data.

## Open question: where should this live?

The repo has no list of named military airframes for this to join. What exists:

- [`src/SpecialAircraft.cpp`](../src/SpecialAircraft.cpp) and
  [`proxy/src/military.ts`](../proxy/src/military.ts) hold address **ranges** (the "MIL" tag),
  never individual airframes.
- [`docs/per-hex-overrides.md`](per-hex-overrides.md) registers `mil:<hex>` overrides, which
  exist only to fill a card both databases leave blank. These three are not blank.
- The Missileer mil-air screen shows what valar-eam-feed's `/status/milair` sends; that
  watchlist (`MILAIR_WATCHLIST`) is in valar-eam-feed, which this repo does not change.

So, for Daniel: should this list (a) stay a reference here, (b) become `hex:` entries in
valar-eam-feed's `MILAIR_WATCHLIST`, (c) become a firmware table, or (d) something else?
