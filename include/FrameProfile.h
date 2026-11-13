// THE FRAME PROFILE (docs/frame-profile.md, P1 / A0a; section 0 governs): where the 1.28" frame's loop time goes, one
// micros() delta per phase per frame, stored beside THE FRAME BENCH's record and dumped after each scene. Bench-only,
// built only under -DFRAME_PROFILE (env blipscope-s3-128-frameprofile, slug-less, never published), which rides on
// -DFRAME_BENCH. The whole instrument is this header and the `// THE FRAME PROFILE` regions. Nothing here writes NVS,
// and nothing prints a coordinate, a name or an address.
//
// THE PHASES (section 0 (c), under M3: the push runs on its own task, so `fr` no longer contains it). The loop's
// boundaries are contiguous micros() marks from frameStartUs to RecordFrameUs, so their sum IS fr: that sum is a
// construction check (every boundary in place), not a measurement. The measurements are the splits inside them:
//   upd       frameStartUs -> just before TakeForDrawing (Update, touch, network service, the sweep angle)
//   wait      pushtask::TakeForDrawing(): the loop waiting for a free buffer
//   under     the clear: DrawWeatherUnder's memcpy or rebuild, or fillScreen black
//   sweep     the sweep block; with weather on, split in DrawSweepOverWeather into sw_raster / sw_blend / sw_beam
//   (draw)    appManager.Draw, split into static (DrawRadar up to the contact loop), contacts (the contact loop, less
//             outline and labels), outline, lbl_layout, lbl_draw, and rest (draw less the others)
//   handover  pushtask::HandOver() and the phase store before it
//   flush     board::DisplayFlush
//   tail      after the flush to RecordFrameUs (the bench's present, the loop's I-6 plant)
// The per-contact phases (outline, lbl_layout, lbl_draw) read the CPU's cycle counter (one register read) and add
// only while the contact loop runs, so a label drawn elsewhere (a card) is never counted. contacts and rest are
// remainders: either below -0.05 ms means a timer double counts, and the reader stops.
//
// WHAT THE PHASES INCLUDE: wall time on the loop's core. The push task runs one priority above the loop on the same
// core, so its short CPU bursts (cache write-back, DMA set-up, the 1 ms polls) land in whichever phase is running.
// M3-4 bounds them (design (i): < 1 ms a frame). They are not separated here.
#pragma once
#ifdef FRAME_PROFILE
#ifndef FRAME_BENCH
#error "FRAME_PROFILE rides on FRAME_BENCH (docs/frame-profile.md section 0 (b))"
#endif

#include <Arduino.h>
#include <esp_cpu.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "FrameBench.h"

