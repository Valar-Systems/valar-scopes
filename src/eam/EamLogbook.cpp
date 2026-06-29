#include "EamLogbook.h"

#include <time.h>

namespace {

constexpr size_t MAX_IDS = 80;          // recent EAM ids kept for dedupe
constexpr size_t MAX_CODEWORDS = 100;   // codewords retained
constexpr size_t MAX_CODEWORD_LEN = 24;
constexpr unsigned long PERSIST_DEBOUNCE_MS = 10000; // coalesce a burst, flush within ~10 s

// Join a string list with '\n'.
String Join(const std::vector<String>& v)
{
    String out;
    for (const String& s : v) { out += s; out += '\n'; }
    return out;
}

void SplitLines(const String& blob, std::vector<String>& out)
{
    int start = 0;
    const int n = (int)blob.length();
    for (int i = 0; i <= n; ++i) {
        if (i == n || blob[i] == '\n') {
            if (i > start) out.push_back(blob.substring(start, i));
            start = i + 1;
        }
    }
}

// epoch (UTC) -> "YYYY-MM-DDThh:mm:ssZ"; "" when unknown (0).
String EpochToIso(long epoch)
{
    if (epoch <= 0) return String();
    time_t tt = (time_t)epoch;
    struct tm tmv;
    gmtime_r(&tt, &tmv);
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return String(buf);
}

String CsvField(const String& v)   // RFC-4180 quoted field
{
    String s = v;
    s.replace("\"", "\"\"");
    return "\"" + s + "\"";
}

String JsonStr(const String& v)
{
    String s = v;
    s.replace("\\", "\\\\");
    s.replace("\"", "\\\"");
    return "\"" + s + "\"";
}

// Split one stored "codeword\tepoch" line into its parts (epoch 0 when absent).
void SplitCw(const String& line, String& word, long& epoch)
{
    const int tab = line.indexOf('\t');
    if (tab < 0) { word = line; epoch = 0; return; }
    word = line.substring(0, tab);
    epoch = line.substring(tab + 1).toInt();
}

} // namespace

void EamLogbook::Begin()
{
    if (loaded) return;
    loaded = true;

    // Create the namespace up front (read-write) so the read-only load below doesn't log
    // "nvs_open NOT_FOUND" before the first flush, mirroring ConfigurationWebServer.
    prefs.begin("eam-log", false);
    prefs.end();

    prefs.begin("eam-log", true);
    eamCount = prefs.getUInt("count", 0);
    const String ids = prefs.getString("ids", "");
    const String cw = prefs.getString("cw", "");
    prefs.end();

    SplitLines(ids, eamIds);

    std::vector<String> cwLines;
    SplitLines(cw, cwLines);
    for (const String& line : cwLines) {
        const int tab = line.indexOf('\t');
        if (tab < 0) { codewords.push_back({line, 0}); continue; }
        codewords.push_back({line.substring(0, tab), line.substring(tab + 1).toInt()});
    }
}

bool EamLogbook::NoteEam(const String& id, long heardEpoch)
{
    (void)heardEpoch; // id-only dedupe; the odometer is the persisted record of "when/how many"
    if (id.isEmpty()) return false;
    for (const String& e : eamIds)
        if (e == id) return false;

    eamIds.push_back(id);
    if (eamIds.size() > MAX_IDS) eamIds.erase(eamIds.begin());
    eamCount++;
    dirty = true;
    return true;
}

bool EamLogbook::NoteCodeword(const String& codeword, long seenEpoch)
{
    if (codeword.isEmpty()) return false;
    for (const auto& c : codewords)
        if (c.first == codeword) return false;

    String cw = codeword;
    if (cw.length() > MAX_CODEWORD_LEN) cw = cw.substring(0, MAX_CODEWORD_LEN);
    codewords.push_back({cw, seenEpoch});
    if (codewords.size() > MAX_CODEWORDS) codewords.erase(codewords.begin());
    dirty = true;
    return true;
}

size_t EamLogbook::CodewordsThisMonth(long nowEpoch) const
{
    if (nowEpoch <= 0) return 0;
    time_t now = (time_t)nowEpoch;
    struct tm nt;
    gmtime_r(&now, &nt);
    size_t count = 0;
    for (const auto& c : codewords) {
        if (c.second <= 0) continue;
        time_t t = (time_t)c.second;
        struct tm ct;
        gmtime_r(&t, &ct);
        if (ct.tm_year == nt.tm_year && ct.tm_mon == nt.tm_mon) count++;
    }
    return count;
}

void EamLogbook::MaybePersist()
{
    if (!dirty) return;
    if (lastPersistMs != 0 && millis() - lastPersistMs < PERSIST_DEBOUNCE_MS) return;
    Persist();
}

void EamLogbook::Persist()
{
    std::vector<String> cwLines;
    cwLines.reserve(codewords.size());
    for (const auto& c : codewords)
        cwLines.push_back(c.first + "\t" + String(c.second));

    prefs.begin("eam-log", false);
    prefs.putUInt("count", eamCount);
    prefs.putString("ids", Join(eamIds));
    prefs.putString("cw", Join(cwLines));
    prefs.end();

    dirty = false;
    lastPersistMs = millis();
}

String EamLogbook::ExportCsv()
{
    Preferences p;
    p.begin("eam-log", true);
    const uint32_t count = p.getUInt("count", 0);
    const String ids = p.getString("ids", "");
    const String cw = p.getString("cw", "");
    p.end();

    std::vector<String> idList; SplitLines(ids, idList);
    std::vector<String> cwLines; SplitLines(cw, cwLines);

    String out = "kind,value,first_seen_utc\r\n";
    out += "eam_total,"; out += count; out += ",\r\n";
    for (const String& id : idList) { out += "eam,"; out += CsvField(id); out += ",\r\n"; }
    for (const String& line : cwLines) {
        String word; long epoch; SplitCw(line, word, epoch);
        out += "codeword,"; out += CsvField(word); out += ","; out += EpochToIso(epoch); out += "\r\n";
    }
    return out;
}

String EamLogbook::ExportJson()
{
    Preferences p;
    p.begin("eam-log", true);
    const uint32_t count = p.getUInt("count", 0);
    const String ids = p.getString("ids", "");
    const String cw = p.getString("cw", "");
    p.end();

    std::vector<String> idList; SplitLines(ids, idList);
    std::vector<String> cwLines; SplitLines(cw, cwLines);

    String out = "{\"eam_count\":"; out += count; out += ",\"eam_ids\":[";
    for (size_t i = 0; i < idList.size(); ++i) { if (i) out += ","; out += JsonStr(idList[i]); }
    out += "],\"codewords\":[";
    for (size_t i = 0; i < cwLines.size(); ++i) {
        String word; long epoch; SplitCw(cwLines[i], word, epoch);
        if (i) out += ",";
        out += "{\"codeword\":"; out += JsonStr(word);
        out += ",\"first_seen_utc\":"; out += JsonStr(EpochToIso(epoch));
        out += ",\"epoch\":"; out += epoch; out += "}";
    }
    out += "]}";
    return out;
}
