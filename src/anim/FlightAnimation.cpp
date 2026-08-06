// FlightAnimation.cpp -- the locked beat sheet, drawn.
//
// Spec: docs/missileer-game-design.md §11 "Animation art direction (LOCKED)"
// and §7 "FLIGHT DIRECTOR". Where this file and that section disagree, that
// section is right and this is a bug.
#include "FlightAnimation.h"

#include <math.h>

namespace missileer {
namespace flight {
namespace {

// ---------------------------------------------------------------------------
// PALETTE -- §11's accents, and AMBER IS NOT AMONG THEM.
//
// §11 reserves amber (#ffb000) for training/EXERCISE traffic and nothing else:
// "a player must be able to tell a drill from the real thing at a glance and
// from across a desk, and a colour that appears anywhere else stops carrying
// that." A launch animation is the most tempting place to spend it -- warning
// glows, staging flashes, a hot plume all want it -- and spending it here is
// exactly how the reservation dies.
//
// So chrome draws from these four and only these four.
// ---------------------------------------------------------------------------
namespace pal {
inline uint32_t SegRed()   { return lgfx::color888(0xFF, 0x2A, 0x1A); } // time, live state
inline uint32_t Red()      { return lgfx::color888(0xC0, 0x28, 0x1C); } // deviation, failure
inline uint32_t Paper()    { return lgfx::color888(0xF2, 0xEF, 0xE6); } // traffic, the record
inline uint32_t Green()    { return lgfx::color888(0x37, 0xD0, 0x5C); } // interactive
inline uint32_t GreenDeep(){ return lgfx::color888(0x1F, 0x8A, 0x3D); }
inline uint32_t Brass()    { return lgfx::color888(0xB0, 0x8D, 0x3E); } // accents
inline uint32_t Dim()      { return lgfx::color888(0x6B, 0x6A, 0x5E); }
inline uint32_t Space()    { return lgfx::color888(0x04, 0x04, 0x08); }
inline uint32_t Earth()    { return lgfx::color888(0x12, 0x2A, 0x3E); } // night ocean, BOA
inline uint32_t EarthLit() { return lgfx::color888(0x1E, 0x44, 0x60); }
inline uint32_t Atmos()    { return lgfx::color888(0x2E, 0x6E, 0xA8); }
inline uint32_t Body()     { return lgfx::color888(0xB8, 0xBC, 0xC4); } // vehicle skin
inline uint32_t BodyDark() { return lgfx::color888(0x5A, 0x5E, 0x66); }
inline uint32_t Rcs()      { return lgfx::color888(0x6C, 0xC8, 0xFF); } // §11: BLUE porcupine
} // namespace pal

/**
 * DETONATION FIRE RAMP -- Plumbbob Hood / Upshot-Knothole Badger (§11, §15).
 *
 * Deliberately a SEPARATE ramp from the chrome palette above, and deliberately
 * not reachable from any chrome path. This is incandescence -- a fireball
 * rendered from AEC test photography -- not an accent colour, and it appears
 * for the ~6 seconds of one beat and never on a HUD, a label or a state
 * indicator.
 *
 * That distinction is the whole reason it is allowed to contain warm hues at
 * all while §11's amber reservation holds: amber signals EXERCISE because
 * amber is *chrome*. A fireball cannot be mistaken for a mode indicator. None
 * of these values is the amber token, and nothing outside DrawDetonation may
 * call this.
 */
uint32_t FireRamp(float t) // t: 0 = white-hot core, 1 = cooled rust
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    struct Stop { float t; uint8_t r, g, b; };
    static const Stop kStops[] = {
        {0.00f, 0xFF, 0xFF, 0xFF}, // white-hot
        {0.18f, 0xFF, 0xF0, 0xC8},
        {0.38f, 0xFF, 0xC2, 0x5A},
        {0.58f, 0xF2, 0x7A, 0x28},
        {0.76f, 0xC4, 0x38, 0x18},
        {0.90f, 0x7E, 0x2A, 0x16},
        {1.00f, 0x4A, 0x2A, 0x1C}, // rust
    };
    constexpr int n = sizeof(kStops) / sizeof(kStops[0]);
    for (int i = 1; i < n; ++i) {
        if (t <= kStops[i].t) {
            const Stop& a = kStops[i - 1];
            const Stop& b = kStops[i];
            const float f = (t - a.t) / (b.t - a.t);
            return lgfx::color888((uint8_t)(a.r + (b.r - a.r) * f),
                                  (uint8_t)(a.g + (b.g - a.g) * f),
                                  (uint8_t)(a.b + (b.b - a.b) * f));
        }
    }
    return lgfx::color888(kStops[n - 1].r, kStops[n - 1].g, kStops[n - 1].b);
}

// ---------------------------------------------------------------------------
// TIME MODES -- and why COMPRESSED IS NOT A SCALE FACTOR.
//
// The obvious compressed mode divides every true duration by ~21 to fit 90 s.
// It destroys the sequence. §11's staging beat is
//
//     burnout -> sep -> ~1 s coast (exposed, UNLIT bell) -> IGNITION -> burn
//
// and that coast is "the pause where a real vehicle is committed and not yet
// accelerating... the only moment in the ascent with any suspense in it". At
// 21x it is 47 ms -- three frames, invisible, and the rig would be used to
// judge an ascent whose best beat had been compressed out of existence.
//
// So the two modes answer two different questions and have two different
// duration columns:
//
//   COMPRESSED preserves BEAT LEGIBILITY. Every beat stays long enough to read;
//              the long quiet (midcourse, 26 real minutes) collapses hard,
//              because there is nothing in it to judge. Used for iterating art.
//   TRUE-TIME  preserves FEEL, at the published marks. Used to answer the only
//              question compressed mode cannot: does the real thing hold up at
//              real speed. §7's whole design rests on that and it must be
//              provable on glass, not asserted.
//
// The coast is 1 s in BOTH columns. That is the point.
// ---------------------------------------------------------------------------
struct BeatSpec {
    const char* name;
    uint32_t    trueMs;
    uint32_t    compressedMs;
};

// Marks from §7 (stage 1 sep ~60 s, stage 3 ~120 s, post-boost ~180 s, boost
// through RV release ~T+0-4 min) and §12's flight-sequence row. Impact is taken
// at the 9,700 km validation case: 31.6 min = 1,896 s, so terminal re-escalates
// at 1,806 s (T-90 s) exactly as §7 requires.
const BeatSpec kBeats[(int)Beat::COUNT] = {
    /* Ignition     */ {"IGNITION",      60000,  6000},
    /* Stage1Sep    */ {"STAGE 1 SEP",    3000,  3000},  // staging beat: coast is 1 s in both
    /* Stage2Burn   */ {"STAGE 2",       22000,  5000},
    /* ShroudEject  */ {"SHROUD",         4000,  3000},
    /* Stage2Sep    */ {"STAGE 2 SEP",    3000,  3000},
    /* Stage3Burn   */ {"STAGE 3",       54000,  5000},
    /* Stage3Sep    */ {"STAGE 3 SEP",    3000,  3000},
    /* PostBoost    */ {"POST-BOOST",    20000,  6000},
    /* PitchOver    */ {"PSRE PITCH",    25000,  6000},
    /* RvRelease    */ {"RV RELEASE",     8000,  6000},
    /* BusBackaway  */ {"BUS BACKAWAY",  12000,  5000},
    /* PenaidDeploy */ {"PENAIDS",       15000,  5000},
    /* Midcourse    */ {"MIDCOURSE",   1546000, 12000},  // the long quiet: 26 min -> 12 s
    /* Reentry      */ {"REENTRY",       88000, 12000},
    /* Detonation   */ {"DETONATION",     6000,  6000},
    /* MatchCut     */ {"MATCH CUT",      8000,  6000},
};

/**
 * Kinematic anchors, one row per beat.
 *
 * A TABLE RATHER THAN A SIMULATION, on purpose: this is a rig for tuning art,
 * and tuning must be editing numbers in one visible place. A physics model
 * would be more defensible and far less adjustable, and nothing here is trying
 * to be right -- §7 already owns the only number that has to be (TOF, from the
 * Lambert solution).
 *
 * angle: 0 = nose straight up, +90 = nose along the horizon, >90 = NOSE DOWN.
 * §11: "PSRE pitch-over continues the arc nose-down -- downrange velocity is
 * conserved, so the RV releases in the direction of travel. Getting this
 * backwards is the tell that an animation was drawn rather than reasoned."
 */
struct Anchor { float x0, y0, x1, y1, a0, a1, alt0, alt1, s0, s1; };
const Anchor kAnchors[(int)Beat::COUNT] = {
    /* Ignition     */ {120, 208, 120, 190,   0,   3, 0.00f, 0.06f, 1.00f, 1.00f},
    /* Stage1Sep    */ {120, 190, 122, 178,   3,   7, 0.06f, 0.13f, 1.00f, 0.96f},
    /* Stage2Burn   */ {122, 178, 132, 152,   7,  17, 0.13f, 0.31f, 0.96f, 0.86f},
    /* ShroudEject  */ {132, 152, 138, 140,  17,  21, 0.31f, 0.39f, 0.86f, 0.81f},
    /* Stage2Sep    */ {138, 140, 142, 132,  21,  25, 0.39f, 0.45f, 0.81f, 0.76f},
    /* Stage3Burn   */ {142, 132, 156, 112,  25,  35, 0.45f, 0.61f, 0.76f, 0.66f},
    /* Stage3Sep    */ {156, 112, 159, 106,  35,  39, 0.61f, 0.67f, 0.66f, 0.61f},
    /* PostBoost    */ {159, 106, 165, 100,  39,  46, 0.67f, 0.73f, 0.61f, 0.56f},
    /* PitchOver    */ {165, 100, 173,  94,  46,  98, 0.73f, 0.79f, 0.56f, 0.51f}, // through horizontal, nose-down
    /* RvRelease    */ {173,  94, 177,  92,  98, 101, 0.79f, 0.81f, 0.51f, 0.46f},
    /* BusBackaway  */ {177,  92, 181,  91, 101, 104, 0.81f, 0.83f, 0.46f, 0.41f},
    /* PenaidDeploy */ {181,  91, 187,  90, 104, 107, 0.83f, 0.85f, 0.41f, 0.34f},
    /* Midcourse    */ {187,  90, 120, 120, 107, 110, 0.85f, 0.92f, 0.34f, 0.05f}, // ends as A DOT
    /* Reentry      */ { 96,  40, 120, 176, 150, 168, 0.92f, 0.20f, 0.05f, 0.34f},
    /* Detonation   */ {120, 176, 120, 176, 168, 168, 0.20f, 0.10f, 0.34f, 0.34f},
    /* MatchCut     */ {120, 120, 120, 120, 110, 110, 0.92f, 0.92f, 0.34f, 0.05f},
};

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

/** Deterministic hash-noise, so a replayed beat draws the identical frame. */
inline float Noise(uint32_t seed)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float)(seed & 0xFFFF) / 65535.0f;
}

} // namespace

