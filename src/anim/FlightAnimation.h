// FlightAnimation.h -- the Missileer flight sequence, as a liftable module.
//
// Implements the beat sheet locked in docs/missileer-game-design.md §11
// ("Animation art direction (LOCKED)") and the phase structure in §7
// ("FLIGHT DIRECTOR -- real-time flights, time-division views").
//
// ---------------------------------------------------------------------------
// THIS IS THE FLIGHT DIRECTOR, NOT TEST SCAFFOLDING.
//
// src/animtest_main.cpp is a harness that drives it; the game will drive it
// too, and neither owns it. So the surface is deliberately narrow and knows
// nothing about either caller:
//
//   * it draws into a CALLER-SUPPLIED LovyanGFX target and never touches the
//     panel itself. The base type, not LGFX_Sprite, precisely so the caller
//     chooses: the harness hands it a full-screen PSRAM sprite, a C3 product
//     build would hand it a BandCanvas band at a time (which is why Render()
//     takes a y-offset instead of assuming the target is the screen), and a
//     PSRAM-less fallback can hand it the panel directly;
//   * it holds no wall clock -- Update() is given the time, so the same code
//     runs at true T+ marks, at compressed bench speed, or stepped frame by
//     frame from a paused harness;
//   * it has no touch, no serial, no network, no game state. A launch is a
//     resolved vote somewhere else; this draws the consequence.
//
// The one thing it DOES own is the beat table, because the beats and their
// published T+ marks are the specification, not a rendering detail.
// ---------------------------------------------------------------------------
//
// LOOK TARGET: docs/reference/missileer-launch-animation-preview.html. Every
// hex and every T+ mark in the .cpp is lifted from it. Where it and the design
// doc disagree the DOC WINS and the deviation is written down -- see the
// DEVIATIONS block at the top of the .cpp.
//
// Palette: §11's four accents. AMBER IS ABSENT BY CONSTRUCTION -- it means
// EXERCISE and nothing else, and a launch animation that borrowed it for a
// warning glow would quietly destroy the one colour a player must be able to
// read across a desk. See the palette note in the .cpp.
#pragma once

#include <Arduino.h>
#include <LovyanGFX.hpp>

