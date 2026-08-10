#include "Logbook.h"

#include <nvs.h>

#include <cstring>
#include <ctime>
#include <memory>
#include <new>

namespace {

constexpr char SEP = '\n';   // record separator: newline, so airline names may contain commas/spaces
constexpr char FIELD = '|';  // field separator inside one record (never appears in codes/names we store)

// Split one blob record "A|B|C" into up to n fields. Returns how many were found.
int splitFields(const String& rec, String* out, int n)
{
    int field = 0, start = 0;
    for (int i = 0; i <= (int)rec.length() && field < n; ++i) {
        if (i == (int)rec.length() || rec[i] == FIELD) {
            out[field++] = rec.substring(start, i);
            start = i + 1;
        }
    }
    return field;
}

// Walk a newline-separated blob, invoking fn(record) per non-empty record.
template <typename Fn>
void forEachRecord(const String& blob, Fn fn)
{
    int start = 0;
    for (int i = 0; i <= (int)blob.length(); ++i) {
        if (i == (int)blob.length() || blob[i] == SEP) {
            if (i > start)
                fn(blob.substring(start, i));
            start = i + 1;
        }
    }
}

// Read one serialized store. v3 writes these as NVS BLOBs; v2 wrote STRINGs, so
// fall back on the string when the key still carries the legacy type (the first
// persist after an OTA rewrites it as a blob). getType() first, because calling
// getBytesLength() on a string-typed key logs an NVS_TYPE_MISMATCH error.
String readStore(Preferences& p, const char* key)
{
    if (!p.isKey(key))
        return "";
    if (p.getType(key) != PT_BLOB)
        return p.getString(key, ""); // legacy v2
    const size_t n = p.getBytesLength(key);
    if (n == 0)
        return "";
    std::unique_ptr<char[]> buf(new (std::nothrow) char[n + 1]);
    if (!buf)
        return "";
    p.getBytes(key, buf.get(), n);
    buf[n] = '\0';
    return String(buf.get());
}

} // namespace

namespace {

// yyyy-mm-dd from days-since-epoch; "" for the 0 "unknown" sentinel.
String dayToIso(uint16_t day)
{
    if (day == 0)
        return "";
    const time_t t = (time_t)day * 86400;
    struct tm tmv;
    gmtime_r(&t, &tmv);
    char buf[11];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return String(buf);
}

String jsonEscape(const String& s)
{
    String out;
    out.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c >= 0x20) out += c; // drop control chars
    }
    return out;
}

} // namespace

uint16_t Logbook::TodayEpochDay()
{
    const time_t utc = time(nullptr);
    if (utc < 1600000000)
        return 0; // NTP hasn't set the clock; "unknown"
    return (uint16_t)(utc / 86400);
}

void Logbook::Begin()
{
    if (started)
        return;
    started = true;

    // Opening read-write creates the namespace if it's missing (first ever run),
    // so the later reads don't log NOT_FOUND.
    prefs.begin("logbook", false);

    // Every store parses SHORT records without complaint, which is what makes the
    // v3 -> v4 upgrade a non-event: a record with no claim field yields claimDay
    // 0 = unclaimed. That is simultaneously the migration and the deliberate
    // reset of every existing score.
    //   types:     v4 "CODE|firstDay|count|claimDay", v2 "CODE|firstDay|count", v1 "CODE"
    //   operators: v4 "NAME|firstDay|claimDay",       v2 "NAME|firstDay",       v1 "NAME"
    //   countries/airports: v4 "NAME|firstDay|claimDay", earlier the bare name
    {
        forEachRecord(readStore(prefs, "types"), [this](const String& rec) {
            String f[4];
            const int n = splitFields(rec, f, 4);
            TypeStat st;
            st.firstDay = n >= 2 ? (uint16_t)f[1].toInt() : 0;
            st.count = n >= 3 ? (uint16_t)f[2].toInt() : 1;
            st.claimDay = n >= 4 ? (uint16_t)f[3].toInt() : 0;
            if (st.count == 0) st.count = 1;
            if (!f[0].isEmpty()) {
                types[f[0]] = st;
                if (st.claimDay != 0) ++claimedTypes;
            }
        });
    }
    const auto loadSeen = [this](Preferences& p, const char* key,
                                 std::map<String, SeenStat>& into, uint16_t& claimedCount) {
        forEachRecord(readStore(p, key), [&](const String& rec) {
            String f[3];
            const int n = splitFields(rec, f, 3);
            if (f[0].isEmpty()) return;
            SeenStat st;
            st.firstDay = n >= 2 ? (uint16_t)f[1].toInt() : 0;
            st.claimDay = n >= 3 ? (uint16_t)f[2].toInt() : 0;
            into[f[0]] = st;
            if (st.claimDay != 0) ++claimedCount;
        });
    };
    loadSeen(prefs, "operators", operators, claimedOperators);
    loadSeen(prefs, "countries", countries, claimedCountries);
    loadSeen(prefs, "airports", airports, claimedAirports);
    contacts = prefs.getUInt("contacts", 0);
    loadRecord(prefs, "rec-high", recHigh);
    loadRecord(prefs, "rec-fast", recFast);
    loadRecord(prefs, "rec-near", recNear);
    prefs.end();

    lastPersist = millis();
    Serial.printf("[logbook] loaded %u types (%u claimed), %u airlines (%u), %u countries (%u), %u contacts\n",
                  (unsigned)types.size(), (unsigned)claimedTypes,
                  (unsigned)operators.size(), (unsigned)claimedOperators,
                  (unsigned)countries.size(), (unsigned)claimedCountries,
                  (unsigned)contacts);
    reportCapacity();
}