// ---------------------------------------------------------------------------
// Table accessors
// ---------------------------------------------------------------------------

const char* BeatName(Beat b)
{
    const int i = (int)b;
    return (i >= 0 && i < (int)Beat::COUNT) ? kBeats[i].name : "?";
}

uint32_t BeatDurationMs(Beat b, TimeMode mode)
{
    const int i = (int)b;
    if (i < 0 || i >= (int)Beat::COUNT) return 0;
    return mode == TimeMode::TrueTime ? kBeats[i].trueMs : kBeats[i].compressedMs;
}

uint32_t BeatTrueStartMs(Beat b)
{
    uint32_t t = 0;
    for (int i = 0; i < (int)b && i < (int)Beat::COUNT; ++i) t += kBeats[i].trueMs;
    return t;
}

uint32_t SequenceDurationMs(TimeMode mode)
{
    uint32_t t = 0;
    for (int i = 0; i < (int)Beat::COUNT; ++i) t += BeatDurationMs((Beat)i, mode);
    return t;
}

// ---------------------------------------------------------------------------
// Director
// ---------------------------------------------------------------------------

void Director::Begin(int screen, TimeMode mode)
{
    screen_ = screen;
    mode_   = mode;
    Restart();
}

void Director::Restart()
{
    seqElapsedMs_ = 0;
    finished_ = false;
    EnterBeat(Beat::Ignition);
}

