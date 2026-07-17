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
class Logbook {
public:
    struct TypeStat {
        uint16_t firstDay = 0; // days since epoch of the first sighting (0 = unknown)
        uint16_t count = 0;    // sightings (one per tracking session), saturating
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
    const std::map<String, uint16_t>& Operators() const  { return operators; }
    const std::set<String>& Countries() const            { return countries; }
    const std::set<String>& Airports() const             { return airports; }
    const Record& HighRecord() const { return recHigh; }
    const Record& FastRecord() const { return recFast; }
    const Record& NearRecord() const { return recNear; }

    // Days-since-epoch for "now" (0 when NTP hasn't synced), shared with display code.
    static uint16_t TodayEpochDay();

    // Full lifelist as JSON, read straight from NVS (the last debounced persist,
    // so at most ~10 min stale). Static + read-only on purpose: it's served by
    // the async web task (/logbook.json) and must never touch the live maps the
    // loop task mutates -- the same pattern as the EAM edition's log export.
    static String ExportJson();

    void MaybePersist(); // flush to NVS when dirty and the debounce has elapsed

private:
    std::map<String, TypeStat> types;      // type code -> first seen + count
    std::map<String, uint16_t> operators;  // airline -> first-seen day
    std::set<String> countries;
    std::set<String> airports; // IATA/ICAO codes seen as route endpoints
    uint32_t contacts = 0;
    Record recHigh, recFast, recNear;
    bool dirty = false;
    bool started = false;
    unsigned long lastPersist = 0;

    Preferences prefs;

    // Bounds chosen so each serialized store stays under the NVS ~4000-byte
    // string cap: types "CODE|day|count" is <=17 B worst case (220 x 17 = 3740),
    // operators "NAME|day" is <=31 B (120 x 31 = 3720). v2 lowered the caps from
    // 400/140 -- existing over-cap lists still load and persist (MAX_BLOB is the
    // hard ceiling); they just can't grow further.
    static constexpr size_t MAX_TYPES     = 220;
    static constexpr size_t MAX_OPERATORS = 120;
    static constexpr size_t MAX_COUNTRIES = 64;
    static constexpr size_t MAX_AIRPORTS  = 300; // codes are <=4 chars: 300 x 5 = 1500 B
    static constexpr size_t MAX_OP_LEN    = 24;   // truncate long airline names
    static constexpr size_t MAX_BLOB      = 3800; // hard ceiling per serialized store
    static constexpr unsigned long PERSIST_INTERVAL_MS = 10UL * 60UL * 1000UL; // 10 min

    static void loadRecord(Preferences& p, const char* key, Record& out);
    void saveRecord(const char* key, const Record& r);
    // True when the offered value beats the stored one (bigger wins unless
    // smallerWins), updating the record in place.
    bool offerRecord(Record& r, const String& cs, float value, bool smallerWins);
};