// How much room this board actually has, checked against what the caps want.
//
// The NVS size is a per-env partition table, not a property of the code, and the
// shipping radar SKUs differ by 4x: blipscope-s3-128 gets 84 KB / 2646 entries
// (partitions-s3-16mb-bignvs.csv), while blipscope-s3-146 and blipscope-pro-s3-21
// take the stock default_16MB.csv and get 20 KB / 630 -- the same 20 KB that
// CSV's own header documents as unable to hold this application. The caps in
// Logbook.h were sized for the big table and say so; nothing in the source can
// see which table a board actually booted with.
//
// So the fit is checked here, against the running partition, on every boot --
// not at the moment a write finally fails ten minutes into a customer's evening.
// Reading the artifact rather than the config is the standing practice in
// CLAUDE.md, and a per-env partition table is exactly the asymmetry it is about.
void Logbook::reportCapacity()
{
    nvs_stats_t st{};
    if (nvs_get_stats(nullptr, &st) != ESP_OK) {
        Serial.println("[logbook] NVS stats unavailable -- capacity UNCHECKED");
        return;
    }

    // Measured on the bench s3-128, 2026-07-31, by reading the partition back off
    // the board and parsing it: config 149 + nvs.net80211 97 + phy 66 + wifi-fast
    // 4 = 316 entries are spoken for before the logbook stores a single aircraft.
    // NVS also permanently reserves one page for garbage collection and can never
    // spend it.
    constexpr size_t kNonLogbookEntries = 316;
    constexpr size_t kGcReservedEntries = 126; // one 4 KB page
    // A blob's payload costs one 32-byte entry per 32 bytes. The four stores are
    // the only part of this that scales with the sky a device is pointed at.
    constexpr size_t kStoresWorstCase = (MAX_BLOB_TOTAL + 31) / 32;

    const size_t overhead = kNonLogbookEntries + kGcReservedEntries;
    const size_t budget = st.total_entries > overhead ? st.total_entries - overhead : 0;

    // "ceilings" and not "caps" on purpose: this is MAX_BLOB_TOTAL, the most the
    // four stores can occupy, which is a slightly larger number than the count caps
    // actually need (they need ~1277 of these 1375). The ceiling is the right
    // number for a capacity check -- it is the bound that cannot be exceeded -- but
    // calling it "caps" would put two different figures under one name, and the
    // next person would reconcile them against Logbook.h and find a discrepancy
    // that isn't one.
    Serial.printf("[logbook] NVS %u/%u entries used (%u free); logbook budget ~%u, ceilings need %u\n",
                  (unsigned)st.used_entries, (unsigned)st.total_entries,
                  (unsigned)st.free_entries, (unsigned)budget, (unsigned)kStoresWorstCase);

    // Everything here is counted in ENTRIES, never bytes. An entry is 32 B but a
    // 4 KB page only holds 126 of them (the rest is page header and state bitmap),
    // so entries*32 renders a 20 KB partition as "19 KB" and invites an argument
    // about which number is wrong. Entries are what NVS actually rations.
    if (kStoresWorstCase > budget)
        Serial.printf("[logbook] WARNING: this partition cannot hold a full logbook. %u total entries "
                      "leaves ~%u for stores; the ceilings need %u. Writes will start failing with "
                      "NOT_ENOUGH_SPACE once the sky fills them -- see partitions-s3-16mb-bignvs.csv.\n",
                      (unsigned)st.total_entries, (unsigned)budget, (unsigned)kStoresWorstCase);
}