void Director::Seek(Beat b)
{
    if ((int)b < 0) b = Beat::Ignition;
    if ((int)b >= (int)Beat::COUNT) b = (Beat)((int)Beat::COUNT - 1);
    // Sequence time follows the beat, so the HUD's T+ stays truthful after a jump.
    seqElapsedMs_ = 0;
    for (int i = 0; i < (int)b; ++i) seqElapsedMs_ += BeatDurationMs((Beat)i, mode_);
    finished_ = false;
    EnterBeat(b);
}

void Director::StepBeat(int delta)
{
    int next = (int)beat_ + delta;
    const int n = (int)Beat::COUNT;
    while (next < 0) next += n;
    while (next >= n) next -= n;
    Seek((Beat)next);
}

void Director::SetMode(TimeMode mode)
{
    if (mode == mode_) return;
    // HOLD THE BEAT, NOT THE ELAPSED TIME. Carrying elapsed ms across a mode
    // switch would land somewhere unrelated -- 40 s into compressed is past the
    // whole boost phase, and 40 s into true time is still first stage. The
    // operator switching modes is asking "how does THIS beat feel at real
    // speed", so the beat is the thing that must survive.
    const Beat keep = beat_;
    mode_ = mode;
    Seek(keep);
}

void Director::EnterBeat(Beat b)
{
    beat_ = b;
    beatElapsedMs_ = 0;

    const Anchor& a = kAnchors[(int)b];
    vx_ = a.x0; vy_ = a.y0;
    angleDeg_ = a.a0;
    altitude_ = a.alt0;
    scale_    = a.s0;

    // Separations throw embers and a spent stage along the flight line (§11:
    // AXIAL, "the spent stage receding on the flight line rather than tumbling
    // off sideways"). Seeded per beat so a replay is identical.
    const bool isSep = (b == Beat::Stage1Sep || b == Beat::Stage2Sep || b == Beat::Stage3Sep);
    stageLife_ = isSep ? 1.0f : 0.0f;
    stageX_ = vx_; stageY_ = vy_;
    for (int i = 0; i < kEmbers; ++i) {
        if (!isSep) { embers_[i].life = 0; continue; }
        const float n1 = Noise((uint32_t)b * 977u + i * 31u + 7u);
        const float n2 = Noise((uint32_t)b * 613u + i * 57u + 11u);
        const float rad = (n1 - 0.5f) * 1.4f;                 // narrow cone, not a sphere
        const float spd = 0.35f + n2 * 0.9f;
        embers_[i] = {vx_, vy_, sinf(rad) * spd, spd * 0.9f, 1.0f};
    }

    if (b == Beat::RvRelease) { rvX_ = vx_; rvY_ = vy_; busX_ = vx_; busY_ = vy_; }
    if (b == Beat::PenaidDeploy) {
        for (int i = 0; i < kPenaids; ++i) {
            const float n1 = Noise(i * 131u + 3u), n2 = Noise(i * 271u + 5u);
            penaids_[i] = {busX_, busY_, (n1 - 0.5f) * 0.5f, -0.18f - n2 * 0.22f, 0.0f};
        }
    }
}

