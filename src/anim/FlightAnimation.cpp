// FlightAnimation.cpp -- the locked beat sheet, drawn.
//
// Spec:        docs/missileer-game-design.md §11 "Animation art direction
//              (LOCKED)" and §7 "FLIGHT DIRECTOR".
// Look target: docs/reference/missileer-launch-animation-preview.html.
//
// The spec locks DIRECTION, the look target locks VALUES. Every hex below and
// every T+ mark in kBeats/kCaptions is lifted from the preview; where the two
// disagree the spec wins and the deviation is listed here rather than quietly
// resolved. Where this file disagrees with either, this file is the bug.
//
// ---------------------------------------------------------------------------
// DEVIATIONS FROM THE LOOK TARGET -- all five deliberate.
//
// 1. THE `IGNITION` CAPTION IS PAPER, NOT #ffd23e. The preview flashes that word
//    in yellow-gold. §11 reserves amber for EXERCISE traffic on the grounds that
//    "a colour that appears anywhere else stops carrying that", and #ffd23e is
//    not the amber token but is indistinguishable from it as a word on a lower
//    third at arm's length. The word already changes; it does not also need to
//    change colour into the fire range. Fire itself is unaffected -- the plume
//    keeps #ffd23e, because a plume cannot be mistaken for a mode indicator.
//    One line to revert if that reads as over-caution on glass.
//
// 2. NO SILO / GROUND CAMERA. The preview's LIFTOFF phase is a side-on hot
//    launch -- ground plane, silo mouth, billowing smoke, camera shake -- and it
//    is a second camera with a second art set. This module opens already
//    airborne. Largest remaining gap between rig and look target.
//
// 3. THE WORLD MAP IS NOT PORTED. The preview carries six coastline arrays, an
//    equirectangular projection and a minimum-energy Lambert time-of-flight
//    solution. §7 puts full map rendering after v1, and a flight director is the
//    wrong home for map data; the match cut needs the DOT to agree, not the
//    coastlines. The data stays in the reference for whoever builds that surface.
//
// 4. ALPHA IS APPROXIMATED. A 16bpp sprite has no per-pixel alpha and every
//    blend would be a read-modify-write. Near-opaque fills (the cloud puffs, at
//    0.92-0.97) are drawn solid; translucent washes (the detonation halo, the
//    shock ring) are PRE-BLENDED against the colour they sit on; the two
//    full-screen white flashes collapse to a shrinking disc instead of fading.
//
// 5. NO PRE-LAUNCH OR CREDITS PHASES. IDLE/ARRIVAL/DECODE/COMMIT/COUNTDOWN/KEY/
//    TCD and CREDITS are game UI reading game state, which this module is
//    defined as not having. They belong to whatever builds the launch face.
// ---------------------------------------------------------------------------
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
// Values are the look target's, name for name. The comment after each is the
// identifier it has in the preview, so the two can be diffed by eye.
// ---------------------------------------------------------------------------
namespace pal {
// The four accents (§11).
inline uint32_t Red()      { return lgfx::color888(0xFF, 0x3B, 0x30); } // RED
inline uint32_t RedDim()   { return lgfx::color888(0x3A, 0x10, 0x0C); } // RED_DIM
inline uint32_t Paper()    { return lgfx::color888(0xEC, 0xE7, 0xD6); } // PAPER
inline uint32_t Green()    { return lgfx::color888(0x3D, 0xDC, 0x84); } // GREEN
inline uint32_t GreenDim() { return lgfx::color888(0x1D, 0x7A, 0x4A); } // GREEN_DIM
inline uint32_t Brass()    { return lgfx::color888(0xC9, 0xA1, 0x5C); } // BRASS
inline uint32_t Grey()     { return lgfx::color888(0x7A, 0x7D, 0x86); } // GREY
inline uint32_t Space()    { return lgfx::color888(0x00, 0x00, 0x00); } // bg()

// Earth limb. The preview's radial runs #123a56 -> #2e7fae @0.82 ->
// #9fd4e8 @0.95 -> #03070c @1.0: a deep ocean core, a bright atmospheric rim,
// and a dark edge where the air runs out. The rim is the whole trick -- without
// it the limb is a blue shape, with it the limb is a planet.
inline uint32_t EarthCore() { return lgfx::color888(0x12, 0x3A, 0x56); }
inline uint32_t EarthMid()  { return lgfx::color888(0x2E, 0x7F, 0xAE); }
inline uint32_t EarthRim()  { return lgfx::color888(0x9F, 0xD4, 0xE8); }
inline uint32_t EarthEdge() { return lgfx::color888(0x03, 0x07, 0x0C); }
inline uint32_t LimbCloud() { return lgfx::color888(0x38, 0x5A, 0x72); } // 0.18 white, pre-blended
inline uint32_t Star()      { return lgfx::color888(0xCF, 0xD6, 0xDD); }

// Vehicle. OLIVE with tan interstage bands -- not the grey-white this module
// first guessed at. The bands are what make the stack read as three stacked
// stages rather than one tube, at a body width of nine pixels.
inline uint32_t Stage()    { return lgfx::color888(0x6F, 0x6E, 0x52); }
inline uint32_t StageAlt() { return lgfx::color888(0x7A, 0x77, 0x58); }
inline uint32_t Band()     { return lgfx::color888(0xB8, 0xA0, 0x6A); }
inline uint32_t Bus()      { return lgfx::color888(0x8A, 0x86, 0x72); }
inline uint32_t Shroud()   { return lgfx::color888(0x3B, 0x3C, 0x38); }
inline uint32_t Throat()   { return lgfx::color888(0x1C, 0x1D, 0x1A); }
inline uint32_t Shell()    { return lgfx::color888(0x4A, 0x4B, 0x45); }
inline uint32_t Decoy()    { return lgfx::color888(0x55, 0x56, 0x4E); }
inline uint32_t RvBody()   { return lgfx::color888(0x2B, 0x2C, 0x28); }
inline uint32_t Rcs()      { return lgfx::color888(0xBE, 0xE1, 0xFF); } // §11: BLUE porcupine
inline uint32_t Gas()      { return lgfx::color888(0x8A, 0x8C, 0x8E); } // vented gas, pre-blended

// Plume (§11 is silent; these are the preview's).
inline uint32_t FlameOuter() { return lgfx::color888(0xFF, 0x7A, 0x29); }
inline uint32_t FlameMid()   { return lgfx::color888(0xFF, 0xD2, 0x3E); }
inline uint32_t FlameCore()  { return lgfx::color888(0xFF, 0xF6, 0xE0); }
inline uint32_t Ember()      { return lgfx::color888(0xFF, 0xAA, 0x46); }
inline uint32_t FlashCore()  { return lgfx::color888(0xFF, 0xFA, 0xE6); }
inline uint32_t FlashMid()   { return lgfx::color888(0xFF, 0xAA, 0x3C); }
inline uint32_t Streak()     { return lgfx::color888(0xFF, 0xEB, 0xD2); } // anamorphic flare

// Reentry.
inline uint32_t SkyHigh()   { return lgfx::color888(0x02, 0x03, 0x0A); }
inline uint32_t SkyMid()    { return lgfx::color888(0x0B, 0x20, 0x36); }
inline uint32_t SkyLow()    { return lgfx::color888(0x2E, 0x5A, 0x7A); }
inline uint32_t SkyHaze()   { return lgfx::color888(0x9F, 0xB8, 0xC8); }
inline uint32_t CloudDeck() { return lgfx::color888(0xE4, 0xEA, 0xEE); }
inline uint32_t Plasma()    { return lgfx::color888(0xFF, 0x96, 0x3C); }
inline uint32_t PlasmaCore(){ return lgfx::color888(0xFF, 0xF2, 0xD7); }

// Map (the far side of the match cut).
inline uint32_t MapLand() { return lgfx::color888(0x0C, 0x20, 0x13); }
inline uint32_t MapGrid() { return lgfx::color888(0x0E, 0x2A, 0x1C); }
} // namespace pal

