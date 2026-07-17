#include "Logbook.h"

#include <ctime>

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
    if (prefs.isKey("types")) {
        forEachRecord(prefs.getString("types", ""), [this](const String& rec) {
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
    if (prefs.isKey("operators")) {
        forEachRecord(prefs.getString("operators", ""), [this](const String& rec) {
            String f[2];
            const int n = splitFields(rec, f, 2);
            if (!f[0].isEmpty()) operators[f[0]] = n >= 2 ? (uint16_t)f[1].toInt() : 0;
        });
    }
    if (prefs.isKey("countries")) {
        forEachRecord(prefs.getString("countries", ""), [this](const String& rec) {
            countries.insert(rec);
        });
    }
    if (prefs.isKey("airports")) {
        forEachRecord(prefs.getString("airports", ""), [this](const String& rec) {
            airports.insert(rec);
        });
    }
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

    prefs.begin("logbook", false);
    prefs.putString("types", typesBlob);
    prefs.putString("operators", opsBlob);
    prefs.putString("countries", countriesBlob);
    prefs.putString("airports", airportsBlob);
    prefs.putUInt("contacts", contacts);
    saveRecord("rec-high", recHigh);
    saveRecord("rec-fast", recFast);
    saveRecord("rec-near", recNear);
    prefs.end();

    lastPersist = now;
    dirty = false;
    Serial.printf("[logbook] persisted (%u types, %u airlines, %u countries, %u contacts)\n",
                  (unsigned)types.size(), (unsigned)operators.size(),
                  (unsigned)countries.size(), (unsigned)contacts);
}
