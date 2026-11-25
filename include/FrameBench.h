// THE FRAME BENCH, C1 (docs/frame-time-bench.md, "C1's bench"; p4's M3-3, docs/m3-push-task.md): a bench-only
// instrument on the M3 tree, built only under -DFRAME_BENCH (envs blipscope-s3-128-framebench-c1a / -c1b, slug-less,
// never published). The whole instrument is this header, FrameBenchTable.h and the `// THE FRAME BENCH` regions in
// main.cpp, AircraftManager.cpp and ConfigurationWebServer.cpp. Nothing here writes NVS, and nothing prints a
// coordinate, a name or an address.
//
// THE PORT (PORT-SPEC b8c558fe87b9, bench/fp-port; never merged): C1's bench and the frame profile on main, serving
// the profile's lists 10 (1b-MAP) and 11 (W) ONLY. THE SCENE: the home is the embedded public fixture ksfo.bmpk's own
// centre at 2 dp and the view radius 50 mi, both in RAM from boot (amendments 1, 2b, 2c item 3; nothing reads or writes
// NVS for them, and nothing prints them); 40 aircraft from the (a) table (fixed_traffic.py's formulas, served into the
// cloud /blips path after the real GET's parse); weather ON over a fixed one-cell field (the real /wx response's
// header, its body replaced); the map NOT forced (amendment 2c item 5), its package the same fixture, loaded once by a
// direct set and never fetched (amendment 2d).
//
// PER FRAME (Rec): fr and p2p in 10 us units; tracked and drawn; flags: bit 0 the weather layer rebuilt on this frame,
// bit 1 the loop's planted delay, bit 2 the weather crop shown (wxShowing == Crop), bit 3 the map enabled, bit 4 the
// night clock's night; and `pres`, the presents since the previous record (M3-2: 1 in lockstep).
//
// C1-L (docs/frame-time-bench.md, "C1-L", for p4's A0a): each scene carries its own level (the table's n, 1..kTableMax)
// and its weather answer, both defaulting to C1's (40, on), so C1's lists below are unchanged. A FRAME_PROFILE build
// takes its list from p4's FrameProfileScenes.h instead (p4 owns that file; its scenes use Kind C1).
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
#include "BasemapPackage.h"     // the port: bmpk::Validate reads the fixture's own centre (the home)

// THE PORT builds the profile's lists 10 and 11 (PORT-SPEC b8c558fe87b9), and bench/fp-port-wsep list 12 (REG-W-sep).
#if !defined(FRAME_PROFILE) || !defined(FRAME_PROFILE_GAINS) || \
    (FRAME_PROFILE_GAINS != 10 && FRAME_PROFILE_GAINS != 11 && FRAME_PROFILE_GAINS != 12)
#error "bench/fp-port builds the frame profile's lists 10, 11 and 12 only (PORT-SPEC b8c558fe87b9; REG-W-sep)"
#endif