// ---------------------------------------------------------------------------
// DETONATION FIRE -- Plumbbob Hood / Upshot-Knothole Badger (§11, §15).
//
// Deliberately SEPARATE from the chrome palette above and deliberately not
// reachable from any chrome path. This is incandescence -- a fireball rendered
// from AEC test photography -- not an accent colour. It appears for the eleven
// seconds of one beat and never on a HUD, a label or a state indicator.
//
// That distinction is the whole reason it is allowed to contain warm hues at
// all while §11's amber reservation holds: amber signals EXERCISE because amber
// is *chrome*, and a fireball cannot be mistaken for a mode indicator. None of
// these values is the amber token.
//
// FOUR ramps, not one, because the cloud has four parts that cool at different
// rates and a single ramp renders a generic fireball. Each is a three-stop
// radial (core, mid, edge) with a hot and a cool end; the beat lerps between
// them, which is §11's "cooling to rust".
// ---------------------------------------------------------------------------
struct Rgb { uint8_t r, g, b; };

/* cap    -- the inner billows at the top of the head, hottest and last to cool */
const Rgb kCapHot[3]  = {{0xFF,0xF9,0xE3},{0xFF,0xD2,0x3E},{0xE0,0x61,0x1A}};
const Rgb kCapCool[3] = {{0xFF,0xB0,0x66},{0xD9,0x6A,0x1A},{0x7A,0x24,0x08}};
/* crown  -- the churned outer ring of the head */
const Rgb kCrnHot[3]  = {{0xFF,0xC2,0x3E},{0xE0,0x7A,0x1F},{0x7A,0x24,0x08}};
const Rgb kCrnCool[3] = {{0xE0,0x8A,0x3C},{0xA0,0x48,0x12},{0x4A,0x14,0x04}};
/* stem   -- the fluted column, lit from within */
const Rgb kStmHot[3]  = {{0xFF,0xD7,0x6A},{0xE8,0x82,0x1F},{0x7A,0x2D,0x0A}};
const Rgb kStmCool[3] = {{0xE0,0x9A,0x4A},{0xA0,0x4E,0x14},{0x4A,0x18,0x06}};
/* skirt  -- the ground dust roll, dirtiest and coolest */
const Rgb kSktHot[3]  = {{0xE0,0x7A,0x1F},{0x9C,0x3A,0x10},{0x3A,0x12,0x06}};
const Rgb kSktCool[3] = {{0xA0,0x4E,0x18},{0x5C,0x22,0x0A},{0x20,0x0A,0x04}};

/**
 * Detonation cost levers, IN MEASURED ORDER OF EFFECT.
 *
 * §11 is explicit that the fireball is full-screen, so the fix for a slow
 * detonation is always a cheaper cloud and never a smaller one. The bench run of
 * 2026-08-06 put numbers on which "cheaper" actually pays:
 *
 *   kHaloRings  10 -> 5   -19 ms   the halo was the whole problem (see DrawDetonation)
 *   kPuffRings   5 -> 4    -4 ms   costs the billows some internal shading
 *   kCrownPuffs 16 -> 10   -3 ms   below ~10 the head starts reading as a dome
 *
 * That order is the opposite of what this file assumed before it was measured:
 * the 46 billows are only about a fifth of the pixels the halo was spending on a
 * wash you can barely see. Do not re-guess it; re-measure it.
 */
constexpr int kHaloRings = 4;
constexpr int kPuffRings = 5;

/* Puff counts. The preview's, unchanged -- pre-emptively trimming them would be
 * assuming what the bus can do, which is the one thing this rig exists not to
 * do. If the beat misses its bar these are the numbers to cut, in this order:
 * crown, stem, skirt, cap. */
constexpr int kCapPuffs   = 8;
constexpr int kCrownPuffs = 16;
constexpr int kStemPuffs  = 14;
constexpr int kSkirtPuffs = 8;

inline uint32_t Rgb2c(const Rgb& c) { return lgfx::color888(c.r, c.g, c.b); }

inline Rgb MixRgb(const Rgb& a, const Rgb& b, float f)
{
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    return {(uint8_t)(a.r + (b.r - a.r) * f),
            (uint8_t)(a.g + (b.g - a.g) * f),
            (uint8_t)(a.b + (b.b - a.b) * f)};
}

/** Pre-blend `fg` over `bg` at `a` -- the stand-in for the alpha we do not have. */
inline uint32_t Over(const Rgb& fg, const Rgb& bg, float a) { return Rgb2c(MixRgb(bg, fg, a)); }

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
//
// The compressed column is the look target's own phase lengths where it has
// them (FLIGHT 12 s, REENTRY 8 s, DETONATION 11 s, CREDITS 6 s) -- those were
// tuned against a real preview and there is no reason to re-guess them.
// ---------------------------------------------------------------------------
struct BeatSpec {
    const char* name;
    uint32_t    trueMs;
    uint32_t    compressedMs;
};

/*
 * TRUE MARKS, and where each comes from.
 *
 * The look target's ascent table carries the Northrop Grumman 2007
 * flight-sequence numbers, which are more precise than §12's row and agree with
 * it: §12 says "stage 1 ~60 s, stage 3 ~120 s, post-boost ~180 s"; the preview
 * says stage 1 separation at T+62 and stage 2 separation (so stage 3 alight) at
 * T+123. No conflict -- the preview refines two of the three and §12 supplies
 * the third.
 *
 * Note SHROUD sits TWO SECONDS before STAGE 2 SEP (T+121 / T+123), which is the
 * published order and not the ~T+85 this table first guessed at. The halves are
 * therefore still in frame during the next beat; see shroudLife_.
 *
 * Impact is the 9,700 km validation case: 31.6 min = 1,896 s. Terminal
 * re-escalates 90 s earlier at T+1,806, exactly as §7 requires, and the
 * detonation begins on the impact second.
 */
const BeatSpec kBeats[(int)Beat::COUNT] = {
    /* Ignition     */ {"IGNITION",      62000,  6000},  // T+0   -> T+62
    /* Stage1Sep    */ {"STAGE 1 SEP",    3000,  3000},  // T+62  -- coast is 1 s in both
    /* Stage2Burn   */ {"STAGE 2",       56000,  5000},  // T+65  -> T+121
    /* ShroudEject  */ {"SHROUD",         2000,  2500},  // T+121 -> T+123
    /* Stage2Sep    */ {"STAGE 2 SEP",    3000,  3000},  // T+123
    /* Stage3Burn   */ {"STAGE 3",       51000,  5000},  // T+126 -> T+177
    /* Stage3Sep    */ {"STAGE 3 SEP",    3000,  3000},  // T+177
    /* PostBoost    */ {"POST-BOOST",    25000,  6000},  // T+180
    /* PitchOver    */ {"PSRE PITCH",    20000,  6000},  // T+205
    /* RvRelease    */ {"RV RELEASE",     8000,  6000},  // T+225
    /* BusBackaway  */ {"BUS BACKAWAY",  12000,  5000},  // T+233
    /* PenaidDeploy */ {"PENAIDS",       15000,  5000},  // T+245
    /* Midcourse    */ {"MIDCOURSE",   1546000, 12000},  // T+260 -> T+1806: 26 min -> 12 s
    /* Reentry      */ {"REENTRY",       90000,  8000},  // T+1806 (impact - 90 s)
    /* Detonation   */ {"DETONATION",    11000, 11000},  // T+1896 (impact)
    /* MatchCut     */ {"MATCH CUT",      8000,  6000},
};

