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
// 2. THE PAD ALTIMETER IS NOT THE PREVIEW'S. Its LIFTOFF phase reads out
//    `pow((t-IGN)*1.35,2.6)*30*38` feet, which reaches 273,000 ft by the end of a
//    phase its own caption track labels T+10 -- and the caption track says T+19 is
//    8,300 ft. The preview contradicts itself, and its own geometry says which
//    half is wrong: it draws a 59.9 ft missile 66 px tall, so a pixel is 0.9 ft,
//    not the 38 ft that constant assumes. The MOTION curve is kept verbatim (it
//    is the look, and matching the published climb rate would have the vehicle
//    crawl out of the silo for four seconds, which is wrong dramatically and
//    physically). Only the NUMBER is re-derived, from the published mark and the
//    preview's own exponent: alt = 8,300 x (t/19)^2.6. The standing rule inverted
//    -- the reference owns what and when, and a readout of a published quantity
//    is neither; §12 and the caption track own that.
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

// Vehicle. FROM THE AIRFRAME, not from the preview.
//
// The preview draws all three stages the same olive with tan bands, and the
// museum article (NASM A19761115000) does not: bottom to top it runs pale sage,
// cream, olive green, with tan interstage bands and a tan aeroshell over the RV.
// Three stages that do not share a colour is worth more here than fidelity to
// the preview, because §11 needs a separation to READ -- and the way a viewer
// knows which stage just left is that the stack visibly changes colour, not just
// length. On a nine-pixel body that is the only cue that survives.
//
// kSegments runs nose -> tail, so index 0 is stage 3 and index 2 is stage 1.
inline uint32_t Stage1()   { return lgfx::color888(0xB2, 0xC0, 0xA8); } // pale sage, the big one
inline uint32_t Stage2()   { return lgfx::color888(0xE2, 0xE0, 0xD6); } // cream
inline uint32_t Stage3()   { return lgfx::color888(0x6E, 0x7A, 0x57); } // olive green
inline uint32_t Band()     { return lgfx::color888(0xA8, 0x9A, 0x6E); } // tan interstage
inline uint32_t Aeroshell(){ return lgfx::color888(0xB9, 0xA8, 0x7A); } // tan, over the RV
inline uint32_t Bus()      { return lgfx::color888(0x8A, 0x86, 0x72); }
inline uint32_t Nozzle()   { return lgfx::color888(0x3B, 0x3C, 0x38); } // bells, pods, throats
inline uint32_t Throat()   { return lgfx::color888(0x1C, 0x1D, 0x1A); }
inline uint32_t Shell()    { return lgfx::color888(0x4A, 0x4B, 0x45); }
inline uint32_t Decoy()    { return lgfx::color888(0x55, 0x56, 0x4E); }
inline uint32_t RvBody()   { return lgfx::color888(0x2B, 0x2C, 0x28); } // reentry only -- see RvLit
/**
 * The RV in space. The reference draws it #2B2C28 -- about 4% luminance -- on
 * black, which is invisible on this panel, and it is invisible in the reference
 * too; nobody noticed because the browser shows it at 2x on a bright laptop.
 *
 * Legibility is a device-side judgment, so the space-side RV is LIT. This is
 * also the physically honest choice: a sunlit object against a black sky is the
 * brightest thing in frame, not the darkest. The dark value stays for REENTRY,
 * where the RV sits inside a bright plasma sheath against a daylit gradient and
 * dark-on-light is what makes it read -- the same object, coloured for the
 * background it is actually on.
 */
inline uint32_t RvLit()    { return lgfx::color888(0xC6, 0xC2, 0xAE); }
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

// The globe (the far side of the match cut), in STROKE-HIERARCHY order --
// dimmest to brightest, which is also the order they are drawn in. The track and
// the vehicle are the brightest things on the screen because they are the only
// two that answer a question; everything else is context.
//
//   ocean disc  <  graticule  <  coastlines  <  track (pal::Green)
inline uint32_t Ocean()     { return lgfx::color888(0x06, 0x18, 0x14); }
inline uint32_t Graticule() { return lgfx::color888(0x12, 0x46, 0x33); }
inline uint32_t Coast()     { return lgfx::color888(0x2A, 0x9E, 0x62); }

// The pad. Ground camera only -- these appear in exactly one beat and nowhere
// else, which is why they can be this dark: the whole frame is a night-ish
// ground shot and the only bright things in it are the flame and the smoke.
inline uint32_t Ground()   { return lgfx::color888(0x22, 0x23, 0x1F); } // grade line
inline uint32_t SiloMouth(){ return lgfx::color888(0x10, 0x11, 0x10); } // the hole
inline uint32_t SiloEdge() { return lgfx::color888(0x3A, 0x3B, 0x36); } // its lip and rails
inline uint32_t Door()     { return lgfx::color888(0x5E, 0x60, 0x58); } // the blast closure
inline uint32_t DoorLit()  { return lgfx::color888(0x8E, 0x90, 0x86); } // its lit top face
// No SiloGlow: the fire at the pad is the PLUME palette (FlameOuter/Mid/Core),
// because it is the same motor. A separate glow colour existed only to render
// the look target's 26x4 px strip, and that strip is not what the video opens on.
inline uint32_t SmokeCore(){ return lgfx::color888(0x8E, 0x8B, 0x80); }
inline uint32_t SmokeRim() { return lgfx::color888(0x4A, 0x48, 0x3F); }
/** The daylight the pad vehicle is lifted toward. See Director::sunLift_. */
inline uint32_t Sun()      { return lgfx::color888(0xFF, 0xF4, 0xE0); }
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
    /**
     * How much of trueMs happens BEFORE T+0. Zero for every beat but LIFTOFF.
     *
     * The launch sequence does not start at T+0 -- T+0 is first-stage ignition,
     * and the locking pin and the 110-ton closure both move before it. That is
     * T-MINUS time: it takes wall clock but it must not consume T+ time, or every
     * published mark downstream slides by however long the door takes.
     *
     * So BeatTrueStartMs subtracts it (the marks are unaffected by anything that
     * happens on the near side of ignition) and TPlusMs holds at T+0 through it.
     * Without this the choice was between a door that opens in half a second and
     * a stage 1 separation at T+66.
     */
    uint32_t    preRollMs;
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
 *
 * LIFTOFF TAKES ITS TEN SECONDS OUT OF STAGE 1, IT DOES NOT ADD THEM. The first
 * stage still separates at T+62; what changed is that the first ten of those
 * seconds are now shot from the ground, so the beat that was "IGNITION, T+0 ->
 * T+62" is "STAGE 1, T+10 -> T+62" and 62000 became 52000. Every published mark
 * downstream is untouched. The rename is not cosmetic either: with the ignition
 * itself now inside LIFTOFF, a beat named IGNITION that begins ten seconds after
 * the motor lit would be lying, and STAGE 1 puts it in the same vocabulary as
 * STAGE 2 and STAGE 3.
 *
 * LIFTOFF IS BARELY COMPRESSED AT ALL -- 11000 against a true 14000 -- and that
 * is the same decision as the staging coast being 1 s in both columns. The look
 * target's 7000 was carried over on the rule that its tuned phase lengths are
 * not re-guessed, and on glass at 7000 the vehicle was visible for 1.5 seconds
 * of it. There is nothing in a ten-second launch to compress: it is already the
 * shortest beat with an event in it, and squeezing the one moment the whole
 * ground camera exists to show is how it gets missed. (Measured before and
 * after: 1.5 s of visible transit -> 5.3 s. See the launch curve in PadBase.)
 *
 * ITS 14000 IS T-4 TO T+10, NOT T+0 TO T+14. The 4000 in the fourth column is
 * preRollMs: the pin and the 110-ton closure both move before first-stage
 * ignition, which is what T+0 actually means. That time is real and has to be on
 * the clock -- a door shoved by a gas generator still takes seconds -- but it is
 * T-minus, so BeatTrueStartMs subtracts it and STAGE 1 still begins at T+10.
 * Without the distinction the only options were a door that opens in half a
 * second or a stage 1 separation at T+66.
 */