void Logbook::loadRecord(Preferences& p, const char* key, Record& out)
{
    if (!p.isKey(key))
        return;
    String f[3];
    if (splitFields(p.getString(key, ""), f, 3) == 3 && !f[0].isEmpty()) {
        out.callsign = f[0];
        out.value = f[1].toFloat();
        out.day = (uint16_t)f[2].toInt();
        out.set = true;
    }
}

void Logbook::saveRecord(const char* key, const Record& r)
{
    if (!r.set)
        return;
    prefs.putString(key, r.callsign + FIELD + String(r.value, 1) + FIELD + String(r.day));
}

// ---- capacity ---------------------------------------------------------------
// A full store used to be a cliff, and a silent one. Note*() refused to insert,
// so the entry was ABSENT -- and absent read as "unclaimed" to the badge
// predicate but as "reject" to Claim*(). The owner got a gold NEW ring on an
// aircraft that could never be claimed, on every contact of every new type,
// forever. IsTypeClaimable() removed the lie by asking one question instead of
// two; noteFull() made the cause visible instead of leaving it to be inferred
// from a store size that happens to equal its cap.
//
// Both of those were repairs to the SYMPTOM. The cause was that the caps were
// reachable at all: 220/220 types, 120/120 airlines, 300/300 airports after ONE
// WEEK under a GA-heavy sky (bench, 2026-08-08). v5 raises them against a
// measured budget AND makes the boundary a slope instead of a cliff -- a full
// store now forgets its least valuable unclaimed entry rather than refusing to
// grow.
//
// So this warning now means something much narrower than it used to, and it is
// worth being precise about it: reaching it means every single entry in the
// store is CLAIMED. That is not a storage failure, it is a customer who has
// collected everything, and refusing is the only honest answer -- the
// alternative is deleting a trophy to make room for a duplicate of one.
void Logbook::noteFull(Store s, const char* what, size_t cap)
{
    if (rejected[s] < 0xFFFF) ++rejected[s];
    if (!warnedFull[s]) {
        warnedFull[s] = true;
        Serial.printf("[logbook] AT CAPACITY: %s full at %u and EVERY entry is claimed, so "
                      "nothing can be evicted. First-time entries are no longer recorded.\n",
                      what, (unsigned)cap);
    }
}

// ---- eviction ---------------------------------------------------------------
// The rule, and it is the whole design: A CLAIM IS NEVER EVICTABLE.
//
// The two things in the book are not equivalent. "Seen" is produced by the sky
// and costs the owner nothing; a claim is the one thing they did on purpose, by
// noticing an aircraft and tapping it. Evicting a claim to make room for
// something a feed happened to mention would spend the only irreplaceable half
// of the record to store the replaceable half. So eviction only ever considers
// unclaimed entries, and when there are none it refuses (see noteFull above).
//
// Among unclaimed entries, "least valuable" is deliberately not "oldest". Oldest
// alone would evict the rarity you saw once in March to make room for the
// fiftieth Cessna of the afternoon, which inverts what a lifelist is for. For
// types the ordering is count first: seen-once beats seen-often, and firstDay
// breaks the tie toward forgetting the more recent one (an old single sighting
// is more interesting than a new one). The seen-only stores carry no count, so
// they fall back to oldest-first.
bool Logbook::evictOneType()
{
    auto victim = types.end();
    for (auto it = types.begin(); it != types.end(); ++it) {
        if (it->second.claimDay != 0)
            continue; // claimed: not a candidate, ever
        if (victim == types.end() ||
            it->second.count > victim->second.count ||
            (it->second.count == victim->second.count &&
             it->second.firstDay > victim->second.firstDay))
            victim = it;
    }
    if (victim == types.end())
        return false; // everything is claimed
    if (evicted[StTypes] < 0xFFFF) ++evicted[StTypes];
    types.erase(victim);
    return true;
}

// The seen-only stores carry no count, so the only signal is age -- and the
// choice goes the same way as the tie-break above: forget the MOST RECENT
// unclaimed entry, not the oldest.
//
// Oldest-first is the reflex, and it is wrong here. It turns a lifelist into a
// rolling window of the last 250 airlines and quietly deletes the beginning of
// the record, which is the part a collection is actually about. Keeping the
// earliest costs one honest consequence, stated here so it isn't discovered as a
// bug: once the store is full its newest slot becomes a revolving door -- each
// new airline evicts the previous new one -- while everything earlier is frozen.
// "Your first 250, plus whoever just flew over" is a defensible book. "Your most
// recent 250" is not.
bool Logbook::evictOneSeen(std::map<String, SeenStat>& store, Store which)
{
    auto victim = store.end();
    for (auto it = store.begin(); it != store.end(); ++it) {
        if (it->second.claimDay != 0)
            continue;
        if (victim == store.end() || it->second.firstDay > victim->second.firstDay)
            victim = it;
    }
    if (victim == store.end())
        return false;
    if (evicted[which] < 0xFFFF) ++evicted[which];
    store.erase(victim);
    return true;
}