/**
 * The lower third.
 *
 * The reference video narrates the ascent, and it is the reason a 62-second
 * first stage is not 62 seconds of nothing: four captions land inside it. These
 * are the published telemetry marks that change no art, which is precisely why
 * they are captions and not beats.
 *
 * Absolute true-time marks, so the track is mode-independent -- TPlusMs() is
 * always the published mark (see below) and the caption follows it in both
 * modes without a second timeline to keep in sync.
 *
 * The separator is '-' rather than the preview's middle dot: the built-in font
 * is ASCII and a missing glyph is worse than a plain one.
 */
struct Caption { uint32_t trueMs; const char* l1; const char* l2; };
const Caption kCaptions[] = {
    {      0, "T-0 - STAGE 1 IGNITION",      ""},
    {   1800, "LIFTOFF",                     ""},
    {   3000, "T+3 - PITCH MANEUVER",        ""},
    {  10000, "T+10 - FIRST ROLL MANEUVER",  ""},
    {  19000, "T+19 - MACH 1",               "1,100 FT/SEC - 8,300 FT ALT"},
    {  39000, "T+39 - MACH 3",               "38,000 FT ALT"},
    {  45000, "T+45 - SECOND ROLL MANEUVER", "50,000 FT ALT"},
    {  62000, "T+62 - STAGE 1 SEPARATION",   "18 NAUTICAL MILES"},
    {  78000, "T+78 - ORDNANCE IGNITES",     "STAGE 1-2 SKIRT SEPARATION"},
    { 121000, "T+121 - SHROUD JETTISONED",   "315,000 FT ALT"},
    { 123000, "T+123 - STAGE 2 SEPARATION",  "90 NM - 240,000 FT ALT"},
    { 150000, "PSRE PREPARED FOR OPERATION", ""},
    { 177000, "STAGE 3 SEPARATION",          ""},
    { 180000, "POST BOOST FLIGHT",           "MANEUVER TO WINDOW IN SPACE"},
    { 205000, "RV POSITIONED",               "THRUST TERMINATED"},
    { 225000, "RV AWAY",                     "BUS BACKS AWAY"},
    { 245000, "PENAIDS DEPLOYED",            "DECOYS - CHAFF"},
    { 260000, "MIDCOURSE - BALLISTIC",       ""},
    {1806000, "TERMINAL - REENTRY",          ""},
    {1896000, "IMPACT",                      ""},
};
constexpr int kCaptionCount = sizeof(kCaptions) / sizeof(kCaptions[0]);

/**
 * Kinematic anchors, one row per beat.
 *
 * A TABLE RATHER THAN A SIMULATION, on purpose: this is a rig for tuning art,
 * and tuning must be editing numbers in one visible place. A physics model
 * would be more defensible and far less adjustable, and nothing here is trying
 * to be right -- §7 already owns the only number that has to be (TOF, from the
 * Lambert solution).
 *
 * TWO THINGS THE LOOK TARGET CORRECTED HERE, both of which had been invented:
 *
 *   * THE VEHICLE BARELY MOVES. It drifts from x=114 to x=134 and holds near the
 *     vertical centre for the entire ascent; the EARTH does the moving. §11:
 *     "the horizon dropping away is what sells altitude on a screen with no
 *     other scale cue." A vehicle that also climbs the frame makes the limb
 *     redundant and runs out of screen by stage three.
 *   * THE SHRINK BELONGS AT THE END. Scale holds at 1.0 through penaid deploy
 *     and collapses only across midcourse, because §11's rule is that "the
 *     ascent ENDS by shrinking the vehicle to a single dot" -- shrinking all the
 *     way through just makes it small early.
 *
 * angle: 0 = nose straight up, +90 = nose along the horizon, >90 = NOSE DOWN.
 * §11: "PSRE pitch-over continues the arc nose-down -- downrange velocity is
 * conserved, so the RV releases in the direction of travel. Getting this
 * backwards is the tell that an animation was drawn rather than reasoned."
 */
struct Anchor { float x0, y0, x1, y1, a0, a1, alt0, alt1, s0, s1; };
const Anchor kAnchors[(int)Beat::COUNT] = {
    /* Ignition     */ {114, 132, 117, 128,   0,  25, 0.00f, 0.22f, 1.00f, 1.00f},
    /* Stage1Sep    */ {117, 128, 118, 127,  25,  28, 0.22f, 0.25f, 1.00f, 1.00f},
    /* Stage2Burn   */ {118, 127, 124, 124,  28,  36, 0.25f, 0.58f, 1.00f, 1.00f},
    /* ShroudEject  */ {124, 124, 125, 123,  36,  37, 0.58f, 0.61f, 1.00f, 1.00f},
    /* Stage2Sep    */ {125, 123, 126, 123,  37,  38, 0.61f, 0.63f, 1.00f, 1.00f},
    /* Stage3Burn   */ {126, 123, 130, 121,  38,  46, 0.63f, 0.82f, 1.00f, 1.00f},
    /* Stage3Sep    */ {130, 121, 131, 121,  46,  48, 0.82f, 0.84f, 1.00f, 1.00f},
    /* PostBoost    */ {131, 121, 133, 120,  48,  52, 0.84f, 0.87f, 1.00f, 1.00f},
    /* PitchOver    */ {133, 120, 134, 120,  52, 150, 0.87f, 0.90f, 1.00f, 1.00f}, // through horizontal, nose-down
    /* RvRelease    */ {134, 120, 134, 120, 150, 151, 0.90f, 0.91f, 1.00f, 1.00f},
    /* BusBackaway  */ {134, 120, 135, 120, 151, 152, 0.91f, 0.92f, 1.00f, 1.00f},
    /* PenaidDeploy */ {135, 120, 136, 120, 152, 153, 0.92f, 0.93f, 1.00f, 1.00f},
    /* Midcourse    */ {136, 120, 120, 120, 153, 155, 0.93f, 0.96f, 1.00f, 0.05f}, // ends as A DOT
    /* Reentry      */ { 46,  42, 174, 190, 155, 168, 0.96f, 0.20f, 0.05f, 0.34f},
    /* Detonation   */ {120, 176, 120, 176, 168, 168, 0.20f, 0.10f, 0.34f, 0.34f},
    /* MatchCut     */ {120, 120, 120, 120, 155, 155, 0.96f, 0.96f, 0.05f, 0.05f},
};

/* Stack geometry, nose to tail, straight off the look target: stage 3 is the
 * short one at the top and stage 1 the long one at the tail. The tan band at
 * each joint is what sells three stages at a nine-pixel body width. */
struct Segment { float len, wide; };
const Segment kSegments[3] = {{14, 9}, {16, 10}, {22, 11}}; // stage 3, 2, 1

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

/** Deterministic hash-noise, so a replayed beat draws the identical frame. */
inline float Noise(uint32_t seed)
{
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return (float)(seed & 0xFFFF) / 65535.0f;
}

/**
 * Body-frame to screen.
 *
 * `along` runs nose -> tail down the flight line, `across` is to its right.
 * Every part of the vehicle is placed through this one function so that a
 * change of attitude cannot leave a nozzle behind.
 */
inline void Axis(float cx, float cy, float r, float along, float across, float& X, float& Y)
{
    X = cx + sinf(r) * along + cosf(r) * across;
    Y = cy + cosf(r) * along - sinf(r) * across;
}

