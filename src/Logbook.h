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
    // biggest allocation alive at once is a single serialized store plus a small
    // carry buffer -- independent of how big the LOGBOOK gets, but NOT independent
    // of how big one STORE gets.
    //
    // That distinction stopped being academic in v5. This said "<=MAX_BLOB, ~5 KB"
    // when there was one shared 5200-byte ceiling; the caps are now much larger and
    // the ceiling is per-store, so the real bound is max(MAX_BLOB_*) = 12 KB. It is
    // the reason those ceilings are 12 KB rather than whatever the NVS entry budget
    // would have allowed: this allocation and a TLS handshake's ~16.7 KB have to be
    // able to coexist. heaphealth::CanHandshake() will defer enrichment rather than
    // fail if they briefly cannot, so the worst case is a few seconds of deferred
    // enrichment while an owner reads their collection.
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

    /**
     * Flush now, ignoring the debounce. For moments where not writing LOSES data
     * rather than delaying it -- switching the logbook off, which otherwise
     * strands every unflushed change in RAM (MaybePersist() is gated on the same
     * flag that was just cleared).
     */
    void PersistNow();

    /** Flush because the Collection page is being read. Dirty-only, rate-limited. */
    void MaybePersistForFetch();

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
    // Entries EVICTED to make room, per store. Distinct from `rejected`: an
    // eviction means the book kept growing and forgot something unclaimed, a
    // rejection means it could not grow at all. Conflating them would hide which
    // of the two a device is doing.
    uint16_t evicted[StCount] = {0, 0, 0, 0};
    uint32_t contacts = 0;
    Record recHigh, recFast, recNear;
    bool dirty = false;
    // Begin() loads NVS ONCE and this flag is what makes it idempotent.
    //
    // THE INVARIANT THAT DEPENDS ON IT, written down because it was violated:
    // while the device is running, RAM IS AUTHORITATIVE and NVS is its durable
    // mirror. Re-entering Begin() must NOT reload, or a reload would clobber
    // live entries with an older snapshot.
    //
    // That is only safe while every path OUT of logging flushes first. Disabling
    // the logbook did not, so RAM and NVS could diverge and stay diverged for the
    // rest of the session -- re-enabling never reloaded, and nothing ever wrote.
    // PersistNow() on the disable edge (AircraftManager::Initialise) is what
    // closes that hole; do not remove one without the other.
    bool started = false;
    unsigned long lastPersist = 0;
    unsigned long lastFetchPersist = 0;
    /** Minimum gap between FETCH-triggered writes. A refreshable page is an
     *  unbounded write trigger, and flash wear is a product-lifetime budget. */
    static constexpr unsigned long FETCH_PERSIST_MIN_MS = 30UL * 1000UL;

    /** The single writer. All three public entry points funnel here. */
    void persist();

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
    // v5 (2026-08-10) raised all four, sized against a MEASURED budget rather than
    // a guess. The bench s3-128 reports 320 of 2646 NVS entries used with an empty
    // logbook; subtracting that and the page NVS reserves for garbage collection
    // leaves ~2204 entries, ~70 KB. See reportCapacity() in Logbook.cpp, which
    // prints the figure on every boot so this can be checked rather than believed.
    //
    // The old caps were not a storage decision, they were an unexamined one, and
    // the sky settled it: 220/220 types, 120/120 airlines, 300/300 airports after
    // ONE WEEK under a GA-heavy sky (bench, 2026-08-08). A lifelist that fills in a
    // week is not a lifelist.
    //
    // Worst case at these caps, against the per-store ceilings below:
    //
    //   types      500 x 22 B = 11,000
    //   operators  220 x 52 B = 11,440   (52 = MAX_OP_LEN 40 + day + claimDay)
    //   countries  200 x 44 B =  8,800
    //   airports   600 x 16 B =  9,600
    //                           ------
    //                           40,840 B = ~1277 entries, 58% of budget
    //
    // The remaining ~930 entries are margin for config, the records, and NVS's own
    // churn. Raising these again means re-reading the boot line first.
    //
    // Two numbers live here and they are NOT the same: the caps need ~1277 entries,
    // while the byte ceilings below permit 1375. The boot line reports the ceiling
    // total, because that is the bound that cannot be exceeded, and it says
    // "ceilings" rather than "caps" so the two can be told apart on sight.
    //
    // OPERATORS IS CAPPED BY A SECOND CONSTRAINT, not by the NVS budget. JsonStream
    // loads ONE WHOLE STORE at a time (see below), so the largest store is also the
    // largest contiguous allocation /logbook.json can ask for -- on a board where a
    // TLS handshake needs ~16.7 KB contiguous of its own. That is what holds the
    // per-store ceilings at 12 KB and operators at 220 rather than the ~250 the
    // entry budget alone would allow.
    static constexpr size_t MAX_TYPES     = 500;
    static constexpr size_t MAX_OPERATORS = 220;
    static constexpr size_t MAX_COUNTRIES = 200; // there are ~195 countries
    static constexpr size_t MAX_AIRPORTS  = 600; // codes are <=4 chars
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
    //
    // v5 (2026-08-10) did the widening, 24 -> 40, WITH the migration the paragraph
    // above demands -- see adoptTruncatedOperator() in Logbook.cpp. The migration
    // cannot be a startup pass, because the full name is exactly what the old store
    // did not keep: "AIR WISCONSIN AIRLINES L" cannot be expanded from itself. So
    // it runs lazily instead, at the moment an aircraft supplies the long spelling,
    // and carries firstDay + claimDay across then. An airline that never flies over
    // again keeps its old truncated entry and its claim; nothing is dropped.
    static constexpr size_t MAX_OP_LEN     = 40; // truncate long operator/owner names
    static constexpr size_t OLD_MAX_OP_LEN = 24; // the v4 cut, for the lazy migration
    static constexpr size_t MAX_CN_LEN     = 32; // truncate long country names
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
    // Affordable now: blipscope-s3-128 has an 84 KB / 2646-entry NVS
    // (partitions-s3-16mb-bignvs.csv). Note "blipscope-s3-128", not "the SKU" --
    // this said "the SKU moved to" until 2026-08-10, which was true of exactly one
    // and read as if it meant the fleet. reportCapacity() now checks the partition
    // the board actually booted with, so this paragraph can no longer be the only
    // thing standing between the caps and a partition that cannot hold them.
    //
    // v5 splits the single ceiling into one per store. The point is the ORDER the
    // two limits bind in: the count caps above must always bind FIRST, so eviction
    // handles a full store gracefully, and these byte ceilings are a pure safety
    // net that never fires in normal operation. Tail-truncation drops the
    // alphabetically-last entries silently, which is why it must stay unreachable
    // -- each ceiling sits comfortably above cap x worst-case record.
    static constexpr size_t MAX_BLOB_TYPES     = 12000; // cap needs 11,000
    static constexpr size_t MAX_BLOB_OPERATORS = 12000; // cap needs 11,440
    static constexpr size_t MAX_BLOB_COUNTRIES =  9500; // cap needs  8,800
    static constexpr size_t MAX_BLOB_AIRPORTS  = 10500; // cap needs  9,600
    static constexpr size_t MAX_BLOB_TOTAL     = MAX_BLOB_TYPES + MAX_BLOB_OPERATORS
                                               + MAX_BLOB_COUNTRIES + MAX_BLOB_AIRPORTS;
    static constexpr unsigned long PERSIST_INTERVAL_MS = 10UL * 60UL * 1000UL; // 10 min

    // Report the running partition's real capacity against what the caps want,
    // and warn when this SKU's partition table cannot hold them. See the
    // definition -- the NVS size varies 4x across shipping SKUs.
    void reportCapacity();

    // Make room in a full store by forgetting its least valuable UNCLAIMED entry.
    // Returns false when every entry is claimed, which is the one case that must
    // still refuse. See the definitions for why a claim is never evictable.
    bool evictOneType();
    bool evictOneSeen(std::map<String, SeenStat>& store, Store which);
    // One-time pass at load: fold EVERY entry banked under "<base>, CO-OWNER"
    // onto its base key. Returns how many were folded. Reopens a store that has
    // already filled, which truncation alone cannot do. Recomputes
    // claimedOperators rather than adjusting it.
    uint16_t foldCommaVariants();
    // Fold an entry banked under "<base>, CO-OWNER" onto its base key, carrying
    // firstDay and claimDay. One operator's own aliases, never distinct entries.
    bool adoptCommaVariant(const String& base);
    // Lazily re-key a v4 entry truncated at OLD_MAX_OP_LEN onto its full spelling,
    // carrying firstDay and claimDay across. Returns true if it adopted one.
    bool adoptTruncatedOperator(const String& fullName);

    static void loadRecord(Preferences& p, const char* key, Record& out);
    void saveRecord(const char* key, const Record& r);
    // True when the offered value beats the stored one (bigger wins unless
    // smallerWins), updating the record in place.
    bool offerRecord(Record& r, const String& cs, float value, bool smallerWins);
};