// ---- the v4 -> v5 operator migration ----------------------------------------
// MAX_OP_LEN widened 24 -> 40, and the truncated name IS the key, so every long
// airline would otherwise re-enter under a new spelling as unclaimed -- the
// customer's claim stranded on a key nothing will ever look up again.
//
// It cannot be fixed by a pass at startup: "AIR WISCONSIN AIRLINES L" is all the
// v4 store kept, and the full name is not recoverable from it. The information
// only comes back when an aircraft supplies it. So the migration runs there, at
// the moment the long spelling first arrives, and hands the old entry's firstDay
// and claimDay to the new key.
//
// Only a key of EXACTLY the old cut is a candidate. A shorter name was never
// truncated, so it is a real name that merely happens to be a prefix -- and
// adopting that would merge two genuinely different operators.
bool Logbook::adoptTruncatedOperator(const String& fullName)
{
    if (fullName.length() <= OLD_MAX_OP_LEN)
        return false; // this name was never truncated by the old cut
    const String oldKey = fullName.substring(0, OLD_MAX_OP_LEN);
    auto old = operators.find(oldKey);
    if (old == operators.end())
        return false;
    operators[fullName] = old->second; // firstDay AND claimDay carry across
    operators.erase(old);
    dirty = true;
    Serial.printf("[logbook] migrated operator '%s' -> '%s' (claim %s)\n",
                  oldKey.c_str(), fullName.c_str(),
                  operators[fullName].claimDay != 0 ? "carried" : "none");
    return true;
}

bool Logbook::NoteType(const String& typeCode)
{
    String t = typeCode;
    t.trim();
    if (t.isEmpty())
        return false;

    auto it = types.find(t);
    if (it != types.end()) {
        if (it->second.count < 0xFFFF) ++it->second.count; // saturating
        dirty = true;
        return false; // known type: counted, not a fresh catch
    }
    if (types.size() >= MAX_TYPES && !evictOneType()) {
        noteFull(StTypes, "types", MAX_TYPES);
        return false; // full AND all claimed: don't claim a new catch we can't store
    }
    types[t] = TypeStat{ TodayEpochDay(), 1 };
    dirty = true;
    return true;
}

namespace {
// Normalisers, shared by the Note (seen) and Claim paths so a claim can never
// miss its own entry because the two spelled the key differently.
String normOperator(const String& raw, size_t maxLen)
{
    String s = raw;
    s.trim();
    if (s.length() > maxLen) s = s.substring(0, maxLen);
    return s;
}
String normCountry(const String& raw, size_t maxLen)
{
    String s = raw;
    s.trim();
    if (s.length() > maxLen) s = s.substring(0, maxLen);
    return s;
}
String normAirport(const String& raw)
{
    String s = raw;
    s.trim();
    s.toUpperCase();
    if (s.length() > 4) return String();
    return s;
}
} // namespace

bool Logbook::NoteOperator(const String& operatorName)
{
    const String op = normOperator(operatorName, MAX_OP_LEN);
    if (op.isEmpty())
        return false;
    if (operators.count(op))
        return false;
    // Before treating this as a new airline, check whether it is an old one under
    // its v4 truncated spelling. Adopting returns the entry to the book WITH its
    // claim, and it is not a fresh catch -- the customer already had it.
    if (adoptTruncatedOperator(op))
        return false;
    if (operators.size() >= MAX_OPERATORS && !evictOneSeen(operators, StOperators)) {
        noteFull(StOperators, "airlines/owners", MAX_OPERATORS);
        return false;
    }
    operators[op] = SeenStat{ TodayEpochDay(), 0 };
    dirty = true;
    return true;
}

bool Logbook::NoteCountry(const String& country)
{
    const String c = normCountry(country, MAX_CN_LEN);
    if (c.isEmpty())
        return false;
    if (countries.count(c))
        return false;
    if (countries.size() >= MAX_COUNTRIES && !evictOneSeen(countries, StCountries)) {
        noteFull(StCountries, "countries", MAX_COUNTRIES);
        return false;
    }
    countries[c] = SeenStat{ TodayEpochDay(), 0 };
    dirty = true;
    return true;
}

