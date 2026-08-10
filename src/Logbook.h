#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <map>
#include <set>

// Persistent "lifelist" of what this device has ever seen overhead. v2 (2026-07)
// deepens the original membership sets into a real spotting log:
//   - per-TYPE first-seen date and sighting count (one count per tracking session);
//   - per-AIRLINE first-seen date;
//   - lifetime record holders: highest / fastest / closest contact ever, with
//     callsign and date;
//   - the original countries set and contact odometer.
// Backed by its own NVS namespace and written back lazily (debounced) to spare
// the flash. Every store is bounded so the logbook can never overrun NVS and
// starve the config namespace that shares the same partition.
//
// Dates are uint16 days-since-Unix-epoch (good to year 2149); 0 = unknown --
// either the clock wasn't NTP-synced yet or the entry predates v2 (legacy blobs
// parse with day 0 / count 1).
// SEEN vs CLAIMED (v4, 2026-08-03). The logbook records two different things and
// the difference is the whole point of the leaderboard rework:
//
//   SEEN     the device received this type/airline/country/airport overhead. It
//            happens on its own, and it measures the ANTENNA -- how busy the sky
//            is and how good the reception is, neither of which the owner picked.
//   CLAIMED  the owner opened this aircraft's detail card. It measures the
//            SPOTTER, and it is the only thing that scores.
//
// Both are kept. "47 of 153 types claimed" is the number that makes the device
// worth looking at; a scoreboard built on `seen` just ranks airspace.
//
// A tap claims EVERYTHING that aircraft is carrying -- its type, its airline, its
// origin country and its route airports -- so one deliberate action credits the
// whole card and the categories stay meaningful without a second mechanic.
class Logbook {
public:
    struct TypeStat {
        uint16_t firstDay = 0; // days since epoch of the first sighting (0 = unknown)
        uint16_t count = 0;    // sightings (one per tracking session), saturating
        uint16_t claimDay = 0; // days since epoch of the claim (0 = NEVER CLAIMED)
    };

    // Every claimed thing carries the day it was claimed; 0 means unclaimed. The
    // sentinel doubles as the migration and the reset: a v3 record has no claim
    // field, parses as 0, and is therefore unclaimed. Nothing to migrate, and
    // existing scores go to zero on their own -- which is what was wanted.
    struct SeenStat {
        uint16_t firstDay = 0;
        uint16_t claimDay = 0;
    };

    struct Record {
        String callsign;
        float value = 0.0f; // ft (high), kt (fast), or km (near)
        uint16_t day = 0;   // when it was set
        bool set = false;
    };

    void Begin(); // load persisted state from NVS (call once)

    // Record a sighting's metadata. NoteType/NoteOperator/NoteCountry return
    // true only when it was a brand-new entry (first time ever seen), so the
    // caller can flag a fresh catch on screen. A repeat type sighting bumps its
    // count. Empty / at-capacity inputs return false.
    bool NoteType(const String& typeCode);
    bool NoteOperator(const String& operatorName);
    bool NoteCountry(const String& country);
    bool NoteAirport(const String& airportCode); // route origin/dest codes from enrichment

    void NoteContact(); // a new contact entered range; bumps the odometer

    // ---- claiming (the scoring half) ----------------------------------------
    // A type is claimable while unclaimed, however long ago it was first seen --
    // that is what makes a C-5 that passed overnight worth a tap the next time
    // one appears. Claim* returns true only on the transition, so the caller can
    // show a confirmation exactly once.
    bool IsTypeClaimed(const String& typeCode) const;
    // The badge predicate. NOT !IsTypeClaimed(): that answers "absent" with
    // "claimable" while ClaimType() answers it with "reject", and once a store is
    // full every new type IS absent. See the capacity note in the .cpp.
    bool IsTypeClaimable(const String& typeCode) const;
    bool ClaimType(const String& typeCode);
    bool ClaimOperator(const String& operatorName);
    bool ClaimCountry(const String& country);
    bool ClaimAirport(const String& airportCode);

