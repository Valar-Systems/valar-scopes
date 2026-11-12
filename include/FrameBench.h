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
#include <string.h>
#include <algorithm>

#ifndef FRAME_BENCH_HOST
#error "FRAME_BENCH needs FRAME_BENCH_HOST: set the untracked env var; scripts/framebench_host.py passes it in"
#endif

namespace framebench {

enum Kind : uint8_t { CTRL = 0, CTRL_DELAY = 1, FIXED = 2, LIVE = 3, LIVE_OWN = 4 };
struct Scene {
    Kind kind;
    uint8_t home;    // 0 Bend, 1 KSFO
    uint8_t level;   // the fixed traffic level (0 for LIVE)
    uint8_t rung;    // the configured radius, statute miles
};

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
constexpr int kNumScenes = sizeof(kScenes) / sizeof(kScenes[0]);
static_assert(kNumScenes == 23, "the registered run is 23 scenes");
static const char* const kKindName[] = {"CTRL", "CTRL+10ms", "FIXED", "LIVE", "LIVE-OWN"};
static const char* const kHomeName[] = {"Bend", "KSFO"};
// The homes, to 2 dp. They are only ever returned to Initialise(); nothing prints them.
static const char* const kHomeLat[] = {"44.06", "37.62"};
static const char* const kHomeLon[] = {"-121.31", "-122.38"};

constexpr uint32_t START_AFTER_MS = 90000;    // the first scene starts 90 s after boot (Wi-Fi, clock, first fetch)
constexpr uint32_t SETTLE_MS = 100000;        // A1: the trail's 90 s age plus a margin
constexpr uint32_t MEASURE_MS = 120000;
constexpr uint32_t MEASURE_OWN_MS = 300000;   // LIVE-OWN: about 10 [health] windows for (iv)
constexpr uint32_t PLANTED_DELAY_US = 10000;  // CTRL+10ms (I-6)
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
    const State& s = S();
    return s.scene >= 0 && !s.done && kScenes[s.scene].kind == CTRL_DELAY;
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
            r.flags = s.rebuilt ? 1 : 0;
            r.pad = 0;
        } else {
            s.overflowFrames++;
        }
    }
    s.drawn = 0;
    s.rebuilt = false;
}

inline uint32_t MeasureMs(int k) { return kScenes[k].kind == LIVE_OWN ? MEASURE_OWN_MS : MEASURE_MS; }

// The URL the bench host serves this scene's traffic on; the server follows the board by ?s=<scene>.
inline void PrepareScene(int k)
{
    State& s = S();
    snprintf(s.url, sizeof s.url, "http://%s/aircraft.json?s=%d", FRAME_BENCH_HOST, k);
    snprintf(s.radius, sizeof s.radius, "%u", (unsigned)kScenes[k].rung);
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
