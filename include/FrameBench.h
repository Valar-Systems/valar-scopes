// THE FRAME BENCH, C1 (docs/frame-time-bench.md, "C1's bench"; p4's M3-3, docs/m3-push-task.md): a bench-only
// instrument on the M3 tree, built only under -DFRAME_BENCH (envs blipscope-s3-128-framebench-c1a / -c1b, slug-less,
// never published). The whole instrument is this header, FrameBenchTable.h and the `// THE FRAME BENCH` regions in
// main.cpp, AircraftManager.cpp and ConfigurationWebServer.cpp. Nothing here writes NVS, and nothing prints a
// coordinate, a name or an address.
//
// THE SCENE (fixed by M3-3's registration): Bend, 40 aircraft from the (a) table (fixed_traffic.py's formulas, served
// into the cloud /blips path after the real GET's parse), 25 mi, weather ON over a fixed one-cell field (the real
// /wx response's header, its body replaced), map OFF (the registered deviation). Image A (-DBLIPSCOPE_NO_PUSH_TASK)
// runs the scene once (A1, then A2); image B (the push task) runs it plain, with D = 10 and D = 60 ms push-task delays
// (vTaskDelay: the push task outranks the loop on its core, so a spin would preempt the loop), and with the loop's
// +10 ms (I-6).
//
// PER FRAME (Rec): fr and p2p in 10 us units; tracked and drawn; flags: bit 0 the weather layer rebuilt on this frame,
// bit 1 the loop's planted delay, bit 2 the weather crop shown (wxShowing == Crop), bit 3 the map enabled, bit 4 the
// night clock's night; and `pres`, the presents since the previous record (M3-2: 1 in lockstep).
#pragma once
#ifdef FRAME_BENCH

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include "FrameBenchTable.h"
#include "WxCrop.h"
#include "variants/Variant.h"   // BLIPSCOPE_PUSH_TASK is a variant HEADER's: included here so every file sees one kScenes