bool Logbook::NoteAirport(const String& airportCode)
{
    const String a = normAirport(airportCode);
    if (a.isEmpty())
        return false;
    if (airports.count(a))
        return false;
    if (airports.size() >= MAX_AIRPORTS && !evictOneSeen(airports, StAirports)) {
        noteFull(StAirports, "airports", MAX_AIRPORTS);
        return false;
    }
    airports[a] = SeenStat{ TodayEpochDay(), 0 };
    dirty = true;
    return true;
}

// ---- claiming ---------------------------------------------------------------
// A claim only ever transitions unclaimed -> claimed, and only for something
// already in the book. Claiming something unseen is silently ignored rather than
// inserted: the seen list is the evidence the aircraft was really overhead, and a
// claim that could create its own evidence would make the two numbers meaningless.
//
// v5 gave that principle a second half, and the two are the same idea read from
// opposite ends. A claim cannot come into existence without a sighting to stand
// on -- and, now that the book evicts, a claim cannot be taken away by one
// either. Eviction only ever removes unclaimed entries (see evictOneType /
// evictOneSeen above), so the sky can add to what a customer owns and can never
// subtract from it.
//
// That asymmetry is what makes the claimed count worth anything. "Seen" is
// produced by whatever happened to fly past and is replaceable; a claim is the
// one thing the owner did deliberately, and it is the half the device promises
// to keep. A cap that could delete a claim to store another sighting would be
// spending the irreplaceable half to hold the replaceable one.

bool Logbook::IsTypeClaimed(const String& typeCode) const
{
    String t = typeCode;
    t.trim();
    const auto it = types.find(t);
    return it != types.end() && it->second.claimDay != 0;
}

// THE BADGE PREDICATE, and the reason it is not simply !IsTypeClaimed().
//
// The two questions differ only on a type that is ABSENT from the book, and they
// used to answer it opposite ways: IsTypeClaimed() says "not claimed" (so the
// badge said claimable), while ClaimType() below refuses to claim something never
// seen. Below capacity that disagreement is unreachable, because every call site
// runs NoteType() first and the insert always succeeds. AT capacity NoteType()
// inserts nothing, so every new type is absent and the disagreement is the only
// outcome: a permanent gold NEW ring on an aircraft whose claim silently fails,
// with no toast and no latch, on every reopen forever. Observed on a bench board
// with 220/220 types, tapping an LJ31 (2026-08-08).
//
// Asking one question instead of two makes the two answers unable to differ. An
// unrecordable type now simply carries no badge -- the collection stops growing,
// which it had already done, but it stops advertising that it hasn't.
bool Logbook::IsTypeClaimable(const String& typeCode) const
{
    String t = typeCode;
    t.trim();
    const auto it = types.find(t);
    return it != types.end() && it->second.claimDay == 0;
}

bool Logbook::ClaimType(const String& typeCode)
{
    String t = typeCode;
    t.trim();
    if (t.isEmpty())
        return false;
    auto it = types.find(t);
    if (it == types.end() || it->second.claimDay != 0)
        return false;
    // A claim before NTP lands would stamp day 0, which reads back as unclaimed
    // and would be re-claimable forever. Stamp 1 ("claimed, date unknown") so the
    // claim is durable even though the date is not.
    const uint16_t today = TodayEpochDay();
    it->second.claimDay = today != 0 ? today : 1;
    ++claimedTypes;
    dirty = true;
    return true;
}

// The three riders. Same shape, and each is a no-op when the thing was never
// seen or is already claimed, so calling them on every card open is free.
bool Logbook::ClaimOperator(const String& operatorName)
{
    const String op = normOperator(operatorName, MAX_OP_LEN);
    auto it = operators.find(op);
    if (op.isEmpty() || it == operators.end() || it->second.claimDay != 0)
        return false;
    const uint16_t today = TodayEpochDay();
    it->second.claimDay = today != 0 ? today : 1;
    ++claimedOperators;
    dirty = true;
    return true;
}

bool Logbook::ClaimCountry(const String& country)
{
    const String c = normCountry(country, MAX_CN_LEN);
    auto it = countries.find(c);
    if (c.isEmpty() || it == countries.end() || it->second.claimDay != 0)
        return false;
    const uint16_t today = TodayEpochDay();
    it->second.claimDay = today != 0 ? today : 1;
    ++claimedCountries;
    dirty = true;
    return true;
}

bool Logbook::ClaimAirport(const String& airportCode)
{
    const String a = normAirport(airportCode);
    auto it = airports.find(a);
    if (a.isEmpty() || it == airports.end() || it->second.claimDay != 0)
        return false;
    const uint16_t today = TodayEpochDay();
    it->second.claimDay = today != 0 ? today : 1;
    ++claimedAirports;
    dirty = true;
    return true;
}

void Logbook::NoteContact()
{
    ++contacts;
    dirty = true;
}

