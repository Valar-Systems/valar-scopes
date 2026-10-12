// Host test for ROADMAP E6: an EAM longer than 30 characters, through the
// SHIPPING feed client, on the host.
//
// WHAT RUNS. src/eam/EamFeedClient.cpp and src/eam/EamModels.cpp, unmodified,
// against ArduinoJson 7.4.3 from .pio/libdeps and the REAL arduino-esp32 String
// (eam_host/Arduino.h says why). The client is driven through its public surface:
// Begin(), Configure(), Poll(), Latest(). Its worker body (RunWorker -> Fetch ->
// GetJson -> ParseMessages, and the on_watch read) runs for real, one request at
// a time (eam_host/freertos/FreeRTOS.h); ApplyResult and MergeLatest run inside
// Poll() as on the device. The network stops at a fake GetJson that hands the
// staged body to the same deserializeJson call (eam_host/HttpRequestManager.h).
//
// THE INPUT is test/fixtures/eam-long/latest-{031,075,290,500}.json -- the exact
// bytes scripts/e6-fixture-server.py serves to the bench board -- plus variants
// this file derives from them by STRING EDITS (never through ArduinoJson, so the
// expected values do not come from the library under test): text with spaces,
// `groups: []` (the firmware's SplitGroups fallback), a "fat" message carrying
// the fields valar-eam-feed keeps off /eam/latest until this check is done
// (machine_text, origin_text, announced_length, station_notes), and all 20
// messages long (the worst case the server's limit=20 allows).
//
// WHAT EACH RESULT MEANS -- written 2026-10-11, before the first run:
//   * every length parses and the stored text is BYTE-IDENTICAL to the input, its
//     groups are the input's groups, char_count/group_count are the served ones:
//     a long message is held whole. EamModels stores text/groups as String and
//     ArduinoJson 7's JsonDocument is elastic, so nothing below 65,535 characters
//     (ARDUINOJSON_STRING_LENGTH_SIZE 2) should cut it.
//   * the text comes back SHORTER or different: something truncates -- the worst
//     outcome here, because a cut copy looks like a real copy (CLAUDE.md "a
//     truncated row is a lie"). Fail, and find the cut.
//   * a length fails to parse that a shorter one passes: a size cliff. Fail.
//   * the steady-state ledger moves between two identical rotations: the poll
//     leaks or grows per cycle. Fail.
//   * heap: PREDICTED before running, for one 500-character station message in a
//     20-message payload, 32-bit build: < 8 KB of extra internal heap at the
//     poll's peak and < 2 KB extra retained per copy in the store. These are
//     reported, NOT asserted (the 64-bit CI build counts different sizes); a
//     figure far above the prediction means the model of the copies is wrong and
//     the bench step's numbers must be read before E6 is closed.
//   * none of the above (a crash, a sanitizer report, an exit the rig does not
//     know): the instrument cannot answer at this scale. Stop and decide.
//
// CONTROLS, so a PASS can be believed: a body whose long text was cut to 255
// characters must be caught by the same comparison every case uses; a body cut
// mid-string must NOT parse (and must leave the store alone); a deliberately
// leaked String must move the steady-state ledger; and the fixed-buffer scan must
// see a planted `char[]`.
//
// Exit: 0 pass, 1 a check failed, 2 BLIND (fixtures or sources not found).
#include <Arduino.h>
#include <ArduinoJson.h>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "EamFeedClient.h"
#include "hostheap.h"

static int failures = 0;

static void expect(bool ok, const std::string& what)
{
    if (!ok) { printf("  FAIL  %s\n", what.c_str()); failures++; }
    else printf("  ok    %s\n", what.c_str());
}

// ============================================================================ the fakes
static uint32_t g_ms = 1000;
uint32_t millis() { return g_ms; }

namespace hostrtos {
struct WorkerIdle {};                 // thrown out of RunWorker's for(;;) when it has no work
struct Queue {
    size_t itemSize;
    bool full;
    unsigned char slot[sizeof(void*)];
};
static TaskFunction_t g_fn = nullptr;
static void* g_arg = nullptr;
static Queue* g_req = nullptr;        // EamFeedClient::Begin creates reqQueue first, then resQueue
static Queue* g_res = nullptr;
static int g_created = 0;
}  // namespace hostrtos

QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t itemSize)
{
    using namespace hostrtos;
    if (itemSize > sizeof(void*)) return nullptr;   // the client queues pointers only
    Queue* q = new Queue{itemSize, false, {0}};
    if (g_created++ % 2 == 0) g_req = q; else g_res = q;
    return q;
}
BaseType_t xQueueSend(QueueHandle_t h, const void* item, TickType_t)
{
    auto* q = static_cast<hostrtos::Queue*>(h);
    if (q->full) return pdFALSE;      // depth 1, as on the device
    std::memcpy(q->slot, item, q->itemSize);
    q->full = true;
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t h, void* item, TickType_t wait)
{
    auto* q = static_cast<hostrtos::Queue*>(h);
    if (!q->full) {
        if (wait == portMAX_DELAY) throw hostrtos::WorkerIdle();
        return pdFALSE;
    }
    std::memcpy(item, q->slot, q->itemSize);
    q->full = false;
    return pdTRUE;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char*, uint32_t, void* arg, UBaseType_t,
                                   TaskHandle_t* handle, BaseType_t)
{
    hostrtos::g_fn = fn;              // recorded, not started: the test runs it, one request at a time
    hostrtos::g_arg = arg;
    if (handle) *handle = reinterpret_cast<TaskHandle_t>(&hostrtos::g_fn);
    return pdTRUE;
}

namespace fakenet {
static std::string g_body;            // what /eam/latest answers
static int g_latestCalls = 0;
static String g_lastUrl;
static bool g_sentDeviceHeader = false;
static bool g_sentLimit20 = false;

// ArduinoJson's custom-reader contract (read / readBytes), handing the body over
// at most 512 bytes per readBytes -- the size of the socket streams' buffer.
struct ChunkReader {
    const std::string* s;
    size_t pos;
    int read() { return pos < s->size() ? (unsigned char)(*s)[pos++] : -1; }
    size_t readBytes(char* buf, size_t n)
    {
        if (n > 512) n = 512;
        if (n > s->size() - pos) n = s->size() - pos;
        std::memcpy(buf, s->data() + pos, n);
        pos += n;
        return n;
    }
};
}  // namespace fakenet

HttpResult HttpRequestManager::GetJson(const String& url, JsonDocument& doc,
                                       const std::vector<std::pair<String, String>>& params,
                                       const std::vector<std::pair<String, String>>& headers)
{
    HttpResult r;
    fakenet::g_lastUrl = url;
    if (!url.endsWith("/eam/latest")) {  // every other endpoint: the fixture server's 404
        r.statusCode = 404;
        return r;
    }
    fakenet::g_latestCalls++;
    for (const auto& h : headers)
        if (h.first == "X-Missileer-Device" && h.second.length() >= 8) fakenet::g_sentDeviceHeader = true;
    for (const auto& p : params)
        if (p.first == "limit" && p.second == "20") fakenet::g_sentLimit20 = true;
    fakenet::ChunkReader reader{&fakenet::g_body, 0};
    const DeserializationError err = deserializeJson(doc, reader);
    r.statusCode = 200;
    r.success = !err;
    r.bodyBytes = fakenet::g_body.size();
    return r;
}

// ======================================================================= the fixtures
static std::string g_root;

static bool ReadFile(const std::string& path, std::string& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}

// Every value of `"key":"..."` in body order. The fixtures' strings are [A-Z0-9-:.]
// with no escapes, so this is exact -- and it shares no code with ArduinoJson.
static std::vector<std::string> StringsOf(const std::string& body, const char* key)
{
    std::vector<std::string> out;
    const std::string pat = std::string("\"") + key + "\":\"";
    size_t p = 0;
    while ((p = body.find(pat, p)) != std::string::npos) {
        p += pat.size();
        const size_t e = body.find('"', p);
        out.push_back(body.substr(p, e - p));
        p = e;
    }
    return out;
}

static std::vector<std::string> Split(const std::string& t)
{
    std::vector<std::string> g;
    size_t i = 0;
    while (i < t.size()) {
        while (i < t.size() && t[i] == ' ') i++;
        size_t j = i;
        while (j < t.size() && t[j] != ' ') j++;
        if (j > i) g.push_back(t.substr(i, j - i));
        i = j;
    }
    return g;
}

static std::string Replace(std::string s, const std::string& from, const std::string& to, int* count = nullptr)
{
    int n = 0;
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) { s.replace(p, from.size(), to); p += to.size(); n++; }
    if (count) *count = n;
    return s;
}

// One expected message: what the bytes say, independent of the parser.
struct Want {
    std::string id, text;
    std::vector<std::string> groups;
    int charCount = 0, groupCount = 0;
};

struct Case {
    std::string name;
    int len = 0;
    std::string body;
    std::vector<Want> want;   // in served order
};

static std::string Quoted(const std::vector<std::string>& v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ",\"" : "\"") + v[i] + "\"";
    return s;
}

// The station form of message m as the generator writes it, for string edits.
static std::string StationTriple(const std::string& t)
{
    return "\"text\":\"" + t + "\",\"groups\":[\"" + t + "\"],\"char_count\":" + std::to_string(t.size()) +
           ",\"group_count\":1";
}