namespace framebench {

enum Kind : uint8_t { C1 = 0, C1_PUSH10 = 1, C1_PUSH60 = 2, C1_LOOP10 = 3, C1_PUSH60_DROP = 4, FT_MAP = 5, FT_WX = 6 };
// FT's roles (docs/frame-time-bench.md, "THE REBUILD ROW"): KORD's A/A pair 1 first (its margins frozen), the timer
// plant, the graded pairs, KORD's A/A pair 2 last (graded against pair 1's margins).
enum Role : uint8_t { GRADED = 0, PAIR1 = 1, PLANT = 2, PAIR2 = 3 };
struct Scene {
    Kind kind;
    uint8_t pkg = 0;      // FT: the index into the row's pinned packages (FrameBenchFt.h); C1: unused
    uint8_t rungMi = 0;   // FT: the scene's rung; 0 is kRungMi (C1's scenes)
    uint8_t role = GRADED;
};
#if defined(FRAME_BENCH_FT)
#include "FrameBenchFt.h"   // the FT row's pinned packages and its scene table (kScenes)
#elif defined(BLIPSCOPE_PUSH_TASK)
#if defined(FRAME_BENCH_PROOF_DROP)
// THE PACKAGE's proof image only (env c1b-proof): image B's four scenes, then S4, push60 with ONE dropped update
static const Scene kScenes[] = {{C1}, {C1_PUSH10}, {C1_PUSH60}, {C1_LOOP10}, {C1_PUSH60_DROP}};
#else
static const Scene kScenes[] = {{C1}, {C1_PUSH10}, {C1_PUSH60}, {C1_LOOP10}};   // image B
#endif
#else
static const Scene kScenes[] = {{C1}};   // image A: A1, and A2 after B
#endif
constexpr int kNumScenes = sizeof(kScenes) / sizeof(kScenes[0]);
static const char* const kKindName[] = {"C1", "C1+push10", "C1+push60", "C1+loop10", "C1+push60drop", "FT-MAP", "FT-WX"};
constexpr uint32_t kPushDelayMs[] = {0, 10, 60, 0, 60, 0, 0};
static const char* const kRoleName[] = {"graded", "pair1", "plant", "pair2"};
// THE PACKAGE's M3-2 (docs/frame-time-bench.md): an exact count per scene, the span boundary carried by a tag.
// FT-HR's window is the longest scene list: 6 + 1 + 36 + 6 = 49.
constexpr int kMaxScenes = 49;
static_assert(kNumScenes <= kMaxScenes, "the per-scene counts hold the longest scene list");
constexpr uint32_t kDropAt = 100;   // the dropped update: the 100th tagged frame of a C1_PUSH60_DROP scene

// Bend, at 2 dp: only ever returned to Initialise() and to the table; nothing prints it.
static const char* const kHomeLat = "44.06";
static const char* const kHomeLon = "-121.31";
constexpr int kRungMi = 25;
constexpr int kLevel = 40;
constexpr uint32_t kTblT = 1794441386;   // TBL-DEV's time (the pinned build epoch)

// THE ECHO CELL (the orchestrator's acceptance of the recorded choices): 15 mi from the home at bearing 135 deg, the
// lowest drawn code, one crop cell; code 0 everywhere else.
constexpr double kCellMi = 15.0;
constexpr double kCellBearingDeg = 135.0;
constexpr uint8_t kCellCode = 2;

constexpr uint32_t START_AFTER_MS = 90000;
constexpr uint32_t SETTLE_MS = 100000;
constexpr uint32_t MEASURE_MS = 120000;
// FT's forced part, after the steady window (docs/frame-time-bench.md, "THE REBUILD ROW"): N = 20 forced rebuilds,
// 2 s apart, each by re-accepting the scene's own package (++mapPkgSeq); fewer than 10 recorded reads INSTRUMENT.
constexpr uint16_t kFtForced = 20;
constexpr uint32_t kFtForceGapMs = 2000;
constexpr uint32_t PLANTED_DELAY_US = 10000;
constexpr int kCap = 4096;
constexpr int kDumpPerLine = 32;
constexpr int kDumpLinesPerFrame = 2;

struct Rec {
    uint16_t fr10;
    uint16_t p2p10;
    uint8_t tracked;
    uint8_t drawn;
    uint8_t flags;
    uint8_t pres;
};
static_assert(sizeof(Rec) == 8, "8 bytes a frame");

// The keys Initialise() read for the scene, noted by GetStoredString's regions while `noting` (the record).
constexpr int kMaxKeys = 96;
struct Noted {
    char key[24];
    char val[12];   // a short safe value, or "set" / "empty"; "bench" for a key the scene answers
};

struct State {
    Rec* buf = nullptr;
    Rec* dumpBuf = nullptr;
    int n = 0;
    int overflowFrames = 0;
    int dumpN = 0, dumpPos = 0, dumpScene = -1;
    int scene = -1;
    bool measuring = false, done = false;
    uint32_t phaseStartMs = 0;
    // M3-2: the present, published by whichever task presents (the push task in B, the loop in A), under Mux()
    uint32_t lastPresentUs = 0, p2pUs = 0, presents = 0;
    bool havePresent = false;
    uint32_t seenPresents = 0;   // the loop's last read of `presents`
    // THE PACKAGE's M3-2: each queued buffer's tag (the scene measuring when it was handed over, or -1), and per
    // scene the frames handed over while measuring, the tagged frames presented, the planted drops, under Mux()
    int8_t tag[2] = {-1, -1};
    uint32_t handed[kMaxScenes] = {}, presented[kMaxScenes] = {}, dropped[kMaxScenes] = {}, dropSeen[kMaxScenes] = {};
    uint16_t drawn = 0;
    bool rebuilt = false;        // the weather layer rebuilt this frame (RebuildWeatherLayer's region)
    bool delayed = false;
    // M3-3's instrument (b): the push's own timing over the measured span (B), under Mux(), and its histogram in
    // 1 ms bins over t2 - t0 (the span the D plant sits inside), for D = 60's p95(push) (p4, 02:05Z)
    uint32_t pushN = 0;
    uint64_t pushUs = 0, syncUs = 0;
    uint16_t pushHist[200] = {};   // bin k: [k, k + 1) ms; the last bin holds everything at or above 199 ms
    // the weather field: the first scene response's geometry
    bool wxHave = false;
    uint16_t wxRows = 0, wxCols = 0;
    uint8_t wxCoarsen = 0;
    uint32_t fbSubmits = 0;      // enrichment submits for the table's hexes in this scene (must be 0)
    bool noting = false;
    int nNoted = 0;
    Noted noted[kMaxKeys];
    // FT's rebuild row (docs/frame-time-bench.md, "THE REBUILD ROW"): the scene's part (0 settle, 1 steady, 2 forced),
    // the forced rebuilds recorded, and the steady window's rebuild count (expected 0)
    uint8_t ftPart = 0;
    uint16_t ftForced = 0, ftSteadyRebuilds = 0;
    uint16_t ftForcings = 0;      // the forced part's re-accepts issued (kFtForced of them, kFtForceGapMs apart)
    uint32_t ftNextForceMs = 0;
    // FT: the scene's home, its package's own centre, set by the scene's application from bmpk's reading (lat0Micro /
    // 1e6, the conversion the map bench's Initialise region uses); returned to Initialise and the table, never printed
    bool ftHome = false;
    double ftLat = 0.0, ftLon = 0.0;
    char ftLatS[16] = {}, ftLonS[16] = {};
};

// FT's rebuild record, filled at DrawMapBase's call site from the composer's laps and BuildInfo, so this header needs
// no map type (it also compiles in the C1 envs, which have no map). Microseconds and counts only: no coordinate.
struct RebuildRec {
    uint32_t totalUs = 0, ms = 0;                             // the µs total around the rebuilding Frame(); millis
    uint32_t load = 0, build = 0, names = 0, compose = 0;     // the four laps
    bool loaded = false, composed = false, ready = false, plant = false;
    uint32_t usS1 = 0, usWater = 0, usRivers = 0, usRoads = 0, usColour = 0, usTotal = 0;   // Build's passes
    int rings = 0, rivers = 0, primary = 0, secondary = 0, primaryInView = 0, secondaryInView = 0, level = -1;
    bool secondaryDrawn = false;
    int placed = 0;
    uint32_t cpuMhz = 0;   // the plant's cycle-to-µs conversion (getCpuFrequencyMhz), per rebuild: a change is seen
};
inline State& S()
{
    static State s;
    return s;
}
inline portMUX_TYPE& Mux()
{
    static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
    return m;
}

// FT: the scene's home, at 2 dp in the strings Initialise reads (exact: FT-CTR proves every pinned centre sits at 2 dp).
inline void SetHome(double lat, double lon)
{
    State& s = S();
    s.ftHome = true;
    s.ftLat = lat;
    s.ftLon = lon;
    snprintf(s.ftLatS, sizeof s.ftLatS, "%.2f", lat);
    snprintf(s.ftLonS, sizeof s.ftLonS, "%.2f", lon);
}

// The scene's rung: FT's own, or C1's kRungMi.
inline int RungMi(int scene)
{
    return (scene >= 0 && scene < kNumScenes && kScenes[scene].rungMi) ? kScenes[scene].rungMi : kRungMi;
}

inline uint16_t Ten(uint32_t us)
{
    const uint32_t t = us / 10;
    return t >= 0xFFFF ? 0xFFFF : (uint16_t)t;
}

inline bool InScene()
{
    const State& s = S();
    return s.scene >= 0 && !s.done;
}

// The scene's keys, answered in RAM; nullptr means "the board's own setting" (recorded, never changed). Called only
// through ConfigurationWebServer::GetStoredString's region, which reads and never writes.
inline const char* Override(const char* key)
{
    if (!InScene()) return nullptr;
    const State& st = S();
    static char rung[8];
    snprintf(rung, sizeof rung, "%d", RungMi(st.scene));   // FT: the scene's rung; C1: kRungMi
    if (strcmp(key, "latitude") == 0) return st.ftHome ? st.ftLatS : kHomeLat;    // FT: the package's own centre
    if (strcmp(key, "longitude") == 0) return st.ftHome ? st.ftLonS : kHomeLon;
    if (strcmp(key, "radius") == 0) return rung;
    if (strcmp(key, "radius-unit") == 0) return "mi";
    if (strcmp(key, "data-source") == 0) return "";      // the cloud, on a cloud build
    if (strcmp(key, "local-url") == 0) return "";
    if (strcmp(key, "local-details") == 0) return "";
    if (strcmp(key, "wx-layer") == 0) return "true";     // weather ON (wxlayerswitch::On; the image has WX_LAYER_SHIPS)
    return nullptr;
}

// The record: every key read during a scene's Initialise(), with a value only where it is short and safe.
inline bool Sensitive(const char* key)
{
    static const char* const kNever[] = {"latitude", "longitude", "cloud-key", "cloud-key-fac", "cloud-url",
                                         "opensky-id", "opensky-secret", "ntfy-topic", "follow", "watchlist",
                                         "lb-name", "mqtt-host", "mqtt-user", "mqtt-pass", "mqtt-base", "local-url"};
    for (const char* k : kNever)
        if (strcmp(key, k) == 0) return true;
    return false;
}
inline void NoteKey(const char* key, const char* val, bool bench)
{
    State& s = S();
    if (!s.noting || s.nNoted >= kMaxKeys) return;
    for (int i = 0; i < s.nNoted; ++i)
        if (strcmp(s.noted[i].key, key) == 0) return;
    Noted& n = s.noted[s.nNoted++];
    snprintf(n.key, sizeof n.key, "%s", key);
    bool safe = !Sensitive(key) && strlen(val) <= 10;
    for (const char* p = val; safe && *p; ++p)
        safe = isalnum((unsigned char)*p) || *p == '.' || *p == '-' || *p == '_';
    snprintf(n.val, sizeof n.val, "%s", bench ? (Sensitive(key) ? "bench" : val) : (safe ? val : (*val ? "set" : "empty")));
    if (!bench && safe && *val == 0) snprintf(n.val, sizeof n.val, "empty");
}
inline void PrintNoted(int k)
{
    State& s = S();
    Serial.printf("[frame-bench] keys S%d: %d read; map=off (bench override)\n", k, s.nNoted);
    char line[200];
    int o = 0;
    for (int i = 0; i < s.nNoted; ++i) {
        const int w = snprintf(line + o, sizeof line - o, "%s=%s ", s.noted[i].key, s.noted[i].val);
        if (w < 0 || o + w >= (int)sizeof line - 1) {
            Serial.printf("[frame-bench] keys S%d: %s\n", k, line);
            o = 0;
            --i;
            continue;
        }
        o += w;
    }
    if (o) Serial.printf("[frame-bench] keys S%d: %s\n", k, line);
}

// MAP OFF (the registered deviation): MapEnabled()'s region returns false whenever this is true, from boot, so the
// map's buffers are never allocated in any arm.
#if defined(FRAME_BENCH_FT)
inline bool MapOff() { return false; }   // FT: the map ON, its pinned package swapped per scene (docs/frame-time-bench.md)
#else
inline bool MapOff() { return true; }
#endif

// The table's synthetic hexes (fb0000 .. fb0027) never leave the board: the enrichment scan's region skips them.
inline bool IsSceneHex(const String& icao)
{
    if (icao.length() != 6 || !icao.startsWith("fb00")) return false;
    return strtol(icao.c_str() + 4, nullptr, 16) < kLevel;
}

// M3-1 and M3-2: the present's stamp, at the push's DMA completion (B) or after the loop's FrameDone (A).
inline void Present(uint32_t nowUs)
{
    State& s = S();
    portENTER_CRITICAL(&Mux());
    s.p2pUs = s.havePresent ? nowUs - s.lastPresentUs : 0;
    s.lastPresentUs = nowUs;
    s.havePresent = true;
    s.presents++;
    portEXIT_CRITICAL(&Mux());
}

// M3-3 (b): the push's own times (B), summed over the measured span.
inline void PushTimes(uint32_t syncUs, uint32_t pushUs)
{
    State& s = S();
    portENTER_CRITICAL(&Mux());
    if (s.measuring) {
        s.pushN++;
        s.pushUs += pushUs;
        s.syncUs += syncUs;
        const uint32_t bin = pushUs / 1000;
        uint16_t& h = s.pushHist[bin < 199 ? bin : 199];
        if (h < 0xFFFF) h++;
    }
    portEXIT_CRITICAL(&Mux());
}

// THE PACKAGE's M3-2 (B): the loop tags the buffer it hands over with the scene measuring now, and counts it.
// A frame belongs to a scene's span if and only if it was handed over while that scene measured; its present
// counts whenever it lands (TagPresented), so the span's boundary is carried by the tag, not by a window.
inline void TagHandOver(uint8_t i)
{
    State& s = S();
    portENTER_CRITICAL(&Mux());
    const int8_t t = (s.scene >= 0 && !s.done && s.measuring) ? (int8_t)s.scene : (int8_t)-1;
    s.tag[i & 1] = t;
    if (t >= 0) s.handed[t]++;
    portEXIT_CRITICAL(&Mux());
}
// ...and the push task counts a tagged frame presented, right after Present().
inline void TagPresented(uint8_t i)
{
    State& s = S();
    portENTER_CRITICAL(&Mux());
    const int8_t t = s.tag[i & 1];
    if (t >= 0) s.presented[t]++;
    portEXIT_CRITICAL(&Mux());
}
// THE DROPPED-UPDATE PLANT (the owner's addition): true exactly once, for the kDropAt-th tagged frame of a
// C1_PUSH60_DROP scene (only the proof image has one). The caller then skips the push and the present.
inline bool DropThis(uint8_t i)
{
    State& s = S();
    bool drop = false;
    portENTER_CRITICAL(&Mux());
    const int8_t t = s.tag[i & 1];
    if (t >= 0 && kScenes[t].kind == C1_PUSH60_DROP && ++s.dropSeen[t] == kDropAt) {
        s.dropped[t]++;
        drop = true;
    }
    portEXIT_CRITICAL(&Mux());
    return drop;
}

// M3-3's plants: the push task's delay (B; vTaskDelay at the call site) and the loop's +10 ms (I-6).
inline uint32_t PushDelayMs()
{
    const State& s = S();
    return (s.scene >= 0 && !s.done) ? kPushDelayMs[kScenes[s.scene].kind] : 0;
}
inline bool DelayOn()
{
    State& s = S();
    s.delayed = s.scene >= 0 && !s.done && kScenes[s.scene].kind == C1_LOOP10;
    return s.delayed;
}

// RecordFrameUs's region, every frame: one record while measuring.
inline void Record(uint32_t frameUs, size_t tracked, uint8_t stateFlags)
{
    State& s = S();
    uint32_t p2p, presents;
    portENTER_CRITICAL(&Mux());
    p2p = s.p2pUs;
    presents = s.presents;
    portEXIT_CRITICAL(&Mux());
    const uint32_t since = presents - s.seenPresents;
    s.seenPresents = presents;
    if (s.measuring && s.buf != nullptr) {
        if (s.n < kCap) {
            Rec& r = s.buf[s.n++];
            r.fr10 = Ten(frameUs);
            r.p2p10 = Ten(p2p);
            r.tracked = (uint8_t)(tracked > 255 ? 255 : tracked);
            r.drawn = (uint8_t)(s.drawn > 255 ? 255 : s.drawn);
            r.flags = (uint8_t)((s.rebuilt ? 1 : 0) | (s.delayed ? 2 : 0) | stateFlags);
            r.pres = (uint8_t)(since > 255 ? 255 : since);
        } else {
            s.overflowFrames++;
        }
    }
    s.drawn = 0;
    s.rebuilt = false;
    s.delayed = false;
}

inline uint32_t MeasureMs(int) { return MEASURE_MS; }
inline uint32_t SettleMs(int) { return SETTLE_MS; }

// FT's rebuild row: one record per map rebuild while a scene runs, labelled by its part. The steady window's rebuilds
// are counted (expected 0: an airports-fetch arrival is the named exception). The residue is the total minus the four
// laps, printed signed so a negative one is visible rather than wrapped.
inline void OnRebuild(const RebuildRec& r)
{
    State& s = S();
    if (s.scene < 0 || s.done) return;
    static const char* const kPart[] = {"settle", "steady", "forced"};
    const uint8_t part = s.ftPart < 3 ? s.ftPart : 0;
    if (part == 1) s.ftSteadyRebuilds++;
    const unsigned idx = part == 2 ? s.ftForced++ : 0;
    const long residue = (long)r.totalUs - (long)(r.load + r.build + r.names + r.compose);
    Serial.printf("[frame-bench] rebuild S%d %s i=%u plant=%d total=%lu load=%lu loaded=%d build=%lu names=%lu "
                  "compose=%lu composed=%d residue=%ld ms=%lu ready=%d bi=%lu/%lu/%lu/%lu/%lu/%lu "
                  "counts=%d/%d/%d/%d/%d/%d/%d/%d placed=%d mhz=%lu\n",
                  s.scene, kPart[part], idx, r.plant ? 1 : 0, (unsigned long)r.totalUs, (unsigned long)r.load,
                  r.loaded ? 1 : 0, (unsigned long)r.build, (unsigned long)r.names, (unsigned long)r.compose,
                  r.composed ? 1 : 0, residue, (unsigned long)r.ms, r.ready ? 1 : 0, (unsigned long)r.usS1,
                  (unsigned long)r.usWater, (unsigned long)r.usRivers, (unsigned long)r.usRoads,
                  (unsigned long)r.usColour, (unsigned long)r.usTotal, r.rings, r.rivers, r.primary, r.secondary,
                  r.primaryInView, r.secondaryInView, r.secondaryDrawn ? 1 : 0, r.level, r.placed,
                  (unsigned long)r.cpuMhz);
}

inline void PrepareScene(int)
{
    State& s = S();
    s.fbSubmits = 0;
    s.nNoted = 0;
}

// TBL-DEV, once at boot: the device's libm against fixed_traffic.py at kTblT.
inline void PrintTable()
{
    Serial.printf("[frame-bench] table h=bend n=%d t=%lu sum=%08lx\n", kLevel, (unsigned long)kTblT,
                  (unsigned long)fbtable::Sum(atof(kHomeLat), atof(kHomeLon), kLevel, (double)kTblT));
}

// THE (a) HOOK (the cloud /blips region, on the fetch task, after the REAL GET's ParseBlips): the parsed content is
// replaced by the table at the device's UTC second, parsed by the same ParseBlips, swapped in only if it parses.
template <typename Rows, typename Parse>
inline void OnCloudPoll(JsonDocument& doc, Rows& rows, long& dataEpoch, Parse parse)
{
    if (!InScene()) return;
    const uint32_t t0 = micros();
    const long now = (long)time(nullptr);
    const State& st = S();   // FT: the table around the scene's own home (its package's centre); C1: Bend
    const double lat0 = st.ftHome ? st.ftLat : atof(kHomeLat), lon0 = st.ftHome ? st.ftLon : atof(kHomeLon);
    doc.clear();
    doc["v"] = 1;
    doc["t"] = now;
    JsonArray a = doc["a"].to<JsonArray>();
    uint32_t crc = 0;
    char line[96];
    for (int i = 0; i < kLevel; ++i) {
        const fbtable::Row r = fbtable::At(lat0, lon0, i, (double)now);
        JsonArray e = a.add<JsonArray>();
        e.add(static_cast<const char*>(r.hex));
        e.add(static_cast<const char*>(r.flight));
        e.add(fbtable::Q4(r.lat));
        e.add(fbtable::Q4(r.lon));
        e.add(r.altFt);
        e.add(lround(r.gs));
        e.add(lround(r.track));
        e.add(0);                      // vertical rate
        e.add(fbtable::kCategoryA3);   // what the local path makes of "A3"
        e.add(0);                      // ageS
        crc = fbtable::Crc32(crc, line, (size_t)fbtable::Line(r, line, sizeof line));
    }
    Rows tmp;
    long e2 = 0;
    const bool ok = parse(doc, tmp, e2);
    if (ok) {
        rows.swap(tmp);
        dataEpoch = e2;
    }
    const uint32_t us = micros() - t0;
    if (ok)
        Serial.printf("[frame-bench] table served n %d t %ld crc %08lx us %lu\n", kLevel, now, (unsigned long)crc,
                      (unsigned long)us);
    else
        Serial.println("[frame-bench] table served FAILED");
}

// THE WEATHER FIELD (ConsumeWeather's region, before `whole` is decided): a whole 200's body is replaced by the fixed
// field; its header (the real frame time and geometry) is kept. Returns the new length (the caller sets `claimed` to
// it). Outside a scene, and on a header the decoder refuses, the bytes are left as they came.
inline size_t WxField(uint8_t* data, size_t len)
{
    State& s = S();
    if (!InScene()) return len;
    wxcrop::Header h;
    if (wxcrop::ParseHeader(data, len, &h) != wxcrop::Status::Ok) {
        Serial.println("[frame-bench] wx served SKIP: the header does not parse");
        return len;
    }
    if (!s.wxHave) {
        s.wxHave = true;
        s.wxRows = h.rows;
        s.wxCols = h.cols;
        s.wxCoarsen = h.coarsen;
    }
    const double lat0 = atof(kHomeLat), lon0 = atof(kHomeLon);
    const double rad = kCellBearingDeg * fbtable::kPi / 180.0;
    const double dn = kCellMi * fbtable::kMiKm * cos(rad), de = kCellMi * fbtable::kMiKm * sin(rad);
    const double cLat = lat0 + dn / 111.0, cLon = lon0 + de / (111.0 * cos(lat0 * fbtable::kPi / 180.0));
    // crop.ts:47: cell r spans lat(r) +- dj/2 with lat(r) = latNw - r*dj; cell c spans lon(c) +- di/2, lon(c) = lonNw + c*di
    const long row = lround((h.latNwMicro / 1e6 - cLat) / (h.cellLatMicro / 1e6));
    const long col = lround((cLon - h.lonNwMicro / 1e6) / (h.cellLonMicro / 1e6));
    if (row < 0 || row >= (long)h.rows || col < 0 || col >= (long)h.cols) {
        Serial.println("[frame-bench] wx served SKIP: the cell is outside the crop");
        return len;
    }
    const uint32_t total = h.Cells(), idx = (uint32_t)row * h.cols + (uint32_t)col;
    size_t o = wxcrop::kHeaderBytes;
    auto run = [&](uint8_t code, uint32_t n) {
        if (n == 0) return;
        if (n <= 15) {
            data[o++] = (uint8_t)((code << 4) | n);
            return;
        }
        data[o++] = (uint8_t)(code << 4);   // a long run: the length follows as LEB128
        for (;;) {
            const uint8_t b = n & 0x7f;
            n >>= 7;
            if (n) {
                data[o++] = b | 0x80;
            } else {
                data[o++] = b;
                break;
            }
        }
    };
    run(0, idx);
    run(kCellCode, 1);
    run(0, total - idx - 1);
    // the CRC covers everything but the 4 frame-time bytes (8..11)
    uint32_t crc = fbtable::Crc32(0, (const char*)data, 8);
    crc = fbtable::Crc32(crc, (const char*)data + 12, o - 12);
    Serial.printf("[frame-bench] wx served rows %u cols %u coarsen %u crc %08lx bytes %u\n", (unsigned)h.rows,
                  (unsigned)h.cols, (unsigned)h.coarsen, (unsigned long)crc, (unsigned)o);
    return o;
}

inline bool Allocate()
{
    State& s = S();
    if (s.buf) return true;
    s.buf = (Rec*)heap_caps_malloc(sizeof(Rec) * kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s.dumpBuf = (Rec*)heap_caps_malloc(sizeof(Rec) * kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s.buf || !s.dumpBuf) {
        Serial.println("[frame-bench] REFUSED: the record buffers did not allocate");
        return false;
    }
    return true;
}

// I-5: the instrument's own per-frame cost, timed on the device (Record into a scratch span, 10,000 calls).
inline void SelfTime()
{
    State& s = S();
    const int keepN = s.n;
    const bool keepM = s.measuring;
    s.measuring = true;
    s.n = 0;
    const uint32_t t0 = micros();
    for (int i = 0; i < 10000; ++i) {
        if (s.n >= kCap) s.n = 0;
        Record(50000, 40, 0);
    }
    const uint32_t t1 = micros();
    s.n = keepN;
    s.measuring = keepM;
    Serial.printf("[frame-bench] record cost %lu ns/frame\n", (unsigned long)((t1 - t0) / 10));
}

inline void EndMeasure()
{
    State& s = S();
    Rec* t = s.dumpBuf;
    s.dumpBuf = s.buf;
    s.buf = t;
    s.dumpN = s.n;
    s.dumpPos = 0;
    s.dumpScene = s.scene;
    Serial.printf("[frame-bench] scene S%d end frames=%d over-cap=%d\n", s.scene, s.n, s.overflowFrames);
    uint32_t pn;
    uint64_t pu, su;
    static uint16_t hist[200];
    portENTER_CRITICAL(&Mux());
    pn = s.pushN;
    pu = s.pushUs;
    su = s.syncUs;
    memcpy(hist, s.pushHist, sizeof hist);
    s.pushN = 0;
    s.pushUs = s.syncUs = 0;
    memset(s.pushHist, 0, sizeof s.pushHist);
    portEXIT_CRITICAL(&Mux());
    Serial.printf("[frame-bench] push S%d: n %lu avg_us %lu sync_avg_us %lu; fb submits %lu\n", s.scene,
                  (unsigned long)pn, (unsigned long)(pn ? pu / pn : 0), (unsigned long)(pn ? su / pn : 0),
                  (unsigned long)s.fbSubmits);
    char line[220];
    int o = snprintf(line, sizeof line, "[frame-bench] push-hist S%d:", s.scene);
    for (int k = 0; k < 200; ++k) {
        if (!hist[k]) continue;
        if (o > (int)sizeof line - 16) {   // flush, then carry on in the next line
            Serial.println(line);
            o = snprintf(line, sizeof line, "[frame-bench] push-hist S%d:", s.scene);
        }
        o += snprintf(line + o, sizeof line - o, " %d:%u", k, (unsigned)hist[k]);
    }
    Serial.println(line);
    s.n = 0;
    s.overflowFrames = 0;
    s.measuring = false;
}

inline void PumpDump()
{
    State& s = S();
    if (s.dumpScene < 0) return;
    static const char* hexd = "0123456789abcdef";
    for (int line = 0; line < kDumpLinesPerFrame && s.dumpPos < s.dumpN; ++line) {
        char out[16 + kDumpPerLine * 16 + 4];
        int o = 0;
        const int end = std::min(s.dumpPos + kDumpPerLine, s.dumpN);
        for (int i = s.dumpPos; i < end; ++i) {
            const uint8_t* b = (const uint8_t*)&s.dumpBuf[i];
            for (int j = 0; j < 8; ++j) {
                out[o++] = hexd[b[j] >> 4];
                out[o++] = hexd[b[j] & 15];
            }
        }
        out[o] = 0;
        Serial.printf("[frame-bench] dump S%d %d %s\n", s.dumpScene, s.dumpPos, out);
        s.dumpPos = end;
    }
    if (s.dumpPos >= s.dumpN) {
        Serial.printf("[frame-bench] dump S%d done %d\n", s.dumpScene, s.dumpN);
#if defined(BLIPSCOPE_PUSH_TASK)
        // THE PACKAGE's M3-2: printed after the dump, seconds after the span, when every in-flight frame has landed
        uint32_t h, p, d;
        portENTER_CRITICAL(&Mux());
        h = s.handed[s.dumpScene];
        p = s.presented[s.dumpScene];
        d = s.dropped[s.dumpScene];
        portEXIT_CRITICAL(&Mux());
        Serial.printf("[frame-bench] presents S%d: handed %lu presented %lu dropped %lu\n", s.dumpScene,
                      (unsigned long)h, (unsigned long)p, (unsigned long)d);
#endif
        s.dumpScene = -1;
    }
}

}  // namespace framebench

#endif  // FRAME_BENCH
