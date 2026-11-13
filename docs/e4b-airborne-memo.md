# "E-4B airborne" on the Blipscope: a design memo (nothing built)

**Status: design only, for review.** Daniel asked for a "Doomsday plane" (E-4B) alert on the Blipscope (2026-10-28).
This memo covers detection in each mode, the identification rule, the UX, why it stays off the Missileer device, and
data licensing. Nothing is built until it is ruled. On the device the feature is called **"E-4B airborne"**; the
nickname is never shown.

## 0. Found while preparing this, and worth fixing first

**The Missileer firmware on main labels four C-2A Greyhounds as E-4Bs.**
- `src/eam/EamModels.cpp` holds `kE4bHexes = { ae0451, ae0452, ae0453, ae0454 }`.
- `ClassifyAbncp()` returns "E-4B" for those addresses, and `DefaultAbncpWatch()` puts them in the OpenSky watch.
- #409's own citations (adsbdb, hexdb.io and tar1090-db) show all four as **C-2A Greyhounds**.
- #409 replaces them with the cited E-4B addresses. It has been open since 2026-10-13 with no review decision.

This is exactly the failure this memo's rule exists to prevent: a type stated on the strength of an address that is
not that type. **Recommendation:** rule on #409 before any E-4B work, and have both firmware editions read one table
(section 2).

## 1. Detection, per mode

There are two different questions, and they need different data:
- **In range:** an E-4B is on this device's own radar.
- **Airborne anywhere:** an E-4B is transmitting somewhere, whether this device can see it or not.

### 1a. In range: on the device, in every mode, with no database

The device already holds each contact's 24-bit ICAO address in every mode: Blipscope Cloud's blips, the owner's
local `aircraft.json`, and OpenSky. The identity rule (section 2) is four addresses, so it runs on the device, with
no lookup and no aircraft database.
- A cloud blip carries no type (`proxy/src/schema.ts`: hex, callsign, position, altitude, speed, track, vertical
  rate, category, age).
- The type reaches the device through Blipscope Cloud's `/enrich`, which every mode already uses for its cards
  (`src/AircraftManager.cpp`, the comment above `fetchPhoto`: BYO is about positions, never enrichment).
- That type is used only as a **veto** (section 2), never to identify.

### 1b. Airborne anywhere

**Network mode.** The source is adsb.fi, which Blipscope Cloud already uses under the written grant that covers
customer devices (`proxy/FEED-SOURCING.md`). Its `/v2/mil` view carries the E-4Bs whenever they transmit ADS-B.

**Local mode.** The owner's receiver sees only its own neighbourhood, so it cannot answer this question.
- **The brief's proposal:** read "E-4B airborne anywhere" from the EAM feed server (`valar-eam-feed`, the backend
  behind missileer.watch).
- It already computes exactly this. `GET /status/abncp` comes from adsb.fi's `/v2/mil`, polled once every 90 s, with
  the E-4B rule from #409 (`wiki/Data-Sources.md`).
- Its public reads are open GETs. The Missileer device's only identifying header is `X-Missileer-Device`, a
  leaderboard id. A Blipscope would send none: it has no reason to identify itself to another product's backend.

**One fact changes the trade-off.** A local-mode Blipscope **already talks to Blipscope Cloud** for every card
(section 1a). So there are two ways to wire "anywhere":

| | A (the brief's) | B (recommended) |
|---|---|---|
| network mode | Blipscope Cloud | Blipscope Cloud |
| local and OpenSky modes | the EAM feed server, read directly | Blipscope Cloud |
| where the E-4B rule runs for "anywhere" | the EAM feed (and the Cloud, if it reads `/v2/mil` itself) | **once**, in the EAM feed. The Cloud reads `/status/abncp` server-to-server, caches it and fans out |
| device code paths | two sources, which can disagree (different poll times) | one |
| a Blipscope depends on | the Missileer backend's uptime, in local mode | the Cloud only, which already serves its cards |
| upstream calls | adds one adsb.fi `/v2/mil` reader, if the Cloud reads it itself | none new: the EAM feed already polls |

**B** keeps one identity implementation and one device path, and no Blipscope ever reaches into the Missileer
backend. Both A and B use adsb.fi data under the same grant. This is a decision for Daniel and the reviewer.

**What the device needs for "anywhere":** a yes or no, and how long ago it was last seen. **No positions.** The line
says only that one is airborne. Where it is shows only when it is on this device's own radar (1a).

## 2. Identification: only a clearly identified E-4B

**The rule.** This is #409's, as the EAM feed already applies it.
- **The address:** an E-4B is one of four ICAO addresses: `ADFEB3` (73-1676), `ADFEB4` (73-1677), `ADFEB5` (74-0787)
  and `ADFEB6` (75-0125). These are #409's cited table, ruled 2026-10-12.
- **The type, as a veto:** at those addresses, a row counts if its type is `B742` or `E4` (compared without case,
  spaces or punctuation) **or if it has no type**. Any other type there fails. The address is the identity; the type
  can only veto it.

**Never counted:**
- **`B742` on its own:** the VC-25As (`ADFDF8`, `ADFDF9`) are `B742` too.
- **Anything inferred:** the US military address block; the on-screen MIL tag; the radar's watchlist prefixes (an
  `adfeb` prefix would also match `ADFEB0`-`ADFEB2` and `ADFEB7`-`ADFEBF`); callsigns, which are reused and not
  fixed; the emitter category; or any untagged, anonymous or tactical address (TIS-B track ids, non-ICAO addresses).