// Spaced copy of exactly `len` characters INCLUDING spaces: 6-character groups,
// the shape an upstream transcript with spacing has (normalize.ts splitGroups).
static std::string Spaced(const std::string& src, int len)
{
    std::string s;
    size_t k = 0;
    while ((int)s.size() < len) {
        const bool gap = s.size() % 7 == 6 && (int)s.size() != len - 1;
        s += gap ? ' ' : src[k++ % src.size()];
    }
    return s;
}

static int NonSpace(const std::string& t)
{
    int n = 0;
    for (char c : t) n += c != ' ';
    return n;
}

static bool BaseCase(int len, Case& c)
{
    char name[32];
    std::snprintf(name, sizeof(name), "latest-%03d.json", len);
    if (!ReadFile(g_root + "/test/fixtures/eam-long/" + name, c.body)) return false;
    c.name = std::string(name) + " station";
    c.len = len;
    const auto ids = StringsOf(c.body, "id");
    const auto texts = StringsOf(c.body, "text");
    if (ids.size() != 20 || texts.size() != 20) return false;
    for (size_t i = 0; i < ids.size(); ++i) {
        Want w;
        w.id = ids[i];
        w.text = texts[i];
        w.groups = {texts[i]};
        w.charCount = (int)texts[i].size();
        w.groupCount = 1;
        c.want.push_back(w);
    }
    return (int)c.want[0].text.size() == len;
}

static Case SpacedCase(const Case& base, bool emptyGroups)
{
    Case c = base;
    const std::string t = Spaced(base.want[0].text, base.len);
    const auto g = Split(t);
    int n = 0;
    c.body = Replace(c.body, StationTriple(base.want[0].text),
                     "\"text\":\"" + t + "\",\"groups\":[" + (emptyGroups ? std::string() : Quoted(g)) +
                         "],\"char_count\":" + std::to_string(NonSpace(t)) +
                         ",\"group_count\":" + std::to_string(g.size()),
                     &n);
    c.name = base.name.substr(0, base.name.find(' ')) + (emptyGroups ? " groups:[] (SplitGroups)" : " spaced");
    c.want[0].text = t;
    c.want[0].groups = g;   // with groups [] the FIRMWARE must split it into exactly these
    c.want[0].charCount = NonSpace(t);
    c.want[0].groupCount = (int)g.size();
    if (n != 1) c.body.clear();   // the edit missed: the case is unusable (reported BLIND)
    return c;
}

// The fields valar-eam-feed keeps off /eam/latest pending this check (store.ts
// ARCHIVE_ONLY_FIELDS, correctedView), plus a correction's two extra copies.
static Case FatCase(const Case& base)
{
    Case c = base;
    const std::string t = base.want[0].text;
    std::string other(t.rbegin(), t.rend());
    std::string notes;
    for (int i = 0; i < 12; ++i) {
        notes += (i ? "," : "");
        notes += "{\"pos\":" + std::to_string(7 + i * 11) + ",\"heard\":[\"Q\",\"K\"],\"reason\":\"read_two_ways\"}";
    }
    const std::string anchor = "\"repeat_last_at\":\"2026-10-11T12:01:00.000Z\"";
    int n = 0;
    c.body = Replace(c.body, anchor,
                     anchor + ",\"machine_text\":\"" + other + "\",\"origin_text\":\"" + other +
                         "\",\"copies_disagree\":true,\"copy_source\":\"desktop\",\"partial\":false"
                         ",\"partial_basis\":\"station\",\"announced_length\":" + std::to_string(base.len) +
                         ",\"station_notes\":[" + notes + "]",
                     &n);
    c.name = base.name.substr(0, base.name.find(' ')) + " fat (archive-only fields)";
    if (n != 1) c.body.clear();
    return c;
}

// All 20 messages at the long length: the most /eam/latest?limit=20 can carry.
static Case All20Case(const Case& base)
{
    Case c = base;
    for (size_t i = 1; i < c.want.size(); ++i) {
        const std::string old = c.want[i].text;
        std::string t;
        while ((int)t.size() < base.len) t += old;
        t.resize(base.len);
        int n = 0;
        c.body = Replace(c.body, StationTriple(old), StationTriple(t), &n);
        if (n != 1) { c.body.clear(); return c; }
        c.want[i].text = t;
        c.want[i].groups = {t};
        c.want[i].charCount = (int)t.size();
    }
    c.name = base.name.substr(0, base.name.find(' ')) + " all 20 long";
    return c;
}

// ========================================================================= the client
struct Rig {
    HttpRequestManager http;
    OpenSkyAuthTokenHandler auth{http};
    EamFeedClient feed{http, auth};

    void Start()
    {
        feed.Begin();
        EamFeedClient::Config cfg;
        cfg.baseUrl = "http://e6-fixture.invalid:8765";   // never resolved: GetJson is the fake
        cfg.abncpSource = EamFeedClient::AbncpSource::Backend;
        feed.Configure(cfg);
    }
};