void Director::UpdateKinematics(uint32_t dtMs)
{
    const float p = BeatProgress();
    const Anchor& a = kAnchors[(int)beat_];
    vx_       = Lerp(a.x0, a.x1, p);
    vy_       = Lerp(a.y0, a.y1, p);
    angleDeg_ = Lerp(a.a0, a.a1, p);
    altitude_ = Lerp(a.alt0, a.alt1, p);
    scale_    = Lerp(a.s0, a.s1, p);

    const float dt = (float)dtMs / 16.0f; // ~frames at 60 Hz, so motion is frame-rate independent

    if (stageLife_ > 0) {
        // Receding ALONG the flight line: back down the velocity vector.
        const float r = angleDeg_ * 0.01745f;
        stageX_ -= sinf(r) * 0.9f * dt;
        stageY_ += cosf(r) * 0.9f * dt;
        stageLife_ -= 0.012f * dt;
    }
    for (int i = 0; i < kEmbers; ++i) {
        if (embers_[i].life <= 0) continue;
        embers_[i].x -= embers_[i].vx * dt;
        embers_[i].y += embers_[i].vy * dt;
        embers_[i].life -= 0.028f * dt;
    }

    if (beat_ == Beat::BusBackaway || beat_ == Beat::PenaidDeploy || beat_ == Beat::Midcourse) {
        // §11: the bus BACKS AWAY under retro thrust. It moves opposite the
        // direction of travel while the RV carries on -- the separation the
        // player reads is the gap opening between them.
        const float r = angleDeg_ * 0.01745f;
        busX_ -= sinf(r) * 0.22f * dt;
        busY_ += cosf(r) * 0.22f * dt;
        rvX_ = vx_; rvY_ = vy_;
    }
    if (beat_ == Beat::PenaidDeploy || beat_ == Beat::Midcourse || beat_ == Beat::Reentry) {
        for (int i = 0; i < kPenaids; ++i) {
            penaids_[i].x += penaids_[i].vx * dt;
            penaids_[i].y += penaids_[i].vy * dt;
            if (beat_ == Beat::Reentry) penaids_[i].burn += 0.010f * dt;
        }
    }
}

void Director::Advance(uint32_t dtMs)
{
    if (finished_) return;
    beatElapsedMs_ += dtMs;
    seqElapsedMs_  += dtMs;

    uint32_t dur = BeatDurationMs(beat_, mode_);
    while (beatElapsedMs_ >= dur) {
        beatElapsedMs_ -= dur;
        const int next = (int)beat_ + 1;
        if (next >= (int)Beat::COUNT) { finished_ = true; beatElapsedMs_ = dur; break; }
        const uint32_t carry = beatElapsedMs_;
        EnterBeat((Beat)next);
        beatElapsedMs_ = carry;
        dur = BeatDurationMs(beat_, mode_);
    }
    UpdateKinematics(dtMs);
}

float Director::BeatProgress() const
{
    const uint32_t dur = BeatDurationMs(beat_, mode_);
    return dur ? Clamp01((float)beatElapsedMs_ / (float)dur) : 0.0f;
}