bool Logbook::offerRecord(Record& r, const String& cs, float value, bool smallerWins)
{
    const bool beats = !r.set || (smallerWins ? value < r.value : value > r.value);
    if (!beats)
        return false;
    r.callsign = cs;
    r.value = value;
    r.day = TodayEpochDay();
    r.set = true;
    dirty = true;
    return true;
}

void Logbook::NoteBest(const String& callsign, float altFt, float speedKt, float distKm)
{
    if (callsign.isEmpty())
        return;
    if (altFt > 0.0f) offerRecord(recHigh, callsign, altFt, false);
    if (speedKt > 0.0f) offerRecord(recFast, callsign, speedKt, false);
    if (distKm > 0.0f) offerRecord(recNear, callsign, distKm, true);
}

namespace {
// One entry of a seen-list store, as JSON. `keyName` differs per store only so
// the browser can render "code" vs "name" without guessing.
String seenEntryJson(const String& rec, const char* keyName, uint32_t& seen, uint32_t& claimed)
{
    String f[3];
    const int n = splitFields(rec, f, 3);
    if (f[0].isEmpty())
        return String();
    const uint16_t claimDay = n >= 3 ? (uint16_t)f[2].toInt() : 0;
    ++seen;
    if (claimDay != 0) ++claimed;
    String out;
    out.reserve(rec.length() + 72);
    out += "{\"";
    out += keyName;
    out += "\":\"" + jsonEscape(f[0]) + "\"";
    out += ",\"first\":\"" + dayToIso(n >= 2 ? (uint16_t)f[1].toInt() : 0) + "\"";
    out += ",\"claimed\":";
    out += claimDay != 0 ? "true" : "false";
    // claimDay 1 is the "claimed before NTP" sentinel: a real claim with no
    // trustworthy date, so report it claimed with an empty date rather than
    // printing 1970-01-02 at somebody.
    out += ",\"claimedOn\":\"" + (claimDay > 1 ? dayToIso(claimDay) : String()) + "\"}";
    return out;
}
} // namespace

Logbook::JsonStream::JsonStream()
{
    open = p.begin("logbook", true); // read-only; absent namespace = empty logbook
}

Logbook::JsonStream::~JsonStream()
{
    if (open)
        p.end();
}

// Append the next piece of the document to `pending`. One call emits either a
// fixed separator or ONE record, so no single step can balloon the buffer.
bool Logbook::JsonStream::Produce()
{
    // Store order matches the phase numbering: 1 types, 3 airlines, 5 countries,
    // 7 airports. Each odd phase walks `blob`; each even phase is punctuation
    // that also loads the next store.
    static const char* const STORE[4] = { "types", "operators", "countries", "airports" };
    static const char* const KEYNAME[4] = { "code", "name", "name", "code" };

    const auto loadStore = [&](int i) {
        blob = open ? readStore(p, STORE[i]) : String();
        pos = 0;
        firstEl = true;
    };

    switch (phase) {
        case 0:
            pending += "{\"v\":4,\"types\":[";
            loadStore(0);
            phase = 1;
            return true;

        case 1: case 3: case 5: case 7: {
            const int store = phase / 2;
            // Next non-empty record in the blob, or move on.
            while (pos < blob.length()) {
                int end = blob.indexOf(SEP, pos);
                if (end < 0) end = blob.length();
                const String rec = blob.substring(pos, end);
                pos = end + 1;
                if (rec.isEmpty())
                    continue;
                String entry;
                if (store == 0) {
                    // types carry the sighting count as well
                    String f[4];
                    const int n = splitFields(rec, f, 4);
                    if (f[0].isEmpty())
                        continue;
                    const uint16_t claimDay = n >= 4 ? (uint16_t)f[3].toInt() : 0;
                    ++nSeen[0];
                    if (claimDay != 0) ++nClaimed[0];
                    entry = "{\"code\":\"" + jsonEscape(f[0]) + "\"";
                    entry += ",\"first\":\"" + dayToIso(n >= 2 ? (uint16_t)f[1].toInt() : 0) + "\"";
                    entry += ",\"count\":" + String(n >= 3 ? f[2].toInt() : 1);
                    entry += ",\"claimed\":";
                    entry += claimDay != 0 ? "true" : "false";
                    entry += ",\"claimedOn\":\"" + (claimDay > 1 ? dayToIso(claimDay) : String()) + "\"}";
                } else {
                    entry = seenEntryJson(rec, KEYNAME[store], nSeen[store], nClaimed[store]);
                    if (entry.isEmpty())
                        continue;
                }
                if (!firstEl) pending += ',';
                firstEl = false;
                pending += entry;
                return true;
            }
            blob = String(); // release the store before the next one is read
            ++phase;
            return true;
        }

        case 2: pending += "],\"airlines\":[";  loadStore(1); phase = 3; return true;
        case 4: pending += "],\"countries\":["; loadStore(2); phase = 5; return true;
        case 6: pending += "],\"airports\":[";  loadStore(3); phase = 7; return true;

        case 8: {
            // The records block and the totals are both small and fixed-size, so
            // they go out in one step. Totals come LAST because they are counted
            // while streaming -- key order is not significant to any JSON parser.
            pending += "],\"records\":{";
            const struct { const char* key; const char* name; const char* unit; } recs[3] = {
                { "rec-high", "high", "ft" }, { "rec-fast", "fast", "kt" }, { "rec-near", "near", "km" },
            };
            bool first = true;
            for (const auto& r : recs) {
                if (!open || !p.isKey(r.key)) continue;
                String f[3];
                if (splitFields(p.getString(r.key, ""), f, 3) != 3 || f[0].isEmpty()) continue;
                if (!first) pending += ',';
                first = false;
                pending += "\"" + String(r.name) + "\":{\"callsign\":\"" + jsonEscape(f[0]) + "\"";
                pending += ",\"value\":" + String(f[1].toFloat(), 1);
                pending += ",\"unit\":\"" + String(r.unit) + "\"";
                pending += ",\"date\":\"" + dayToIso((uint16_t)f[2].toInt()) + "\"}";
            }
            pending += "},\"contacts\":" + String(open ? p.getUInt("contacts", 0) : 0);
            pending += ",\"counts\":{\"types\":" + String(nSeen[0]) +
                       ",\"airlines\":" + String(nSeen[1]) +
                       ",\"countries\":" + String(nSeen[2]) +
                       ",\"airports\":" + String(nSeen[3]) + "}";
            pending += ",\"claimed\":{\"types\":" + String(nClaimed[0]) +
                       ",\"airlines\":" + String(nClaimed[1]) +
                       ",\"countries\":" + String(nClaimed[2]) +
                       ",\"airports\":" + String(nClaimed[3]) + "}}";
            phase = 9;
            return true;
        }

        default:
            return false; // phase 9: complete
    }
}