/** A rotated rectangle, as two triangles. */
void FillQuad(LovyanGFX& g, float cx, float cy, float r,
              float a0, float a1, float halfW, int dy, uint32_t col)
{
    float x0, y0, x1, y1, x2, y2, x3, y3;
    Axis(cx, cy, r, a0, -halfW, x0, y0);
    Axis(cx, cy, r, a0,  halfW, x1, y1);
    Axis(cx, cy, r, a1,  halfW, x2, y2);
    Axis(cx, cy, r, a1, -halfW, x3, y3);
    g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, col);
    g.fillTriangle((int)x0, (int)y0 - dy, (int)x2, (int)y2 - dy, (int)x3, (int)y3 - dy, col);
}

/**
 * One cloud billow: a shaded disc with its highlight up and to the left, which
 * is the preview's offset radial gradient rendered with the primitives this
 * panel has. Concentric rather than per-pixel because a 46-puff cloud cannot
 * afford a square root per pixel.
 */
void Puff(LovyanGFX& g, float x, float y, float r, int dy,
          const Rgb* hot, const Rgb* cool, float c)
{
    if (r < 1.0f) return;
    const Rgb core = MixRgb(hot[0], cool[0], c);
    const Rgb mid  = MixRgb(hot[1], cool[1], c);
    const Rgb edge = MixRgb(hot[2], cool[2], c);
    for (int i = kPuffRings - 1; i >= 0; --i) {
        const float f  = (float)i / (float)(kPuffRings - 1); // 1 = rim, 0 = core
        const int   rr = (int)(r * (0.20f + 0.80f * f));
        if (rr < 1) continue;
        const Rgb col = (f > 0.55f) ? MixRgb(mid, edge, (f - 0.55f) / 0.45f)
                                    : MixRgb(core, mid, f / 0.55f);
        g.fillCircle((int)(x - r * 0.24f * (1.0f - f)),
                     (int)(y - r * 0.28f * (1.0f - f)) - dy, rr, Rgb2c(col));
    }
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

    const bool isSep = (b == Beat::Stage1Sep || b == Beat::Stage2Sep || b == Beat::Stage3Sep);

    // The joint, not the vehicle's origin: a separation happens where the two
    // stages part, which is the far end of whatever REMAINS attached. Putting
    // the flash at vx_/vy_ instead would light it up at the payload. kSegments
    // runs nose -> tail, so "what remains" is the first `after` entries; at the
    // last separation nothing remains and the joint is the bus itself.
    const int after = (b == Beat::Stage1Sep) ? 2 : (b == Beat::Stage2Sep ? 1 : 0);
    float stack = 0;
    for (int i = 0; i < after; ++i) stack += kSegments[i].len;
    const float r = angleDeg_ * 0.01745f;
    Axis(vx_, vy_, r, stack + 2.0f, 0.0f, stageX_, stageY_);

    // Separations throw embers and a spent stage along the flight line (§11:
    // AXIAL, "the spent stage receding on the flight line rather than tumbling
    // off sideways"). Seeded per beat so a replay is identical.
    stageLife_ = isSep ? 1.0f : 0.0f;
    for (int i = 0; i < kEmbers; ++i) {
        if (!isSep) { embers_[i].life = 0; continue; }
        const float n1 = Noise((uint32_t)b * 977u + i * 31u + 7u);
        const float n2 = Noise((uint32_t)b * 613u + i * 57u + 11u);
        const float rad = (n1 - 0.5f) * 1.4f;                 // narrow cone, not a sphere
        const float spd = 0.35f + n2 * 0.9f;
        embers_[i] = {stageX_, stageY_, sinf(rad) * spd, spd * 0.9f, 1.0f};
    }

    if (b == Beat::ShroudEject) shroudLife_ = 1.0f;
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
    // Reentry accelerates: the preview eases it as f*f, and a linear descent
    // reads as a glide rather than as something falling.
    float p = BeatProgress();
    if (beat_ == Beat::Reentry) p = p * p;

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
        stageX_ += sinf(r) * 0.9f * dt;
        stageY_ += cosf(r) * 0.9f * dt;
        stageLife_ -= 0.012f * dt;
    }
    if (shroudLife_ > 0) shroudLife_ -= 0.006f * dt;
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
        busX_ += sinf(r) * 0.22f * dt;
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
        // Clamshell halves outlive their own beat by design (T+121 -> T+123).
        // max(), not assignment: entering ShroudEject is what ARMS them, and
        // clobbering that with the old value would delete the shroud entirely.
        const float keepShroud = shroudLife_;
        EnterBeat((Beat)next);
        if (keepShroud > shroudLife_) shroudLife_ = keepShroud;
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

