#include "Logbook.h"

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

    // types: v2 records are "CODE|firstDay|count"; a legacy v1 record is the
    // bare code and parses as day 0 (unknown) / count 1.
    {
        forEachRecord(readStore(prefs, "types"), [this](const String& rec) {
            String f[3];
            const int n = splitFields(rec, f, 3);
            TypeStat st;
            st.firstDay = n >= 2 ? (uint16_t)f[1].toInt() : 0;
            st.count = n >= 3 ? (uint16_t)f[2].toInt() : 1;
            if (st.count == 0) st.count = 1;
            if (!f[0].isEmpty()) types[f[0]] = st;
        });
    }
    // operators: v2 "NAME|firstDay"; legacy is the bare name.
    {
        forEachRecord(readStore(prefs, "operators"), [this](const String& rec) {
            String f[2];
            const int n = splitFields(rec, f, 2);
            if (!f[0].isEmpty()) operators[f[0]] = n >= 2 ? (uint16_t)f[1].toInt() : 0;
        });
    }
    forEachRecord(readStore(prefs, "countries"), [this](const String& rec) {
        countries.insert(rec);
    });
    forEachRecord(readStore(prefs, "airports"), [this](const String& rec) {
        airports.insert(rec);
    });
    contacts = prefs.getUInt("contacts", 0);
    loadRecord(prefs, "rec-high", recHigh);
    loadRecord(prefs, "rec-fast", recFast);
    loadRecord(prefs, "rec-near", recNear);
    prefs.end();

    lastPersist = millis();
    Serial.printf("[logbook] loaded %u types, %u airlines, %u countries, %u contacts\n",
                  (unsigned)types.size(), (unsigned)operators.size(),
                  (unsigned)countries.size(), (unsigned)contacts);
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

bool Logbook::NoteOperator(const String& operatorName)
{
    String op = operatorName;
    op.trim();
    if (op.length() > MAX_OP_LEN)
        op = op.substring(0, MAX_OP_LEN);
    if (op.isEmpty())
        return false;
    if (operators.count(op))
        return false;
    if (operators.size() >= MAX_OPERATORS)
        return false;
    operators[op] = TodayEpochDay();
    dirty = true;
    return true;
}

bool Logbook::NoteCountry(const String& country)
{
    String c = country;
    c.trim();
    if (c.isEmpty())
        return false;
    if (countries.count(c))
        return false;
    if (countries.size() >= MAX_COUNTRIES)
        return false;
    countries.insert(c);
    dirty = true;
    return true;
}

bool Logbook::NoteAirport(const String& airportCode)
{
    String a = airportCode;
    a.trim();
    a.toUpperCase();
    if (a.isEmpty() || a.length() > 4)
        return false;
    if (airports.count(a))
        return false;
    if (airports.size() >= MAX_AIRPORTS)
        return false;
    airports.insert(a);
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

String Logbook::ExportJson()
{
    Preferences p;
    if (!p.begin("logbook", true)) // read-only; absent namespace = empty logbook
        return "{\"contacts\":0,\"types\":[],\"airlines\":[],\"countries\":[],\"airports\":[],\"records\":{}}";

    String json;
    json.reserve(8192); // typical logbooks are a few KB; worst case grows beyond, S3 heap absorbs it

    json += "{\"contacts\":" + String(p.getUInt("contacts", 0));

    json += ",\"types\":[";
    bool first = true;
    forEachRecord(readStore(p, "types"), [&](const String& rec) {
        String f[3];
        const int n = splitFields(rec, f, 3);
        if (f[0].isEmpty()) return;
        if (!first) json += ',';
        first = false;
        json += "{\"code\":\"" + jsonEscape(f[0]) + "\"";
        json += ",\"first\":\"" + dayToIso(n >= 2 ? (uint16_t)f[1].toInt() : 0) + "\"";
        json += ",\"count\":" + String(n >= 3 ? f[2].toInt() : 1) + "}";
    });

    json += "],\"airlines\":[";
    first = true;
    forEachRecord(readStore(p, "operators"), [&](const String& rec) {
        String f[2];
        const int n = splitFields(rec, f, 2);
        if (f[0].isEmpty()) return;
        if (!first) json += ',';
        first = false;
        json += "{\"name\":\"" + jsonEscape(f[0]) + "\"";
        json += ",\"first\":\"" + dayToIso(n >= 2 ? (uint16_t)f[1].toInt() : 0) + "\"}";
    });

    const auto stringArray = [&](const char* key) {
        bool firstEl = true;
        forEachRecord(readStore(p, key), [&](const String& rec) {
            if (!firstEl) json += ',';
            firstEl = false;
            json += "\"" + jsonEscape(rec) + "\"";
        });
    };
    json += "],\"countries\":[";
    stringArray("countries");
    json += "],\"airports\":[";
    stringArray("airports");

    json += "],\"records\":{";
    const struct { const char* key; const char* name; const char* unit; } recs[3] = {
        { "rec-high", "high", "ft" }, { "rec-fast", "fast", "kt" }, { "rec-near", "near", "km" },
    };
    first = true;
    for (const auto& r : recs) {
        if (!p.isKey(r.key)) continue;
        String f[3];
        if (splitFields(p.getString(r.key, ""), f, 3) != 3 || f[0].isEmpty()) continue;
        if (!first) json += ',';
        first = false;
        json += "\"" + String(r.name) + "\":{\"callsign\":\"" + jsonEscape(f[0]) + "\"";
        json += ",\"value\":" + String(f[1].toFloat(), 1);
        json += ",\"unit\":\"" + String(r.unit) + "\"";
        json += ",\"date\":\"" + dayToIso((uint16_t)f[2].toInt()) + "\"}";
    }
    json += "}}";

    p.end();
    return json;
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
        const String rec = code + FIELD + String(st.firstDay) + FIELD + String(st.count);
        if (typesBlob.length() + rec.length() + 1 > MAX_BLOB) break;
        if (!typesBlob.isEmpty()) typesBlob += SEP;
        typesBlob += rec;
    }
    String opsBlob;
    for (const auto& [name, day] : operators) {
        const String rec = name + FIELD + String(day);
        if (opsBlob.length() + rec.length() + 1 > MAX_BLOB) break;
        if (!opsBlob.isEmpty()) opsBlob += SEP;
        opsBlob += rec;
    }
    String countriesBlob;
    for (const String& c : countries) {
        if (countriesBlob.length() + c.length() + 1 > MAX_BLOB) break;
        if (!countriesBlob.isEmpty()) countriesBlob += SEP;
        countriesBlob += c;
    }
    String airportsBlob;
    for (const String& a : airports) {
        if (airportsBlob.length() + a.length() + 1 > MAX_BLOB) break;
        if (!airportsBlob.isEmpty()) airportsBlob += SEP;
        airportsBlob += a;
    }

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
    Serial.printf("[logbook] %s (%u types, %u airlines, %u countries, %u contacts; %u B blobs, %u NVS entries free)\n",
                  failed == 0 ? "persisted" : "PARTIALLY persisted",
                  (unsigned)types.size(), (unsigned)operators.size(),
                  (unsigned)countries.size(), (unsigned)contacts,
                  (unsigned)(typesBlob.length() + opsBlob.length() +
                             countriesBlob.length() + airportsBlob.length()),
                  (unsigned)freeLeft);
}