uint32_t Director::TPlusMs() const
{
    // ALWAYS THE PUBLISHED MARK, in both modes. In compressed mode the elapsed
    // wall time is meaningless (12 s standing in for 26 minutes), and a HUD
    // that showed it would be reporting the rig's clock as if it were the
    // flight's. The operator needs to know which T+ mark they are looking at;
    // that is the number §7 specifies the beats against.
    return BeatTrueStartMs(beat_) + (uint32_t)(BeatProgress() * (float)kBeats[(int)beat_].trueMs);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void Director::DrawSky(LovyanGFX& g, int dy) const
{
    g.fillScreen(pal::Space());
    // Stars brighten as the atmosphere thins -- the cheapest altitude cue after
    // the limb itself, and it costs 40 pixels.
    const int n = 40;
    const uint8_t v = (uint8_t)(60 + 180 * Clamp01(altitude_ * 1.4f));
    for (int i = 0; i < n; ++i) {
        const int x = (int)(Noise(i * 7919u + 13u) * screen_);
        const int y = (int)(Noise(i * 6271u + 29u) * screen_ * 0.8f);
        const uint8_t s = (uint8_t)(v * (0.5f + 0.5f * Noise(i * 104729u)));
        g.drawPixel(x, y - dy, lgfx::color888(s, s, (uint8_t)min(255, s + 20)));
    }
}

void Director::DrawEarthLimb(LovyanGFX& g, int dy) const
{
    // §11: "the whole ascent plays as a script over a SINKING EARTH LIMB -- the
    // horizon dropping away is what sells altitude on a screen with no other
    // scale cue."
    //
    // A circle whose top edge is the horizon. As altitude rises the radius
    // shrinks (more visible curvature) and the top edge slides DOWN the screen
    // (the Earth receding). Drawn column-wise: 240 vertical spans is cheaper
    // than a 5,000 px-radius fillCircle and clips itself for free.
    const float R    = Lerp(5000.0f, 200.0f, Clamp01(altitude_));
    const float topY = Lerp(196.0f, 238.0f, Clamp01(altitude_));
    const float cy   = topY + R;
    const float cx   = screen_ * 0.5f;

    for (int x = 0; x < screen_; ++x) {
        const float ddx = (float)x - cx;
        const float under = R * R - ddx * ddx;
        if (under <= 0) continue;
        const int ly = (int)(cy - sqrtf(under));
        if (ly >= screen_) continue;
        // Thin lit atmosphere band on the limb -- the blue line that reads as
        // "air" from above and separates the Earth from space.
        const int band = (int)Lerp(3.0f, 7.0f, Clamp01(altitude_));
        for (int b = 0; b < band; ++b) {
            const int yy = ly - band + b;
            if (yy < 0 || yy >= screen_) continue;
            const float f = (float)b / (float)band;
            g.drawPixel(x, yy - dy, lgfx::color888((uint8_t)(0x2E * f), (uint8_t)(0x6E * f), (uint8_t)(0xA8 * f)));
        }
        if (ly < screen_) {
            g.drawFastVLine(x, ly - dy, screen_ - ly, pal::Earth());
            // A hint of sunlit ocean toward the limb's crown, so the sphere
            // reads as lit from one side rather than as a flat fill.
            if (fabsf(ddx) < screen_ * 0.28f) {
                g.drawFastVLine(x, ly - dy, 6, pal::EarthLit());
            }
        }
    }
}

void Director::DrawPlume(LovyanGFX& g, int dy) const
{
    // Lit only when a motor is actually burning. The staging beat's ~1 s coast
    // is defined by this being OFF (see DrawVehicle's bell).
    bool burning = false;
    switch (beat_) {
        case Beat::Ignition:
        case Beat::Stage2Burn:
        case Beat::Stage3Burn: burning = true; break;
        case Beat::Stage1Sep:
        case Beat::Stage2Sep:
        case Beat::Stage3Sep: {
            // burnout -> sep -> ~1 s coast -> IGNITION -> burn, inside one beat.
            const uint32_t coastEnd = 1000 + (BeatDurationMs(beat_, mode_) - 3000) / 2;
            burning = beatElapsedMs_ > coastEnd + 1000;
            break;
        }
        default: burning = false; break;
    }
    if (!burning) return;

    const float r  = angleDeg_ * 0.01745f;
    const float bx = vx_ + sinf(r) * 9.0f * scale_;
    const float by = vy_ + cosf(r) * 9.0f * scale_;
    const float flick = 0.75f + 0.25f * Noise(beatElapsedMs_ / 40u + 3u);
    const float len = (14.0f + 10.0f * flick) * scale_;

    // Three tapering tongues rather than one triangle: cheap, and it flickers
    // asymmetrically the way a real exhaust does.
    for (int i = 0; i < 3; ++i) {
        const float sp = (i - 1) * 0.16f;
        const float ex = bx + sinf(r + sp) * len * (i == 1 ? 1.0f : 0.72f);
        const float ey = by + cosf(r + sp) * len * (i == 1 ? 1.0f : 0.72f);
        const uint32_t c = (i == 1) ? lgfx::color888(0xFF, 0xF4, 0xD8) : FireRamp(0.32f);
        g.drawLine((int)bx, (int)by - dy, (int)ex, (int)ey - dy, c);
    }
    g.fillCircle((int)bx, (int)by - dy, (int)max(1.0f, 2.5f * scale_), lgfx::color888(0xFF, 0xFF, 0xF0));
}

void Director::DrawVehicle(LovyanGFX& g, int dy) const
{
    if (beat_ >= Beat::RvRelease) return; // from here the RV and bus are drawn separately
    const float r = angleDeg_ * 0.01745f;
    const float L = 20.0f * scale_;       // body length
    const float W = max(2.0f, 4.0f * scale_);

    const float nx = vx_ - sinf(r) * -L * 0.5f, ny = vy_ - cosf(r) * L * 0.5f; // nose
    const float tx = vx_ + sinf(r) * L * 0.5f,  ty = vy_ + cosf(r) * L * 0.5f; // tail

    // Body: a thick line plus a nose triangle. At this size anything more
    // detailed is sub-pixel on a 240 px face.
    for (int o = -1; o <= 1; ++o) {
        const float ox = cosf(r) * o * W * 0.5f, oy = -sinf(r) * o * W * 0.5f;
        g.drawLine((int)(nx + ox), (int)(ny + oy) - dy, (int)(tx + ox), (int)(ty + oy) - dy,
                   o == 0 ? pal::Body() : pal::BodyDark());
    }

    // THE EXPOSED, UNLIT BELL during the staging coast. §11 calls the coast
    // "the whole beat"; the bell is what makes it legible -- the player sees an
    // engine that is there and is not firing, which is the suspense.
    const bool isSep = (beat_ == Beat::Stage1Sep || beat_ == Beat::Stage2Sep || beat_ == Beat::Stage3Sep);
    if (isSep) {
        const uint32_t coastEnd = 1000 + (BeatDurationMs(beat_, mode_) - 3000) / 2;
        if (beatElapsedMs_ > 900 && beatElapsedMs_ <= coastEnd + 1000) {
            const float bw = W * 1.6f;
            g.drawLine((int)(tx - cosf(r) * bw), (int)(ty + sinf(r) * bw) - dy,
                       (int)(tx + cosf(r) * bw), (int)(ty - sinf(r) * bw) - dy, pal::BodyDark());
            g.drawLine((int)(tx - cosf(r) * bw), (int)(ty + sinf(r) * bw) - dy,
                       (int)(tx + sinf(r) * 4), (int)(ty + cosf(r) * 4) - dy, pal::BodyDark());
        }
    }
}

void Director::DrawSeparationFlash(LovyanGFX& g, int dy) const
{
    const bool isSep = (beat_ == Beat::Stage1Sep || beat_ == Beat::Stage2Sep || beat_ == Beat::Stage3Sep);
    if (!isSep || beatElapsedMs_ > 700) return;

    // §11: flash plus an ANAMORPHIC STREAK at the joint. Anamorphic = the wide
    // horizontal flare of a cinema lens, so the streak is drawn across the
    // frame, not along the vehicle -- that is what makes it read as a camera
    // artefact (the shot) rather than as an explosion (the vehicle).
    const float f = 1.0f - (float)beatElapsedMs_ / 700.0f;
    const int   y = (int)vy_ - dy;
    const int   half = (int)(screen_ * 0.48f * f);
    const uint8_t v = (uint8_t)(255 * f);
    g.drawFastHLine((int)vx_ - half, y, half * 2, lgfx::color888(v, v, (uint8_t)min(255, v + 10)));
    if (f > 0.5f) {
        g.drawFastHLine((int)vx_ - half / 2, y - 1, half, lgfx::color888(v, v, v));
        g.drawFastHLine((int)vx_ - half / 2, y + 1, half, lgfx::color888(v, v, v));
    }
    g.fillCircle((int)vx_, y, (int)(7 * f), lgfx::color888(v, v, (uint8_t)(v * 0.9f)));
}

void Director::DrawDebris(LovyanGFX& g, int dy) const
{
    for (int i = 0; i < kEmbers; ++i) {
        const Ember& e = embers_[i];
        if (e.life <= 0) continue;
        g.drawPixel((int)e.x, (int)e.y - dy, FireRamp(1.0f - e.life * 0.8f));
    }
    if (stageLife_ > 0) {
        // The spent stage, receding on the flight line and shrinking.
        const int s = (int)max(1.0f, 4.0f * stageLife_ * scale_);
        g.fillRect((int)stageX_ - s / 2, (int)stageY_ - dy - s, s, s * 2, pal::BodyDark());
    }
}

void Director::DrawShroud(LovyanGFX& g, int dy) const
{
    if (beat_ != Beat::ShroudEject) return;
    // §11: CLAMSHELL HALVES. Two arcs hinging apart and away -- not a single
    // cone popping off, which is a different vehicle's shroud.
    const float p = BeatProgress();
    const float r = angleDeg_ * 0.01745f;
    const float open = p * 26.0f;
    for (int side = -1; side <= 1; side += 2) {
        const float hx = vx_ + cosf(r) * side * open;
        const float hy = vy_ - sinf(r) * side * open;
        const float tilt = side * p * 1.1f;
        const int   L = (int)(11 * scale_);
        g.drawLine((int)hx, (int)hy - dy,
                   (int)(hx + sinf(r + tilt) * -L), (int)(hy - cosf(r + tilt) * L) - dy, pal::Body());
        g.drawLine((int)(hx + cosf(r) * side * 2), (int)(hy - sinf(r) * side * 2) - dy,
                   (int)(hx + sinf(r + tilt) * -L + cosf(r) * side * 2),
                   (int)(hy - cosf(r + tilt) * L - sinf(r) * side * 2) - dy, pal::BodyDark());
    }
}

void Director::DrawRcs(LovyanGFX& g, int dy) const
{
    if (beat_ != Beat::PostBoost && beat_ != Beat::PitchOver) return;
    // §11: "post-boost = BLUE PORCUPINE RCS". Short quills in many directions,
    // pulsing -- the bus talking to itself. Blue because it is cold gas, and
    // because it is the one moment that must not read as a main engine.
    const float r = angleDeg_ * 0.01745f;
    const int   n = 12;
    const float phase = (float)(beatElapsedMs_ % 700) / 700.0f;
    for (int i = 0; i < n; ++i) {
        const float a = r + (float)i * 6.2832f / n;
        const float pulse = Noise(i * 3571u + beatElapsedMs_ / 90u);
        if (pulse < 0.45f) continue;
        const float len = (3.0f + 4.0f * pulse) * (0.6f + 0.4f * sinf(phase * 6.2832f));
        g.drawLine((int)vx_, (int)vy_ - dy,
                   (int)(vx_ + sinf(a) * len), (int)(vy_ + cosf(a) * len) - dy, pal::Rcs());
    }
}

void Director::DrawRvAndBus(LovyanGFX& g, int dy) const
{
    if (beat_ < Beat::RvRelease || beat_ > Beat::Midcourse) return;

    // THE RELEASE IS SILENT. §11: "no ordnance, no bang. The quietest moment in
    // the sequence is the one that matters most." So there is deliberately NO
    // flash, NO streak and NO ember call here -- the RV simply is separate, and
    // the absence is the direction. Anything added to this function to make the
    // moment "land" is the mistake the locked section is guarding against.
    const float r = angleDeg_ * 0.01745f;

    // Bus: blunter, and drawn only while it is still nearby.
    if (beat_ >= Beat::BusBackaway) {
        const int s = (int)max(2.0f, 5.0f * scale_);
        g.fillRect((int)busX_ - s / 2, (int)busY_ - dy - s / 2, s, s, pal::BodyDark());
        // Retro plumes point FORWARD (it is thrusting against the direction of
        // travel to back away).
        for (int i = -1; i <= 1; i += 2) {
            g.drawLine((int)busX_, (int)busY_ - dy,
                       (int)(busX_ - sinf(r + i * 0.4f) * 6), (int)(busY_ - cosf(r + i * 0.4f) * 6) - dy,
                       pal::Rcs());
        }
    }

    // The RV: a small dart, nose along the direction of travel.
    const float L = 9.0f * scale_;
    g.fillTriangle((int)(rvX_ - sinf(r) * -L), (int)(rvY_ - cosf(r) * L) - dy,
                   (int)(rvX_ + cosf(r) * 2.5f), (int)(rvY_ - sinf(r) * 2.5f) - dy,
                   (int)(rvX_ - cosf(r) * 2.5f), (int)(rvY_ + sinf(r) * 2.5f) - dy,
                   pal::Body());
}

void Director::DrawPenaids(LovyanGFX& g, int dy) const
{
    if (beat_ < Beat::PenaidDeploy || beat_ > Beat::Reentry) return;
    for (int i = 0; i < kPenaids; ++i) {
        const Penaid& p = penaids_[i];
        if (p.burn >= 1.0f) continue; // burned out -- gone, not faded to grey
        if (beat_ == Beat::Reentry && p.burn > 0) {
            // §11: "reentry shows DECOY STREAKS BURNING OUT". A streak that
            // shortens and cools, then stops existing -- the point is that the
            // decoys do not survive, which is what makes the RV's survival read.
            const float len = 10.0f * (1.0f - p.burn);
            g.drawLine((int)p.x, (int)p.y - dy, (int)p.x, (int)(p.y - len) - dy, FireRamp(0.25f + p.burn * 0.6f));
        } else {
            g.drawPixel((int)p.x, (int)p.y - dy, pal::Dim());
        }
    }
}

void Director::DrawReentry(LovyanGFX& g, int dy) const
{
    if (beat_ != Beat::Reentry) return;
    const float p = BeatProgress();
    const float r = angleDeg_ * 0.01745f;

    // Plasma: a bow shock ahead of the vehicle, growing as the air thickens.
    const float heat = Clamp01(p * 1.3f);
    const int   n = 5;
    for (int i = n; i >= 1; --i) {
        const float f = (float)i / n;
        const float ax = vx_ - sinf(r) * -(10.0f + 9.0f * f) * scale_;
        const float ay = vy_ - cosf(r) * (10.0f + 9.0f * f) * scale_;
        g.fillCircle((int)ax, (int)ay - dy, (int)(2 + 6 * f * heat), FireRamp(0.15f + 0.55f * f));
    }
    // The RV itself, still there inside the sheath.
    g.fillCircle((int)vx_, (int)vy_ - dy, (int)max(2.0f, 4.0f * scale_), pal::Paper());
    // Trailing ionisation.
    for (int i = 1; i < 14; ++i) {
        const float t = (float)i / 14.0f;
        g.drawPixel((int)(vx_ + sinf(r) * 12 * t * 4), (int)(vy_ + cosf(r) * 12 * t * 4) - dy,
                    FireRamp(0.45f + t * 0.5f));
    }
}

void Director::DrawDetonation(LovyanGFX& g, int dy) const
{
    if (beat_ != Beat::Detonation) return;
    // §11: Plumbbob Hood / Upshot-Knothole Badger palette, FULL-SCREEN, cooling
    // to rust. Full-screen is the direction: at this point the frame is the
    // event, and a fireball that politely stays inside a viewport is a
    // firework.
    const float p = BeatProgress();
    const float grow = Clamp01(p * 3.2f);          // fast rise
    const float cool = Clamp01((p - 0.25f) / 0.75f); // slow decay
    const float maxR = screen_ * 0.95f;
    const int   rings = 9;
    for (int i = rings; i >= 0; --i) {
        const float f = (float)i / rings;
        const int   rr = (int)(maxR * grow * f);
        if (rr <= 0) continue;
        g.fillCircle(screen_ / 2, (int)vy_ - dy, rr, FireRamp(cool * 0.75f + f * 0.35f));
    }
    // The shock-lit ground under it, and a white core that is the last thing to go.
    if (p < 0.5f) {
        g.fillCircle(screen_ / 2, (int)vy_ - dy, (int)(maxR * grow * 0.10f * (1.0f - p * 2)),
                     lgfx::color888(0xFF, 0xFF, 0xFF));
    }
}

void Director::DrawMatchCut(LovyanGFX& g, int dy) const
{
    if (beat_ != Beat::MatchCut && beat_ != Beat::Midcourse) return;

    // §11's MATCH-CUT RULE: "Ascent ends by shrinking the vehicle to a single
    // dot; the map opens with that same dot. One continuous object across a cut
    // between two entirely different renderers -- it is the cheapest possible
    // way to make two views read as one flight, and it stops working the
    // instant either side redraws the dot differently."
    //
    // So the dot is drawn from vx_/vy_ on BOTH sides of the cut, by this one
    // function. A second dot-drawing site is the failure mode the rule names,
    // and the way to not have one is to not have one.
    const float p = BeatProgress();
    const bool  mapSide = (beat_ == Beat::Midcourse) ? (p > 0.45f) : (p > 0.5f);

    if (mapSide) {
        // Minimal map: the globe, the great-circle track, and the dot. Not the
        // product's map -- §7 puts full map rendering post-v1 -- just enough to
        // judge whether the cut lands.
        const int c = screen_ / 2;
        const int R = (int)(screen_ * 0.40f);
        g.drawCircle(c, c - dy, R, pal::GreenDeep());
        for (int i = -2; i <= 2; ++i) {
            g.drawEllipse(c, c - dy, R, (int)(R * fabsf(i * 0.35f) + 2), pal::Dim());
        }
        // The track: a shallow arc from the launch field to the aim point.
        for (int i = 0; i <= 40; ++i) {
            const float t = (float)i / 40.0f;
            const int   x = (int)Lerp((float)(c - R * 0.7f), (float)(c + R * 0.6f), t);
            const int   y = (int)(c + R * 0.35f - sinf(t * 3.1416f) * R * 0.55f);
            g.drawPixel(x, y - dy, t < 0.5f ? pal::Green() : pal::Dim());
        }
    }

    // THE DOT. Same coordinates, same size, both sides of the cut.
    const float shrink = mapSide ? 1.0f : Lerp(4.0f, 1.0f, Clamp01(p * 2.0f));
    g.fillCircle((int)vx_, (int)vy_ - dy, (int)max(1.0f, shrink), pal::SegRed());
}

void Director::Render(LovyanGFX& g, int yOffset)
{
    const int dy = yOffset;

    if (beat_ == Beat::Detonation) {
        // The fireball owns the frame; sky and limb would only show through as
        // a rim, and drawing them costs the most expensive frame in the
        // sequence its headroom.
        g.fillScreen(pal::Space());
        DrawEarthLimb(g, dy);
        DrawDetonation(g, dy);
        return;
    }

    DrawSky(g, dy);
    DrawEarthLimb(g, dy);

    if (beat_ == Beat::Midcourse || beat_ == Beat::MatchCut) {
        DrawPenaids(g, dy);
        DrawRvAndBus(g, dy);
        DrawMatchCut(g, dy);
        return;
    }

    DrawDebris(g, dy);
    DrawPlume(g, dy);
    DrawVehicle(g, dy);
    DrawShroud(g, dy);
    DrawRcs(g, dy);
    DrawRvAndBus(g, dy);
    DrawPenaids(g, dy);
    DrawReentry(g, dy);
    DrawSeparationFlash(g, dy); // last: the flash is over everything, like a real one
}

} // namespace flight
} // namespace missileer