namespace frameprofile {

// The stored phases, in this order, each in 10 us units (framebench::Ten).
enum Phase : uint8_t {
    UPD, WAIT, UNDER, SWEEP, SW_RASTER, SW_BLEND, SW_BEAM, STATIC, CONTACTS, OUTLINE, LBL_LAYOUT, LBL_DRAW, REST,
    HANDOVER, FLUSH, TAIL, kPhases
};
static const char* const kPhaseName[kPhases] = {"upd", "wait", "under", "sweep", "sw_raster", "sw_blend", "sw_beam",
                                                "static", "contacts", "outline", "lbl_layout", "lbl_draw", "rest",
                                                "handover", "flush", "tail"};
// The micros() boundaries. The loop's seven are required on every frame; the radar's three and the sweep split's
// four only when that code ran (the radar view; weather under the sweep).
enum Bound : uint8_t { B_UPD, B_WAIT, B_UNDER, B_SWEEP, B_DRAW, B_HAND, B_FLUSH, B_RADAR, B_LOOP0, B_LOOP1,
                       B_SWR, B_SWB, B_SWM, B_SWE, kBounds };
constexpr uint16_t kLoopMask = (1u << B_UPD) | (1u << B_WAIT) | (1u << B_UNDER) | (1u << B_SWEEP) | (1u << B_DRAW) |
                               (1u << B_HAND) | (1u << B_FLUSH);
constexpr uint16_t kRadarMask = (1u << B_RADAR) | (1u << B_LOOP0) | (1u << B_LOOP1);
constexpr uint16_t kSweepMask = (1u << B_SWR) | (1u << B_SWB) | (1u << B_SWM) | (1u << B_SWE);
enum Acc : uint8_t { A_OUTLINE, A_LAYOUT, A_DRAW, kAccs };

struct ProfRec {
    uint16_t ph[kPhases];
};
static_assert(sizeof(ProfRec) == 32, "32 bytes a frame");
constexpr uint16_t kPartial = 0xFFFE;   // in ph[TAIL]: a loop boundary was missing on this frame (counted, not graded)

struct State {
    ProfRec* buf = nullptr;
    ProfRec* dumpBuf = nullptr;
    int lastN = 0;
    int dumpN = 0, dumpPos = 0, dumpScene = -1;
    uint32_t partial = 0, negContacts = 0, negRest = 0;   // this scene's counts, printed at its end
    uint32_t start = 0;
    uint32_t t[kBounds] = {};
    uint16_t seen = 0;
    bool inLoop = false;
    uint32_t cyc[kAccs] = {};
    uint32_t mhz = 0;
};
inline State& P()
{
    static State s;
    return s;
}

inline uint32_t Cyc() { return esp_cpu_get_cycle_count(); }

// frameStartUs's region: a new frame.
inline void Start(uint32_t t0)
{
    State& p = P();
    p.start = t0;
    p.seen = 0;
    p.inLoop = false;
    for (int a = 0; a < kAccs; ++a) p.cyc[a] = 0;
}
inline void Mark(Bound b)
{
    State& p = P();
    p.t[b] = micros();
    p.seen |= (uint16_t)(1u << b);
}
inline void LoopBegin() { Mark(B_LOOP0); P().inLoop = true; }
inline void LoopEnd() { P().inLoop = false; Mark(B_LOOP1); }
// A per-contact span, from c0 (a Cyc() read) to now, added only inside the contact loop.
inline void AddCyc(Acc a, uint32_t c0)
{
    State& p = P();
    const uint32_t c1 = Cyc();
    if (p.inLoop) p.cyc[a] += c1 - c0;
}

inline uint16_t CycTen(uint32_t cycles, uint32_t mhz) { return framebench::Ten(mhz ? cycles / mhz : 0); }
inline int32_t Span(const State& p, Bound a, Bound b) { return (int32_t)(p.t[b] - p.t[a]); }
inline uint16_t TenS(int32_t us) { return framebench::Ten(us < 0 ? 0 : (uint32_t)us); }

// The phases of the frame just finished, from its marks. Returns false if a loop boundary is missing.
inline bool Build(uint32_t frameUs, ProfRec& r)
{
    State& p = P();
    memset(&r, 0, sizeof r);
    if ((p.seen & kLoopMask) != kLoopMask) {
        r.ph[TAIL] = kPartial;
        return false;
    }
    r.ph[UPD] = TenS((int32_t)(p.t[B_UPD] - p.start));
    r.ph[WAIT] = TenS(Span(p, B_UPD, B_WAIT));
    r.ph[UNDER] = TenS(Span(p, B_WAIT, B_UNDER));
    r.ph[SWEEP] = TenS(Span(p, B_UNDER, B_SWEEP));
    if ((p.seen & kSweepMask) == kSweepMask) {
        r.ph[SW_RASTER] = TenS(Span(p, B_SWR, B_SWB));
        r.ph[SW_BLEND] = TenS(Span(p, B_SWB, B_SWM));
        r.ph[SW_BEAM] = TenS(Span(p, B_SWM, B_SWE));
    }
    const int32_t draw = Span(p, B_SWEEP, B_DRAW);
    const uint32_t mhz = p.mhz;
    const int32_t outlineUs = mhz ? (int32_t)(p.cyc[A_OUTLINE] / mhz) : 0;
    const int32_t layoutUs = mhz ? (int32_t)(p.cyc[A_LAYOUT] / mhz) : 0;
    const int32_t lblDrawUs = mhz ? (int32_t)(p.cyc[A_DRAW] / mhz) : 0;
    int32_t staticUs = 0, loopUs = 0;
    if ((p.seen & kRadarMask) == kRadarMask) {
        staticUs = Span(p, B_RADAR, B_LOOP0);
        loopUs = Span(p, B_LOOP0, B_LOOP1);
    }
    const int32_t contactsUs = loopUs - outlineUs - layoutUs - lblDrawUs;
    const int32_t restUs = draw - staticUs - loopUs;
    if (contactsUs < -50) p.negContacts++;
    if (restUs < -50) p.negRest++;
    r.ph[STATIC] = TenS(staticUs);
    r.ph[CONTACTS] = TenS(contactsUs);
    r.ph[OUTLINE] = TenS(outlineUs);
    r.ph[LBL_LAYOUT] = TenS(layoutUs);
    r.ph[LBL_DRAW] = TenS(lblDrawUs);
    r.ph[REST] = TenS(restUs);
    r.ph[HANDOVER] = TenS(Span(p, B_DRAW, B_HAND));
    r.ph[FLUSH] = TenS(Span(p, B_HAND, B_FLUSH));
    r.ph[TAIL] = TenS((int32_t)(frameUs - (p.t[B_FLUSH] - p.start)));
    return true;
}

// RecordFrameUs's region, right after framebench::Record: store this frame's phases beside its record.
inline void Commit(uint32_t frameUs)
{
    State& p = P();
    framebench::State& fb = framebench::S();
    if (!fb.measuring || p.buf == nullptr || fb.n <= 0 || fb.n == p.lastN) return;
    p.lastN = fb.n;
    if (!Build(frameUs, p.buf[fb.n - 1])) p.partial++;
}

inline bool Allocate()
{
    State& p = P();
    p.mhz = getCpuFrequencyMhz();
    if (p.buf) return true;
    p.buf = (ProfRec*)heap_caps_malloc(sizeof(ProfRec) * framebench::kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    p.dumpBuf = (ProfRec*)heap_caps_malloc(sizeof(ProfRec) * framebench::kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p.buf || !p.dumpBuf) {
        Serial.println("[frame-profile] REFUSED: the record buffers did not allocate");
        return false;
    }
    return true;
}

// PC-1 (section 4): the profile's own cost per frame, timed on the device. One frame of 60 contacts: the loop's
// seven marks, the radar's three, the sweep split's four, 60 x (outline + two layouts + one label draw) cycle spans,
// and the build. 10,000 frames, into a scratch record; the live state is saved and restored around it.
inline void SelfTime()
{
    State& p = P();
    const State keep = p;
    ProfRec scratch;
    const uint32_t t0 = micros();
    for (int i = 0; i < 10000; ++i) {
        Start(micros());
        Mark(B_UPD);
        Mark(B_WAIT);
        Mark(B_UNDER);
        Mark(B_SWR);
        Mark(B_SWB);
        Mark(B_SWM);
        Mark(B_SWE);
        Mark(B_SWEEP);
        Mark(B_RADAR);
        LoopBegin();
        for (int c = 0; c < 60; ++c) {
            const uint32_t o0 = Cyc();
            AddCyc(A_OUTLINE, o0);
            const uint32_t l0 = Cyc();
            AddCyc(A_LAYOUT, l0);
            const uint32_t l1 = Cyc();
            AddCyc(A_LAYOUT, l1);
            const uint32_t d0 = Cyc();
            AddCyc(A_DRAW, d0);
        }
        LoopEnd();
        Mark(B_DRAW);
        Mark(B_HAND);
        Mark(B_FLUSH);
        Build(50000, scratch);
    }
    const uint32_t t1 = micros();
    p = keep;
    Serial.printf("[frame-profile] cost %lu ns/frame cpu=%lu\n", (unsigned long)((t1 - t0) / 10),
                  (unsigned long)p.mhz);
}

// framebench::EndMeasure's region: this scene's profile goes to the dump, and its counts are printed.
inline void EndMeasure(int scene)
{
    State& p = P();
    ProfRec* t = p.dumpBuf;
    p.dumpBuf = p.buf;
    p.buf = t;
    p.dumpN = p.lastN;
    p.dumpPos = 0;
    p.dumpScene = scene;
    Serial.printf("[frame-profile] scene S%d end frames=%d partial=%lu neg-contacts=%lu neg-rest=%lu\n", scene,
                  p.dumpN, (unsigned long)p.partial, (unsigned long)p.negContacts, (unsigned long)p.negRest);
    p.lastN = 0;
    p.partial = p.negContacts = p.negRest = 0;
}

// After the bench's own dump: 32 bytes a frame, 16 frames a line, two lines a loop pass.
inline void PumpDump()
{
    State& p = P();
    if (p.dumpScene < 0 || framebench::S().dumpScene >= 0) return;
    static const char* hexd = "0123456789abcdef";
    constexpr int kPerLine = 16;
    for (int line = 0; line < 2 && p.dumpPos < p.dumpN; ++line) {
        char out[kPerLine * 64 + 4];
        int o = 0;
        const int end = p.dumpPos + kPerLine < p.dumpN ? p.dumpPos + kPerLine : p.dumpN;
        for (int i = p.dumpPos; i < end; ++i) {
            const uint8_t* b = (const uint8_t*)&p.dumpBuf[i];
            for (int j = 0; j < (int)sizeof(ProfRec); ++j) {
                out[o++] = hexd[b[j] >> 4];
                out[o++] = hexd[b[j] & 15];
            }
        }
        out[o] = 0;
        Serial.printf("[frame-profile] dump S%d %d %s\n", p.dumpScene, p.dumpPos, out);
        p.dumpPos = end;
    }
    if (p.dumpPos >= p.dumpN) {
        Serial.printf("[frame-profile] dump S%d done %d\n", p.dumpScene, p.dumpN);
        p.dumpScene = -1;
    }
}
// The next scene's measure waits for this dump too, so no dump line is printed inside a measured span.
inline bool DumpIdle() { return P().dumpScene < 0; }

}  // namespace frameprofile

#endif  // FRAME_PROFILE