static eam::EamEndpoint QueuedEndpoint(bool& any)
{
    any = hostrtos::g_req && hostrtos::g_req->full;
    if (!any) return eam::EamEndpoint::Latest;
    eam::EamFetchRequest* req = nullptr;
    std::memcpy(&req, hostrtos::g_req->slot, sizeof(req));
    return req->endpoint;
}

static void RunWorkerOnce()
{
    try { hostrtos::g_fn(hostrtos::g_arg); } catch (const hostrtos::WorkerIdle&) {}
}

// Service every OTHER due feed (404s) without letting /eam/latest go, so the
// measured poll has the client to itself.
static void DrainOthers(Rig& r)
{
    for (int guard = 0; guard < 64; ++guard) {
        r.feed.Poll();
        bool any = false;
        const eam::EamEndpoint e = QueuedEndpoint(any);
        if (!any) return;
        if (e == eam::EamEndpoint::Latest) return;   // caller's clock was wrong; it will see it queued
        RunWorkerOnce();
    }
}

struct PollCost {
    hostheap::Totals before, fetchPeak, afterFetch, applyPeak, afterApply;
    bool latestQueued = false, quietAfter = false;
};

// One /eam/latest poll, measured in two phases: the worker (GetJson + parse, the
// JsonDocument alive) and the loop (ApplyResult + MergeLatest + delete res).
static PollCost MeasuredLatestPoll(Rig& r)
{
    PollCost pc;
    r.feed.Poll();                     // dispatch
    bool any = false;
    pc.latestQueued = QueuedEndpoint(any) == eam::EamEndpoint::Latest && any;
    if (!pc.latestQueued) return pc;
    pc.before = hostheap::Now();
    hostheap::ResetPeak();
    RunWorkerOnce();
    pc.fetchPeak = hostheap::Peak();
    pc.afterFetch = hostheap::Now();
    hostheap::ResetPeak();
    r.feed.Poll();                     // apply
    pc.applyPeak = hostheap::Peak();
    pc.afterApply = hostheap::Now();
    QueuedEndpoint(any);
    pc.quietAfter = !any;              // nothing else dispatched inside the measurement
    return pc;
}

// Did the store take message i whole? Returns "" when it did, else the first difference.
static std::string Compare(const eam::Msg& m, const Want& w)
{
    if (std::string(m.id.c_str()) != w.id) return "id " + std::string(m.id.c_str()) + " != " + w.id;
    if (m.text.length() != w.text.size())
        return "text length " + std::to_string(m.text.length()) + " != " + std::to_string(w.text.size());
    if (std::memcmp(m.text.c_str(), w.text.data(), w.text.size()) != 0) return "text bytes differ";
    if (m.groups.size() != w.groups.size())
        return "groups " + std::to_string(m.groups.size()) + " != " + std::to_string(w.groups.size());
    for (size_t g = 0; g < w.groups.size(); ++g)
        if (std::string(m.groups[g].c_str()) != w.groups[g]) return "group " + std::to_string(g) + " differs";
    if (m.charCount != w.charCount) return "char_count " + std::to_string(m.charCount);
    if (m.groupCount != w.groupCount) return "group_count " + std::to_string(m.groupCount);
    return "";
}

static std::string CompareAll(const std::vector<eam::Msg>& got, const std::vector<Want>& want)
{
    if (got.size() != want.size())
        return "store holds " + std::to_string(got.size()) + " messages, payload had " + std::to_string(want.size());
    for (size_t i = 0; i < want.size(); ++i) {
        const std::string d = Compare(got[i], want[i]);
        if (!d.empty()) return "message " + std::to_string(i) + ": " + d;
    }
    return "";
}

static long Kb10(long b) { return (b * 10 + 512) / 1024; }   // tenths of a KB, rounded
static std::string Kb(long b)
{
    char s[24];
    const long t = Kb10(b);
    std::snprintf(s, sizeof(s), "%s%ld.%ld", t < 0 ? "-" : "", (t < 0 ? -t : t) / 10, (t < 0 ? -t : t) % 10);
    return s;
}

struct Row {
    std::string name;
    size_t body;
    PollCost pc;
};