void Director::CurrentCaption(const char*& line1, const char*& line2) const
{
    line1 = ""; line2 = "";

    // The staging beat overrides the track, because during those three seconds
    // the caption IS the direction: §11's coast is "the pause where a real
    // vehicle is committed and not yet accelerating", and naming it is what
    // stops a viewer reading an unlit engine as a dropped frame.
    const bool isSep = (beat_ == Beat::Stage1Sep || beat_ == Beat::Stage2Sep || beat_ == Beat::Stage3Sep);
    if (isSep) {
        if (beatElapsedMs_ < 1000)      { line1 = "SEPARATION - COAST"; return; }
        else if (beatElapsedMs_ < 1800) { line1 = "IGNITION";           return; }
    }

    const uint32_t t = TPlusMs();
    for (int i = kCaptionCount - 1; i >= 0; --i) {
        if (t >= kCaptions[i].trueMs) { line1 = kCaptions[i].l1; line2 = kCaptions[i].l2; return; }
    }
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
    const float b = Clamp01(altitude_ * 1.4f);
    for (int i = 0; i < n; ++i) {
        const int   x = (int)(Noise(i * 7919u + 13u) * screen_);
        const int   y = (int)(Noise(i * 6271u + 29u) * screen_ * 0.8f);
        const float v = b * (0.2f + 0.5f * Noise(i * 104729u));
        g.drawPixel(x, y - dy, lgfx::color888((uint8_t)(0xCF * v), (uint8_t)(0xD6 * v), (uint8_t)(0xDD * v)));
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
    //
    // The colour ramp down from the rim is the look target's radial, flattened
    // into a depth ramp: a bright atmospheric rim over deepening ocean. Four
    // spans per column rather than a per-pixel gradient -- at this depth the
    // banding is invisible and the cost is a quarter.
    const float u    = screen_ / 240.0f;
    const float R    = Lerp(5000.0f, 200.0f, Clamp01(altitude_)) * u;
    const float topY = Lerp(196.0f, 238.0f, Clamp01(altitude_)) * u;
    const float cy   = topY + R;
    const float cx   = screen_ * 0.5f;

    const int rim = (int)(3 * u + 1);
    const int mid = (int)(9 * u + 1);

    for (int x = 0; x < screen_; ++x) {
        const float ddx  = (float)x - cx;
        const float under = R * R - ddx * ddx;
        if (under <= 0) continue;
        const int ly = (int)(cy - sqrtf(under));
        if (ly >= screen_) continue;

        g.drawPixel(x, ly - rim - 1 - dy, pal::EarthEdge());          // where the air runs out
        g.drawFastVLine(x, ly - rim - dy, rim, pal::EarthRim());      // the lit atmosphere
        g.drawFastVLine(x, ly - dy, mid, pal::EarthMid());
        if (ly + mid < screen_) {
            g.drawFastVLine(x, ly + mid - dy, screen_ - ly - mid, pal::EarthCore());
        }
    }

    // Sparse cloud on the limb -- seven ellipses along the crown. Scale cue and
    // almost free; without it the ocean is a flat colour.
    for (int i = 0; i < 7; ++i) {
        const float a  = -1.35f + i * 0.11f;
        const int   ex = (int)(cx + cosf(a) * R * 0.92f);
        const int   ey = (int)(cy + sinf(a) * R * 0.92f);
        if (ey - dy < -10 || ey - dy > screen_ + 10) continue;
        g.fillEllipse(ex, ey - dy, (int)(14 * u), (int)(4 * u), pal::LimbCloud());
    }
}

void Director::DrawCaption(LovyanGFX& g, int dy) const
{
    const char *l1 = "", *l2 = "";
    CurrentCaption(l1, l2);
    if (!l1 || !*l1) return;

    const float u = screen_ / 240.0f;
    g.setTextSize(1);
    g.setTextDatum(textdatum_t::top_center);

    // THE ROWS ARE HIGHER THAN THE PREVIEW'S, and this is arithmetic rather than
    // taste. THE FACE IS ROUND. The preview draws its lower third at y=206/220
    // on a canvas that clips to the circle; on glass that clip eats the ends of
    // real words. The longest caption on either line is 27 characters, which is
    // 162 px of ink in the built-in font, and the chord of a 240 px circle is:
    //
    //     y=192 -> 192 px      y=204 -> 171 px
    //     y=206 -> 167 px      y=216 -> 144 px   <- "MANEUVER TO WINDOW IN
    //                                               SPACE" loses 18 px, 9 a side
    //
    // So 192/204: the widest line still clears by 9 px, and the block still sits
    // in the bottom fifth where a lower third belongs. Any caption added to
    // kCaptions longer than 27 characters breaks this and must be shortened.
    //
    // Paper for the headline, grey for the telemetry under it -- the preview's
    // hierarchy. See DEVIATION 1 for why IGNITION is not yellow here.
    g.setTextColor(pal::Paper());
    g.drawString(l1, screen_ / 2, (int)(192 * u) - dy);
    if (l2 && *l2) {
        g.setTextColor(pal::Grey());
        g.drawString(l2, screen_ / 2, (int)(204 * u) - dy);
    }
    g.setTextDatum(textdatum_t::top_left);
}

void Director::DrawPlume(LovyanGFX& g, int dy) const
{
    // Lit only when a motor is actually burning. The staging beat's ~1 s coast
    // is defined by this being OFF (see DrawVehicle's bell).
    bool burning = false;
    bool justLit = false;
    int  remaining = 3;
    switch (beat_) {
        case Beat::Ignition:    burning = true; remaining = 3; break;
        case Beat::Stage2Burn:
        case Beat::ShroudEject: burning = true; remaining = 2; break;
        case Beat::Stage3Burn:  burning = true; remaining = 1; break;
        // burnout -> sep -> 1 s coast -> IGNITION -> burn, inside one beat.
        case Beat::Stage1Sep:
            burning = beatElapsedMs_ > 1000; remaining = 2;
            justLit = burning && beatElapsedMs_ < 1300;
            break;
        case Beat::Stage2Sep:
            burning = beatElapsedMs_ > 1000; remaining = 1;
            justLit = burning && beatElapsedMs_ < 1300;
            break;
        // STAGE 3 SEP IS THE ONE STAGING BEAT WITH NO IGNITION AT THE END.
        // Nothing is left to light: what takes over is the post-boost bus, which
        // is cold gas. §11's coast still applies -- the unlit bell is still the
        // beat -- but a main plume here would be a fourth motor the vehicle does
        // not have. DrawRcs picks it up after the coast instead.
        default: burning = false; break;
    }
    if (!burning) return;

    const float u = screen_ / 240.0f;
    const float r = angleDeg_ * 0.01745f;

    // Stack length: the plume comes out of the tail of whatever is still
    // attached, and kSegments runs nose -> tail.
    float stack = 0;
    for (int i = 0; i < remaining; ++i) stack += kSegments[i].len;
    const float tail = (stack + 4.0f) * scale_ * u;

    if (justLit) {
        // The ignition burst at the bell -- the payoff the coast sets up.
        const float ia = 1.0f - (beatElapsedMs_ - 1000) / 300.0f;
        float bx, by; Axis(vx_, vy_, r, tail, 0, bx, by);
        for (int i = 3; i >= 1; --i) {
            g.fillCircle((int)bx, (int)by - dy, (int)(14 * u * ia * i / 3.0f),
                         i == 1 ? pal::FlashCore() : pal::FlashMid());
        }
    }

    const float flick = 0.75f + 0.25f * Noise(beatElapsedMs_ / 40u + 3u);
    const float len   = (20.0f + 10.0f * flick) * scale_ * u;

    // Three nested teardrops, widest and coolest outside. Triangles rather than
    // quadratics: at this size the curve is two pixels of difference.
    const struct { float w, l; uint32_t c; } layers[3] = {
        {12.0f, 1.00f, pal::FlameOuter()},
        { 8.0f, 0.70f, pal::FlameMid()},
        { 5.0f, 0.42f, pal::FlameCore()},
    };
    for (int i = 0; i < 3; ++i) {
        const float hw = layers[i].w * 0.5f * scale_ * u;
        float x0, y0, x1, y1, x2, y2;
        Axis(vx_, vy_, r, tail, -hw, x0, y0);
        Axis(vx_, vy_, r, tail,  hw, x1, y1);
        Axis(vx_, vy_, r, tail + len * layers[i].l, 0, x2, y2);
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, layers[i].c);
    }
}