namespace missileer {
namespace flight {

// ---------------------------------------------------------------------------
// The beat sheet. Order is the flight; the marks are PUBLISHED, not chosen.
//
// Structural beats only -- a beat exists where the ART CHANGES (a stage leaves,
// a motor lights, an attitude reverses). The published telemetry marks that
// change nothing structural (T+19 Mach 1, T+39 Mach 3, T+45 second roll) are
// carried by the caption track instead; see CurrentCaption(). That split is why
// there are 17 beats and 20 captions.
// ---------------------------------------------------------------------------
enum class Beat : uint8_t {
    // THE ONLY BEAT SHOT FROM THE GROUND. A silo camera: fixed, side-on, the
    // vehicle rising out of frame and the shot HOLDING on the smoke after it has
    // gone. Everything from Stage1Burn onward is the chase cam.
    //
    // It is a beat rather than a camera split inside Stage1Burn for three
    // reasons, and the first is the instrument: per-beat worst-case frame time is
    // this rig's deliverable, and burying the sequence's most expensive new art
    // (the smoke) inside a beat that also contains 50 s of cheap chase-cam would
    // average its cost into invisibility -- which is exactly how the detonation's
    // caption halo nearly hid. Second, tap/swipe/hold all operate on beats, so a
    // split would make the most iconic moment in the sequence the least iterable.
    // Third, §7's flight director may want liftoff separately gated, and
    // expressing it as a beat now is cheaper than refactoring one out later.
    Liftoff,       // T+0    -- silo, hot launch, ground camera
    Stage1Burn,    // T+10   -- chase cam. First stage running, the limb still flat
    Stage1Sep,     // T+62   -- STAGING BEAT (burnout -> sep -> coast -> ignition)
    Stage2Burn,    // T+65   -- second stage running, limb sinking
    ShroudEject,   // T+121  -- clamshell halves. TWO SECONDS before stage 2 sep
    Stage2Sep,     // T+123  -- STAGING BEAT
    Stage3Burn,    // T+126
    Stage3Sep,     // T+177  -- STAGING BEAT, last one
    PostBoost,     // T+180  -- blue porcupine RCS
    PitchOver,     // T+205  -- PSRE continues the arc NOSE-DOWN
    RvRelease,     // T+225  -- SILENT. No ordnance, no flash
    BusBackaway,   // T+233  -- bus retros away from the RV
    PenaidDeploy,  // T+245  -- penaids leave the BACKING bus
    Midcourse,     // T+260  -- the long quiet; §7 hands the screen back
    Reentry,       // T+1806 = impact-90 s -- plasma, decoys burning out
    Detonation,    // T+1896 = impact -- Hood/Badger palette, cooling to rust
    MatchCut,      // the cut: vehicle shrinks to a dot, map opens on that dot
    COUNT
};

/** Timing mode. See TIME MODES in the .cpp for why these are not a scale factor. */
enum class TimeMode : uint8_t {
    Compressed, // whole sequence ~90 s -- for iterating on art
    TrueTime,   // beats at their published T+ marks -- for judging whether it FEELS right
};

/** Human-readable beat name, for HUD chrome and log tags. Never null. */
const char* BeatName(Beat b);

/** Duration of a beat in the given mode, milliseconds. */
uint32_t BeatDurationMs(Beat b, TimeMode mode);

/** Wall-clock T+ mark at which a beat begins in TRUE time, milliseconds. */
uint32_t BeatTrueStartMs(Beat b);

/** Total sequence length in the given mode, milliseconds. */
uint32_t SequenceDurationMs(TimeMode mode);

/**
 * The director.
 *
 * Owns beat state and the small amount of physics the art needs (altitude,
 * arc angle, the debris that separations throw). Deterministic: the same
 * elapsed time always produces the same frame, so a paused harness can step
 * and a recording can be reproduced.
 */
class Director {
public:
    /**
     * @param screen  square panel edge in px (240 on the Kit S3). The art is
     *                composed for a ROUND face -- nothing load-bearing sits in
     *                a corner, because on this SKU the corners do not exist.
     */
    void Begin(int screen, TimeMode mode);

    /** Restart the whole sequence at T+0. */
    void Restart();

    /** Jump to a beat's first frame. Clamped to the sequence. */
    void Seek(Beat b);

    /** Advance to the next/previous beat, wrapping at the ends. */
    void StepBeat(int delta);

    /** Switch mode, holding the CURRENT BEAT (not the elapsed time -- see .cpp). */
    void SetMode(TimeMode mode);

    /**
     * Advance the sequence by `dtMs`. Pausing is the caller's job: a paused
     * caller simply stops calling this, which keeps "paused" out of the
     * module's state and makes single-stepping free.
     */
    void Advance(uint32_t dtMs);

    /** Draw the current frame. `yOffset` supports banded rendering (see header). */
    void Render(LovyanGFX& g, int yOffset = 0);

    /**
     * The NG-style lower third for this instant: up to two lines, either of
     * which may be "" (never null).
     *
     * Part of the picture, not chrome -- the reference video's captions ARE how
     * the ascent tells you what it is doing, and a silent 62-second first stage
     * is the failure mode they exist to prevent. Exposed as well as drawn so a
     * product HUD can place them itself without re-deriving the marks.
     */
    void CurrentCaption(const char*& line1, const char*& line2) const;