- **In local mode, a non-ICAO row is refused before the table is consulted.** The local parser already reads
  readsb's `type`, the message-source kind. A row marked `*_other` or `tisb_trackfile`, or a hex with readsb's `~`
  prefix, carries no ICAO address at all.
- **A new airframe** is not recognised until its address is added by a ruling.

**One table, both editions.** The four addresses would live in one pure header in `include/`, which the Missileer
build (after #409) and the radar build both read. A host test would compare it with the EAM feed's default list,
taking its input from the other side of the contract, so the two cannot drift.

**Stated plainly: some E-4B flights will not show, and the device must never imply otherwise.**
- An E-4B can fly with ADS-B position off, or transmit only what aggregators cannot position.
- It can be outside every feeder's coverage, over the ocean or in remote areas.
- An airframe outside the table is not recognised.

So **no line never means none airborne.** The device shows only the positive line, and it has **no "none airborne"
string at all**. That is asserted when it is built (section 6).

## 3. UX

**On the screen,** calm and small:
- **The line:** while the "anywhere" flag is set, one status line on the Radar screen reads **"E-4B airborne"**, in
  the radar's ordinary label colour. No ring, no flash, no colour of alarm.
- **In range:** the contact also gets a plain **"E-4B"** tag on its blip, in the list and on its card, in place of the
  generic MIL tag.
- **Out of range:** the line appears with no position.

**No phone alert** (Daniel, 2026-10-28). The ntfy setting, trigger and re-arm interval this memo once proposed are
dropped, and ntfy itself is being removed from every edition (`docs/ntfy-removal-plan.md`).

**The setting:** one checkbox on the config page, **"E-4B airborne"**, which turns the line and the tag on and off.
Its description is **"An E-4B is showing on ADS-B"**. It is **off by default** (Daniel, question 6): nothing shows until
an owner ticks it.

**The text, exactly:** the line **"E-4B airborne"**, the tag **"E-4B"**, and the sentence **"An E-4B is showing on
ADS-B"** wherever the feature needs one. **The feature's own text never names the source.**

**Never in any string:** EAMs, messages, Missileer or its traffic, "doomsday", anything alarming, or the name of a
source. This is asserted by construction when built: a host test over every string the feature can emit, with a
control proving the test can see a planted word.

**Attribution:** adsb.fi's credit stays where it already is, on the config page and `/credits`.

## 4. Not on the Missileer device

- Aircraft and messages never share a screen. The feature compiles **only into the radar build**: `src/eam/` and the
  `missileer-*` envs never get it.
- Missileer's existing command-post screen is its own screen, and this memo does not change it. Section 0 is about its
  data, not its design.
- **Asserted when built:** the feature's strings are absent from every `missileer-*` ELF, by the same kind of scan
  `scripts/check-no-bench-hooks.sh` runs, with a positive control on a radar ELF.

## 5. Data licensing

| data | what it is | licence or permission | used by this design |
|---|---|---|---|
| the four E-4B addresses | four public identifiers, each tied to a serial, cited in #409 (Joe Baugher's serial list, tar1090-db, adsbdb, hexdb.io) | facts, not a database. Nothing is copied from any source's table. *To confirm with Daniel:* we read four identifiers as no substantial part of any database | yes: the identity |
| adsb.fi | positions and types; the `/v2/mil` view | a **written** commercial grant, which covers customer devices. Conditions: its rate limit, plus a citation and a link | yes: "anywhere" (both A and B) |
| Blipscope Cloud's enrich types | adsb.lol (ODbL 1.0), with adsbdb as the type backfill | as the repo already records them | only as the veto, when a type is known |
| plane-alert-db | the curated military list the EAM feed uses for labels | ODbL 1.0: attribution, and share-alike for a derived database | **no** |
| Mictronics aircraft database | type names and the military export | ODC-By 1.0: attribution | **no** |
| the owner's own receiver database | whatever readsb's `--db-file` holds on their machine, often tar1090-db | the owner's own install; the device redistributes nothing. The local parser reads readsb's `type` (the message-source kind), never the aircraft type `t`, and this design adds nothing that reads `t`. tar1090-db's licence is not recorded in this repo, so check it before any use | **no** |
| OpenSky | positions, in OpenSky mode only | device-side, bring your own account. Never through a shared backend (`valar-eam-feed` `wiki/Data-Sources.md`) | in range only, from the owner's own data |

**For local mode specifically:** **no aircraft database ships on the device**. The identity is four constants, and
"anywhere" is adsb.fi data under the grant, wired either way (A or B).

## 6. When it is built: what gets predicted first (a sketch, not frozen)

**Host rows:**
- the rule table: each address against `B742`, `E4`, untyped and another type;
- `B742` at a VC-25A address, refused;
- an untyped address in the military block, refused;
- a callsign, never consulted. The sabotage is a callsign match, and that row must turn red.

**Strings:**
- the feature's three texts are exactly "E-4B airborne", "E-4B" and "An E-4B is showing on ADS-B";
- no forbidden word and no source name, with a planted-word control;
- no negative statement ("none", "no E-4B");
- the line has no digit.

**Builds:**
- the strings are absent from every `missileer-*` ELF and present in a radar ELF (the control);
- **the setting defaults to off**: with the key absent, neither the line nor the tag draws. It is a new key, so the
  default reaches every device;
- no ntfy path: the feature has no sender of any kind.

**Board:** the line and the tag in range, from a bench feed that replays a recorded E-4B row. Never a live one,
since one may not fly during the window.

## 7. Questions for Daniel and the reviewer

1. Wire "anywhere" as A or B. **Ruled: B.**
2. Is the in-range "E-4B" tag wanted, beyond the line? **Answered: yes, "E-4B".**
3. The ntfy re-arm interval. **Dropped: no phone alert.**
4. The exact wording. **Answered: "E-4B airborne", and "An E-4B is showing on ADS-B".**
5. Rule on #409 first? **Daniel gave it to this session.** The table fix is its own small PR off main.
6. Is the "E-4B airborne" setting on or off by default? **Answered by Daniel: "Off by default".** Nothing shows until
   an owner ticks it on the config page. This memo had recommended on.

## Rulings, 2026-10-28 (reviewer chat, pasted by Daniel), recorded with their source

- **Accepted as a design.** It is built after #445 merges, not in it.
- **Q1: B.** Blipscope Cloud reads the EAM feed's `/status/abncp` server-to-server, caches it and fans it out.
  - **Unknown, not "no":** if the EAM feed can't be read, or its answer is stale, the Cloud says "unknown" and devices
    show nothing.
  - **When it is built,** a recorded `/status/abncp` reply pins the contract in our tests, and Daniel tells the EAM
    feed's owner it has a second reader.
- **Q2-Q4 are Daniel's.** He has been asked. The reviewer recommended the in-range tag, a 6 h re-arm, and the wording
  as written.
- **Q5, #409:** Daniel decides who reviews it, since it is the Missileer's PR.
  - The reviewer checked section 0: `AE0451` is a Navy C-2A (162157), and `ADFEB3` is E-4B 73-1676.
- **The four addresses are facts, not a database:** agreed.
- **The ROADMAP line for the themes:** yes, in the next docs PR. It is in this one, as ROADMAP F2, beside this memo as
  F1.

## Daniel's answers, 2026-10-28 (reviewer chat, pasted by Daniel), recorded with their source

- **The name:** "E-4B is ok name." The in-range tag reads "E-4B", and the line "E-4B airborne".
- **No phone alert:** "I don't want phone alerts any more. I thought got rid of ntfy. If not, i want it completely
  gone". The memo's ntfy setting, trigger and re-arm interval are dropped. ntfy's removal from every edition is its own
  plan: `docs/ntfy-removal-plan.md`.
- **The wording:** "An E-4B is showing on ADS-B", wherever the feature needs a sentence (for example, the setting's
  description on the config page). The feature's own text never names the source; adsb.fi's credit stays where it
  already is.
- **#409:** Daniel gave it to this session ("You can fix it"). The table fix goes now, as a small PR off main. The rest
  of #409 is rebased after #445 merges.
- **The default (question 6), 2026-10-28:** asked whether new firmware should show the E-4B line and tag without the
  owner turning them on, Daniel chose **"Off by default"**. Nothing shows until an owner ticks it on the config page.
  It is a new key, so the default reaches every device. When the feature is built, "the setting defaults to off" is
  one of its predicted rows.