void Director::DrawVehicle(LovyanGFX& g, int dy) const
{
    if (beat_ >= Beat::RvRelease) return; // from here the RV and bus are drawn separately

    const float u = screen_ / 240.0f;
    const float k = scale_ * u;
    const float r = angleDeg_ * 0.01745f;

    // How much stack is left. Stages leave AT the flash, a few hundred ms into
    // their beat -- not at the beat boundary, which would drop them silently one
    // frame early.
    int remaining = 3;
    switch (beat_) {
        case Beat::Ignition:    remaining = 3; break;
        case Beat::Stage1Sep:   remaining = beatElapsedMs_ < 300 ? 3 : 2; break;
        case Beat::Stage2Burn:
        case Beat::ShroudEject: remaining = 2; break;
        case Beat::Stage2Sep:   remaining = beatElapsedMs_ < 300 ? 2 : 1; break;
        case Beat::Stage3Burn:  remaining = 1; break;
        case Beat::Stage3Sep:   remaining = beatElapsedMs_ < 300 ? 1 : 0; break;
        default:                remaining = 0; break;
    }

    // Stages, nose to tail, each with its tan interstage band at the top.
    // kSegments IS in nose -> tail order, so what survives a separation is the
    // FIRST `remaining` entries -- stage 3 is nearest the payload and stage 1 is
    // the one at the tail that leaves first.
    float along = 0;
    for (int i = 0; i < remaining; ++i) {
        const Segment& s = kSegments[i];
        const float hw = s.wide * 0.5f * k;
        FillQuad(g, vx_, vy_, r, along * k, (along + s.len) * k, hw, dy,
                 (i == 1) ? pal::StageAlt() : pal::Stage());
        FillQuad(g, vx_, vy_, r, along * k, (along + 2.0f) * k, hw, dy, pal::Band());
        along += s.len;
    }

    // Post-boost bus, ahead of the stack.
    FillQuad(g, vx_, vy_, r, -8.0f * k, 0.0f, 4.5f * k, dy, pal::Bus());

    // Shroud (a long ogive) until it is jettisoned; after that the bare RV cone.
    const bool shroudOn = (beat_ < Beat::ShroudEject);
    {
        const float noseLen = shroudOn ? 14.0f : 9.0f;
        float x0, y0, x1, y1, x2, y2;
        Axis(vx_, vy_, r, -8.0f * k, -4.5f * k, x0, y0);
        Axis(vx_, vy_, r, -8.0f * k,  4.5f * k, x1, y1);
        Axis(vx_, vy_, r, (-8.0f - noseLen) * k, 0, x2, y2);
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, pal::Shroud());
    }

    // THE EXPOSED, UNLIT BELL. §11 calls the coast "the whole beat"; the bell is
    // what makes it legible -- the player sees an engine that is there and is
    // not firing, which is the suspense. It is drawn whenever a stage is
    // attached, and it STARS during the coast because nothing else is lit.
    if (remaining >= 1) {
        const float tail = along * k;
        float x0, y0, x1, y1, x2, y2, x3, y3;
        Axis(vx_, vy_, r, tail,             -3.0f * k, x0, y0);
        Axis(vx_, vy_, r, tail,              3.0f * k, x1, y1);
        Axis(vx_, vy_, r, tail + 4.0f * k,   4.5f * k, x2, y2);
        Axis(vx_, vy_, r, tail + 4.0f * k,  -4.5f * k, x3, y3);
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, pal::Shroud());
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x2, (int)y2 - dy, (int)x3, (int)y3 - dy, pal::Shroud());
        FillQuad(g, vx_, vy_, r, tail + 3.0f * k, tail + 4.0f * k, 3.0f * k, dy, pal::Throat());
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
    const int   x = (int)stageX_;
    const int   y = (int)stageY_ - dy;
    const int   half = (int)(screen_ * 0.28f * f);
    const uint8_t v = (uint8_t)(255 * f);

    g.drawFastHLine(x - half, y, half * 2, lgfx::color888(v, (uint8_t)(v * 0.92f), (uint8_t)(v * 0.82f)));
    if (f > 0.5f) {
        g.drawFastHLine(x - half / 2, y - 1, half, pal::Streak());
        g.drawFastHLine(x - half / 2, y + 1, half, pal::Streak());
    }
    g.fillCircle(x, y, (int)(11 * f), pal::FlashMid());
    g.fillCircle(x, y, (int)(6 * f), pal::FlashCore());
}

void Director::DrawDebris(LovyanGFX& g, int dy) const
{
    for (int i = 0; i < kEmbers; ++i) {
        const Ember& e = embers_[i];
        if (e.life <= 0) continue;
        g.drawPixel((int)e.x, (int)e.y - dy, pal::Ember());
    }
    if (stageLife_ > 0) {
        // The spent stage, receding on the flight line and shrinking. Still an
        // olive body with its tan band -- it is the stage that just left, and
        // recognising it as such is what makes the separation read.
        const float u = screen_ / 240.0f;
        const float r = angleDeg_ * 0.01745f;
        const float L = 14.0f * stageLife_ * scale_ * u;
        FillQuad(g, stageX_, stageY_, r, 0, L, L * 0.35f, dy, pal::Stage());
        FillQuad(g, stageX_, stageY_, r, 0, L * 0.14f, L * 0.35f, dy, pal::Band());
    }
}

void Director::DrawShroud(LovyanGFX& g, int dy) const
{
    if (shroudLife_ <= 0) return;
    // §11: CLAMSHELL HALVES. Two shells peeling forward and sideways off the
    // nose -- not a single cone popping off, which is a different vehicle's
    // shroud. They outlive their own beat: the shroud goes at T+121 and stage 2
    // separates at T+123, so they are still in frame for the next separation.
    const float u = screen_ / 240.0f;
    const float k = scale_ * u;
    const float r = angleDeg_ * 0.01745f;
    const float p = 1.0f - shroudLife_;

    float nx, ny;
    Axis(vx_, vy_, r, -22.0f * k, 0, nx, ny); // the nose they came off
    for (int side = -1; side <= 1; side += 2) {
        const float open = p * 30.0f * u;
        float hx, hy;
        Axis(nx, ny, r, -open * 0.9f, side * open, hx, hy);
        const float tilt = side * p * 2.2f;
        float ax, ay, bx, by, cx2, cy2;
        Axis(hx, hy, r + tilt,  6.0f * k, 0, ax, ay);
        Axis(hx, hy, r + tilt, -6.0f * k, 0, bx, by);
        Axis(hx, hy, r + tilt, 0, side * 5.0f * k, cx2, cy2);
        g.fillTriangle((int)ax, (int)ay - dy, (int)bx, (int)by - dy, (int)cx2, (int)cy2 - dy, pal::Shell());
    }
}

void Director::DrawRcs(LovyanGFX& g, int dy) const
{
    // Picks up where the last staging coast leaves off: stage 3 separates and
    // the cold-gas bus is what answers, not a fourth motor. See DrawPlume.
    const bool afterLastSep = (beat_ == Beat::Stage3Sep && beatElapsedMs_ > 1000);
    if (!afterLastSep && beat_ != Beat::PostBoost && beat_ != Beat::PitchOver) return;
    // §11: "post-boost = BLUE PORCUPINE RCS". Short quills in many directions,
    // pulsing -- the bus talking to itself. Blue because it is cold gas, and
    // because it is the one moment that must not read as a main engine.
    const float u = screen_ / 240.0f;
    const float r = angleDeg_ * 0.01745f;
    const int   n = 12;
    const float phase = (float)(beatElapsedMs_ % 700) / 700.0f;
    for (int i = 0; i < n; ++i) {
        const float a = r + (float)i * 6.2832f / n;
        const float pulse = Noise(i * 3571u + beatElapsedMs_ / 90u);
        if (pulse < 0.45f) continue;
        const float len = (4.0f + 6.0f * pulse) * u * (0.6f + 0.4f * sinf(phase * 6.2832f));
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
    const float u = screen_ / 240.0f;
    const float k = scale_ * u;
    const float r = angleDeg_ * 0.01745f;

    if (beat_ >= Beat::BusBackaway) {
        FillQuad(g, busX_, busY_, r, -4.0f * k, 4.0f * k, 4.5f * k, dy, pal::Bus());
        // Retro plumes point FORWARD -- it is thrusting against the direction of
        // travel to open the gap. Exhaust toward the RV, motion away from it.
        for (int i = -1; i <= 1; i += 2) {
            float ex, ey;
            Axis(busX_, busY_, r + i * 0.4f, -10.0f * u, 0, ex, ey);
            g.drawLine((int)busX_, (int)busY_ - dy, (int)ex, (int)ey - dy, pal::Rcs());
        }
    }

    // The RV: a small dart, nose along the direction of travel.
    float x0, y0, x1, y1, x2, y2;
    Axis(rvX_, rvY_, r, -10.0f * k, 0, x0, y0);
    Axis(rvX_, rvY_, r, 4.0f * k, -4.0f * k, x1, y1);
    Axis(rvX_, rvY_, r, 4.0f * k,  4.0f * k, x2, y2);
    g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, pal::RvBody());
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
            const float len = 14.0f * (1.0f - p.burn);
            g.drawLine((int)p.x, (int)p.y - dy, (int)p.x, (int)(p.y - len) - dy, pal::Plasma());
            g.fillCircle((int)p.x, (int)p.y - dy, 2, pal::Ember());
        } else {
            // Chaff and decoys leaving the backing bus: paper-coloured flecks
            // and two darker cones, per the look target.
            g.drawPixel((int)p.x, (int)p.y - dy, (i < 2) ? pal::Decoy() : pal::Paper());
        }
    }
}