    Beat CurrentBeat() const { return beat_; }
    TimeMode Mode() const { return mode_; }

#ifdef ANIM_PROFILE
    /**
     * Microseconds spent in the LIFTOFF smoke pass on the last Render().
     *
     * BENCH-ONLY, and behind a flag because the module's contract is that it
     * holds no wall clock -- a product build must not call micros(). The smoke is
     * the one genuinely new cost this beat introduces and the only draw the
     * harness cannot isolate from outside (it is one call inside one frame), so
     * the measurement has to come from in here or not at all. Its worst case is
     * the number that decides whether the particle count stands.
     */
    uint32_t SmokeUs() const { return smokeUs_; }
#endif
    /** 0..1 through the current beat. */
    float BeatProgress() const;
    /** Simulated T+ in ms -- the published mark, in BOTH modes (see .cpp). */
    uint32_t TPlusMs() const;
    /** True once the sequence has run past the final beat. */
    bool Finished() const { return finished_; }

    /**
     * Where the vehicle is on screen, in px. The MATCH-CUT depends on this
     * being readable from outside: §11's rule is that the ascent ends by
     * shrinking the vehicle to a dot and the map opens with THAT SAME DOT, and
     * a cut that recomputes the position on the other side is exactly how the
     * rule gets broken by a later refactor.
     */
    void VehicleScreenPos(float& x, float& y) const { x = vx_; y = vy_; }

private:
    void EnterBeat(Beat b);
    void UpdateKinematics(uint32_t dtMs);
    /** scale_, floored so the subject stays readable on glass. See the .cpp. */
    float SubjectScale() const;

    /** Beat progress expressed in the look target's own LIFTOFF seconds (0..7). */
    float PadSeconds() const;
    /** Bottom of the vehicle body at pad time `ts`, in 240-space. The hot launch. */
    static float PadBase(float ts);

    // ---- art helpers, one per locked beat element -------------------------
    void DrawSky(LovyanGFX& g, int dy) const;
    void DrawEarthLimb(LovyanGFX& g, int dy) const;
    /** The ground camera: silo, glow, smoke, and the vehicle clipped at grade. */
    void DrawLiftoff(LovyanGFX& g, int dy) const;
    /**
     * The ground smoke.
     *
     * CLOSED-FORM, not an integrated particle system: every puff is a pure
     * function of (spawn index, beat elapsed), so there is no per-frame state to
     * step and a replayed beat is identical frame for frame -- the same promise
     * the rest of the module makes, kept without another array to keep in sync.
     * It also bounds itself: the spawn window is fixed, so the cloud can never
     * be more than kSmokeMax puffs however long the beat runs or how fast.
     */
    void DrawSmoke(LovyanGFX& g, int dy) const;
    void DrawCaption(LovyanGFX& g, int dy) const;
    void DrawVehicle(LovyanGFX& g, int dy) const;
    /**
     * The payload cone, drawn from vx_/vy_ in the stack's own frame.
     *
     * ONE DEFINITION, because it is one object: under the shroud it is the
     * shroud, after jettison it is the RV, and after release it is still the RV.
     * Drawn from two places with two geometries it jumped backwards into the bus
     * at the release boundary.
     */
    void DrawNoseCone(LovyanGFX& g, float baseX, float baseY, float r, float k,
                      float len, float halfW, int dy, uint32_t col, uint32_t rim = 0) const;
    /**
     * The post-boost vehicle: body plus its four attitude-thruster pods.
     *
     * One definition, drawn from the stack and after the release, for the same
     * reason DrawNoseCone is one definition -- and the pods matter because they
     * are the hardware §11's "porcupine" comes out of. Quills with no nozzles
     * under them read as light with no source.
     */
    void DrawBus(LovyanGFX& g, float ox, float oy, float r, float k, int dy,
                 bool showEngine) const;
    void DrawPlume(LovyanGFX& g, int dy) const;
    void DrawDebris(LovyanGFX& g, int dy) const;
    void DrawSeparationFlash(LovyanGFX& g, int dy) const;
    void DrawShroud(LovyanGFX& g, int dy) const;
    void DrawRcs(LovyanGFX& g, int dy) const;
    void DrawRvAndBus(LovyanGFX& g, int dy) const;
    void DrawPenaids(LovyanGFX& g, int dy) const;
    void DrawReentry(LovyanGFX& g, int dy) const;
    void DrawDetonation(LovyanGFX& g, int dy) const;
    void DrawMap(LovyanGFX& g, int dy) const;
    void DrawMatchCut(LovyanGFX& g, int dy) const;