    size_t ClaimedTypeCount() const     { return claimedTypes; }
    size_t ClaimedOperatorCount() const { return claimedOperators; }
    size_t ClaimedCountryCount() const  { return claimedCountries; }
    size_t ClaimedAirportCount() const  { return claimedAirports; }

    // Offer one contact's measurements against the lifetime records; any that
    // beat the stored holder replace it. Caller supplies display units and is
    // responsible for plausibility bounds.
    void NoteBest(const String& callsign, float altFt, float speedKt, float distKm);

    size_t TypeCount() const     { return types.size(); }
    size_t OperatorCount() const { return operators.size(); }
    size_t CountryCount() const  { return countries.size(); }
    size_t AirportCount() const  { return airports.size(); }
    uint32_t Contacts() const    { return contacts; }

    const std::map<String, TypeStat>& Types() const      { return types; }
    const std::map<String, SeenStat>& Operators() const  { return operators; }
    const std::map<String, SeenStat>& Countries() const  { return countries; }
    const std::map<String, SeenStat>& Airports() const   { return airports; }
    const Record& HighRecord() const { return recHigh; }
    const Record& FastRecord() const { return recFast; }
    const Record& NearRecord() const { return recNear; }

    // Days-since-epoch for "now" (0 when NTP hasn't synced), shared with display code.
    static uint16_t TodayEpochDay();

    // Full lifelist as JSON, read straight from NVS (the last debounced persist,
    // so at most ~10 min stale). Read-only on purpose: it's served by the async
    // web task (/logbook.json) and must never touch the live maps the loop task
    // mutates -- the same pattern as the EAM edition's log export.
    //
    // STREAMED, NOT BUILT. The old version returned one String and reserved 8 KB;
    // at full caps the document is ~25 KB, which would want a single contiguous
    // block on a device whose largest free block was measured at 36-44 KB with
    // TLS also wanting one. This walks the stores one at a time instead, so the
    // biggest allocation alive at once is a single serialized store (<=MAX_BLOB,
    // ~5 KB) plus a small carry buffer -- independent of how big the logbook gets.
    //
    // Usage: construct, then call Read() until it returns 0.
    class JsonStream {
    public:
        JsonStream();
        ~JsonStream();
        size_t Read(uint8_t* out, size_t maxLen); // 0 = complete
    private:
        bool Produce(); // append the next piece to `pending`; false when finished
        Preferences p;
        bool open = false;
        int phase = 0;
        size_t pos = 0;      // read offset into `blob`
        bool firstEl = true; // first element of the array being emitted
        String blob;         // the store currently being walked
        String pending;      // produced but not yet handed to the caller
        uint32_t nSeen[4] = {0, 0, 0, 0};    // types, airlines, countries, airports
        uint32_t nClaimed[4] = {0, 0, 0, 0};
    };

    void MaybePersist(); // flush to NVS when dirty and the debounce has elapsed

private:
    std::map<String, TypeStat> types;      // type code -> first seen + count + claim
    std::map<String, SeenStat> operators;  // airline -> first seen + claim
    std::map<String, SeenStat> countries;
    std::map<String, SeenStat> airports;   // IATA/ICAO codes seen as route endpoints
    // Claimed tallies, maintained incrementally. Counting them on demand would
    // walk four maps on every Stats frame and every leaderboard submit.
    uint16_t claimedTypes = 0, claimedOperators = 0, claimedCountries = 0, claimedAirports = 0;
    // First-time entries REFUSED because a store was full, saturating. These are
    // the only measurement of how fast a sky actually fills a store: a saturated
    // count tells you that it filled, never how fast it was filling, so a cap can
    // never be sized from a device that already hit one. Reported on the persist
    // line, and only when non-zero.
    enum Store : uint8_t { StTypes = 0, StOperators, StCountries, StAirports, StCount };
    uint16_t rejected[StCount] = {0, 0, 0, 0};
    bool warnedFull[StCount] = {false, false, false, false};
    void noteFull(Store s, const char* what, size_t cap);
    uint32_t contacts = 0;
    Record recHigh, recFast, recNear;
    bool dirty = false;
    bool started = false;
    unsigned long lastPersist = 0;

    Preferences prefs;