void Director::DrawReentry(LovyanGFX& g, int dy) const
{
    if (beat_ != Beat::Reentry) return;
    const float u = screen_ / 240.0f;
    const float p = BeatProgress();

    // The sky STOPS BEING SPACE. This is the only beat where the ground comes
    // up to meet the camera, and the vertical ramp from #02030a to a hazy
    // #9fb8c8 is what says "atmosphere" without drawing one.
    const Rgb hi{0x02,0x03,0x0A}, md{0x0B,0x20,0x36}, lo{0x2E,0x5A,0x7A}, hz{0x9F,0xB8,0xC8};
    for (int y = 0; y < screen_; ++y) {
        const float f = (float)y / (float)screen_;
        Rgb c;
        if (f < 0.45f)      c = MixRgb(hi, md, f / 0.45f);
        else if (f < 0.80f) c = MixRgb(md, lo, (f - 0.45f) / 0.35f);
        else                c = MixRgb(lo, hz, (f - 0.80f) / 0.20f);
        g.drawFastHLine(0, y - dy, screen_, Rgb2c(c));
    }
    for (int i = 0; i < 40; ++i) { // the stars that are still above us
        const int y = (int)(Noise(i * 6271u + 29u) * screen_ * 0.8f);
        if (y > screen_ * 0.375f) continue;
        g.drawPixel((int)(Noise(i * 7919u + 13u) * screen_), y - dy, pal::Star());
    }
    for (int i = 0; i < 10; ++i) { // cloud deck, the last thing between RV and ground
        g.fillEllipse((int)((10 + i * 24) * u), (int)((206 + (i % 3) * 5) * u) - dy,
                      (int)(20 * u), (int)(7 * u), pal::CloudDeck());
    }

    // Plasma sheath + trail. The RV is running nose-first down its own track, so
    // the trail is drawn back up the flight line rather than straight up.
    const float r  = angleDeg_ * 0.01745f;
    const float heat = Clamp01(p * 1.3f);
    float tx, ty;
    Axis(vx_, vy_, r, -(18.0f + 38.0f * p) * u, 0, tx, ty);
    g.drawLine((int)tx, (int)ty - dy, (int)vx_, (int)vy_ - dy, pal::Plasma());

    for (int i = 4; i >= 1; --i) {
        const float f = (float)i / 4.0f;
        float ax, ay;
        Axis(vx_, vy_, r, (6.0f + 8.0f * f) * u, 0, ax, ay);
        g.fillCircle((int)ax, (int)ay - dy, (int)((2 + 7 * f * heat) * u),
                     i == 1 ? pal::PlasmaCore() : pal::Plasma());
    }
    float x0, y0, x1, y1, x2, y2;
    Axis(vx_, vy_, r, -7.0f * u, 0, x0, y0);
    Axis(vx_, vy_, r,  4.0f * u, -3.5f * u, x1, y1);
    Axis(vx_, vy_, r,  4.0f * u,  3.5f * u, x2, y2);
    g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, pal::RvBody());

    // Into the detonation on a white frame. The preview wipes to white over the
    // last 0.4 s and cuts; a shrinking disc is the alpha-free stand-in (see
    // DEVIATION 4) and on a round face it reads as the flash arriving.
    if (p > 0.95f) {
        const float f = (p - 0.95f) / 0.05f;
        g.fillCircle(screen_ / 2, screen_ / 2 - dy, (int)(screen_ * 0.75f * f),
                     lgfx::color888(0xFF, 0xFF, 0xFF));
    }
}

