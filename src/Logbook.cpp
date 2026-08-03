#include "Logbook.h"

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
    if (types.size() >= MAX_TYPES)
        return false; // at capacity: don't claim a new catch we can't store
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
    if (operators.size() >= MAX_OPERATORS)
        return false;
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
    if (countries.size() >= MAX_COUNTRIES)
        return false;
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
    if (airports.size() >= MAX_AIRPORTS)
        return false;
    airports[a] = SeenStat{ TodayEpochDay(), 0 };
    dirty = true;
    return true;
}

// ---- claiming ---------------------------------------------------------------
// A claim only ever transitions unclaimed -> claimed, and only for something
// already in the book. Claiming something unseen is silently ignored rather than
// inserted: the seen list is the evidence the aircraft was really overhead, and a
// claim that could create its own evidence would make the two numbers meaningless.

bool Logbook::IsTypeClaimed(const String& typeCode) const
{
    String t = typeCode;
    t.trim();
    const auto it = types.find(t);
    return it != types.end() && it->second.claimDay != 0;
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
    String typesBlob;
    for (const auto& [code, st] : types) {
        const String rec = code + FIELD + String(st.firstDay) + FIELD + String(st.count)
                         + FIELD + String(st.claimDay);
        if (typesBlob.length() + rec.length() + 1 > MAX_BLOB) break;
        if (!typesBlob.isEmpty()) typesBlob += SEP;
        typesBlob += rec;
    }
    const auto seenBlob = [](const std::map<String, SeenStat>& src) {
        String blob;
        for (const auto& [name, st] : src) {
            const String rec = name + FIELD + String(st.firstDay) + FIELD + String(st.claimDay);
            if (blob.length() + rec.length() + 1 > MAX_BLOB) break;
            if (!blob.isEmpty()) blob += SEP;
            blob += rec;
        }
        return blob;
    };
    const String opsBlob       = seenBlob(operators);
    const String countriesBlob = seenBlob(countries);
    const String airportsBlob  = seenBlob(airports);

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
}