    // Bounds chosen so each serialized store stays under the NVS ~4000-byte
    // per-entry cap. NOTE (v3, 2026-07-31): the stores are written as BLOBs, not
    // strings -- a string must fit contiguously in one 4 KB page, which is what
    // made `operators` fail with NOT_ENOUGH_SPACE at only ~2.6 KB. Blobs are
    // chunked across pages by NVS, so the real remaining limit is total space in
    // the 20 KB partition shared with the config namespace. Watch the
    // `NVS entries free` figure on the [logbook] persist line before raising any
    // of these: types "CODE|day|count" is <=17 B worst case (220 x 17 = 3740),
    // operators "NAME|day" is <=31 B (120 x 31 = 3720). v2 lowered the caps from
    // 400/140 -- existing over-cap lists still load and persist (MAX_BLOB is the
    // hard ceiling); they just can't grow further.
    static constexpr size_t MAX_TYPES     = 220;
    static constexpr size_t MAX_OPERATORS = 120;
    static constexpr size_t MAX_COUNTRIES = 64;
    static constexpr size_t MAX_AIRPORTS  = 300; // codes are <=4 chars
    // CHANGING EITHER OF THESE ORPHANS EXISTING CLAIMS, and that is not obvious
    // from the fact that they look like display widths. The truncated name IS the
    // map key and IS what gets persisted, so widening the cut re-spells every
    // entry: "AIR WISCONSIN AIRLINES L" and "AIR WISCONSIN AIRLINES LLC" are two
    // different keys, the old one keeps the claim, and the aircraft that once
    // claimed it now enters as a new unclaimed entry under the new spelling. On a
    // store that is already at capacity the new spelling cannot even be inserted.
    //
    // So a widening is a MIGRATION, not a constant bump: it needs a pass that
    // re-keys the existing store and carries each claimDay across. Worth doing --
    // the current 24 produces "CSC DELAWARE TRUST CO TR" — but not by editing the
    // number. The Collection page marks a name cut at these lengths with an
    // ellipsis (ConfigurationWebServer.cpp) so a clipped name at least reads as
    // clipped; keep the two in step by hand.
    static constexpr size_t MAX_OP_LEN    = 24;  // truncate long operator/owner names
    static constexpr size_t MAX_CN_LEN    = 32;  // truncate long country names
    // v4 raised this from 3800 because every record grew a claim-day field, and
    // the two big stores no longer fit under the old ceiling:
    //
    //   types      "CODE|day|count|claimDay"  <=22 B x 220 = 4840
    //   operators  "NAME|day|claimDay"        <=36 B x 120 = 4320
    //   countries  "NAME|day|claimDay"        <=44 B x  64 = 2816
    //   airports   "CODE|day|claimDay"        <=16 B x 300 = 4800
    //
    // Truncating at the tail instead would silently drop the alphabetically-last
    // types -- and they would come back as unclaimed NEW on the next boot, so the
    // loss would look like a scoring bug rather than a storage one.
    //
    // Affordable now: the SKU moved to an 84 KB / 2646-entry NVS
    // (partitions-s3-16mb-bignvs.csv). Worst case here is ~17 KB of stores against
    // ~10 KB for wifi/phy/config, so ~1/3 of the partition at full caps. Blobs are
    // chunked across pages by NVS, so this needs the space, not contiguity.
    // Watch `NVS entries free` on the [logbook] persist line before raising again.
    static constexpr size_t MAX_BLOB      = 5200; // hard ceiling per serialized store
    static constexpr unsigned long PERSIST_INTERVAL_MS = 10UL * 60UL * 1000UL; // 10 min

    // Report the running partition's real capacity against what the caps want,
    // and warn when this SKU's partition table cannot hold them. See the
    // definition -- the NVS size varies 4x across shipping SKUs.
    void reportCapacity();

    static void loadRecord(Preferences& p, const char* key, Record& out);
    void saveRecord(const char* key, const Record& r);
    // True when the offered value beats the stored one (bigger wins unless
    // smallerWins), updating the record in place.
    bool offerRecord(Record& r, const String& cs, float value, bool smallerWins);
};