const BeatSpec kBeats[(int)Beat::COUNT] = {
    /* Liftoff      */ {"LIFTOFF",       14000, 11000, 4000},  // T-4 -> T+10, from the ground
    /* Stage1Burn   */ {"STAGE 1",       52000,  6000},  // T+10  -> T+62
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
    // LIFTOFF's y is DERIVED, not lerped -- the hot launch is a power curve, not
    // a straight line, so UpdateKinematics overwrites vy_ from PadBase(). The
    // endpoints below are the curve's, recorded so the row still reads as a
    // position; they are not a second opinion about where the vehicle is (see
    // the note on Reentry's derived attitude for why that distinction matters).
    /* Liftoff      */ {120, 236, 120, -60,   0,   0, 0.00f, 0.00f, 1.00f, 1.00f},
    /* Stage1Burn   */ {114, 132, 117, 128,   0,  25, 0.00f, 0.22f, 1.00f, 1.00f},
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

/** Airframe colour by kSegments index (0 = stage 3 at the nose, 2 = stage 1). */
inline uint32_t StageColour(int i)
{
    return (i == 0) ? pal::Stage3() : (i == 1) ? pal::Stage2() : pal::Stage1();
}

/**
 * THE SHROUD AND THE RV ARE NOT THE SAME SHAPE, and jettison is a real reveal.
 *
 * From the NG 2007 video: at T+121 the aeroshell is a BLUNT cone as wide as the
 * bus it sits on; at "REENTRY VEHICLE RELEASED" the RV that comes out of it is a
 * long slender cone, roughly 4:1, markedly narrower than the PBV's forward face.
 * Drawing both with one geometry made the shroud jettison a colour change and
 * nothing more.
 */
constexpr float kShroudLen = 14.0f, kShroudHalfW = 4.5f;
constexpr float kRvLen     = 18.0f, kRvHalfW     = 2.5f;

// Stack extents in the body frame, 240-space -- DERIVED from the geometry above
// rather than restated, because the pad needs three of them and a hand-copied
// 74 would go stale the first time a segment length or the cone changed.
//
//   along = 0 is the vehicle origin (vx_/vy_), which is the BUS's forward face.
//   Negative is toward the nose; positive is toward the tail.
/** The stack's tail: the sum of kSegments. */
constexpr float kTailAlong = 14.0f + 16.0f + 22.0f;             // +52
/** The shroud tip: the bus's -8 front face, plus the cone standing on it. */
constexpr float kNoseAlong = -8.0f - kShroudLen;                // -22
/** Nose tip to stage-1 tail. The number every other vehicle state is measured against. */
constexpr float kStackLen  = kTailAlong - kNoseAlong;           //  74

// ---------------------------------------------------------------------------
// THE PAD -- the ground camera's whole geometry, in 240-space.
//
// All of it is the look target's scrLiftoff(), which is the only phase it draws
// from a camera that is not moving with the vehicle. Ported rather than
// reinvented because the choreography here is unusually load-bearing: the beat
// is a hot launch, and a hot launch is legible only if the vehicle is FULLY
// BURIED first. A vehicle that starts at grade and slides up is a rocket on a
// pad, not a missile coming out of a hole.
//
// kPhaseS is the reference's own phase length, and the beat is parameterised in
// those seconds rather than in wall time so the choreography is identical in
// both time modes -- true time simply plays the same seven seconds over ten.
// ---------------------------------------------------------------------------
// kPhaseS was the reference's own 7.0 with ignition at its 0.9. Both grew when
// the closure door turned out to need seconds rather than half of one: the pad
// now runs 8.75 pad-seconds with ignition at 2.5, so everything before T+0 has
// room and everything after it is unchanged in shape.
//
// The split is exactly 2:5 (2.5 pre, 6.25 post), which is what makes the beat's
// 14000/4000 preRoll come out at a clean 10000 ms of T+ time.
constexpr float kPhaseS  = 8.75f;   // whole pad phase, T-minus included
constexpr float kIgnS    = 2.50f;   // T+0. First-stage ignition, inside the tube
constexpr float kGroundY = 208.0f;
constexpr float kSiloHalfW = 14.0f;

/**
 * THE LAUNCHER CLOSURE DOOR, and it is the first thing that happens.
 *
 * Not "the blast door" -- that is its name. A 110-ton slab of reinforced
 * concrete and steel, 3.5 ft thick, whose job is to keep a nuclear near-miss out
 * of the tube. On an emergency launch a heavy steel LOCKING PIN retracts, and
 * then a BALLISTIC GAS GENERATOR throws the lid open on steel tracks. It does not
 * open; it is fired open.
 *
 * Neither the NG animation nor the preview has any of this -- both open on a hole
 * that is simply already there -- and it is the single most recognisable piece of
 * hardware on a missile field. It also fixes a real problem rather than adding a
 * detail: the beat's first half-second was a dark rectangle and a glow ramping
 * up, with nothing moving.
 *
 * THREE THINGS THE REAL MECHANISM CHANGES, all of them cheap:
 *
 *   1. THE PIN GOES FIRST. A short beat of the locking bolt withdrawing, then a
 *      pause, then the slab. Two-stage motion is what makes an opening read as a
 *      MECHANISM rather than as a panel sliding; one continuous move reads as a
 *      drawer. It costs one 6x2 px rect.
 *   2. IT IS FAST, BUT IT IS NOT INSTANT. The gas generator moves 110 tons "in
 *      seconds" -- violent, not teleported. The easing is EASE-OUT so it is at
 *      speed on the first frame (smoothstep, which is what this had, eases IN as
 *      well, and a 110-ton lid that accelerates gently is a crank), but the slide
 *      gets 0.42 of the 0.9 pad-seconds before ignition rather than a 10-frame
 *      blur. Fast enough to read as fired, slow enough to read at all.
 *   3. IT LEAVES THE FRAME. It is thrown clear, so it does not park politely
 *      beside the hole. kDoorSlide clears the round face with room to spare (the
 *      chord at grade spans x 38..202). The gas generator itself is not drawn.
 *
 * WHY SIDEWAYS AND NOT HINGED, recorded so nobody "improves" it into a flap: a
 * lid that slides can shove its way clear through the dirt and debris a near-miss
 * dumps on the surface. A hinged one lifts into that debris and jams.
 *
 * IGNITION IS AFTER THE DOOR, AND IT HAPPENS INSIDE THE TUBE. The first-stage
 * motor lights once the closure has cleared the path, with the vehicle still
 * fully below grade -- which is exactly what kIgnS at 0.90 against an emergence
 * at 1.91 already does, and is now the reason rather than a coincidence.
 *
 * SCALE IS TAKEN OFF THE VEHICLE IN THE SAME FRAME, which is the only honest way
 * to size it: kSegments gives stage 1 an 11 px body for a 5.5 ft airframe, so
 * 1 px = 0.5 ft here. 3.5 ft of slab is 7 px, and the ~21 ft closure is 42 px
 * across. Both were guessed before those figures existed and both were close;
 * they are derived now, so a future change to the vehicle's width carries.
 *
 * Sequence: pin 0.15 -> 0.75, slab 0.85 -> 2.30, first light 2.30, ignition 2.50.
 *
 * THE SLIDE IS 1.45 PAD-SECONDS, WHICH IS 1.8 s ON THE BENCH AND 2.3 s AT TRUE
 * SPEED. It was 0.42 (0.54 s) and looked like a panel snapping aside: 110 tons
 * has to be SEEN to move. That is the whole reason the beat grew a pre-roll --
 * the door could not be slowed while ignition sat at 0.9 pad-seconds, because
 * there was nowhere to put the time.
 */
constexpr float kPinStartS  = 0.15f;
constexpr float kPinEndS    = 0.75f;
constexpr float kDoorStartS = 0.85f;
constexpr float kDoorEndS   = 2.30f;   // gas generator, but 110 tons of it
constexpr float kDoorHalfW  = 21.0f;   // ~21 ft closure at 0.5 ft/px
constexpr float kDoorThick  = 7.0f;    // 3.5 ft of concrete and steel
constexpr float kDoorSlide  = 130.0f;  // clean off the panel, not parked beside the hole
constexpr float kFireStartS = 2.30f;   // the motor lights once the path is clear

/**
 * THE FIRE IS A COLUMN OUT OF THE HOLE, NOT A DOME ON THE GROUND.
 *
 * Same footage: flame shoots STRAIGHT UP out of the silo, a vertical jet taller
 * than it is wide, and the missile is still inside it -- in four consecutive
 * frames of that launch the vehicle is not visible at all, only fire. It appears
 * later, emerging from the TOP of the fireball. This was three stacked ellipses
 * at grade, which is a pool of fire, not a jet.
 *
 * The column therefore has to be drawn AFTER the vehicle, so it hides it. That
 * is the whole reason the moment reads: the vehicle is not a silhouette sliding
 * out of a slot, it is something that comes out of the fire.
 *
 * Height is driven by two terms that pull opposite ways, which is what keeps it
 * from swallowing the beat:
 *
 *   IGNITION ramps it up over 0.6 s -- the motor coming to pressure.
 *   THE VEHICLE'S OWN HEIGHT collapses it, because once the motor is out of the
 *   hole there is nothing left down there burning. Physical, not a timer, so it
 *   automatically tracks any future change to the launch curve.
 *
 * The result: hidden through the emergence, out of the fire by ~3.2 pad-seconds,
 * and 2.5 pad-seconds of clean climbing vehicle after that.
 */
constexpr float kFireMaxH   = 76.0f;   // 240-space, ~a third of the frame
constexpr float kFireOutH   = 60.0f;   // vehicle height at which the column is spent
constexpr int   kFireBlobs  = 8;       // see the note in DrawLiftoff on why blobs

/** Where the vehicle sits before the motor lights: fully below grade, plus 6 px. */
constexpr float kPadBase0 = kGroundY + kStackLen + 6.0f;

/**
 * SMOKE. A COLUMN, not a ground bank -- and that is the NG video, not the look
 * target.
 *
 * The look target rolls its puffs outward at +/-21 px/s and lifts them at 0-6,
 * which builds a low bank hugging the pad. The NG 2007 video that §11 names as
 * the beat sheet does not look like that at any point: at T-0 the frame is a
 * TALL VERTICAL COLUMN reaching two thirds of the way up, with a fireball at its
 * foot and a billowing head -- and the vehicle is not visible at all. At T+3 the
 * vehicle sits on TOP of that column. The column is the subject; the missile is
 * the small thing riding it.
 *
 * The authority order settles which one wins: the design doc is the spec, and
 * the spec names the video. docs/reference/* is the LOOK, which fills in where
 * the spec is silent -- it does not overrule the source the spec cites. So the
 * rise dominates the drift here, and the outward roll is kept only for the flare
 * at the base.
 *
 * BOUNDED BY ARITHMETIC rather than by a cap. The reference spawns ~51 puffs a
 * second, which is ~100 alive at once; 100 filled discs of 40 px radius is not a
 * cost worth discovering on glass. The spawn window is choreography and is kept
 * (smoke starts before first motion, stops when the vehicle is clear of it);
 * only the RATE is thinned, and at kSmokeDtS spacing the window admits a fixed
 * number whatever the frame rate, time mode, or how long the beat runs:
 *
 *     (6.58 - 2.05) / 0.095 + 1  =  48 puffs, ever
 *
 * BOTH ENDS MOVE WITH THE REST OF THE PAD, never independently. The start is the
 * reference's own "half a second before ignition" and the end is its "while
 * base > groundY - 90", re-solved for the launch curve in PadBase. Hand-tuning
 * either after a timing change is how the smoke ends up starting before the door
 * has opened or stopping while the vehicle is still in the hole.
 */
constexpr float kSmokeStartS = 2.05f;   // reference: half a second before ignition
constexpr float kSmokeEndS   = 6.58f;   // reference: while base > groundY - 90
constexpr float kSmokeDtS    = 0.095f;
constexpr int   kSmokeMax    = 52;      // ceil((end-start)/dt) + 1, with spares
constexpr float kSmokeGrowth = 12.0f;   // px/s
constexpr float kSmokeRMax   = 34.0f;   // a column stops being one if every puff is huge
constexpr float kSmokeRise   = 30.0f;   // px/s, the column term -- reference had 0-6

/**
 * MIX TOWARD DAYLIGHT. See Director::sunLift_ for why the pad vehicle is the
 * ascent vehicle under a different exposure rather than a different paint.
 *
 * 0.35 at full sun: enough that the airframe reads as lit from outside rather
 * than as a silhouette, and not so much that the three stage colours converge --
 * losing them at the pad would cost the separation cue the scheme exists for.
 */
inline uint32_t Lit(uint32_t c, float sun)
{
    if (sun <= 0.0f) return c;
    const uint32_t s = pal::Sun();
    const Rgb base = {(uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c};
    const Rgb sky  = {(uint8_t)(s >> 16), (uint8_t)(s >> 8), (uint8_t)s};
    return Rgb2c(MixRgb(base, sky, 0.35f * sun));
}

/**
 * A colour at `a` opacity OVER BLACK, which on the pad is exact rather than an
 * approximation: the ground shot has no background to composite against, so
 * scaling the channels IS the blend. DEVIATION 4's caveat about pre-blending
 * does not apply here.
 */
inline uint32_t Shade(uint32_t c, float a)
{
    if (a <= 0) return 0;
    if (a > 1) a = 1;
    return lgfx::color888((uint8_t)(((c >> 16) & 0xFF) * a),
                          (uint8_t)(((c >>  8) & 0xFF) * a),
                          (uint8_t)(( c        & 0xFF) * a));
}

/**
 * APPARENT-SIZE FLOOR -- the first place the reference is deliberately not
 * followed, under the standing rule that it is authoritative for WHAT and WHEN
 * and never for WHETHER IT CAN BE SEEN.
 *
 * The reference was authored on a 240x240 canvas displayed at 480 CSS px on a
 * bright laptop. This panel is 240 px across ~32 mm of glass at desk distance,
 * so 1 px is 0.135 mm. Measured off the geometry above -- kNoseAlong to the tail
 * of whatever is still attached, which is what DrawVehicle actually draws:
 *
 *                                        px     mm    vs pad
 *     pad / full stack + bus + shroud    74   10.0     1.00   <- the reference
 *     after stage 1                      52    7.0     0.70
 *     after stage 2 (shroud gone, RV)    40    5.4     0.54
 *     after stage 3 (bus + cone)         26    3.5     0.35   <- 45 s of true time
 *     RV alone                           18    2.4     0.24   <- and shrinking
 *
 * THE PAD IS THE REFERENCE BECAUSE THE PAD IS WHERE THE VEHICLE IS LARGEST, and
 * because the chase cam opens on the same 74 px -- a cut that changed the
 * subject's size would break the same-object read as surely as a colour change
 * would (see Director::sunLift_ for the other half of that).
 *
 * The stack is fine. Everything after the third separation is at or below the
 * size where a viewer can tell what the object is, and the RV is the subject of
 * five consecutive beats. §11's "shrink to a dot" is about the END of the
 * ascent and the match cut, where being a dot is the point -- it is not a
 * licence for the vehicle to be unreadable through the whole of midcourse.
 *
 * So the vehicle is boosted once the stack is gone. RAMPED ACROSS THE LAST
 * SEPARATION rather than switched, because a silhouette that doubles in one
 * frame is a pop; ramped, it reads as the camera pushing in as the stack falls
 * away, which is what a real flight-sequence video does at exactly this moment.
 *
 * Proportions are untouched -- this multiplies scale_, so every relative size
 * the reference chose survives. Only the apparent size changes.
 *
 * What the boost buys, against the same pad reference:
 *
 *     after stage 3 (bus + cone)  26 -> 52 px   3.5 -> 7.0 mm   0.35 -> 0.70
 *     RV alone                    18 -> 36 px   2.4 -> 4.9 mm   0.24 -> 0.49
 *
 * So the post-stack vehicle ends up the same apparent size it was after the
 * first separation, and the RV alone never falls below half the pad silhouette.
 * That is the whole claim: the subject of the last five beats stays as legible
 * as the subject of the first five.
 */
constexpr float kSubjectBoost = 2.0f;   // 18 px -> 36 px (2.4 mm -> 4.9 mm)

/**
 * MIDCOURSE CHOREOGRAPHY, as fractions of the beat.
 *
 * §11: "Ascent ends by shrinking the vehicle to a single dot; the map opens with
 * that same dot." Three things have to happen in that order and they did not:
 *
 *   0.00 .. 0.34   HOLD. Full size, where the ascent left it. §7 hands the
 *                  screen back to monitoring here; nothing should be moving.
 *   0.34 .. 0.44   SHRINK AND SLIDE, on ONE easing parameter, so the vehicle
 *                  arrives at its track position exactly as it becomes a dot.
 *   0.44           IT IS THE DOT. The RV stops being drawn and the dot starts,
 *                  same place, same instant -- there is never both.
 *   0.45           The map opens under a dot that is already sitting still.
 *
 * What was there before drew the red dot on top of the full-size RV for the
 * whole beat and slid the pair across the frame at full size, which is a dot
 * appearing on a vehicle and the vehicle then walking to the map -- the exact
 * opposite of the read the rule asks for.
 */
constexpr float kShrinkStart = 0.34f;
constexpr float kDotAt       = 0.44f;

/**
 * THE POST-BOOST VEHICLE HAS TWO PROPULSION SYSTEMS, AND THEY ARE NOT THE SAME
 * THING. From the Peacekeeper Stage IV cutaway (Rockwell LC600-520F):
 *
 *   AXIAL ENGINE (1)              -- aft centre. Propels the vehicle forward.
 *                                    This is what shapes the trajectory during
 *                                    post-boost ("MANEUVER TO WINDOW IN SPACE").
 *   ATTITUDE CONTROL ENGINE (8)   -- a ring around the aft periphery. These only
 *                                    fire when RE-ORIENTING the vehicle. They do
 *                                    not push it anywhere; they point it.
 *
 * The rig had four pods firing continuously through every bus beat and no main
 * engine at all, which is both halves wrong: the porcupine was on when the
 * vehicle was not turning, and the thing that actually moves it was missing.
 *
 * Eight nozzles arranged around a cylinder cannot all be silhouetted in a side
 * view, so four are drawn on the flanks (the two the silhouette would show, fore
 * and aft on each side) and each fires a fanned pair -- eight jets, four visible
 * nozzles, which is what the projection allows at eighteen pixels of bus.
 */
constexpr float kBusHalfW = 4.5f;
constexpr float kPodOut   = 6.4f;   // how far a nozzle stands off the flank
const float kRcsPods[4][2] = {{-1.8f, -1.0f}, {-1.8f, 1.0f},
                              {-4.4f, -1.0f}, {-4.4f, 1.0f}};

// ---------------------------------------------------------------------------
// THE GLOBE -- the far side of the match cut.
//
// AN ORTHOGRAPHIC SPHERE, not a flat map. A sphere seen from outside is already
// a circle and this panel is a circle, so it is the one projection whose natural
// shape is the display's; nothing is clipped into corners that do not exist.
// More importantly it is the only projection on which the great circle READS AS
// AN ARC, which is the most educational thing this screen can show -- and on the
// flat map it was provably invisible: the GOLF-07 track bowed 0.83 px off a
// straight line, because a near-meridional great circle IS straight under
// equirectangular. The map was not wrong; it could not express the thing.
//
// Coastlines are Natural Earth 1:110m, decimated by spherical Douglas-Peucker at
// 0.5 deg (~1 px at this radius) -- see scripts/gen_coastlines.py, which is the
// only thing that should ever edit Coastlines.inc. They replace 126 hand-drawn
// whole-degree points that had the Gulf of Mexico as land, no Hudson Bay, no
// British Isles and no Japan.
//
// NO TRIG IN THE INNER LOOP. Every vertex is a precomputed int16 unit vector, so
// a frame is: 3x3 rotate (9 mul, 6 add), one z>0 test to drop the far
// hemisphere, and take x,y -- which IS the orthographic projection. Measured at
// 262 ns/vertex on this board, so 1,306 vertices cost 0.34 ms and vertex count
// is not the budget. LINE PIXELS ARE, at 1.06 us/px measured: a dozen long
// graticule lines outweigh hundreds of short coastline segments, which is why
// there are six meridians and three parallels rather than twelve and five.
//
// AND NOTHING IS CACHED. A 240x240 PSRAM sprite costs 6.24 ms/frame to blit,
// which is more than drawing the whole globe live, and spends 115 KB doing it.
// Measured; see the BENCH block in animtest_main.cpp.
//
// NO LAND FILL. Filling a continent on a sphere means clipping its polygon to
// the limb, and the scanline fill it would use assumes straight projected edges,
// which is false here. The disc is filled once as ocean and coastlines are
// stroked over it; land is implied by its outline, which is also the look every
// missile-plot in the genre has.
// ---------------------------------------------------------------------------
struct GeoVec { int16_t x, y, z; };
struct Coastline { const GeoVec* v; int n; };
#include "Coastlines.inc"
constexpr int kCoastCount = (int)(sizeof(kCoast) / sizeof(kCoast[0]));

/* The look target's GOLF-07 scenario: F.E. Warren AFB to the South Pacific pole
 * of inaccessibility. Open ocean, per the tone rule -- the aim point is never a
 * populated place. */
constexpr float kLaunchLon = -104.87f, kLaunchLat =  41.15f;
constexpr float kAimLon    = -123.39f, kAimLat    = -48.87f;

constexpr float kGlobeR = 110.0f;   // 240-space; leaves the caption its rows

/**
 * TILT OFF THE GREAT-CIRCLE PLANE, and the obvious value is the broken one.
 *
 * Centre the view on the arc's midpoint -- the natural choice -- and the view
 * direction lies IN the great-circle plane, so the arc projects to a STRAIGHT
 * LINE through the centre of the disc. That is the same failure the flat map
 * had, faithfully reproduced on a sphere, and it would have been found on glass
 * rather than on paper.
 *
 * Tilting the view toward the plane's normal by phi bows it. Computed for
 * GOLF-07 at R=110, with both endpoints' angular distance from the disc centre:
 *
 *     phi     bow       endpoints     r/R
 *      0     0.0 px     45.7 deg     0.72   <- the trap
 *     20    11.4 px     49.0 deg     0.75
 *     30    16.6 px     52.8 deg     0.80   <- here
 *     45    23.5 px     60.4 deg     0.87   severe foreshortening
 *
 * 30 deg is clearly bowed with the endpoints at 80% of the radius, where
 * foreshortening is still mild. DERIVED FROM THE TRAJECTORY rather than dialled
 * in, so it stays correct for whatever target the game picks later.
 */
constexpr float kGlobeTilt = 30.0f;

inline void UnitVec(float lonDeg, float latDeg, float* o)
{
    const float d2r = 0.0174533f;
    const float c = cosf(latDeg * d2r);
    o[0] = c * cosf(lonDeg * d2r);
    o[1] = c * sinf(lonDeg * d2r);
    o[2] = sinf(latDeg * d2r);
}

inline void Norm3(float* v)
{
    const float m = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (m > 1e-9f) { v[0] /= m; v[1] /= m; v[2] /= m; }
}

inline void Cross3(const float* a, const float* b, float* o)
{
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}

inline float Dot3(const float* a, const float* b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/**
 * The camera. FIXED for the whole beat, and that is a decision, not a shortcut.
 *
 * A following camera -- vehicle centred, ground sliding under it -- answers
 * nothing: the dot never moves, so there is no progress cue at all, and a world
 * sliding under a stationary marker reads as the WORLD moving. A fixed
 * orientation with the launch point and the target both on the visible
 * hemisphere answers "how far along am I" at a glance, which is the question the
 * flat map left entirely to a frame counter. The match-cut rule needs it too:
 * the map opens on the dot the ascent shrank to, and a permanently centred dot
 * has no payoff. And this screen is handed back to monitoring for twenty-six
 * minutes -- a continuously rotating globe is a screensaver.
 *
 * NORTH IS UP, and the tilt is applied toward the great-circle normal, so the
 * arc bows across the disc instead of running down its spine.
 */
struct GlobeBasis { float v[3], r[3], u[3]; };
GlobeBasis gGlobe;
bool gGlobeReady = false;

void BuildGlobeBasis()
{
    if (gGlobeReady) return;
    gGlobeReady = true;

    float L[3], A[3];
    UnitVec(kLaunchLon, kLaunchLat, L);
    UnitVec(kAimLon,    kAimLat,    A);

    float n[3]; Cross3(L, A, n); Norm3(n);                 // great-circle normal
    float m[3] = {L[0] + A[0], L[1] + A[1], L[2] + A[2]};  // arc midpoint
    Norm3(m);

    const float phi = kGlobeTilt * 0.0174533f;
    const float cp = cosf(phi), sp = sinf(phi);
    for (int i = 0; i < 3; ++i) gGlobe.v[i] = m[i] * cp + n[i] * sp;
    Norm3(gGlobe.v);

    // right = worldNorth x view, up = view x right. Degenerate only looking
    // straight down a pole, which this tilt cannot produce.
    float north[3] = {0.0f, 0.0f, 1.0f};
    Cross3(north, gGlobe.v, gGlobe.r);
    if (Dot3(gGlobe.r, gGlobe.r) < 1e-6f) {
        gGlobe.r[0] = 1.0f; gGlobe.r[1] = 0.0f; gGlobe.r[2] = 0.0f;
    }
    Norm3(gGlobe.r);
    Cross3(gGlobe.v, gGlobe.r, gGlobe.u);
    Norm3(gGlobe.u);
}

/** World unit vector -> screen. Returns true on the near hemisphere. */
inline bool GlobePt(float x, float y, float z, float c, float R, float& sx, float& sy)
{
    const float zz = gGlobe.v[0] * x + gGlobe.v[1] * y + gGlobe.v[2] * z;
    sx = c + (gGlobe.r[0] * x + gGlobe.r[1] * y + gGlobe.r[2] * z) * R;
    sy = c - (gGlobe.u[0] * x + gGlobe.u[1] * y + gGlobe.u[2] * z) * R;
    return zz > 0.0f;
}

/** Great-circle interpolation, launch -> aim, as a unit vector. */
void GreatCircle(float f, float* o)
{
    float v1[3], v2[3];
    UnitVec(kLaunchLon, kLaunchLat, v1);
    UnitVec(kAimLon,    kAimLat,    v2);
    float d = Dot3(v1, v2);
    if (d >  1.0f) d =  1.0f;
    if (d < -1.0f) d = -1.0f;
    const float ang = acosf(d);
    const float s   = sinf(ang);
    float a = 1.0f - f, b = f;
    if (s > 1e-5f) { a = sinf((1.0f - f) * ang) / s; b = sinf(f * ang) / s; }
    for (int i = 0; i < 3; ++i) o[i] = v1[i] * a + v2[i] * b;
    Norm3(o);
}

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
    // SIGN OF THE ROTATION, and it was wrong until 2026-08-06.
    //
    // Positive r must pitch the nose DOWNRANGE -- toward +x -- because that is
    // what the anchor table's angle column says ("+90 = nose along the horizon")
    // and what the reference does (`ctx.rotate(ang)` with the nose at local -y
    // sends the nose to +x for positive ang). This function had the sin terms
    // the other way round, so every positive angle canted the nose to the LEFT
    // while the vehicle drifted right.
    //
    // §11 on exactly this: "downrange velocity is conserved, so the RV releases
    // in the direction of travel. Getting this backwards is the tell that an
    // animation was drawn rather than reasoned." It was backwards. It showed up
    // at REENTRY because that is the only beat whose screen motion is large
    // enough to contradict the attitude out loud.
    //
    // Nose direction is (sin r, -cos r); the tail is the negative of that.
    X = cx - sinf(r) * along + cosf(r) * across;
    Y = cy + cosf(r) * along + sinf(r) * across;
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
    // MINUS THE PRE-ROLL. T-minus time takes wall clock but is not T+ time; see
    // BeatSpec::preRollMs. Only LIFTOFF has any, and this is what keeps STAGE 1
    // beginning at T+10 however long the closure door is given to open.
    uint32_t t = 0;
    for (int i = 0; i < (int)b && i < (int)Beat::COUNT; ++i) {
        t += kBeats[i].trueMs - kBeats[i].preRollMs;
    }
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
    EnterBeat(Beat::Liftoff);
}

void Director::Seek(Beat b)
{
    if ((int)b < 0) b = Beat::Liftoff;
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

    // Seek() enters a beat without advancing it, so the first frame after a jump
    // draws before UpdateKinematics has ever run. Both of these have to be right
    // in that frame: an unset sunLift_ would flash the pad vehicle in altitude
    // paint, and a stale shake would leave the ground furniture offset.
    sunLift_ = (b == Beat::Liftoff || b == Beat::Stage1Burn) ? 1.0f : 0.0f;
    shakeX_ = shakeY_ = 0.0f;

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
    // The bus starts where the stack was. The RV needs no origin of its own --
    // DrawRvAndBus draws it from vx_ through the same DrawNoseCone the stack
    // used, which is what stops it jumping at the release boundary.
    if (b == Beat::RvRelease) { busX_ = vx_; busY_ = vy_; }
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

    // REENTRY POINTS WHERE IT IS GOING, and the attitude is COMPUTED FROM THE
    // PATH rather than stated beside it. An attitude column and a position
    // column are two independent places to write down "which way is the vehicle
    // heading", and two places to disagree -- which they did, by 16-29 degrees.
    // Derived, they cannot: edit the anchor's endpoints and the nose follows.
    //
    // This is the one beat with enough screen travel for the mismatch to read.
    // The ascent deliberately keeps its attitude in the table, because there the
    // vehicle barely moves and the limb does the work (see the anchors note).
    if (beat_ == Beat::Reentry) {
        angleDeg_ = atan2f(a.x1 - a.x0, -(a.y1 - a.y0)) * 57.29578f;
    }
    altitude_ = Lerp(a.alt0, a.alt1, p);
    scale_    = Lerp(a.s0, a.s1, p);

    // THE SHRINK IS A MOMENT, NOT THE BEAT. Midcourse is 26 minutes of true
    // time, and lerping scale 1.00 -> 0.05 across all of it left the RV under
    // 5 px for the last ~60% -- unreadable for a quarter of an hour to buy a
    // transition that lasts seconds. §11 wants the ascent to END by shrinking to
    // a dot, immediately before the map opens with that same dot, so the shrink
    // is held off until just before DrawMatchCut flips to the map side at 0.45.
    if (beat_ == Beat::Midcourse) {
        // ONE easing parameter drives both the shrink and the slide onto the
        // great circle, so they cannot happen at different times -- which is
        // what made the vehicle wander across the frame at full size. Before
        // kShrinkStart it is zero, so the vehicle simply holds. See the
        // choreography note at kShrinkStart.
        const float e = Smooth(Clamp01((p - kShrinkStart) / (kDotAt - kShrinkStart)));
        scale_ = Lerp(1.0f, 0.05f, e);
        float tx, ty;
        TrackPoint(TrackFraction(), tx, ty);
        vx_ = Lerp(a.x0, tx, e);
        vy_ = Lerp(a.y0, ty, e);
    } else if (beat_ == Beat::MatchCut) {
        TrackPoint(TrackFraction(), vx_, vy_);
    }

    // ---- the pad ---------------------------------------------------------
    //
    // Position comes off the launch curve, not the anchor lerp (see the kAnchors
    // note). The vehicle stays vertical: the reference does not tilt it here, and
    // the T+3 PITCH caption is announcing a manoeuvre the ground camera cannot
    // resolve -- a two-degree cant on a vertical stack would read as a drawing
    // error, not as a pitch program.
    sunLift_ = 0.0f;
    shakeX_ = shakeY_ = 0.0f;
    if (beat_ == Beat::Liftoff) {
        const float u  = screen_ / 240.0f;
        const float ts = PadSeconds();
        // PadBase() is the BOTTOM OF THE BODY (what the reference calls `base`
        // and where the flame comes out); vx_/vy_ is the origin the body frame
        // measures from, which sits kTailAlong above it. Subtracting the full
        // stack length here instead would put the vehicle 22 px too high and the
        // flame 22 px inside it.
        vx_ = 120.0f * u;
        vy_ = (PadBase(ts) - kTailAlong) * u;
        angleDeg_ = 0.0f;
        sunLift_  = 1.0f;

        // Camera shake, decaying to nothing by 2.2 s. Deterministic, so a replay
        // shakes identically -- the reference uses Math.random() here and a rig
        // whose frames differ between runs cannot be photographed for review.
        if (ts < 2.2f) {
            const float amp = 2.4f * (1.0f - ts / 2.2f) * u;
            shakeX_ = (Noise(beatElapsedMs_ / 33u + 101u) - 0.5f) * amp;
            shakeY_ = (Noise(beatElapsedMs_ / 33u + 211u) - 0.5f) * amp * 0.6f;
            vx_ += shakeX_;
            vy_ += shakeY_;
        }
    } else if (beat_ == Beat::Stage1Burn) {
        // The daylight fades out over the opening quarter of the chase cam, so
        // the frame on either side of the cut is the same object in the same
        // light and the exposure change happens where nothing can be compared
        // against a previous frame. See Director::sunLift_.
        sunLift_ = 1.0f - Clamp01(BeatProgress() / 0.25f);
    }

    const float dt = (float)dtMs / 16.0f; // ~frames at 60 Hz, so motion is frame-rate independent

    if (stageLife_ > 0) {
        // Receding ALONG the flight line: back down the velocity vector, which
        // is the tail direction (-sin r, +cos r). See the sign note in Axis().
        const float r = angleDeg_ * 0.01745f;
        stageX_ -= sinf(r) * 0.9f * dt;
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
        busX_ -= sinf(r) * 0.22f * dt;   // tail direction; see Axis()
        busY_ += cosf(r) * 0.22f * dt;
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

float Director::PadSeconds() const
{
    return BeatProgress() * kPhaseS;
}

float Director::PadBase(float ts)
{
    // THE HOT LAUNCH. The look target's curve is
    //
    //     base = groundY + mh + nose + 6 - pow((t - IGN) * 1.35, 2.6) * 30
    //
    // and the 2.6 power is the whole read: the vehicle barely creeps out of the
    // silo and is then gone, which is what a hot launch looks like and what a
    // linear rise never does. The exponent is kept.
    //
    // THE TIME SCALE IS NOT. At 1.35 the vehicle clears grade at t = 1.30 s and
    // has left the frame by t = 2.76 -- ONE AND A HALF SECONDS of visible
    // transit, in a beat that then holds four more on an empty pad. That is the
    // worst of both: the event is over before it registers and the beat still
    // feels long. It was missable on glass, and the whole reason this beat exists
    // as a beat is that the launch is the moment nobody should miss.
    //
    // The coefficient is solved, not dialled. With the exponent fixed at 2.6 the
    // ratio between "clears grade" and "leaves frame" is fixed too -- 4.66,
    // whatever the coefficient -- so there is exactly one degree of freedom.
    // Below, as SECONDS AFTER IGNITION, which is the frame that matters now that
    // ignition is not at the top of the beat:
    //
    //                clears grade   leaves frame   visible   hold before the cut
    //     1.35 (ref)     0.40 s         1.89 s      1.5 s          4.2 s
    //     0.534          1.01 s         4.78 s      3.8 s          1.3 s
    //     0.478          1.13 s         5.34 s      4.2 s          0.9 s
    //
    // 0.478 re-solves the same equation against the longer phase (8.75 rather
    // than 7.0 pad-seconds): the exit lands at 7.84, leaving the same held ground
    // shot before the cut. In wall clock the visible transit is 5.3 s, against
    // 1.5 s for the reference's own coefficient.
    //
    // It also puts the nose through grade at T+1.61 with the LIFTOFF caption at
    // T+1.80 -- the near-alignment that the previous retune lost, back for free,
    // because BOTH are now anchored to ignition rather than to the top of a beat
    // that has a pre-roll in front of it.
    //
    // The cost is a coincidence worth recording as lost: at 1.35 the nose came
    // through grade at T+1.86 and kCaptions has had LIFTOFF at T+1.80 since
    // before this beat existed. It now emerges at T+2.73, which is if anything
    // more correct -- LIFTOFF is called at FIRST MOTION, which happens at kIgnS
    // with the vehicle still in the hole, not when it clears the lip. The caption
    // is on screen T+1.8 to T+3.0, so it now spans the emergence instead of
    // landing on it.
    if (ts <= kIgnS) return kPadBase0;
    return kPadBase0 - powf((ts - kIgnS) * 0.478f, 2.6f) * 30.0f;
}

float Director::SubjectScale() const
{
    // See kSubjectBoost. Ramped across STAGE 3 SEP so the stack's departure and
    // the push-in are one move rather than a jump.
    if (beat_ == Beat::Stage3Sep) return scale_ * Lerp(1.0f, kSubjectBoost, BeatProgress());
    if (beat_ >  Beat::Stage3Sep) return scale_ * kSubjectBoost;
    return scale_;
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
    //
    // AND IT HOLDS AT T+0 THROUGH A PRE-ROLL. LIFTOFF opens on the locking pin
    // and the closure door, which happen before first-stage ignition -- so for
    // those four seconds the honest reading is T-0, exactly as the caption says,
    // and the T+ clock starts when the motor lights. Running it from the top of
    // the beat instead would have the whole caption track four seconds early and
    // the altimeter reporting climb before there was any.
    const BeatSpec& s = kBeats[(int)beat_];
    const uint32_t elapsed = (uint32_t)(BeatProgress() * (float)s.trueMs);
    if (elapsed <= s.preRollMs) return BeatTrueStartMs(beat_);
    return BeatTrueStartMs(beat_) + (elapsed - s.preRollMs);
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

void Director::DrawSmoke(LovyanGFX& g, int dy) const
{
#ifdef ANIM_PROFILE
    const uint32_t t0 = micros();
#endif
    const float u  = screen_ / 240.0f;
    const float ts = PadSeconds();

    // Oldest first, so the young bright puffs sit on top of the old faint ones --
    // which is what gives a flat 2-ring disc any depth at all.
    for (int s = 0; s < kSmokeMax; ++s) {
        const float born = kSmokeStartS + (float)s * kSmokeDtS;
        if (born > kSmokeEndS) break;   // the spawn window closed; none after this
        const float age = ts - born;
        if (age <= 0) break;            // not born yet, and every later one is later

        const uint32_t h  = (uint32_t)s * 2654435761u;
        const float    n0 = Noise(h + 1u), n1 = Noise(h + 2u);
        const float    n2 = Noise(h + 3u), n3 = Noise(h + 4u);

        // continue, NOT break: lives differ, so an older puff can outlast a
        // younger one and the loop must keep looking.
        const float life = 2.6f + n3 * 1.6f;
        if (age >= life) continue;

        // RISE DOMINATES DRIFT -- this is the column, see the note at kSmokeRise.
        // The outward term is kept but halved: it is what flares the foot of the
        // column and splits it around the vehicle, and without any of it the
        // smoke reads as a rope rather than as a blast deflector doing its job.
        const float vx = (n1 - 0.5f) * 11.0f + (n0 < 0.5f ? -5.0f : 5.0f);
        const float vy = -(kSmokeRise * (0.55f + n2 * 0.75f));

        float r = 5.0f + n2 * 7.0f + age * kSmokeGrowth;
        if (r > kSmokeRMax) r = kSmokeRMax;
        r *= u;

        const float x = (120.0f + (n0 - 0.5f) * 30.0f + vx * age) * u + shakeX_;
        const float y = (kGroundY - 2.0f + vy * age) * u + shakeY_;
        if (x + r < 0 || x - r > screen_ || y + r < 0) continue;

        const float a = fminf(0.75f, (life - age) * 0.5f);
        if (a < 0.06f) continue;   // below this it is a black disc costing real fill

        g.fillCircle((int)x, (int)y - dy, (int)r, Shade(pal::SmokeRim(), a));
        const int ri = (int)(r * 0.62f);
        if (ri > 0) {
            g.fillCircle((int)(x - r * 0.18f), (int)(y - r * 0.20f) - dy, ri,
                         Shade(pal::SmokeCore(), a));
        }
    }
#ifdef ANIM_PROFILE
    smokeUs_ = micros() - t0;
#endif
}

void Director::DrawLiftoff(LovyanGFX& g, int dy) const
{
    const float u    = screen_ / 240.0f;
    const float ts   = PadSeconds();
    const float base = PadBase(ts);

    // The furniture shakes with the vehicle, from the same stored offsets -- see
    // Director::shakeX_. Recomputing the shake per element is how a ground line
    // and a silo mouth end up moving independently in the same frame.
    const int gy = (int)(kGroundY * u + shakeY_);
    const int cx = (int)(screen_ * 0.5f + shakeX_);

    g.fillScreen(pal::Space());

    // ---- ground plane and silo mouth -------------------------------------
    // The grade line runs the full width and simply leaves the glass at both
    // ends, which is correct on a round face: a horizon that stopped short of
    // the bezel would read as a drawn object rather than as the ground.
    g.drawFastHLine(0, gy - dy, screen_, pal::Ground());
    g.drawFastHLine(0, gy + 1 - dy, screen_, pal::Ground());

    const int mw = (int)(kSiloHalfW * u);
    const int mt = gy - (int)(2 * u);
    const int mh = (int)(6 * u) + 1;
    g.fillRect(cx - mw, mt - dy, mw * 2, mh, pal::SiloMouth());
    g.drawRect(cx - mw, mt - dy, mw * 2, mh, pal::SiloEdge());

    // ---- the blast door, on its rails -------------------------------------
    //
    // See kDoorStartS. The rails are drawn even before the door moves, because
    // they are what makes the motion legible when it starts -- a slab that slides
    // with nothing under it reads as a glitch, and two lines cost nothing. They
    // run to the edge of the glass, because that is where the door goes.
    {
        g.drawFastHLine(cx, gy + (int)(3 * u) - dy, screen_ - cx, pal::SiloEdge());
        g.drawFastHLine(cx, gy + (int)(5 * u) - dy, screen_ - cx, pal::SiloEdge());

        // EASE-OUT, not smoothstep: gas-generator driven, so it is at speed on
        // the first frame. Smoothstep would ease in as well, which is a crank.
        //
        // The exponent softened from 2.0 to 1.6 when the slide got 3.5x longer.
        // A square ease-out puts a third of the travel in the first fifth of the
        // time, which over 1.8 seconds reads as a snap followed by a long drift
        // -- the fast part looked as quick as the old door and the rest looked
        // like it was running out of gas.
        const float t     = Clamp01((ts - kDoorStartS) / (kDoorEndS - kDoorStartS));
        const float slide = 1.0f - powf(1.0f - t, 1.6f);
        const int   dx    = (int)(kDoorSlide * slide * u);
        const int   dw    = (int)(kDoorHalfW * u);
        const int   dh    = (int)(kDoorThick * u) + 1;
        const int   dt    = gy - dh / 2;

        // THE LOCKING PIN, and it goes FIRST. A steel bolt withdrawing, a pause,
        // then 110 tons of concrete -- two-stage motion is what makes an opening
        // read as a MECHANISM. One continuous move reads as a drawer. It is six
        // pixels of steel and it is the cheapest characterful thing in the beat.
        if (ts < kDoorStartS) {
            const float pt = 1.0f - Clamp01((ts - kPinStartS) / (kPinEndS - kPinStartS));
            const int   pl = (int)(6 * u * pt) + 1;
            g.fillRect(cx - dw - pl, gy - (int)(3 * u) - dy, pl, (int)(2 * u) + 1,
                       pal::DoorLit());
        }

        g.fillRect(cx + dx - dw, dt - dy, dw * 2, dh, pal::Door());
        // A lit top face, which is the only thing that separates a concrete slab
        // from the ground it is sitting on at this size.
        g.drawFastHLine(cx + dx - dw, dt - dy, dw * 2, pal::DoorLit());
    }

    DrawSmoke(g, dy);

    // ---- the vehicle, CLIPPED AT GRADE -----------------------------------
    //
    // The clip is what makes this a hot launch rather than a rocket on a pad: the
    // vehicle exists below the ground line for the first second and a bit and
    // simply is not drawn there, so it emerges from the silo instead of sliding
    // up past it. The flame is inside the clip too -- an exhaust plume painted
    // over the ground while the motor is still in the hole is the single most
    // obvious way to break it.
    g.setClipRect(0, -dy, screen_, gy + 2);
    DrawPlume(g, dy);
    DrawVehicle(g, dy);
    g.clearClipRect();

    // ---- the fire column, OVER THE VEHICLE --------------------------------
    //
    // See kFireMaxH. The ordering is the point: in the launch footage the missile
    // is INSIDE the fire for four consecutive frames and only appears coming out
    // of the top of it. Drawn under the vehicle -- which is where a glow belongs
    // and where this sat until the footage arrived -- the emergence is a clean
    // silhouette sliding out of a slot. Drawn over it, the vehicle comes out of
    // the fire, which is the shot.
    //
    // Plume palette, not the detonation ramps: this is a motor, and spending
    // Hood/Badger on an engine would cost the one fire palette that means
    // warhead. (Nor amber -- see the palette note. Fire is not chrome.)
    if (ts > kFireStartS) {
        const float up  = kGroundY - base;   // how far the tail is above grade
        const float ign = Clamp01((ts - kIgnS) / 0.6f);
        const float out = Clamp01(up / kFireOutH);
        const float fl  = 0.86f + 0.14f * Noise(beatElapsedMs_ / 45u + 61u);

        // A small fire in the hole before the motor lights, growing to the full
        // jet, then collapsing as the vehicle takes it with it.
        float h = (18.0f + (kFireMaxH - 18.0f) * ign) * (1.0f - 0.78f * out) * fl;
        float a = fminf(1.0f, (ts - kFireStartS) / 0.35f) * (1.0f - 0.55f * out);

        if (h > 2.0f && a > 0.03f) {
            // A STACK OF BLOBS, NOT A TAPERED JET.
            //
            // This was three nested triangle-pair frusta and on glass it was a
            // hard-edged orange RECTANGLE with two boxes inside it. Four faults,
            // all downstream of that one primitive:
            //
            //   * fillTriangle pairs give a FLAT HORIZONTAL TOP. Nothing else
            //     mattered as much -- a flame does not end in a straight line,
            //     and that single edge is what made it read as a box;
            //   * the taper was 1.55 -> 0.95 half-width, a 39% narrowing, which
            //     at 240 px with no antialiasing is indistinguishable from
            //     vertical sides;
            //   * three layers of similar width read as concentric rectangles,
            //     not as a gradient;
            //   * once `out` collapsed the height, 43 px wide by 40 tall is a
            //     square whatever the taper says.
            //
            // The vehicle's own plume in the same frame looks right, and the
            // difference is that DrawPlume's teardrops come to a POINT.
            //
            // So: the same primitive the smoke uses, in flame colours, cooling
            // upward. The top is a round blob rather than an edge, the overlaps
            // are ragged for free, and fire at the bottom becomes smoke at the
            // top as ONE CONTINUOUS THING -- which is what the launch footage
            // actually shows. Collapsed, it is a fireball in the mouth instead of
            // a wide flat bar. It is also cheaper than what it replaces.
            const Rgb hot  = {0xFF, 0xF6, 0xE0};   // FlameCore, at the throat
            const Rgb warm = {0xFF, 0xD2, 0x3E};   // FlameMid
            const Rgb cool = {0xFF, 0x7A, 0x29};   // FlameOuter, handing off to smoke

            // Back to front: the hottest blob is at the bottom and must be drawn
            // last, over the cooler ones stacked above it.
            for (int i = kFireBlobs - 1; i >= 0; --i) {
                const float f = (float)i / (float)(kFireBlobs - 1);  // 0 root, 1 tip
                const uint32_t s = beatElapsedMs_ / 45u + (uint32_t)i * 2654435761u;

                // The axis wavers, and more so with height -- a column of fire is
                // not a symmetric solid of revolution. This is the other half of
                // what stops it reading as a drawn shape.
                const float jx = (Noise(s + 1u) - 0.5f) * 8.0f * f;
                const float rr = (12.0f - 5.5f * f) * (0.80f + 0.40f * Noise(s + 2u));
                const float by = kGroundY - h * f;

                const Rgb c = (f < 0.5f) ? MixRgb(hot, warm, f * 2.0f)
                                         : MixRgb(warm, cool, (f - 0.5f) * 2.0f);
                g.fillCircle((int)((120.0f + jx) * u + shakeX_),
                             (int)(by * u + shakeY_) - dy,
                             (int)(rr * u),
                             Shade(Rgb2c(c), a * (1.0f - 0.5f * f)));
            }
            // The splash at grade -- the jet hitting the deflector and spreading
            // sideways. Low and wide, under the stack, so it reads as spread
            // rather than as a base to the column.
            g.fillEllipse(cx, gy - dy, (int)(kSiloHalfW * 1.5f * u * fl), (int)(4 * u),
                          Shade(pal::FlameCore(), a));
        }
    }

    // ---- altimeter --------------------------------------------------------
    // Re-derived from the published mark, not the reference's own constant; see
    // DEVIATION 2. Suppressed until there is motion to report -- a readout of
    // zero feet is noise, and the frame before first motion is deliberately bare.
    if (ts > kIgnS) {
        const float alt = 8300.0f * powf((float)TPlusMs() / 19000.0f, 2.6f);
        char buf[20];
        snprintf(buf, sizeof(buf), "ALT %06d FT", (int)alt);
        g.setTextSize(1);
        g.setTextDatum(textdatum_t::top_center);
        g.setTextColor(pal::Grey());
        g.drawString(buf, screen_ / 2, (int)(224 * u) - dy);
        g.setTextDatum(textdatum_t::top_left);
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
    // AND IT IS HALOED, because no fixed row can be safe. The lower third sits
    // over the Earth limb, and the limb's bright atmospheric rim (#9FD4E8)
    // SWEEPS DOWN THROUGH THE FRAME as altitude rises -- from y~192 at liftoff
    // to y~235 at the top of the ascent. Whatever row the caption occupies, the
    // rim crosses it at some point in the climb, and paper-white text on a pale
    // blue rim is unreadable. It is unreadable at T+0 today.
    //
    // Moving the caption cannot fix a band that moves. A scrim would cover the
    // art the caption is describing. So: a one-pixel dark halo, which costs
    // eight extra drawString calls and works over the rim, over the ocean, over
    // space and over the fireball without hiding any of them.
    // THE PAD PUTS ITS LOWER THIRD AT THE TOP, and so does the reference -- for
    // the same reason, which is that on the ground camera the bottom of the frame
    // is the GROUND. The grade line is at y=208 and the silo mouth spans 206-212,
    // so the usual 192/204 block would straddle the one piece of furniture that
    // establishes where the vehicle is coming out of, and the altimeter sits at
    // 224 under it. There is no halo that fixes a caption drawn across a silo.
    //
    // Same chord arithmetic as below. y=36 -> 171 px, and the longest caption in
    // this beat ("T+10 - FIRST ROLL MANEUVER", 26 chars) is 156 px of ink, so it
    // clears by 7 px a side. Anything longer added for LIFTOFF breaks that.
    const bool pad = (beat_ == Beat::Liftoff);
    const int y1 = (int)((pad ? 36 : 192) * u) - dy;
    const int y2 = (int)((pad ? 48 : 204) * u) - dy;
    const int cx = screen_ / 2;

    // FOUR OFFSETS, NOT EIGHT. Eight measured at +2.8 ms on a two-line caption
    // and pushed the detonation from 49.7 ms back over its 50 ms bar -- a
    // legibility fix is not worth spending the sequence's tightest beat on.
    // At a six-pixel font the diagonals are already covered by the two cardinals
    // either side of them, so this is half the cost and the same picture.
    static const int8_t kHalo[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    g.setTextColor(lgfx::color888(0x06, 0x08, 0x0A));
    for (int i = 0; i < 4; ++i) {
        g.drawString(l1, cx + kHalo[i][0], y1 + kHalo[i][1]);
        if (l2 && *l2) g.drawString(l2, cx + kHalo[i][0], y2 + kHalo[i][1]);
    }
    // Paper for the headline, grey for the telemetry under it -- the preview's
    // hierarchy. See DEVIATION 1 for why IGNITION is not yellow here.
    g.setTextColor(pal::Paper());
    g.drawString(l1, cx, y1);
    if (l2 && *l2) {
        g.setTextColor(pal::Grey());
        g.drawString(l2, cx, y2);
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
        // The pad motor lights partway INTO the beat -- everything before kIgnS
        // is the vehicle sitting in the hole with the glow building, which is the
        // suspense the whole ground camera exists for.
        case Beat::Liftoff:     burning = PadSeconds() > kIgnS; remaining = 3; break;
        case Beat::Stage1Burn:  burning = true; remaining = 3; break;
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
        // THE AXIAL ENGINE, which is a different thing from the eight attitude
        // engines and the only one that actually moves the vehicle. It burns to
        // shape the trajectory -- the beat's own caption is "MANEUVER TO WINDOW
        // IN SPACE" -- and it is small, steady and liquid, nothing like the
        // solid motors above it.
        case Beat::PostBoost: {
            const float uu = screen_ / 240.0f;
            const float kk = SubjectScale() * uu;
            const float rr = angleDeg_ * 0.01745f;
            const float fl = (7.0f + 3.0f * Noise(beatElapsedMs_ / 60u + 17u)) * kk;
            const struct { float w, l; uint32_t c; } ax[2] = {
                {4.4f, 1.00f, pal::FlameOuter()},
                {2.4f, 0.55f, pal::FlameCore()},
            };
            for (int i = 0; i < 2; ++i) {
                float a0, b0, a1, b1, a2, b2;
                Axis(vx_, vy_, rr, 3.4f * kk, -ax[i].w * 0.5f * kk, a0, b0);
                Axis(vx_, vy_, rr, 3.4f * kk,  ax[i].w * 0.5f * kk, a1, b1);
                Axis(vx_, vy_, rr, 3.4f * kk + fl * ax[i].l, 0, a2, b2);
                g.fillTriangle((int)a0, (int)b0 - dy, (int)a1, (int)b1 - dy,
                               (int)a2, (int)b2 - dy, ax[i].c);
            }
            return;
        }
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
    float len  = (20.0f + 10.0f * flick) * scale_ * u;
    float wide = 1.0f;

    // THE PAD FLAME IS A DIFFERENT SIZE, NOT A DIFFERENT FLAME. The reference's
    // ground shot runs 26 + jitter and grows 12 px/s as the motor comes up to
    // pressure, against 18/13/7 widths instead of 12/8/5 -- roughly half again as
    // wide and twice as long as the in-flight plume. That is a real difference:
    // at sea level the nozzle is over-expanded and the plume is short and fat,
    // and it is also simply closer to the camera. Scaling the one plume rather
    // than writing a second is the same rule DrawNoseCone follows -- one object,
    // one definition, or the two drift.
    if (beat_ == Beat::Liftoff) {
        len  = (26.0f + 22.0f * (flick - 0.75f) * 4.0f
                + fminf(28.0f, (PadSeconds() - kIgnS) * 12.0f)) * u;
        wide = 1.5f;
    }

    // Three nested teardrops, widest and coolest outside. Triangles rather than
    // quadratics: at this size the curve is two pixels of difference.
    const struct { float w, l; uint32_t c; } layers[3] = {
        {12.0f, 1.00f, pal::FlameOuter()},
        { 8.0f, 0.70f, pal::FlameMid()},
        { 5.0f, 0.42f, pal::FlameCore()},
    };
    for (int i = 0; i < 3; ++i) {
        const float hw = layers[i].w * 0.5f * wide * scale_ * u;
        float x0, y0, x1, y1, x2, y2;
        Axis(vx_, vy_, r, tail, -hw, x0, y0);
        Axis(vx_, vy_, r, tail,  hw, x1, y1);
        Axis(vx_, vy_, r, tail + len * layers[i].l, 0, x2, y2);
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, layers[i].c);
    }

    // Sparks, on the pad only. Debris coming off a launch mount is a ground-shot
    // detail -- there is nothing left to shed by the time the chase cam picks the
    // vehicle up, and adding them at altitude would read as the vehicle breaking
    // up. Three per frame, deterministic, straight from the reference.
    if (beat_ == Beat::Liftoff) {
        for (int i = 0; i < 3; ++i) {
            const uint32_t s = beatElapsedMs_ / 30u + (uint32_t)i * 7919u;
            float sx, sy;
            Axis(vx_, vy_, r, tail + Noise(s + 1u) * len, (Noise(s + 2u) - 0.5f) * 16.0f * u, sx, sy);
            g.fillRect((int)sx, (int)sy - dy, 2, 2, pal::Ember());
        }
    }
}

void Director::DrawVehicle(LovyanGFX& g, int dy) const
{
    if (beat_ >= Beat::RvRelease) return; // from here the RV and bus are drawn separately

    const float u = screen_ / 240.0f;
    const float k = SubjectScale() * u;
    const float r = angleDeg_ * 0.01745f;

    // How much stack is left. Stages leave AT the flash, a few hundred ms into
    // their beat -- not at the beat boundary, which would drop them silently one
    // frame early.
    int remaining = 3;
    switch (beat_) {
        case Beat::Liftoff:
        case Beat::Stage1Burn:  remaining = 3; break;
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
    //
    // Lit() is the ONLY thing the pad changes about the vehicle. Same segments,
    // same colours, same proportions, same function -- see Director::sunLift_ for
    // why the ground camera does not get its own paint. It is a no-op everywhere
    // except LIFTOFF and the opening quarter of STAGE 1.
    float along = 0;
    for (int i = 0; i < remaining; ++i) {
        const Segment& s = kSegments[i];
        const float hw = s.wide * 0.5f * k;
        FillQuad(g, vx_, vy_, r, along * k, (along + s.len) * k, hw, dy,
                 Lit(StageColour(i), sunLift_));
        FillQuad(g, vx_, vy_, r, along * k, (along + 2.0f) * k, hw, dy,
                 Lit(pal::Band(), sunLift_));
        along += s.len;
    }

    // Post-boost bus, ahead of the stack.
    DrawBus(g, vx_, vy_, r, k, dy, remaining == 0);

    // The payload cone: the shroud until T+121, the RV itself afterwards. Same
    // geometry either way -- jettisoning an aeroshell reveals the vehicle that
    // was inside it, it does not change its size.
    float bx, by;
    Axis(vx_, vy_, r, -8.0f * k, 0, bx, by);
    if (beat_ < Beat::ShroudEject) {
        DrawNoseCone(g, bx, by, r, k, kShroudLen, kShroudHalfW, dy,
                     Lit(pal::Aeroshell(), sunLift_));
    } else {
        DrawNoseCone(g, bx, by, r, k, kRvLen, kRvHalfW, dy, pal::RvBody(), pal::RvLit());
    }

    // THE EXPOSED, UNLIT BELL. §11 calls the coast "the whole beat"; the bell is
    // what makes it legible -- the player sees an engine that is there and is
    // not firing, which is the suspense.
    //
    // ---- RESOLVING A CONTRADICTION IN THE REFERENCE -------------------------
    // The reference comments this block "exposed engine bell at the tail --
    // visible always, and STARRING during the coast gap" and then guards it with
    // `if(st>=1)`, which stops drawing it the moment the last stage is gone.
    // Comment says always; code says stages-only. They cannot both be right and
    // the port inherited the disagreement as a flicker.
    //
    // Taken against the beat sheet, BOTH halves are half-right:
    //
    //   * The COMMENT is right that a nozzle is always there while something is
    //     propelling. §11 gives every separation the full staging beat --
    //     "burnout -> sep -> ~1 s coast (exposed, UNLIT bell) -> IGNITION" --
    //     and STAGE 3 SEP is a separation. Under `st>=1` that last coast had no
    //     nozzle at all from 300 ms in, so the final staging beat, the one §11
    //     calls the whole point, was the only one drawn without its subject.
    //   * The CODE is right that it is not the same bell. §11 lists post-boost
    //     as "blue porcupine RCS": the PBV is a separate propulsion element, and
    //     inheriting stage 3's big solid bell would claim a fourth solid motor
    //     the vehicle does not have.
    //
    // So: a nozzle is drawn whenever something is attached to propel with, and
    // it changes identity when the propulsion does. Solid bell while a stage is
    // there; the PSRE's smaller nozzle on the bus once they are gone.
    if (remaining >= 1) {
        const float tail = along * k;
        float x0, y0, x1, y1, x2, y2, x3, y3;
        Axis(vx_, vy_, r, tail,             -3.0f * k, x0, y0);
        Axis(vx_, vy_, r, tail,              3.0f * k, x1, y1);
        Axis(vx_, vy_, r, tail + 4.0f * k,   4.5f * k, x2, y2);
        Axis(vx_, vy_, r, tail + 4.0f * k,  -4.5f * k, x3, y3);
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, pal::Nozzle());
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x2, (int)y2 - dy, (int)x3, (int)y3 - dy, pal::Nozzle());
        FillQuad(g, vx_, vy_, r, tail + 3.0f * k, tail + 4.0f * k, 3.0f * k, dy, pal::Throat());
    }
    // No else: once the stages are gone the exposed nozzle is the PBV's AXIAL
    // ENGINE, and DrawBus owns that -- it is the bus's own hardware, not a
    // stand-in for a fourth stage.
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
        // The spent stage keeps ITS OWN colour, which is the whole point of the
        // three-colour scheme: the thing falling away is recognisably the piece
        // that just left the stack.
        const int gone = (beat_ == Beat::Stage1Sep) ? 2 : (beat_ == Beat::Stage2Sep ? 1 : 0);
        FillQuad(g, stageX_, stageY_, r, 0, L, L * 0.35f, dy, StageColour(gone));
        FillQuad(g, stageX_, stageY_, r, 0, L * 0.14f, L * 0.35f, dy, pal::Band());
    }
}

void Director::DrawNoseCone(LovyanGFX& g, float baseX, float baseY, float r, float k,
                            float len, float halfW, int dy, uint32_t col, uint32_t rim) const
{
    float x0, y0, x1, y1, x2, y2;
    Axis(baseX, baseY, r,        0, -halfW * k, x0, y0);
    Axis(baseX, baseY, r,        0,  halfW * k, x1, y1);
    Axis(baseX, baseY, r, -len * k,          0, x2, y2);
    g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, col);
    // A LIT EDGE, which is how a black object stays visible on a black sky.
    //
    // The RV is genuinely dark -- the NG video shows it near-black against the
    // sunlit Earth. It is also lit from one side once it is out in space, which
    // is the frame that matters here: a flat #2B2C28 fill on #000000 is ~4%
    // luminance and simply is not there on this panel. Dark body plus a rim
    // keeps the reference's colour AND obeys the standing rule that legibility
    // is a device-side judgment. Rim of 0 means no highlight (the shroud, which
    // spends its whole life against the bright limb and needs none).
    if (rim) {
        g.drawTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, rim);
    }
}

void Director::DrawBus(LovyanGFX& g, float ox, float oy, float r, float k, int dy,
                       bool showEngine) const
{
    // Lit() carries the pad's daylight; a no-op once sunLift_ has faded, which is
    // long before the bus is ever seen on its own.
    FillQuad(g, ox, oy, r, -8.0f * k, 0.0f, kBusHalfW * k, dy, Lit(pal::Bus(), sunLift_));

    // THE AXIAL ENGINE. Only once the stages are gone -- while they are attached
    // this nozzle is buried inside stage 3 and drawing it puts an engine bell in
    // the middle of a solid motor.
    //
    // It is also the exposed, unlit nozzle §11 needs during the last staging
    // coast: stage 3 leaves, and what is revealed underneath is the thing that
    // will take over.
    if (showEngine) {
        float e0, f0, e1, f1, e2, f2, e3, f3;
        Axis(ox, oy, r, 0.0f,        -1.6f * k, e0, f0);
        Axis(ox, oy, r, 0.0f,         1.6f * k, e1, f1);
        Axis(ox, oy, r, 3.4f * k,     3.0f * k, e2, f2);
        Axis(ox, oy, r, 3.4f * k,    -3.0f * k, e3, f3);
        g.fillTriangle((int)e0, (int)f0 - dy, (int)e1, (int)f1 - dy, (int)e2, (int)f2 - dy, pal::Nozzle());
        g.fillTriangle((int)e0, (int)f0 - dy, (int)e2, (int)f2 - dy, (int)e3, (int)f3 - dy, pal::Nozzle());
        FillQuad(g, ox, oy, r, 2.6f * k, 3.4f * k, 2.0f * k, dy, pal::Throat());
    }

    // The four visible attitude-control nozzles, standing off the flanks.
    for (int i = 0; i < 4; ++i) {
        const float a = kRcsPods[i][0], s = kRcsPods[i][1];
        float x0, y0, x1, y1, x2, y2, x3, y3;
        Axis(ox, oy, r, (a - 1.1f) * k, s * kBusHalfW * k, x0, y0);
        Axis(ox, oy, r, (a + 1.1f) * k, s * kBusHalfW * k, x1, y1);
        Axis(ox, oy, r, (a + 1.1f) * k, s * kPodOut   * k, x2, y2);
        Axis(ox, oy, r, (a - 1.1f) * k, s * kPodOut   * k, x3, y3);
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x1, (int)y1 - dy, (int)x2, (int)y2 - dy, pal::Nozzle());
        g.fillTriangle((int)x0, (int)y0 - dy, (int)x2, (int)y2 - dy, (int)x3, (int)y3 - dy, pal::Nozzle());
    }
}

void Director::DrawShroud(LovyanGFX& g, int dy) const
{
    if (shroudLife_ <= 0) return;
    // ONE PIECE, not clamshell halves.
    //
    // §11's table said "clamshell halves" and the preview's comment credits it
    // to a third-party animation (AiTelly). Neither source is the one §11
    // actually names: THE NG 2007 VIDEO SHOWS A SINGLE SHROUD AT T+121, leaving
    // forward and to the side with a separation-motor flare, and the MM3
    // MIRV-path diagram (item E) shows the same. Both sources we rely on agreed
    // with each other and not with the code. §11 corrected 2026-08-06.
    //
    // A worked example of the standing rule's neighbour: a reference can be
    // wrong about WHAT, too, when the claim came from somewhere else.
    //
    // It outlives its own beat: the shroud goes at T+121 and stage 2 separates
    // at T+123, so it is still in frame for the next separation -- which is the
    // diagram's step 2 as well, the discarded shroud falling behind while the
    // stack flies on.
    const float u = screen_ / 240.0f;
    const float k = SubjectScale() * u;
    const float r = angleDeg_ * 0.01745f;
    const float p = 1.0f - shroudLife_;

    // Lifts off the nose along the flight line, drifts aside and tumbles.
    float bx, by;
    Axis(vx_, vy_, r, -8.0f * k - p * 52.0f * u, p * 16.0f * u, bx, by);
    DrawNoseCone(g, bx, by, r + p * 2.6f, k, kShroudLen, kShroudHalfW, dy, pal::Shell());
}

void Director::DrawRcs(LovyanGFX& g, int dy) const
{
    // ATTITUDE CONTROL ENGINES FIRE ONLY WHEN RE-ORIENTING THE VEHICLE.
    //
    // Per the Stage IV cutaway these eight engines point the vehicle; they do
    // not move it. So they run on exactly the two beats where the vehicle is
    // being turned:
    //
    //   POST-BOOST  "MANEUVER TO WINDOW IN SPACE" -- settling and slewing after
    //               staging, which is also §11's "post-boost = blue porcupine".
    //   PSRE PITCH  the nose-down slew that aims the RV. Asymmetric, below.
    //
    // NOT at RV release -- §11 calls that the quietest moment in the sequence
    // and a lit thruster is not quiet. NOT during bus backaway, which is the
    // retro burn, a different system with its own plumes. NOT on the staging
    // coast, where the whole point is that nothing is lit.
    if (beat_ != Beat::PostBoost && beat_ != Beat::PitchOver) return;
    // §11: "post-boost = BLUE PORCUPINE RCS". Short quills in many directions,
    // pulsing -- the bus talking to itself. Blue because it is cold gas, and
    // because it is the one moment that must not read as a main engine.
    const float u = screen_ / 240.0f;
    const float r = angleDeg_ * 0.01745f;
    // Quills come OUT OF THE NOZZLES, and the bus is at the vehicle origin on
    // both of these beats (it separates later).
    const float qx = vx_, qy = vy_;
    const float k  = SubjectScale() * u;

    // DURING THE PITCH-OVER THE FIRING IS ASYMMETRIC, because that is what turns
    // a vehicle. §11 has the PSRE continue the arc nose-down to aim the RV; a
    // bus venting evenly in every direction would be holding attitude, not
    // changing it. Forward pods on one flank, aft pods on the other: a couple.
    const bool turning = (beat_ == Beat::PitchOver);

    for (int i = 0; i < 4; ++i) {
        const float a = kRcsPods[i][0], s = kRcsPods[i][1];
        if (turning) {
            const bool fwd = (a < -4.0f);
            if ((fwd && s < 0) || (!fwd && s > 0)) continue; // the idle diagonal
        }
        float px, py;
        Axis(qx, qy, r, a * k, s * kPodOut * k, px, py);

        // Three jets per nozzle, fanned -- the "porcupine" is the spread, not
        // one line per pod.
        for (int j = -1; j <= 1; ++j) {
            const float pulse = Noise((uint32_t)(i * 3571 + j * 811) + beatElapsedMs_ / 90u);
            if (!turning && pulse < 0.40f) continue;
            const float spread = j * 0.42f;
            const float ux = cosf(r + spread) * s, uy = sinf(r + spread) * s;
            const float len = (5.0f + 7.0f * pulse) * u * (turning ? 1.15f : 1.0f);
            g.drawLine((int)px, (int)py - dy,
                       (int)(px + ux * len), (int)(py + uy * len) - dy, pal::Rcs());
        }
    }
}

void Director::DrawRvAndBus(LovyanGFX& g, int dy) const
{
    if (beat_ < Beat::RvRelease || beat_ > Beat::Midcourse) return;
    // Past kDotAt the vehicle IS the dot and DrawMatchCut owns it. Drawing both
    // is what put a red dot in the middle of a full-size RV.
    if (beat_ == Beat::Midcourse && BeatProgress() >= kDotAt) return;

    // THE RELEASE IS SILENT. §11: "no ordnance, no bang. The quietest moment in
    // the sequence is the one that matters most." So there is deliberately NO
    // flash, NO streak and NO ember call here -- the RV simply is separate, and
    // the absence is the direction. Anything added to this function to make the
    // moment "land" is the mistake the locked section is guarding against.
    const float u = screen_ / 240.0f;
    const float k = SubjectScale() * u;
    const float r = angleDeg_ * 0.01745f;

    // THE BUS DOES NOT VANISH WHEN THE RV LEAVES IT.
    //
    // This guard used to be `beat_ >= BusBackaway`, and DrawVehicle returns early
    // from RvRelease onward -- so across the whole RV RELEASE beat (8 s of true
    // time) NEITHER function drew the bus, and it reappeared at BusBackaway. A
    // part that comes back is a state bug: the two functions disagreed about
    // which one owned the bus during the handover beat, and the answer was
    // "neither". Same family as the #155 survivor-order bugs -- a guard
    // describing what exists that does not match what has actually separated.
    //
    // The bus exists continuously from the moment the stack is gone. What starts
    // at BusBackaway is the RETRO BURN, not the bus.
    if (beat_ >= Beat::RvRelease) {
        // -8..0, THE SAME SPAN DrawVehicle GIVES IT. It was -4..+4 here, so at
        // the release boundary the bus jumped forward half its length and
        // swallowed the RV. The MM3 diagram is unambiguous: the RV rides on the
        // nose of the post-boost vehicle and leaves forward from it -- it is
        // never inside it.
        DrawBus(g, busX_, busY_, r, k, dy, true);
        if (beat_ >= Beat::BusBackaway) {
            // Retro plumes point FORWARD -- it is thrusting against the direction
            // of travel to open the gap. Exhaust toward the RV, motion away.
            for (int i = -1; i <= 1; i += 2) {
                float ex, ey;
                Axis(busX_, busY_, r + i * 0.4f, -10.0f * u, 0, ex, ey);
                g.drawLine((int)busX_, (int)busY_ - dy, (int)ex, (int)ey - dy, pal::Rcs());
            }
        }
    }

    // THE RV IS THE SAME CONE IT WAS A FRAME AGO. Drawn through DrawNoseCone
    // from the same base offset DrawVehicle uses, so the release boundary moves
    // nothing: one frame it is the tip of the stack, the next it is a free
    // vehicle, and it is in the same place both times. It had its own geometry
    // here (-10..+4 instead of -8..-22) and consequently jumped backwards into
    // the bus at exactly the beat §11 calls the quietest in the sequence.
    float bx, by;
    Axis(vx_, vy_, r, -8.0f * k, 0, bx, by);
    DrawNoseCone(g, bx, by, r, k, kRvLen, kRvHalfW, dy, pal::RvBody(), pal::RvLit());
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

    // Plasma sheath and trail, and BOTH ENDS WERE ON THE WRONG SIDE. The trail
    // was drawn ahead of the vehicle and the sheath behind it -- an RV flying
    // backwards down its own wake. The reference has it right: the ionised trail
    // runs BACK along the flight line (positive along = tail) and the sheath is
    // centred ON the vehicle, because a bow shock wraps the nose rather than
    // following it.
    const float r  = angleDeg_ * 0.01745f;
    const float heat = Clamp01(p * 1.3f);
    float tx, ty;
    Axis(vx_, vy_, r, (18.0f + 38.0f * p) * u, 0, tx, ty);
    g.drawLine((int)tx, (int)ty - dy, (int)vx_, (int)vy_ - dy, pal::Plasma());

    for (int i = 4; i >= 1; --i) {
        const float f = (float)i / 4.0f;
        g.fillCircle((int)vx_, (int)vy_ - dy, (int)((2 + 7 * f * heat) * u),
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
    //
    // Trimmed again to 1.15x, 2026-08-06, to buy the beat real margin. Area goes
    // as r^2, so 1.35 -> 1.15 is a 27% cut for ~2 ms, and it costs only how far
    // the glow REACHES -- the ring count stays at four, so the wash does not
    // gain any banding it did not already have. The billows are untouched: they
    // are the fireball, and §11 is explicit that the fix for a slow detonation
    // is a cheaper cloud and never a smaller one.
    const float haloA = 0.34f * (1.0f - cool * 0.6f);
    for (int i = kHaloRings; i >= 1; --i) {
        const float f = (float)i / (float)kHaloRings;
        g.fillCircle((int)cx, (int)capCY - dy, (int)(capR * 1.15f * f),
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

bool Director::OnMapSide() const
{
    if (beat_ == Beat::Midcourse) return BeatProgress() > 0.45f;
    return beat_ == Beat::MatchCut && BeatProgress() > 0.5f;
}

float Director::TrackFraction() const
{
    // Fraction of the flight flown, by the published clock: impact is T+1,896 s.
    constexpr float kImpactMs = 1896000.0f;
    return Clamp01((float)TPlusMs() / kImpactMs);
}

void Director::TrackPoint(float f, float& x, float& y) const
{
    BuildGlobeBasis();
    float p[3];
    GreatCircle(Clamp01(f), p);
    const float u = screen_ / 240.0f;
    // The near-hemisphere flag is discarded on purpose: the whole GOLF-07 arc
    // sits within 53 deg of the disc centre, and a caller that wanted to hide the
    // dot behind the Earth would be hiding the one thing the screen is for.
    GlobePt(p[0], p[1], p[2], screen_ * 0.5f, kGlobeR * u, x, y);
}

void Director::DrawMap(LovyanGFX& g, int dy) const
{
    BuildGlobeBasis();
    const float u = screen_ / 240.0f;
    const float R = kGlobeR * u;
    const float c = screen_ * 0.5f;
    const int   ci = (int)c;

    // ---- ocean disc -------------------------------------------------------
    // The dimmest thing in the frame, per the stroke hierarchy: track and
    // vehicle brightest, then coastlines, then graticule, then this. It is also
    // the cheapest -- a span fill at 28.6 ns/px against 1.06 us/px for a line,
    // so 38,000 px of disc costs less than a thousand pixels of stroke.
    g.fillCircle(ci, ci - dy, (int)R, pal::Ocean());

    // ---- graticule --------------------------------------------------------
    // SIX MERIDIANS AND THREE PARALLELS, not twelve and five, and the reason is
    // measured rather than aesthetic: cost is per LINE PIXEL (1.06 us/px), and
    // the graticule is a handful of very long lines while the coastline is
    // hundreds of very short ones. Twelve and five cost 3.75 ms -- half again
    // what every coastline on the planet costs -- and bought clutter, because at
    // R=110 twelve meridians bunch into mush near the limb.
    //
    // Its only job is to say "sphere", and the limb circle plus the coastlines'
    // own foreshortening already carry most of that. Nothing in a graticule
    // answers "where is it" or "how far along"; the arc, the dot and the two
    // endpoints do.
    for (int mi = 0; mi < 6; ++mi) {
        const float lon = -180.0f + mi * 60.0f;
        float px = 0, py = 0; bool pv = false;
        for (int j = 0; j <= 18; ++j) {
            const float lat = -90.0f + j * 10.0f;
            float w[3]; UnitVec(lon, lat, w);
            float x, y; const bool v = GlobePt(w[0], w[1], w[2], c, R, x, y);
            if (v && pv) g.drawLine((int)px, (int)py - dy, (int)x, (int)y - dy, pal::Graticule());
            px = x; py = y; pv = v;
        }
    }
    for (int pi = 0; pi < 3; ++pi) {
        const float lat = 45.0f - pi * 45.0f;   // +45, 0, -45
        float px = 0, py = 0; bool pv = false;
        for (int j = 0; j <= 24; ++j) {
            const float lon = -180.0f + j * 15.0f;
            float w[3]; UnitVec(lon, lat, w);
            float x, y; const bool v = GlobePt(w[0], w[1], w[2], c, R, x, y);
            if (v && pv) g.drawLine((int)px, (int)py - dy, (int)x, (int)y - dy, pal::Graticule());
            px = x; py = y; pv = v;
        }
    }

    // ---- coastlines -------------------------------------------------------
    // A segment is drawn only when BOTH ends are on the near hemisphere. The
    // alternative -- clipping the segment to the limb -- buys at most half a
    // pixel here, because the data is decimated to ~1 px and everything near the
    // limb is foreshortened to less than that.
    constexpr float kInv = 1.0f / 32767.0f;
    for (int i = 0; i < kCoastCount; ++i) {
        const GeoVec* v = kCoast[i].v;
        const int     n = kCoast[i].n;
        float px = 0, py = 0; bool pv = false;
        for (int a = 0; a <= n; ++a) {
            const GeoVec& p = v[a == n ? 0 : a];      // close the ring
            float x, y;
            const bool vis = GlobePt(p.x * kInv, p.y * kInv, p.z * kInv, c, R, x, y);
            if (vis && pv) g.drawLine((int)px, (int)py - dy, (int)x, (int)y - dy, pal::Coast());
            px = x; py = y; pv = vis;
        }
    }

    // ---- limb -------------------------------------------------------------
    // The one line that makes the disc a sphere rather than a circle of noise.
    g.drawCircle(ci, ci - dy, (int)R, pal::Graticule());

    // ---- the track --------------------------------------------------------
    // The SAME great circle the dot rides, so the dot cannot drift off it -- and
    // on a sphere it finally reads as an arc. On the flat map this bowed 0.83 px,
    // which is not a bug in the map: a near-meridional great circle IS a straight
    // line under equirectangular. See kGlobeTilt for why the view is tilted 30
    // deg off the arc's own plane, and what happens at zero.
    const float flown = TrackFraction();
    float prevX = 0, prevY = 0; bool prevV = false;
    for (int i = 0; i <= 60; ++i) {
        const float t = (float)i / 60.0f;
        float p[3]; GreatCircle(t, p);
        float x, y;
        const bool vis = GlobePt(p[0], p[1], p[2], c, R, x, y);
        if (vis && prevV) {
            g.drawLine((int)prevX, (int)prevY - dy, (int)x, (int)y - dy,
                       t <= flown ? pal::Green() : pal::GreenDim());
        }
        prevX = x; prevY = y; prevV = vis;
    }

    // ---- endpoints --------------------------------------------------------
    // BOTH of them, because two fixed marks with a dot crawling between is the
    // whole reason the camera does not follow the vehicle: it is what turns the
    // screen from "here is a dot" into "here is how far along it is".
    float lx, ly, ax, ay;
    float Lv[3], Av[3];
    UnitVec(kLaunchLon, kLaunchLat, Lv);
    UnitVec(kAimLon,    kAimLat,    Av);
    if (GlobePt(Lv[0], Lv[1], Lv[2], c, R, lx, ly)) {
        g.drawCircle((int)lx, (int)ly - dy, (int)(3 * u), pal::Green());
    }
    if (GlobePt(Av[0], Av[1], Av[2], c, R, ax, ay)) {
        g.drawCircle((int)ax, (int)ay - dy, (int)(5 * u), pal::Red());
        g.drawFastHLine((int)ax - (int)(8 * u), (int)ay - dy, (int)(16 * u), pal::Red());
        g.drawFastVLine((int)ax, (int)ay - (int)(8 * u) - dy, (int)(16 * u), pal::Red());
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
    //
    // AND THE DOT MUST BE ON THE TRACK. The first version drew the map's track as
    // a decorative arc in screen coordinates while the dot sat at vx_/vy_ --
    // screen centre -- about 19 px off it. Two independent computations of "where
    // the vehicle is", which is the same failure the match-cut rule exists to
    // prevent, just on one side of the cut instead of across it.
    //
    // Now there is one: TrackPoint(). UpdateKinematics eases vx_/vy_ onto it
    // during the shrink, so by the time the map opens the dot is already on the
    // great circle, and DrawMap draws that same great circle.
    //
    // AND THE DOT ONLY EXISTS ONCE THE VEHICLE HAS BECOME IT. Drawn from the
    // start of midcourse it was a red pip sitting inside a full-size RV for a
    // third of the beat, which reads as a marker attached to the vehicle rather
    // than as the vehicle seen from far enough away.
    if (beat_ == Beat::Midcourse && BeatProgress() < kDotAt) return;
    g.fillCircle((int)vx_, (int)vy_ - dy, 2, pal::Red());
}

void Director::Render(LovyanGFX& g, int yOffset)
{
    const int dy = yOffset;

#ifdef ANIM_PROFILE
    // ZEROED PER FRAME, not left to whatever DrawSmoke last wrote. Only LIFTOFF
    // draws smoke, so on every other beat the reading is stale -- and the harness
    // takes a per-beat MAX, so one stale sample at a beat boundary is enough to
    // attribute LIFTOFF's cost to STAGE 1. It did: 1.9 ms, on a beat with no
    // smoke in it. Worse, it read 0.0 the first time this was measured and looked
    // correct, because the beat then happened to end after the last puff had
    // died. An instrument that is right by luck is the kind that gets believed.
    smokeUs_ = 0;
#endif

    // THE GROUND CAMERA. It owns its whole frame: no sky gradient and no Earth
    // limb, because at grade the limb IS the ground line and drawing both would
    // put a horizon in the sky above a horizon on the floor.
    //
    // THE CUT OUT OF IT IS A CUT ON ABSENCE, not a match cut. §11's match-cut
    // rule governs the ascent -> map transition and matches on SHAPE (the vehicle
    // becomes a dot; the map opens on that dot). This one matches on nothing,
    // deliberately: the ground camera holds until the vehicle has left the frame
    // -- it is gone by ~39% of the beat and the rest of the shot is smoke -- and
    // the cut happens because the subject has departed, which is the oldest
    // motivated cut there is and needs no device. What carries the continuity
    // across it instead is that the vehicle is the same object at the same size
    // in the same paint on both sides; see Director::sunLift_.
    if (beat_ == Beat::Liftoff) {
        DrawLiftoff(g, dy);
        DrawCaption(g, dy);
        return;
    }

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

    if (beat_ == Beat::Midcourse || beat_ == Beat::MatchCut) {
        // THE CUT. Before it, the vehicle over the limb; after it, the map. The
        // limb does not survive the cut -- the camera is no longer beside the
        // vehicle -- and drawing it under the map was what made the placeholder
        // globe read as a sticker rather than as a change of view.
        if (OnMapSide()) {
            g.fillScreen(pal::Space());
            DrawMap(g, dy);
        } else {
            DrawSky(g, dy);
            DrawEarthLimb(g, dy);
            DrawPenaids(g, dy);
            DrawRvAndBus(g, dy);
        }
        DrawMatchCut(g, dy);
        DrawCaption(g, dy);
        return;
    }

    DrawSky(g, dy);
    DrawEarthLimb(g, dy);

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