void Director::DrawDetonation(LovyanGFX& g, int dy) const
{
    if (beat_ != Beat::Detonation) return;

    // §11: Plumbbob Hood / Upshot-Knothole Badger palette, FULL-SCREEN, cooling
    // to rust. Full-screen is the direction: at this point the frame is the
    // event, and a fireball that politely stays inside a viewport is a firework.
    //
    // The camera is ON THE GROUND -- this is the one beat shot from outside the
    // vehicle, and it is why the look target draws a horizon here. A fireball
    // seen from space is a dot; a mushroom cloud needs something to be taller
    // than.
    const float u = screen_ / 240.0f;
    const float t = beatElapsedMs_ / 1000.0f;
    const float e = Smooth(Clamp01(t / 5.0f));                 // the rise
    const float cool = Clamp01((t - 5.5f) / 5.0f);             // incandescence fades late
    const float groundY = 206.0f * u;
    const float cx = screen_ * 0.5f;
    const float capCY = (150.0f - 72.0f * e) * u - max(0.0f, t - 5.0f) * 1.2f * u;
    const float capR  = (26.0f + 82.0f * e) * u + max(0.0f, t - 5.0f) * 0.8f * u;

    // Sky: black overhead, deep red toward the horizon (Badger).
    const Rgb skyTop{0x05,0x01,0x01};
    const Rgb skyMid = MixRgb({0x3A,0x0A,0x04}, {0x2A,0x05,0x03}, cool);
    const Rgb skyBot = MixRgb({0x7A,0x16,0x06}, {0x4A,0x0E,0x05}, cool);
    const int  horizon = (int)(groundY + 10 * u);
    for (int y = 0; y < horizon && y < screen_; ++y) {
        const float f = (float)y / (float)horizon;
        const Rgb c = (f < 0.55f) ? MixRgb(skyTop, skyMid, f / 0.55f)
                                  : MixRgb(skyMid, skyBot, (f - 0.55f) / 0.45f);
        g.drawFastHLine(0, y - dy, screen_, Rgb2c(c));
    }

    // Warm halo around the head (Hood). Pre-blended rings, outermost first.
    //
    // MEASURED 2026-08-06, and it is the reason this beat used to miss its bar.
    // The preview's halo is a radial to 1.9x capR, which at full rise is a 215 px
    // radius on a 240 px screen -- so ten of them was ~558k pixels of nearly
    // full-screen overdraw, four and a half times the cost of all 46 billows put
    // together. It read as a subtle warm wash and cost more than the fireball.
    //
    // Five rings to 1.35x is ~160k px. Same wash, 19 ms cheaper, and the outer
    // stops were mostly off-screen anyway.
    const float haloA = 0.34f * (1.0f - cool * 0.6f);
    for (int i = kHaloRings; i >= 1; --i) {
        const float f = (float)i / (float)kHaloRings;
        g.fillCircle((int)cx, (int)capCY - dy, (int)(capR * 1.35f * f),
                     Over({0xFF,0x8C,0x28}, skyMid, haloA * (1.0f - f) * 1.6f));
    }

    // Ground: dark rust, a lit rim, silhouetted brush.
    g.fillRect(0, (int)(groundY + 6 * u) - dy, screen_, screen_, lgfx::color888(0x16, 0x05, 0x03));
    g.fillRect(0, (int)(groundY + 6 * u) - dy, screen_, (int)max(1.0f, 2 * u),
               Over({0xFF,0x8C,0x32}, {0x16,0x05,0x03}, 0.5f * (1.0f - cool * 0.5f)));
    for (int i = 0; i < kSkirtPuffs; ++i) {
        const float x = (30 + i * 36) * u;
        g.fillTriangle((int)(x - 5 * u), (int)(groundY + 8 * u) - dy,
                       (int)x, (int)(groundY + (2 - (i % 3) * 2) * u) - dy,
                       (int)(x + 4 * u), (int)(groundY + 8 * u) - dy,
                       lgfx::color888(0x0A, 0x02, 0x02));
    }

    // Ground shock ring, racing out ahead of the dust.
    if (t > 0.35f && t < 2.6f) {
        const float rx = (t - 0.35f) * 150.0f * u;
        const float a  = max(0.0f, 1.0f - (t - 0.35f) / 2.2f);
        g.drawEllipse((int)cx, (int)(groundY + 4 * u) - dy, (int)rx, (int)max(1.0f, rx * 0.16f),
                      Over({0xFF,0xD2,0x8C}, {0x16,0x05,0x03}, a));
    }

    // The cloud, bottom up: dust skirt, stem, crown, inner cap.
    for (int i = 0; i < kSkirtPuffs; ++i) {
        const float o  = ((i - 3.5f) * 15.0f + (Noise(i * 811u + 1u) - 0.5f) * 8.0f) * u;
        const float jr = 0.7f + Noise(i * 409u + 2u) * 0.7f;
        Puff(g, cx + o * (0.6f + e), groundY - 4 * u, (17 + 20 * e) * u * jr, dy, kSktHot, kSktCool, cool);
    }
    const float stemTop = capCY + capR * 0.30f;
    for (int i = 0; i < kStemPuffs; ++i) {
        const float f  = (float)i / (float)(kStemPuffs - 1);
        const float o  = (Noise(i * 577u + 3u) - 0.5f) * 12.0f * u;
        const float jr = 0.7f + Noise(i * 953u + 4u) * 0.55f;
        const float y  = groundY - 8 * u - (groundY - 8 * u - stemTop) * f * e - 2 * u;
        const float wob = sinf(t * 0.9f + i * 1.7f) * 3.0f * e * u;
        Puff(g, cx + o * 0.55f + wob, y, (8 + 10 * e * (0.5f + f * 0.8f)) * u * jr, dy,
             kStmHot, kStmCool, cool);
    }
    // The head is a CHURNED CLUSTER, not a dome. A single filled dome is the
    // shape a cartoon mushroom has; the reference photographs are billows.
    for (int i = 0; i < kCrownPuffs; ++i) {
        const float a  = (float)i / kCrownPuffs * 6.2832f + Noise(i * 331u + 5u) * 0.2f
                       + t * (0.06f + Noise(i * 149u + 6u) * 0.12f) * ((i & 1) ? 1.0f : -1.0f);
        const float jr = 0.55f + Noise(i * 227u + 7u) * 0.7f;
        Puff(g, cx + cosf(a) * capR * 0.62f, capCY + sinf(a) * capR * 0.36f,
             capR * 0.30f * jr, dy, kCrnHot, kCrnCool, cool);
    }
    for (int i = 0; i < kCapPuffs; ++i) {
        const float a  = (float)i / kCapPuffs * 6.2832f + Noise(i * 691u + 8u) * 0.4f
                       + t * (0.05f + Noise(i * 83u + 9u) * 0.09f) * ((i & 1) ? -1.0f : 1.0f);
        const float jr = 0.6f + Noise(i * 313u + 10u) * 0.6f;
        Puff(g, cx + cosf(a) * capR * 0.30f, capCY - capR * 0.06f + sinf(a) * capR * 0.20f,
             capR * 0.28f * jr, dy, kCapHot, kCapCool, cool);
    }

    // White-hot core -- strong early, a floor until it finally goes.
    const float coreA = max(t < 8.0f ? 0.18f : 0.0f, 1.0f - cool);
    if (coreA > 0) {
        for (int i = 4; i >= 1; --i) {
            const float f = (float)i / 4.0f;
            g.fillCircle((int)cx, (int)(capCY - capR * 0.05f) - dy, (int)(capR * 0.55f * f),
                         Over(i == 1 ? Rgb{0xFF,0xFC,0xEB} : Rgb{0xFF,0xCD,0x5A},
                              MixRgb(kCapHot[0], kCapCool[0], cool), coreA * (1.0f - f * 0.6f)));
        }
    }

    // The detonation flash itself: white, then collapsing into the fireball.
    if (t < 0.20f) {
        g.fillScreen(lgfx::color888(0xFF, 0xFF, 0xFF));
    } else if (t < 0.5f) {
        const float f = 1.0f - (t - 0.20f) / 0.30f;
        g.fillCircle((int)cx, (int)capCY - dy, (int)(screen_ * 0.8f * f), lgfx::color888(0xFF, 0xFF, 0xFF));
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
    // function, at one size, in one colour, with no blink. A second dot-drawing
    // site is the failure mode the rule names, and the way to not have one is to
    // not have one.
    const float p = BeatProgress();
    const bool  mapSide = (beat_ == Beat::Midcourse) ? (p > 0.45f) : (p > 0.5f);

    if (mapSide) {
        // Minimal map: a graticule globe, the great-circle track, the aim point.
        // NOT the product's map -- §7 puts full map rendering post-v1, and the
        // look target's coastline data deliberately stays in the reference (see
        // DEVIATION 3). The colours are its, so the cut lands on the right green.
        const int c = screen_ / 2;
        const int R = (int)(screen_ * 0.40f);
        g.fillCircle(c, c - dy, R, pal::MapLand());
        g.drawCircle(c, c - dy, R, pal::GreenDim());
        for (int i = -2; i <= 2; ++i) {
            g.drawEllipse(c, c - dy, R, (int)(R * fabsf(i * 0.35f) + 2), pal::MapGrid());
        }
        for (int i = 0; i <= 40; ++i) {
            const float t = (float)i / 40.0f;
            const int   x = (int)Lerp((float)(c - R * 0.7f), (float)(c + R * 0.6f), t);
            const int   y = (int)(c + R * 0.35f - sinf(t * 3.1416f) * R * 0.55f);
            g.drawPixel(x, y - dy, t < 0.5f ? pal::Green() : pal::GreenDim());
        }
        const int tx = (int)(c + R * 0.6f), ty = (int)(c + R * 0.35f);
        g.drawCircle(tx, ty - dy, 5, pal::Red());
        g.drawFastHLine(tx - 8, ty - dy, 16, pal::Red());
        g.drawFastVLine(tx, ty - 8 - dy, 16, pal::Red());
    }

    // THE DOT. Same coordinates, same size, same colour, both sides of the cut.
    g.fillCircle((int)vx_, (int)vy_ - dy, 2, pal::Red());
}

void Director::Render(LovyanGFX& g, int yOffset)
{
    const int dy = yOffset;

    // Two beats own their whole frame -- they are shot from somewhere else and
    // the ascent's sky and limb would only fight them.
    if (beat_ == Beat::Reentry) {
        DrawReentry(g, dy);
        DrawPenaids(g, dy);
        DrawCaption(g, dy);
        return;
    }
    if (beat_ == Beat::Detonation) {
        DrawDetonation(g, dy);
        DrawCaption(g, dy);
        return;
    }

    DrawSky(g, dy);
    DrawEarthLimb(g, dy);

    if (beat_ == Beat::Midcourse || beat_ == Beat::MatchCut) {
        DrawPenaids(g, dy);
        DrawRvAndBus(g, dy);
        DrawMatchCut(g, dy);
        DrawCaption(g, dy);
        return;
    }

    DrawDebris(g, dy);
    DrawPlume(g, dy);
    DrawVehicle(g, dy);
    DrawShroud(g, dy);
    DrawRcs(g, dy);
    DrawRvAndBus(g, dy);
    DrawPenaids(g, dy);
    DrawSeparationFlash(g, dy); // over everything, like a real one
    DrawCaption(g, dy);         // except the lower third, which is the shot
}

} // namespace flight
} // namespace missileer
