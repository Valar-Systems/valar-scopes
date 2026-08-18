// DrawDrill — the drill, rendered. Holds no state.
//
// ===========================================================================
// EVERYTHING IT KNOWS ARRIVES IN THE ARGUMENTS.
//
// No statics, no members, no "last frame" cache, no animation counters of its
// own. Called twice with the same State it draws the same pixels, which is what
// makes it assertable at all: `DrillMachine` decides, this draws, and the seam
// between them is a struct.
//
// This is NOT a pure TU and does not pretend to be. It includes BandCanvas and
// therefore LovyanGFX, so it is excluded from test/host/run.sh's purity gate by
// that script's explicit file list. If somebody widens that list to "all of
// src/game", the gate will fail on this file — correctly, and the fix is to
// narrow the list again rather than to widen the include path.
//
// SCREEN_STATE_PROBE: every draw call is accompanied by a Note* that records
// what was decided, on the same line, taking the same arguments. That is the
// design constraint from src/probe/ScreenStateProbe.cpp — a probe that logs
// what a renderer MEANT to draw is a restatement, and a restatement drifts. Two
// things on one line drift visibly.
// ===========================================================================

#ifndef BLIPSCOPE_GAME_DRAWDRILL_H
#define BLIPSCOPE_GAME_DRAWDRILL_H

#include "../eam/EamTheme.h"
#include "BandCanvas.h"
#include "DrillMachine.h"
#include "GameFormat.h"

namespace game {

/// Render one frame of the drill. `nowUs` drives only cosmetic motion that the
/// machine does not own (the strip's scan line); every decision comes from `st`.
///
/// The palette is PASSED IN, not chosen here. Rail 4 locks it, and the app owns
/// which of the two it is running -- a renderer that picked its own would be a
/// second place the colours are decided.
/// `cfg` is here because the DISPLAY UNIT is a served rule, not a local choice.
///
/// The first version of this file rendered the deviation in tenths, with a
/// comment above it citing the 0.2 s bucket -- so the same sortie read `0.3 s`
/// on the device and `0.4` on the leaderboard. Nothing was wrong with either
/// number in isolation, which is why it survived review: it is the contract
/// between them that was never exercised. The bucket now arrives from the
/// server through Config, and test/host/test_game_format.cpp grades the result
/// against strings the server itself produced.
void DrawDrill(BandCanvas& c, const eam::Palette& palette, const State& st, const Config& cfg,
               uint64_t nowUs);

}  // namespace game

#endif  // BLIPSCOPE_GAME_DRAWDRILL_H