static void PrintRows(const std::vector<Row>& rows, const Row* ref)
{
    printf("    %-40s %6s | %9s %9s | %9s %9s | %6s\n", "payload (one poll, fresh client)", "body",
           "fetch pk", "in-flight", "apply pk", "retained", "psram");
    for (const Row& r : rows) {
        const PollCost& p = r.pc;
        printf("    %-40s %6u | %9s %9s | %9s %9s | %6s\n", r.name.c_str(), (unsigned)r.body,
               Kb(p.fetchPeak.devInternal - p.before.devInternal).c_str(),
               Kb(p.afterFetch.devInternal - p.before.devInternal).c_str(),
               Kb(p.applyPeak.devInternal - p.before.devInternal).c_str(),
               Kb(p.afterApply.devInternal - p.before.devInternal).c_str(),
               Kb(std::max(p.fetchPeak.devPsram, p.applyPeak.devPsram) - p.before.devPsram).c_str());
        if (ref && &r != ref)
            printf("    %-40s %6s | %9s %9s | %9s %9s |\n", "    minus the 31-char row", "",
                   Kb((p.fetchPeak.devInternal - p.before.devInternal) -
                      (ref->pc.fetchPeak.devInternal - ref->pc.before.devInternal)).c_str(),
                   "",
                   Kb((p.applyPeak.devInternal - p.before.devInternal) -
                      (ref->pc.applyPeak.devInternal - ref->pc.before.devInternal)).c_str(),
                   Kb((p.afterApply.devInternal - p.before.devInternal) -
                      (ref->pc.afterApply.devInternal - ref->pc.before.devInternal)).c_str());
    }
}

// ================================================================= fixed-buffer inventory
// Every fixed char buffer and every bounded/unbounded C copy in the code a message's
// text can reach, with what feeds it. A NEW site fails this test until it is added
// here with its feed: "this buffer never sees message text" is a premise about code
// the next change may not know it is touching (CLAUDE.md "a justification is a claim
// about code you are not editing" -- assert it, don't state it).
//
// Where the text goes, end to end (read 2026-10-11):
//   EamModels.cpp ParseMsg     text/groups -> String (and SplitGroups -> String)
//   EamFeedClient.cpp          copies of std::vector<Msg>: res.messages, incoming, merged
//   EamScreens.cpp DrawTicker  groups[i] -> BandCanvas::drawString/textWidth (LovyanGFX;
//                              graded by test_eam_ticker_draw.cpp); charCount -> String
//   EamManager.cpp CheckAlerts m.text -> String body -> NtfyAlerter::Send -> http.Post
//   EamManager.cpp elsewhere   only m.id / m.heardAt (logbook, drill, USB open)
// None of the buffers below is on that path.
struct Site {
    const char* file;
    const char* token;   // "char NAME[" or "CALL("
    int count;
    const char* feed;
};
static const Site kInventory[] = {
    {"src/eam/OnWatch.h",       "char tmp[",     1, "digits of the on_watch count (a long)"},
    {"src/eam/EamLogbook.cpp",  "char buf[",     1, "an epoch formatted as ISO-8601 (ints)"},
    {"src/eam/EamLogbook.cpp",  "snprintf(",     1, "the same, bounded by sizeof"},
    {"src/eam/EamManager.cpp",  "char buf[",     1, "FormatCountdown: days/h/m/s (longs)"},
    {"src/eam/EamManager.cpp",  "char b[",       1, "tempo alert: Tempo.level (feed String) + an int"},
    {"src/eam/EamManager.cpp",  "snprintf(",     3, "the two above, bounded by sizeof"},
    {"src/eam/EamScreens.cpp",  "char kAdsbFiCredit[", 1, "a const string literal (read-only)"},
    {"src/eam/EamScreens.cpp",  "char rb[",      1, "tempo ratio (float)"},
    {"src/eam/EamScreens.cpp",  "char fl[",      1, "frequency in MHz (int)"},
    {"src/eam/EamScreens.cpp",  "char seen[",    1, "codewords this month (unsigned)"},
    {"src/eam/EamScreens.cpp",  "char bd[",      2, "ABNCP / mil-air bearing + km (ints)"},
    {"src/eam/EamScreens.cpp",  "char more[",    1, "mil-air paging (ints)"},
    {"src/eam/EamScreens.cpp",  "char sk[",      1, "SFI / K index (ints)"},
    {"src/eam/EamScreens.cpp",  "char count[",   1, "ON WATCH digits (OnWatch::Text)"},
    {"src/eam/EamScreens.cpp",  "snprintf(",     7, "the six numeric buffers above, bounded by sizeof"},
    {"src/eam/KeyTouchSampler.cpp", "char runTag[", 1, "bench run tag (a char + an unsigned)"},
    {"src/eam/KeyTouchSampler.cpp", "snprintf(",  1, "the same, bounded by sizeof"},
};
static const char* kCalls[] = {"snprintf(", "sprintf(", "vsnprintf(", "strncpy(", "strcpy(", "strcat(",
                               "strncat(", "memcpy(", "memmove(", "strlcpy("};

