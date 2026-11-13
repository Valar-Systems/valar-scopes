// THE FRAME BENCH (docs/frame-time-bench.md on branch docs/frame-time-bench): a bench-only instrument, built only
// under -DFRAME_BENCH (env:blipscope-s3-128-framebench, slug-less, never published). The whole instrument is this
// header plus the `// THE FRAME BENCH` regions in main.cpp, AircraftManager.cpp and ConfigurationWebServer.cpp.
// Nothing here writes NVS, and nothing prints a coordinate, a name or an address.
//
// WHAT IT RECORDS, per presented frame, while a scene is measuring:
//   fr   RecordFrameUs's own value (main.cpp: loop start .. after FrameDone), in 10 us units
//   p2p  present to present: micros() just after FrameDone() minus the previous frame's, in 10 us units
//   tracked, drawn   trackedAircraft.size(), and the aircraft DrawRadar actually drew this frame
//   flags  bit 0: the map rebuilt on this frame (v20; v19 has no map, so it is never set)
// 0xFFFF in fr/p2p marks a value at or over 655.35 ms (counted, and graded as an overflow).
//
// THE RUN: 23 scenes, each SETTLE_MS then MEASURE_MS, driven from RecordFrameUs's region on the loop task. A scene is
// applied through the radar's own config path: Override() answers the keys Initialise() reads, and Initialise() runs.
// The zoom is the CONFIGURED radius, so zoomIdx == zoomLadder.Top() and the 10-min idle return cannot fire.
#pragma once
#ifdef FRAME_BENCH

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>

#ifndef FRAME_BENCH_HOST
#error "FRAME_BENCH needs FRAME_BENCH_HOST: set the untracked env var; scripts/framebench_host.py passes it in"
#endif