size_t Logbook::JsonStream::Read(uint8_t* out, size_t maxLen)
{
    if (maxLen == 0)
        return 0;
    while (pending.length() < maxLen && Produce()) {
        // keep producing until we can fill this chunk or the document ends
    }
    const size_t n = pending.length() < maxLen ? pending.length() : maxLen;
    if (n == 0)
        return 0; // nothing pending and nothing left to produce: end of response
    memcpy(out, pending.c_str(), n);
    pending.remove(0, n);
    return n;
}

void Logbook::MaybePersist()
{
    if (!dirty)
        return;
    const unsigned long now = millis();
    if (now - lastPersist < PERSIST_INTERVAL_MS)
        return;

    // Serialize each store, honoring the MAX_BLOB safety ceiling (the per-store
    // caps keep us short of it; legacy over-cap lists truncate at the tail).
    // The ceilings are per-store now, and they are a safety net rather than a
    // working limit: the count caps bind first, so a break here means a record got
    // longer than its worst case was believed to be. Say so instead of truncating
    // in silence -- tail-truncation drops the alphabetically-last entries, which
    // come back as unclaimed NEW and read as a scoring bug.
    size_t clipped = 0;
    String typesBlob;
    for (const auto& [code, st] : types) {
        const String rec = code + FIELD + String(st.firstDay) + FIELD + String(st.count)
                         + FIELD + String(st.claimDay);
        if (typesBlob.length() + rec.length() + 1 > MAX_BLOB_TYPES) { ++clipped; break; }
        if (!typesBlob.isEmpty()) typesBlob += SEP;
        typesBlob += rec;
    }
    const auto seenBlob = [&clipped](const std::map<String, SeenStat>& src, size_t ceiling) {
        String blob;
        for (const auto& [name, st] : src) {
            const String rec = name + FIELD + String(st.firstDay) + FIELD + String(st.claimDay);
            if (blob.length() + rec.length() + 1 > ceiling) { ++clipped; break; }
            if (!blob.isEmpty()) blob += SEP;
            blob += rec;
        }
        return blob;
    };
    const String opsBlob       = seenBlob(operators, MAX_BLOB_OPERATORS);
    const String countriesBlob = seenBlob(countries, MAX_BLOB_COUNTRIES);
    const String airportsBlob  = seenBlob(airports, MAX_BLOB_AIRPORTS);
    if (clipped)
        Serial.printf("[logbook] BUG: %u store(s) hit the byte ceiling before the count cap. "
                      "Entries were dropped from the tail. A record is longer than "
                      "Logbook.h's worst case assumes.\n", (unsigned)clipped);

    if (!prefs.begin("logbook", false)) {
        Serial.println("[logbook] PERSIST FAILED: cannot open the NVS namespace");
        lastPersist = now; // don't spin; retry on the next debounce with dirty still set
        return;
    }

    // Stores go in as BLOBs, not strings. An NVS string must fit CONTIGUOUSLY
    // inside one 4 KB page, so at ~86 airlines the operators store (~2.6 KB, ~82
    // of a page's 126 entries) needed an 82-entry run in a single page -- and once
    // the 20 KB partition's pages are fragmented by ten-minute rewrites, no such
    // run exists and nvs_set_str returns NOT_ENOUGH_SPACE while the partition
    // still has plenty of free entries. Blobs are chunked across pages by NVS
    // itself, so they only need the space, not the contiguity. Observed failing
    // 2026-07-31 on the bench s3-128 ("nvs_set_str fail: operators
    // NOT_ENOUGH_SPACE") -- silently, because nobody read the return values.
    int failed = 0;
    const auto put = [&](const char* key, const String& blob) {
        if (prefs.putBytes(key, blob.c_str(), blob.length()) == blob.length())
            return;
        ++failed;
        Serial.printf("[logbook] PERSIST FAILED: key=%s bytes=%u freeEntries=%u\n",
                      key, (unsigned)blob.length(), (unsigned)prefs.freeEntries());
    };
    put("types", typesBlob);
    put("operators", opsBlob);
    put("countries", countriesBlob);
    put("airports", airportsBlob);
    prefs.putUInt("contacts", contacts);
    saveRecord("rec-high", recHigh);
    saveRecord("rec-fast", recFast);
    saveRecord("rec-near", recNear);
    const size_t freeLeft = prefs.freeEntries();
    prefs.end();

    lastPersist = now;
    // Only clear `dirty` when everything landed -- a partial write must be retried
    // on the next debounce, not forgotten. The old code cleared it unconditionally
    // and printed "persisted", so a full NVS lost entries with no trace.
    if (failed == 0)
        dirty = false;
    Serial.printf("[logbook] %s (%u/%u types claimed, %u airlines, %u countries, %u contacts; %u B blobs, %u NVS entries free)\n",
                  failed == 0 ? "persisted" : "PARTIALLY persisted",
                  (unsigned)claimedTypes, (unsigned)types.size(), (unsigned)operators.size(),
                  (unsigned)countries.size(), (unsigned)contacts,
                  (unsigned)(typesBlob.length() + opsBlob.length() +
                             countriesBlob.length() + airportsBlob.length()),
                  (unsigned)freeLeft);
    // A second line ONLY when something was refused, so a healthy device's log is
    // unchanged. These counts are since boot, and they are the input to any
    // decision about the caps: store sizes pinned at their cap say a store filled,
    // never how fast, so the growth rate cannot be recovered from them afterwards.
    if (rejected[StTypes] || rejected[StOperators] || rejected[StCountries] || rejected[StAirports])
        Serial.printf("[logbook] REFUSED since boot: %u types, %u airlines/owners, %u countries, "
                      "%u airports  (stores %u/%u, %u/%u, %u/%u, %u/%u)\n",
                      (unsigned)rejected[StTypes], (unsigned)rejected[StOperators],
                      (unsigned)rejected[StCountries], (unsigned)rejected[StAirports],
                      (unsigned)types.size(), (unsigned)MAX_TYPES,
                      (unsigned)operators.size(), (unsigned)MAX_OPERATORS,
                      (unsigned)countries.size(), (unsigned)MAX_COUNTRIES,
                      (unsigned)airports.size(), (unsigned)MAX_AIRPORTS);
    // And a third ONLY when something was forgotten. Kept separate from REFUSED on
    // purpose: these are the two different things a full store can do, and a single
    // combined number would make "the book is growing and shedding its dullest
    // entries" indistinguishable from "the book has stopped". The first is the
    // design working; the second means every entry is claimed.
    if (evicted[StTypes] || evicted[StOperators] || evicted[StCountries] || evicted[StAirports])
        Serial.printf("[logbook] EVICTED since boot: %u types, %u airlines/owners, %u countries, "
                      "%u airports (unclaimed only -- a claim is never evictable)\n",
                      (unsigned)evicted[StTypes], (unsigned)evicted[StOperators],
                      (unsigned)evicted[StCountries], (unsigned)evicted[StAirports]);
}