// Comments out, so a buffer named in prose is not a buffer (the run.sh symbol scan's lesson).
static std::string StripComments(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s.compare(i, 2, "//") == 0) { while (i < s.size() && s[i] != '\n') i++; o += '\n'; continue; }
        if (s.compare(i, 2, "/*") == 0) { const size_t e = s.find("*/", i + 2); i = e == std::string::npos ? s.size() : e + 1; continue; }
        if (s[i] == '"' || s[i] == '\'') {   // a literal's CONTENT is not code: "char x[" in a string
            const char q = s[i++];            // is not a declaration, and '"' must not open a string
            while (i < s.size() && s[i] != q) i += s[i] == '\\' ? 2 : 1;
            o += q;
            o += q;
            continue;
        }
        o += s[i];
    }
    return o;
}

// Every "char NAME[" (any spacing) and every listed call, as tokens with counts.
static std::vector<std::pair<std::string, int>> Sites(const std::string& src)
{
    const std::string s = StripComments(src);
    std::vector<std::pair<std::string, int>> out;
    auto add = [&](const std::string& t) {
        for (auto& p : out) if (p.first == t) { p.second++; return; }
        out.push_back({t, 1});
    };
    for (size_t i = 0; (i = s.find("char", i)) != std::string::npos; i += 4) {
        if (i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_')) continue;
        size_t j = i + 4;
        if (j >= s.size() || !isspace((unsigned char)s[j])) continue;
        while (j < s.size() && isspace((unsigned char)s[j])) j++;
        size_t k = j;
        while (k < s.size() && (isalnum((unsigned char)s[k]) || s[k] == '_')) k++;
        if (k == j) continue;
        size_t b = k;
        while (b < s.size() && s[b] == ' ') b++;
        if (b < s.size() && s[b] == '[') add("char " + s.substr(j, k - j) + "[");
    }
    for (const char* call : kCalls)
        for (size_t i = 0; (i = s.find(call, i)) != std::string::npos; i += std::strlen(call)) {
            if (i > 0 && (isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_')) continue;
            add(call);
        }
    return out;
}

// ================================================================================ main
int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: %s <repo root> <source files to scan...>\n", argv[0]); return 2; }
    g_root = argv[1];
    printf("pointer size %u: %s\n", (unsigned)sizeof(void*),
           sizeof(void*) == 4 ? "32-bit, as the ESP32-S3 -- heap figures are device-shaped"
                              : "NOT 32-bit -- heap figures below are this host's, not the device's");

    std::vector<Case> bases;
    for (int len : {31, 75, 290, 500}) {
        Case c;
        if (!BaseCase(len, c)) {
            printf("BLIND: test/fixtures/eam-long/latest-%03d.json missing or not the generator's shape\n", len);
            return 2;
        }
        bases.push_back(c);
    }

    // ---------------------------------------------------------------- parse, every shape
    printf("\n-- the shipping client takes each payload whole --\n");
    std::vector<Case> cases;
    for (const Case& b : bases) {
        cases.push_back(b);
        cases.push_back(SpacedCase(b, false));
        cases.push_back(SpacedCase(b, true));
    }
    cases.push_back(FatCase(bases[3]));
    cases.push_back(All20Case(bases[3]));
    cases.push_back(FatCase(All20Case(bases[3])));
    cases.back().name = "latest-500.json all 20 long, fat";
    for (const Case& c : cases)
        if (c.body.empty()) { printf("BLIND: could not derive '%s' from the fixture\n", c.name.c_str()); return 2; }

    std::vector<Row> rows;
    for (const Case& c : cases) {
        Rig rig;
        rig.Start();
        g_ms += 300;                    // Configure staged /eam/latest at +300 ms, everything else later
        fakenet::g_body = c.body;
        fakenet::g_sentDeviceHeader = fakenet::g_sentLimit20 = false;
        const PollCost pc = MeasuredLatestPoll(rig);
        const std::string diff = CompareAll(rig.feed.Latest(), c.want);
        expect(pc.latestQueued && pc.quietAfter && diff.empty(),
               c.name + ": " + std::to_string(c.want[0].text.size()) + " chars stored whole" +
                   (diff.empty() ? "" : " -- " + diff));
        expect(fakenet::g_sentDeviceHeader && fakenet::g_sentLimit20 && rig.feed.OnWatch().value == 3,
               c.name + ": limit=20 + X-Missileer-Device sent, on_watch read");
        rows.push_back({c.name, c.body.size(), pc});
    }

    // --------------------------------------------------------------------- the controls
    printf("\n-- controls: each must FAIL the check it stands next to --\n");
    {
        // A cut at 255 -- a uint8 length -- is the classic fixed-buffer truncation.
        Case cut = bases[3];
        const std::string t = cut.want[0].text;
        cut.body = Replace(cut.body, StationTriple(t), StationTriple(t.substr(0, 255)));
        Rig rig;
        rig.Start();
        g_ms += 300;
        fakenet::g_body = cut.body;
        const PollCost pc = MeasuredLatestPoll(rig);
        const std::string diff = CompareAll(rig.feed.Latest(), bases[3].want);
        expect(pc.latestQueued && !diff.empty(), "a text cut to 255 is reported (\"" + diff + "\")");
    }
    {
        // A transfer cut mid-text: must not parse, must not reach the store.
        Rig rig;
        rig.Start();
        g_ms += 300;
        const std::string& b = bases[3].body;
        fakenet::g_body = b.substr(0, b.find(bases[3].want[0].text) + 200);
        const PollCost pc = MeasuredLatestPoll(rig);
        expect(pc.latestQueued && rig.feed.Latest().empty() && !rig.feed.OnWatch().Known(),
               "a body cut mid-text does not parse and leaves the store empty");
    }

    // ------------------------------------------------------------ the bench rotation
    // What env missileer-s3-128-e6bench sees: the fixture server answers 031, 075,
    // 290, 500, 031, ... one per /eam/latest poll, every other endpoint 404. Same
    // client for the whole run, so the store, the 50-message retention and the
    // MergeLatest copies are the device's. Three rotations; the second and third
    // must leave the ledger EXACTLY where it was (no per-cycle growth).
    printf("\n-- the bench rotation (31 -> 75 -> 290 -> 500, 3 times, one client) --\n");
    Rig bench;
    bench.Start();
    uint32_t latestDue = g_ms + 300;
    // RESERVED UP FRONT: the ledger counts this test's own vectors too, and a
    // push_back that grows one between two snapshots reads as the client growing.
    // (It did, on the first run: +20 B, exactly one Totals.)
    std::vector<hostheap::Totals> endOfRotation;
    endOfRotation.reserve(3);
    std::vector<PollCost> rotationCosts;
    rotationCosts.reserve(4);
    bool rotationOk = true;
    for (int k = 0; k < 12; ++k) {
        const Case& c = bases[k % 4];
        g_ms = latestDue - 1;
        DrainOthers(bench);            // the other feeds' 404s, before /eam/latest is due
        g_ms = latestDue;
        fakenet::g_body = c.body;
        const PollCost pc = MeasuredLatestPoll(bench);
        latestDue = g_ms + 30000;      // LATEST_MS, interval scale 1 (the bench env forces 1)
        rotationOk = rotationOk && pc.latestQueued && pc.quietAfter &&
                     Compare(bench.feed.Latest().front(), c.want[0]).empty();
        if (k >= 4 && k < 8) rotationCosts.push_back(pc);
        if (k % 4 == 3) endOfRotation.push_back(hostheap::Now());
    }
    const size_t storeSize = bench.feed.Latest().size();
    expect(rotationOk, "12 polls: each time the newest message is the fixture's, whole");
    expect(storeSize == 23, "the store settles at 19 + 4 = 23 messages (holds " + std::to_string(storeSize) + ")");
    expect(endOfRotation.size() == 3 && endOfRotation[1].reqBytes == endOfRotation[2].reqBytes &&
               endOfRotation[1].blocks == endOfRotation[2].blocks,
           "rotations 2 and 3 end on the same ledger (" + std::to_string(endOfRotation[1].reqBytes) + " B / " +
               std::to_string(endOfRotation[1].blocks) + " blocks, then " +
               std::to_string(endOfRotation[2].reqBytes) + " B / " + std::to_string(endOfRotation[2].blocks) +
               ")");
    {
        // CONTROL for the line above: the same comparison with one String leaked in between.
        const hostheap::Totals a = hostheap::Now();
        String* leak = new String(bases[3].want[0].text.c_str());
        const hostheap::Totals b = hostheap::Now();
        expect(a.reqBytes != b.reqBytes || a.blocks != b.blocks, "control: a leaked 500-char String moves the ledger");
        delete leak;
    }

    // ------------------------------------------------------------------- heap figures
    printf("\n-- heap, in KB of INTERNAL device heap (DeviceBytes of blocks <= 4096 B; derived) --\n");
    printf("   fetch pk  = worker peak over its start: JsonDocument + the parsed result being built\n");
    printf("   in-flight = the parsed result handed to the loop (the doc is gone)\n");
    printf("   apply pk  = loop peak over the same start: result + `incoming` + `merged` + old store\n");
    printf("   retained  = what the store keeps after the poll\n");
    printf("   psram     = peak of blocks > 4096 B (go to PSRAM on the S3)\n\n");
    std::vector<Row> stationRows;
    for (const Row& r : rows)
        if (r.name.find(" station") != std::string::npos) stationRows.push_back(r);
    PrintRows(stationRows, &stationRows[0]);
    printf("\n");
    std::vector<Row> otherRows;
    for (const Row& r : rows)
        if (r.name.find(" station") == std::string::npos) otherRows.push_back(r);
    PrintRows(otherRows, nullptr);

    // THE BENCH'S PREDICTION, in its own vocabulary (src/eam/E6Bench.h phases): the drop
    // in int_free from phase=pre to each later phase, for the steady-state rotation the
    // fixture server produces. docs/e6-long-message-check.md reads these against the
    // device's lines. Only the 32-bit build's numbers are the device's shape.
    printf("\n    bench rotation, second pass -- PREDICTED [e6-bench] int_free drop from phase=pre, KB%s:\n",
           sizeof(void*) == 4 ? "" : " (64-bit host: NOT the device's)");
    printf("    %-8s | %6s %6s %6s %6s\n", "len", "doc", "res", "merge", "kept");
    for (int i = 0; i < 4; ++i) {
        const PollCost& p = rotationCosts[i];
        printf("    %-8d | %6s %6s %6s %6s\n", bases[i].len,
               Kb(p.fetchPeak.devInternal - p.before.devInternal).c_str(),
               Kb(p.afterFetch.devInternal - p.before.devInternal).c_str(),
               Kb(p.applyPeak.devInternal - p.before.devInternal).c_str(),
               Kb(p.afterApply.devInternal - p.before.devInternal).c_str());
    }
    printf("    %-8s | %6s %6s %6s %6s\n", "500-31",
           Kb((rotationCosts[3].fetchPeak.devInternal - rotationCosts[3].before.devInternal) -
              (rotationCosts[0].fetchPeak.devInternal - rotationCosts[0].before.devInternal)).c_str(),
           Kb((rotationCosts[3].afterFetch.devInternal - rotationCosts[3].before.devInternal) -
              (rotationCosts[0].afterFetch.devInternal - rotationCosts[0].before.devInternal)).c_str(),
           Kb((rotationCosts[3].applyPeak.devInternal - rotationCosts[3].before.devInternal) -
              (rotationCosts[0].applyPeak.devInternal - rotationCosts[0].before.devInternal)).c_str(),
           Kb((rotationCosts[3].afterApply.devInternal - rotationCosts[3].before.devInternal) -
              (rotationCosts[0].afterApply.devInternal - rotationCosts[0].before.devInternal)).c_str());
    printf("    store after 12 polls: %u messages (19 fillers + the 4 long ones)\n", (unsigned)storeSize);

    // ---------------------------------------------------------- fixed-buffer inventory
    printf("\n-- every fixed buffer the EAM code declares, and what feeds it --\n");
    {
        // CONTROL first: the scanner must see a planted buffer and ignore one in prose.
        const auto planted = Sites("void f() { char x [8]; snprintf(x, 8, \"%s\", t); } // char y[4]\n"
                                   "/* char z[2]; */ const char* s = \"char q[3]\";");
        bool sawX = false, sawY = false, sawQ = false, sawCall = false;
        for (const auto& p : planted) {
            sawX |= p.first == "char x[";
            sawY |= p.first == "char y[" || p.first == "char z[";
            sawQ |= p.first == "char q[";
            sawCall |= p.first == "snprintf(";
        }
        expect(sawX && sawCall && !sawY && !sawQ, "control: a planted char x[8] + snprintf is seen; comments/strings are not");
    }
    // THE FILES come from run.sh (every src/eam/*.h and *.cpp, plus NtfyAlerter.h), so
    // a new file is scanned without anyone remembering to list it here.
    bool blind = argc < 3;
    if (blind) printf("BLIND: no source files given to scan\n");
    std::vector<std::string> unknown;
    std::vector<int> matched(sizeof(kInventory) / sizeof(kInventory[0]), 0);
    for (int a = 2; a < argc; ++a) {
        const char* f = argv[a];
        std::string src;
        if (!ReadFile(g_root + "/" + f, src)) { printf("BLIND: cannot read %s\n", f); blind = true; continue; }
        for (const auto& site : Sites(src)) {
            bool known = false;
            for (size_t i = 0; i < matched.size(); ++i)
                if (std::strcmp(kInventory[i].file, f) == 0 && site.first == kInventory[i].token) {
                    known = site.second == kInventory[i].count;
                    matched[i] = site.second;
                }
            if (!known) unknown.push_back(std::string(f) + ": " + site.first + " x" + std::to_string(site.second));
        }
    }
    for (size_t i = 0; i < matched.size(); ++i)
        printf("    %-28s %-14s x%d  %s\n", kInventory[i].file, kInventory[i].token, matched[i], kInventory[i].feed);
    for (const std::string& u : unknown) printf("    NOT IN THE INVENTORY: %s -- classify what feeds it\n", u.c_str());
    bool allMatched = true;
    for (size_t i = 0; i < matched.size(); ++i) allMatched &= matched[i] == kInventory[i].count;
    expect(unknown.empty() && allMatched,
           "every fixed buffer / C copy in src/eam + NtfyAlerter.h is inventoried, none fed by message text");

    if (blind) return 2;
    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