    /** True once the cut has happened and the map owns the frame. */
    bool  OnMapSide() const;
    /** How far along the launch->aim great circle the vehicle is, 0..1. */
    float TrackFraction() const;
    /** Screen position of a point on the track. THE DOT COMES FROM HERE. */
    void  TrackPoint(float f, float& x, float& y) const;

    int      screen_ = 240;
    TimeMode mode_   = TimeMode::Compressed;
    Beat     beat_   = Beat::Liftoff;
    uint32_t beatElapsedMs_ = 0;
    uint32_t seqElapsedMs_  = 0;
    bool     finished_ = false;

    // Kinematics the art reads. Not a simulation -- the numbers exist to make
    // the drawing consistent between beats, not to be right.
    float vx_ = 0, vy_ = 0;      // vehicle screen position, px
    float angleDeg_ = 0;         // 0 = straight up; positive = pitched downrange
    float altitude_ = 0;         // 0..1, drives limb sink and star brightness
    float scale_ = 1.0f;         // vehicle size multiplier; ends at "a dot"

    // Separation debris: spent stages and embers, thrown along the flight line.
    struct Ember { float x, y, vx, vy, life; };
    static constexpr int kEmbers = 18;
    Ember embers_[kEmbers] {};
    float stageX_ = 0, stageY_ = 0, stageLife_ = 0;

    // Penaids/decoys, deployed from the BACKING bus and tracked to reentry so
    // the same objects are the ones that burn out.
    static constexpr int kPenaids = 6;
    struct Penaid { float x, y, vx, vy, burn; };
    Penaid penaids_[kPenaids] {};

    float busX_ = 0, busY_ = 0;  // bus, once it starts backing away
    // The RV deliberately has NO position of its own: it is drawn from vx_ in
    // the stack's frame, so "the tip of the stack" and "the free vehicle" cannot
    // disagree about where it is.

    // Clamshell halves outlive their own beat -- the shroud goes at T+121 and
    // stage 2 separates at T+123, so the halves are still in frame during the
    // next beat. 0 = gone.
    float shroudLife_ = 0;

    /**
     * DAYLIGHT, 1 at the pad and 0 at altitude -- and it is the answer to the one
     * trap the ground camera sets.
     *
     * The look target draws its pad vehicle #e8e4d6 (white, sunlit) and its
     * ascent vehicle olive. Across an instant cut a colour change on the same
     * object does not read as a change of light; it reads as a DIFFERENT OBJECT,
     * which would break the cut in the same way a size change would.
     *
     * So the pad vehicle is not a second colour scheme. It is the SAME airframe
     * -- same three stage colours, same tan bands, drawn by the same DrawVehicle
     * -- lifted toward daylight by this one scalar, which then fades out across
     * the opening quarter of STAGE 1. The frame before the cut and the frame
     * after it are the same object at the same size in the same paint, and only
     * the exposure moves. That is also what a real cut from a pad camera to a
     * high chase cam looks like, so the honest choice and the legible one agree.
     */
    float sunLift_ = 0;

    // Camera shake, decaying over the first 2.2 pad-seconds. Held here rather
    // than recomputed per draw so the ground furniture, the smoke and the vehicle
    // cannot shake by different amounts in the same frame.
    float shakeX_ = 0, shakeY_ = 0;

#ifdef ANIM_PROFILE
    mutable uint32_t smokeUs_ = 0;
#endif
};

} // namespace flight
} // namespace missileer