namespace framebench {

enum Kind : uint8_t { CTRL = 0, CTRL_DELAY = 1, FIXED = 2, LIVE = 3, LIVE_OWN = 4, SURVEY = 5, REC = 6, REPLAY = 7,
                      CTRL_IL = 8, LIVE_APT = 9 };
struct Scene {
    Kind kind;
    uint8_t home;    // 0 Bend, 1 KSFO; for SURVEY and REC, an index into kAptCode
    uint8_t level;   // the fixed traffic level (0 for LIVE)
    uint8_t rung;    // the configured radius, statute miles
};

// THE BAR'S RUNGS (the owner, 2026-11-12 23:31Z): the six scenes are the two homes at these radii. ONE constant: the
// replay's scene table and its reader derive from it (FB-R6). 10 mi is reported beside it, never in the worst-of-six.
static const uint8_t kRungs[] = {25, 50, 100};
constexpr uint8_t kReportedMi = 10;
constexpr uint8_t kShippingRadiusMi = 100;   // an unset "radius" reads 100, an unset unit "mi": /blips r=161, limit 40

// THE AIRPORTS (the recording plan, RP2 and Q-RC3), copied from include/Airports.h at v19 at 2 dp (FB-R5 holds the
// copy to it). Index 0 is KSFO. Only ever returned to Initialise() and compared to a request; never printed.
static const char* const kAptCode[] = {"SFO", "ATL", "ORD", "DFW", "DEN", "LAX", "JFK", "LAS", "CLT"};
static const char* const kAptLat[] = {"37.62", "33.64", "41.98", "32.90", "39.86", "33.94", "40.64", "36.08", "35.21"};
static const char* const kAptLon[] = {"-122.38", "-84.43", "-87.90", "-97.04", "-104.67", "-118.41", "-73.78",
                                      "-115.15", "-80.94"};
constexpr int kNumApt = sizeof(kAptCode) / sizeof(kAptCode[0]);
static_assert(kNumApt == 9, "KSFO and Q-RC3's eight candidates");

#if defined(FRAME_BENCH_RUN_SURVEY)
// RP2's survey: one scene per airport, the shipping query at its home.
static const Scene kScenes[] = {
    {SURVEY, 0, 0, kShippingRadiusMi}, {SURVEY, 1, 0, kShippingRadiusMi}, {SURVEY, 2, 0, kShippingRadiusMi},
    {SURVEY, 3, 0, kShippingRadiusMi}, {SURVEY, 4, 0, kShippingRadiusMi}, {SURVEY, 5, 0, kShippingRadiusMi},
    {SURVEY, 6, 0, kShippingRadiusMi}, {SURVEY, 7, 0, kShippingRadiusMi}, {SURVEY, 8, 0, kShippingRadiusMi},
};
static_assert(sizeof(kScenes) / sizeof(kScenes[0]) == kNumApt, "one survey scene per airport");
#elif defined(FRAME_BENCH_RUN_REC)
// RP1's captures: KSFO, then the survey's choice (FRAME_BENCH_REC_B, one pre-built image per candidate).
#if !defined(FRAME_BENCH_REC_B) || FRAME_BENCH_REC_B < 1 || FRAME_BENCH_REC_B > 8
#error "FRAME_BENCH_RUN_REC needs FRAME_BENCH_REC_B in 1..8 (the survey's choice)"
#endif
static const Scene kScenes[] = {{REC, 0, 0, kShippingRadiusMi}, {REC, FRAME_BENCH_REC_B, 0, kShippingRadiusMi}};
#elif defined(FRAME_BENCH_RUN_REPLAY)
// RP5, the v19 run on the recordings: the interleaved control, THE SIX (KSFO and the choice at kRungs), the 10 mi
// reported rows, the packed stress rows (Bend, 40 and 60, at kRungs), LIVE at both airports, LIVE-OWN.
#if !defined(FRAME_BENCH_REC_B) || FRAME_BENCH_REC_B < 1 || FRAME_BENCH_REC_B > 8
#error "FRAME_BENCH_RUN_REPLAY needs FRAME_BENCH_REC_B in 1..8 (the survey's choice)"
#endif
#include "framebench_rec_gen.h"   // GENERATED from the two pinned recordings (scripts/framebench_rec.py), untracked
static_assert(sizeof(kRungs) / sizeof(kRungs[0]) == 3, "the six: two homes x three rungs (the owner, 23:31Z)");
static const Scene kScenes[] = {
    {CTRL_IL, 0, 0, kRungs[1]},
    {REPLAY, 0, 0, kRungs[0]}, {REPLAY, 0, 0, kRungs[1]}, {REPLAY, 0, 0, kRungs[2]},
    {REPLAY, FRAME_BENCH_REC_B, 0, kRungs[0]}, {REPLAY, FRAME_BENCH_REC_B, 0, kRungs[1]},
    {REPLAY, FRAME_BENCH_REC_B, 0, kRungs[2]},
    {REPLAY, 0, 0, kReportedMi}, {REPLAY, FRAME_BENCH_REC_B, 0, kReportedMi},
    {FIXED, 0, 40, kRungs[0]}, {FIXED, 0, 40, kRungs[1]}, {FIXED, 0, 40, kRungs[2]},
    {FIXED, 0, 60, kRungs[0]}, {FIXED, 0, 60, kRungs[1]}, {FIXED, 0, 60, kRungs[2]},
    {LIVE_APT, 0, 0, kShippingRadiusMi}, {LIVE_APT, FRAME_BENCH_REC_B, 0, kShippingRadiusMi},
    {LIVE_OWN, 0, 0, 0},
};
#else
// docs/frame-time-bench.md, section 4 and A1: CTRL (plain, then +10 ms), each home's levels 20/40/60 through rungs
// 10/25/50, then LIVE at each home at 25 mi.
static const Scene kScenes[] = {
    {CTRL, 0, 40, 25}, {CTRL_DELAY, 0, 40, 25},
    {FIXED, 0, 20, 10}, {FIXED, 0, 20, 25}, {FIXED, 0, 20, 50},
    {FIXED, 0, 40, 10}, {FIXED, 0, 40, 25}, {FIXED, 0, 40, 50},
    {FIXED, 0, 60, 10}, {FIXED, 0, 60, 25}, {FIXED, 0, 60, 50},
    {FIXED, 1, 20, 10}, {FIXED, 1, 20, 25}, {FIXED, 1, 20, 50},
    {FIXED, 1, 40, 10}, {FIXED, 1, 40, 25}, {FIXED, 1, 40, 50},
    {FIXED, 1, 60, 10}, {FIXED, 1, 60, 25}, {FIXED, 1, 60, 50},
    {LIVE, 0, 0, 25}, {LIVE, 1, 0, 25},
    {LIVE_OWN, 0, 0, 0},   // (iv): the board's own settings, for the release cross-check
};
static_assert(sizeof(kScenes) / sizeof(kScenes[0]) == 23, "the registered run is 23 scenes");
#endif
constexpr int kNumScenes = sizeof(kScenes) / sizeof(kScenes[0]);
static const char* const kKindName[] = {"CTRL", "CTRL+10ms", "FIXED", "LIVE", "LIVE-OWN", "SURVEY", "REC",
                                        "REPLAY", "CTRL-IL", "LIVE-APT"};
static const char* const kHomeName[] = {"Bend", "KSFO"};
// The homes, to 2 dp. They are only ever returned to Initialise(); nothing prints them.
static const char* const kHomeLat[] = {"44.06", "37.62"};
static const char* const kHomeLon[] = {"-121.31", "-122.38"};

constexpr uint32_t START_AFTER_MS = 90000;    // the first scene starts 90 s after boot (Wi-Fi, clock, first fetch)
constexpr uint32_t SETTLE_MS = 100000;        // A1: the trail's 90 s age plus a margin
constexpr uint32_t MEASURE_MS = 120000;
constexpr uint32_t MEASURE_OWN_MS = 300000;   // LIVE-OWN: about 10 [health] windows for (iv)
constexpr uint32_t PLANTED_DELAY_US = 10000;  // CTRL+10ms (I-6)
constexpr uint32_t SURVEY_SETTLE_MS = 20000;  // RP2: the home applied, the first poll at it
constexpr uint32_t SURVEY_MS = 180000;        // RP2: at least 10 polls at the idle cadence
constexpr uint32_t REC_SETTLE_MS = 20000;
constexpr uint32_t REC_MS = 300000;           // RP1: 280 s needed, 300 s recorded
constexpr int kCap = 4096;                    // frames per measured span
constexpr int kDumpPerLine = 32;              // records per dump line
constexpr int kDumpLinesPerFrame = 2;         // the dump is paced across the next settle

struct Rec {
    uint16_t fr10;
    uint16_t p2p10;
    uint8_t tracked;
    uint8_t drawn;
    uint8_t flags;
    uint8_t pad;
};
static_assert(sizeof(Rec) == 8, "8 bytes a frame");

struct State {
    Rec* buf = nullptr;        // the measuring scene
    Rec* dumpBuf = nullptr;    // the scene being dumped
    int n = 0;
    int overflowFrames = 0;    // frames past kCap in a measured span
    int dumpN = 0, dumpPos = 0, dumpScene = -1;
    int scene = -1;
    bool measuring = false, done = false;
    uint32_t phaseStartMs = 0;
    uint32_t lastPresentUs = 0, p2pUs = 0;
    bool havePresent = false;
    uint16_t drawn = 0;        // DrawRadar's region counts this frame's drawn aircraft
    bool rebuilt = false;      // v20's map region sets it on a rebuild frame
    char url[112] = {0};
    char radius[8] = {0};
    uint32_t sceneStartMs = 0; // the scene's settle start (SURVEY, REC and REPLAY time their polls from it)
    bool delayed = false;      // this frame carried the planted delay (CTRL+10ms, and CTRL-IL's on blocks)
    int polls = 0;             // SURVEY and REC: polls seen in this scene's measured span
};
inline State& S()
{
    static State s;
    return s;
}

inline uint16_t Ten(uint32_t us)
{
    const uint32_t t = us / 10;
    return t >= 0xFFFF ? 0xFFFF : (uint16_t)t;
}

// The keys Initialise() reads, answered for the current scene; nullptr means "the board's own setting". Only ever
// called through ConfigurationWebServer::GetStoredString's region, which reads and never writes.
inline const char* Override(const char* key)
{
    const State& s = S();
    if (s.scene < 0 || s.done) return nullptr;
    const Scene& sc = kScenes[s.scene];
    if (sc.kind == LIVE_OWN) return nullptr;   // (iv): every key is the board's own
    if (sc.kind == SURVEY || sc.kind == REC || sc.kind == REPLAY || sc.kind == CTRL_IL || sc.kind == LIVE_APT) {
        // the airport home, the cloud source; the radius is the scene's rung (100 mi, the shipping default, for
        // SURVEY, REC and LIVE-APT)
        if (strcmp(key, "latitude") == 0) return kAptLat[sc.home];
        if (strcmp(key, "longitude") == 0) return kAptLon[sc.home];
        if (strcmp(key, "radius") == 0) return s.radius;
        if (strcmp(key, "radius-unit") == 0) return "mi";
        if (strcmp(key, "data-source") == 0) return "";
        if (strcmp(key, "local-url") == 0) return "";
        if (strcmp(key, "local-details") == 0) return "";
        return nullptr;
    }
    if (strcmp(key, "latitude") == 0) return kHomeLat[sc.home];
    if (strcmp(key, "longitude") == 0) return kHomeLon[sc.home];
    if (strcmp(key, "radius") == 0) return s.radius;
    if (strcmp(key, "radius-unit") == 0) return "mi";
    if (strcmp(key, "data-source") == 0) return sc.kind == LIVE ? "" : "local";   // "" is the cloud on cloud builds
    if (strcmp(key, "local-url") == 0) return sc.kind == LIVE ? "" : s.url;
    if (strcmp(key, "local-details") == 0) return "";                           // Off: no detail lookups
    return nullptr;
}

// main.cpp, just after FrameDone(): the present timestamp, then CTRL+10ms's planted delay inside the frame span.
inline void Present(uint32_t nowUs)
{
    State& s = S();
    s.p2pUs = s.havePresent ? nowUs - s.lastPresentUs : 0;
    s.lastPresentUs = nowUs;
    s.havePresent = true;
}
inline bool DelayOn()
{
    State& s = S();
    bool on = s.scene >= 0 && !s.done && kScenes[s.scene].kind == CTRL_DELAY;
    // CTRL-IL: the plant on in alternate 10 s blocks of the MEASURED span, off in the others (I-6's (3): one scene)
    if (s.scene >= 0 && !s.done && kScenes[s.scene].kind == CTRL_IL && s.measuring)
        on = ((millis() - s.phaseStartMs) / 10000u) % 2u == 1u;
    s.delayed = on;
    return on;
}

// RecordFrameUs's region, every frame: one record while measuring, then this frame's counters reset.
inline void Record(uint32_t frameUs, size_t tracked)
{
    State& s = S();
    if (s.measuring && s.buf != nullptr) {
        if (s.n < kCap) {
            Rec& r = s.buf[s.n++];
            r.fr10 = Ten(frameUs);
            r.p2p10 = Ten(s.p2pUs);
            r.tracked = (uint8_t)(tracked > 255 ? 255 : tracked);
            r.drawn = (uint8_t)(s.drawn > 255 ? 255 : s.drawn);
            r.flags = (uint8_t)((s.rebuilt ? 1 : 0) | (s.delayed ? 2 : 0));
            r.pad = 0;
        } else {
            s.overflowFrames++;
        }
    }
    s.drawn = 0;
    s.rebuilt = false;
    s.delayed = false;
}

inline uint32_t MeasureMs(int k)
{
    switch (kScenes[k].kind) {
    case LIVE_OWN: return MEASURE_OWN_MS;
    case SURVEY: return SURVEY_MS;
    case REC: return REC_MS;
    default: return MEASURE_MS;
    }
}
inline uint32_t SettleMs(int k)
{
    switch (kScenes[k].kind) {
    case SURVEY: return SURVEY_SETTLE_MS;
    case REC: return REC_SETTLE_MS;
    default: return SETTLE_MS;
    }
}
inline const char* HomeName(int k)
{
    const Scene& sc = kScenes[k];
    return (sc.kind == SURVEY || sc.kind == REC || sc.kind == REPLAY || sc.kind == CTRL_IL || sc.kind == LIVE_APT)
               ? kAptCode[sc.home] : kHomeName[sc.home];
}

// The URL the bench host serves this scene's traffic on; the server follows the board by ?s=<scene>.
inline void PrepareScene(int k)
{
    State& s = S();
#if defined(FRAME_BENCH_RUN_REPLAY)
    // the packed stress rows ask fixed_traffic.py by home and level (its ?s= table is the baseline run's)
    snprintf(s.url, sizeof s.url, "http://%s/aircraft.json?home=%s&n=%u", FRAME_BENCH_HOST,
             kScenes[k].home == 0 ? "bend" : "ksfo", (unsigned)kScenes[k].level);
#else
    snprintf(s.url, sizeof s.url, "http://%s/aircraft.json?s=%d", FRAME_BENCH_HOST, k);
#endif
    snprintf(s.radius, sizeof s.radius, "%u", (unsigned)kScenes[k].rung);
    s.sceneStartMs = millis();
    s.polls = 0;
}

// THE CLOUD POLL's region (AircraftManager's fetch task, after ParseBlips parsed a /blips reply): the survey's counts
// and the recorder. latSent/lonSent are the request's own 2-dp strings (wireloc::Center), exactly what was sent.
// A REC poll whose request is not at the scene's airport prints "skip home-ok 0" and NO body (FB-R2): no body from any
// other home can be printed. Nothing here prints a coordinate.
// CRC-32 (IEEE, zlib's), bitwise: the replay's CRC is computed ON THE DEVICE over the body it deserialized.
inline uint32_t Crc32(const char* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= (uint8_t)p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

// A template, so this header needs neither ArduinoJson nor models/Aircraft.h: the region's own types fill it in.
template <typename Doc, typename Rows, typename Parse>
inline void OnCloudPoll(Doc& doc, Rows& rows, long& dataEpoch, const char* latSent, const char* lonSent, Parse parse)
{
    State& s = S();
    const int k = s.scene;
    if (k < 0 || s.done || !s.measuring) return;
    const Scene& sc = kScenes[k];
#if defined(FRAME_BENCH_RUN_REPLAY)
    if (sc.kind == REPLAY || sc.kind == CTRL_IL) {
        // THE REPLAY (on the fetch task, core 0): the live GET has been made and parsed as shipped; its content is
        // replaced by the recorded body at the same elapsed time since the scene's settle began.
        const int ai = sc.home == 0 ? 0 : 1;
        const RecPoll* rp = kRecPolls[ai];
        const uint32_t el = millis() - s.sceneStartMs;
        int pick = 0;
        for (int i = 0; i < kRecN[ai]; ++i)
            if (rp[i].ms <= el) pick = i;
        const uint32_t t0 = micros();
        doc.clear();
        deserializeJson(doc, rp[pick].body);
        const long age = rp[pick].epoch - doc["t"].template as<long>();   // the recording's own data age
        doc["t"] = (long)time(nullptr) - age;
        Rows tmp;
        long e2 = 0;
        const bool parsed = parse(doc, tmp, e2);
        if (parsed) {
            rows.swap(tmp);
            dataEpoch = e2;
        }
        const uint32_t crc = Crc32(rp[pick].body, strlen(rp[pick].body));
        const uint32_t us = micros() - t0;
        if (parsed)
            Serial.printf("[frame-bench] served A%u %d crc %08lx us %lu\n", (unsigned)sc.home, rp[pick].seq,
                          (unsigned long)crc, (unsigned long)us);
        else   // the live picture stands this poll: counted as a miss (the replay's condition 3)
            Serial.printf("[frame-bench] served A%u %d replay FAILED\n", (unsigned)sc.home, rp[pick].seq);
        return;
    }
#endif
    if (sc.kind != SURVEY && sc.kind != REC) return;
    const bool homeOk = strcmp(latSent, kAptLat[sc.home]) == 0 && strcmp(lonSent, kAptLon[sc.home]) == 0;
    const int poll = ++s.polls;
    if (sc.kind == SURVEY) {
        const double lat0 = atof(kAptLat[sc.home]), lon0 = atof(kAptLon[sc.home]);
        const double cosLat = cos(lat0 * 0.017453292519943295);
        unsigned in10 = 0, in25 = 0;
        for (const auto& a : rows) {
            const double dn = (a.latitude - lat0) * 111.0, de = (a.longitude - lon0) * 111.0 * cosLat;
            const double mi = sqrt(dn * dn + de * de) / 1.609344;
            in10 += mi <= 10.0;
            in25 += mi <= 25.0;
        }
        Serial.printf("[frame-bench] survey A%u poll %d rows %u in10 %u in25 %u home-ok %d\n", (unsigned)sc.home, poll,
                      (unsigned)rows.size(), in10, in25, homeOk ? 1 : 0);
        return;
    }
    if (!homeOk) {
        Serial.printf("[frame-bench] rec A%u skip home-ok 0\n", (unsigned)sc.home);
        return;
    }
    const size_t len = measureJson(doc);
    char* body = (char*)heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == nullptr) {
        Serial.printf("[frame-bench] rec A%u REFUSED: no buffer for %u B\n", (unsigned)sc.home, (unsigned)len);
        return;
    }
    serializeJson(doc, body, len + 1);
    const unsigned long ms = (unsigned long)(millis() - s.sceneStartMs);
    const long epoch = (long)time(nullptr);
    constexpr size_t kPart = 256;
    const int parts = (int)((len + kPart - 1) / kPart);
    static const char* hexd = "0123456789abcdef";
    char out[2 * kPart + 1];
    for (int p = 0; p < parts; ++p) {
        const size_t a = p * kPart, b = std::min(len, a + kPart);
        size_t o = 0;
        for (size_t i = a; i < b; ++i) {
            out[o++] = hexd[((uint8_t)body[i]) >> 4];
            out[o++] = hexd[((uint8_t)body[i]) & 15];
        }
        out[o] = 0;
        Serial.printf("[frame-bench] rec A%u %d %lu %ld %d/%d %s\n", (unsigned)sc.home, poll, ms, epoch, p + 1, parts,
                      out);
    }
    heap_caps_free(body);
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

// I-5: the instrument's own per-frame cost, timed on the device: Record() into a scratch buffer, and Override() on a
// key it does not answer (the per-frame "scanline" read), each over 10,000 calls.
inline void SelfTime()
{
    State& s = S();
    Rec* keep = s.buf;
    const int keepN = s.n;
    const bool keepM = s.measuring;
    s.measuring = true;
    s.n = 0;
    const uint32_t t0 = micros();
    for (int i = 0; i < 10000; ++i) {
        if (s.n >= kCap) s.n = 0;
        Record(50000, 40);
    }
    const uint32_t t1 = micros();
    volatile int hits = 0;
    const int keepScene = s.scene;
    s.scene = 0;   // a scene is active, so Override() walks its whole compare list for an unknown key
    for (int i = 0; i < 10000; ++i)
        if (Override("scanline") != nullptr) hits = hits + 1;
    const uint32_t t2 = micros();
    s.scene = keepScene;
    s.buf = keep;
    s.n = keepN;
    s.measuring = keepM;
    Serial.printf("[frame-bench] record cost %lu ns/frame; override cost %lu ns/call\n",
                  (unsigned long)((t1 - t0) / 10), (unsigned long)((t2 - t1) / 10));
    (void)hits;
}

// Ends the measured span: the records move to the dump buffer, printed a little each frame during the next settle.
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