namespace framebench {

// The port's kinds (PORT-SPEC amendment 1 B): SWEEP_OLD blends the wedge with the per-pixel reference in BOTH sweep
// paths (wxsweep::Blend / mapsweep::Blend(pct, ..)), SWEEP_LUT is main's shared table; OUTLINE_8COPY is today's 8-copy
// loop, OUTLINE_W main's W path, OUTLINE_WCHECK draws both and counts the pixels that differ (untimed; W only).
// REG-W-sep (list 12): OUTLINE_WA / WB / WI call W's three separate copies (src/WsepCopy.inc), Wa and Wb in flash at two
// addresses and Wi whole in IRAM; then, as on OUTLINE_8COPY, the shipping else-branch ends the span and draws the marker.
enum Kind : uint8_t { C1 = 0, C1_PUSH10 = 1, C1_PUSH60 = 2, C1_LOOP10 = 3, SWEEP_OLD = 4, SWEEP_LUT = 5,
                      OUTLINE_8COPY = 6, OUTLINE_W = 7, OUTLINE_WCHECK = 8, OUTLINE_WA = 9, OUTLINE_WB = 10,
                      OUTLINE_WI = 11, kKinds };
constexpr int kLevel = 40;      // C1's level: a scene's default, and Level() outside a scene
constexpr int kTableMax = 60;   // the rows TBL-HOST verifies (tbl_host_lv.py: i < 60); Allocate() refuses a level above
struct Scene {
    Kind kind;
    uint8_t level = kLevel;   // the (a) table's n in this scene
    bool wx = true;           // this scene's "wx-layer" answer (Override)
};
#if defined(FRAME_PROFILE)
#include "FrameProfileScenes.h"   // p4's (docs/frame-profile.md): defines kScenes; one owner per line
#else
#if defined(BLIPSCOPE_PUSH_TASK)
static const Scene kScenes[] = {{C1}, {C1_PUSH10}, {C1_PUSH60}, {C1_LOOP10}};   // image B
#else
static const Scene kScenes[] = {{C1}};   // image A: A1, and A2 after B
#endif
#endif
constexpr int kNumScenes = sizeof(kScenes) / sizeof(kScenes[0]);
static const char* const kKindName[] = {"C1",        "C1+push10",     "C1+push60", "C1+loop10",    "sweep-old",
                                        "sweep-lut", "outline-8copy", "outline-w", "outline-wcheck",
                                        "outline-wa", "outline-wb",    "outline-wi"};
static_assert(sizeof(kKindName) / sizeof(kKindName[0]) == kKinds, "kKindName needs one entry per Kind");
constexpr uint32_t kPushDelayMs[] = {0, 10, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static_assert(sizeof(kPushDelayMs) / sizeof(kPushDelayMs[0]) == kKinds, "kPushDelayMs needs one entry per Kind");

// THE HOME (PORT-SPEC amendments 1 and 2b): the embedded public fixture ksfo.bmpk's own centre (its header's
// lat0Micro / lon0Micro), rounded to 2 dp, in RAM only. No number is written here: it is read from the package. Only
// ever returned to Initialise() and to the table; nothing prints it. kHomeName is what the scene lines print.
extern const uint8_t kFpKsfo[] asm("_binary_test_fixtures_wx_basemap_ksfo_bmpk_start");
extern const uint8_t kFpKsfoEnd[] asm("_binary_test_fixtures_wx_basemap_ksfo_bmpk_end");
static const char* const kHomeName = "KSFO";
constexpr int kRungMi = 50;   // the bench's view radius, 50 mi (CS6's own; amendment 2)
struct Home {
    bool ok = false;
    char lat[16] = {};
    char lon[16] = {};
};
inline const Home& H()
{
    static Home h;
    static bool tried = false;
    if (!tried) {
        tried = true;
        bmpk::Package pk;
        if (bmpk::Validate(kFpKsfo, (size_t)(kFpKsfoEnd - kFpKsfo), &pk) == bmpk::Status::kOk) {
            snprintf(h.lat, sizeof h.lat, "%.2f", (double)lround(pk.lat0Micro / 1e4) / 100.0);
            snprintf(h.lon, sizeof h.lon, "%.2f", (double)lround(pk.lon0Micro / 1e4) / 100.0);
            h.ok = true;
            Serial.println("[frame-bench] map home and radius set in RAM before the first query");
        } else {
            Serial.println("[frame-bench] home REFUSED: the package's header does not validate");
        }
    }
    return h;
}
constexpr uint32_t kTblT = 1794441386;   // TBL-DEV's time (the pinned build epoch)

// THE ECHO CELL (the orchestrator's acceptance of the recorded choices): 15 mi from the home at bearing 135 deg, the
// lowest drawn code, one crop cell; code 0 everywhere else.
constexpr double kCellMi = 15.0;
constexpr double kCellBearingDeg = 135.0;
constexpr uint8_t kCellCode = 2;

constexpr uint32_t START_AFTER_MS = 90000;
constexpr uint32_t SETTLE_MS = 100000;
constexpr uint32_t MEASURE_MS = 120000;
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
    // G-MAP: this frame's sweep took DrawSweepOverMap and drew (and with the weather); the measured span's counts; and
    // the last ended scene's, printed by the profile's end line's region.
    bool mapSwept = false, mapSweptWx = false;
    uint32_t mapFrames = 0, mapWxFrames = 0;
    uint32_t endMapFrames = 0, endMapWx = 0;
    // W-CHECK (list 11's S0): the scratch pair noised this frame; the scene's frames, contacts and mismatched pixels
    bool wcNoised = false;
    uint32_t wcFrames = 0, wcContacts = 0, wcMismatch = 0;
    // REG-W-sep (list 12's S0): the mismatched pixels of the copies Wa, Wb and Wi, over the same contacts as W0's
    uint32_t wsMismatch[3] = {0, 0, 0};
    bool noting = false;
    int nNoted = 0;
    Noted noted[kMaxKeys];
};
inline State& S()
{
    static State s;
    return s;
}
// G-MAP (PORT-SPEC amendment 1 B): DrawSweepOverMap's region notes that this frame's sweep took the map path and
// drew, and whether the composite carried the weather (withWx); Record counts it beside the frame's own record.
inline void NoteMapSweep(bool withWx)
{
    State& s = S();
    s.mapSwept = true;
    s.mapSweptWx = withWx;
}
inline portMUX_TYPE& Mux()
{
    static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
    return m;
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

// C1-L: the current scene's level (kLevel outside a scene), the list's largest, and whether every level is one
// TBL-HOST verified.
inline int Level() { return InScene() ? (int)kScenes[S().scene].level : kLevel; }
inline int MaxLevel()
{
    int m = 0;
    for (const Scene& sc : kScenes) m = std::max(m, (int)sc.level);
    return m;
}
inline bool LevelsOk()
{
    for (const Scene& sc : kScenes)
        if (sc.level < 1 || sc.level > kTableMax) return false;
    return true;
}

// The scene's keys, answered in RAM; nullptr means "the board's own setting" (recorded, never changed). Called only
// through ConfigurationWebServer::GetStoredString's region, which reads and never writes.
inline const char* Override(const char* key)
{
    // The port: the home and the view radius are the bench's FROM BOOT, so the first OnQuery's key is built from them
    // and never from NVS (amendment 2c item 3). The first answer computes the home and prints the one boot line.
    static char rung[8];
    snprintf(rung, sizeof rung, "%d", kRungMi);
    if (strcmp(key, "latitude") == 0) return H().lat;    // "" if the package's header is refused: no location at all
    if (strcmp(key, "longitude") == 0) return H().lon;
    if (strcmp(key, "radius") == 0) return rung;
    if (strcmp(key, "radius-unit") == 0) return "mi";
    if (!InScene()) return nullptr;
    if (strcmp(key, "data-source") == 0) return "";      // the cloud, on a cloud build
    if (strcmp(key, "local-url") == 0) return "";
    if (strcmp(key, "local-details") == 0) return "";
    if (strcmp(key, "wx-layer") == 0) return kScenes[S().scene].wx ? "true" : "false";   // the scene's (C1: on)
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
    Serial.printf("[frame-bench] keys S%d: %d read; map=not forced\n", k, s.nNoted);
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

// C1's MAP OFF, withdrawn on the port (PORT-SPEC amendment 2c item 5): the map is NOT forced either way, so
// MapEnabled()'s region never returns false and its own conditions decide (a cloud source, a URL, the location).
inline bool MapOff() { return false; }

// The port's kinds, read by AircraftManager's FRAME_BENCH regions.
inline bool SweepOld() { return InScene() && kScenes[S().scene].kind == SWEEP_OLD; }
inline bool OutlineOld() { return InScene() && kScenes[S().scene].kind == OUTLINE_8COPY; }
inline bool OutlineWCheck() { return InScene() && kScenes[S().scene].kind == OUTLINE_WCHECK; }
// REG-W-sep: the copy this scene's kind names, 0 (Wa), 1 (Wb) or 2 (Wi), the index into wsep::kCopy; -1 otherwise
static_assert(OUTLINE_WB == OUTLINE_WA + 1 && OUTLINE_WI == OUTLINE_WA + 2, "the copies' kinds index wsep::kCopy");
inline int OutlineWVariant()
{
    if (!InScene()) return -1;
    const Kind k = kScenes[S().scene].kind;
    return (k >= OUTLINE_WA && k <= OUTLINE_WI) ? (int)(k - OUTLINE_WA) : -1;
}

// The table's synthetic hexes (fb0000 up to the list's largest level) never leave the board: the enrichment scan's
// region skips them. The LARGEST level, never the current one: after a scene drops from 60 to 20, fb0014 .. fb003b stay
// tracked until they are evicted, and must still never be enriched.
inline bool IsSceneHex(const String& icao)
{
    if (icao.length() != 6 || !icao.startsWith("fb00")) return false;
    return strtol(icao.c_str() + 4, nullptr, 16) < MaxLevel();
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
            if (s.mapSwept) {   // G-MAP, counted only for a stored record, so map <= frames by construction
                s.mapFrames++;
                if (s.mapSweptWx) s.mapWxFrames++;
            }
        } else {
            s.overflowFrames++;
        }
    }
    s.drawn = 0;
    s.mapSwept = s.mapSweptWx = false;
    s.wcNoised = false;
    s.rebuilt = false;
    s.delayed = false;
}

inline uint32_t MeasureMs(int) { return MEASURE_MS; }
inline uint32_t SettleMs(int) { return SETTLE_MS; }

inline void PrepareScene(int)
{
    State& s = S();
    s.fbSubmits = 0;
    s.nNoted = 0;
}

// TBL-DEV, once at boot: the device's libm against fixed_traffic.py at kTblT, one line per distinct level in the
// order of first appearance (C1's lists: the one n = 40 line).
inline void PrintTable()
{
    for (int k = 0; k < kNumScenes; ++k) {
        const int n = kScenes[k].level;
        bool seen = false;
        for (int j = 0; j < k; ++j) seen = seen || kScenes[j].level == n;
        if (seen) continue;
        Serial.printf("[frame-bench] table h=ksfo n=%d t=%lu sum=%08lx\n", n, (unsigned long)kTblT,
                      (unsigned long)fbtable::Sum(atof(H().lat), atof(H().lon), n, (double)kTblT));
    }
}

// THE (a) HOOK (the cloud /blips region, on the fetch task, after the REAL GET's ParseBlips): the parsed content is
// replaced by the table at the device's UTC second, parsed by the same ParseBlips, swapped in only if it parses.
template <typename Rows, typename Parse>
inline void OnCloudPoll(JsonDocument& doc, Rows& rows, long& dataEpoch, Parse parse)
{
    if (!InScene()) return;
    const uint32_t t0 = micros();
    const long now = (long)time(nullptr);
    const double lat0 = atof(H().lat), lon0 = atof(H().lon);
    doc.clear();
    doc["v"] = 1;
    doc["t"] = now;
    JsonArray a = doc["a"].to<JsonArray>();
    uint32_t crc = 0;
    char line[96];
    const int n = Level();
    for (int i = 0; i < n; ++i) {
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
        Serial.printf("[frame-bench] table served n %d t %ld crc %08lx us %lu\n", n, now, (unsigned long)crc,
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
    const double lat0 = atof(H().lat), lon0 = atof(H().lon);
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
    if (!LevelsOk()) {   // C1-L: a row TBL-HOST never verified is never served; no scene starts
        static bool said = false;
        if (!said) Serial.printf("[frame-bench] REFUSED: a scene's level is outside 1..%d, the rows TBL-HOST verifies\n",
                                 kTableMax);
        said = true;
        return false;
    }
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
    if (kScenes[s.scene].kind == OUTLINE_WCHECK) {   // W-CHECK's one line, at its scene's end (REG-W amendment 1 item 3)
        Serial.printf("[speed-p5] outline W-CHECK frames %lu contacts %lu mismatched-px %lu\n", (unsigned long)s.wcFrames,
                      (unsigned long)s.wcContacts, (unsigned long)s.wcMismatch);
#if FRAME_PROFILE_GAINS == 12
        static const char* const kCopyName[3] = {"Wa", "Wb", "Wi"};   // REG-W-sep: one line per copy, K = 0 each
        for (int v = 0; v < 3; ++v) {
            Serial.printf("[wsep] W-CHECK %s frames %lu contacts %lu mismatched-px %lu\n", kCopyName[v],
                          (unsigned long)s.wcFrames, (unsigned long)s.wcContacts, (unsigned long)s.wsMismatch[v]);
            s.wsMismatch[v] = 0;
        }
#endif
        s.wcFrames = s.wcContacts = s.wcMismatch = 0;
    }
    s.endMapFrames = s.mapFrames;
    s.endMapWx = s.mapWxFrames;
    s.mapFrames = s.mapWxFrames = 0;
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
        s.dumpScene = -1;
    }
}

}  // namespace framebench

#endif  // FRAME_BENCH
