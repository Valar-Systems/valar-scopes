#pragma once

/* ===========================================================================
 * OVERHEAD ALERTS: the pure half (docs/v17-overhead-card.md, "Owner controls").
 *
 * ONE switch and ONE radius for every overhead behaviour -- the card, the cyan
 * "Look up!" ring and the "Look up!" phone push (Daniel, 2026-10-11). The radius
 * is the owner's dropdown value (1, 2, 3 or 5) in the device's own unit; nothing
 * in the firmware holds a second one. Graded on the host in
 * test/host/test_overhead.cpp before it reaches the panel.
 * ======================================================================== */

#include <cmath>
#include <cstdint>

namespace overhead {

constexpr bool     DEFAULT_ON          = true;      // a unit that never saved: ON
constexpr int      DEFAULT_DISTANCE    = 3;         // in the device's unit
constexpr int      DISTANCES[4]        = { 1, 2, 3, 5 };
constexpr uint32_t CARD_SPACING_MS     = 60000;     // internal: >= 60 s between any two cards
constexpr uint32_t CARD_IDLE_MS        = 30000;     // a card only when nobody touched for 30 s
constexpr uint32_t CARD_SHOW_MS        = 8000;      // how long a card stays up

/// The stored distance, snapped onto the dropdown: the nearest of 1/2/3/5, a tie to the smaller
/// (fewer cards). Anything unreadable or non-positive is the default. The old free field allowed
/// 0.5 steps, so a migrated device can hold 2.5 or 4.
inline int SnapDistance(double stored)
{
    if (!(stored > 0.0)) return DEFAULT_DISTANCE;
    int best = DISTANCES[0];
    double bestGap = std::fabs(stored - best);
    for (int d : DISTANCES) {
        const double gap = std::fabs(stored - d);
        if (gap < bestGap - 1e-9) { best = d; bestGap = gap; }   // strictly nearer; ties keep the smaller
    }
    return best;
}

/// The radius in km for a dropdown value in the device's unit. `kmPerUnit` comes from the firmware's
/// one conversion (units::ToKm(1, unit): mi, km or nmi), so there is no second set of constants. A
/// unit change keeps the VALUE ("3" stays 3) and reinterprets it -- it is never converted.
inline double RadiusKm(int distance, double kmPerUnit)
{
    return (double)distance * kmPerUnit;
}

/// Why an overhead edge did or did not become a card.
enum class Card : uint8_t { Show, Off, Busy, Spacing, Screen, Alert };

/// The card decision for one overhead edge (the first frame of a pass inside the radius).
///  on          the owner's toggle
///  sinceTouch  ms since the last touched sample
///  screenOk    a face the card may cover (Radar / List / Stats, nothing open on top)
///  alertActive the emergency/military flash is up (precedence: the flash wins)
///  sinceCard   ms since the last card, or a huge number if there has been none
inline Card DecideCard(bool on, uint32_t sinceTouch, bool screenOk, bool alertActive, uint32_t sinceCard)
{
    if (!on) return Card::Off;
    if (alertActive) return Card::Alert;
    if (!screenOk) return Card::Screen;
    if (sinceTouch < CARD_IDLE_MS) return Card::Busy;
    if (sinceCard < CARD_SPACING_MS) return Card::Spacing;
    return Card::Show;
}

inline const char* CardName(Card c)
{
    switch (c) {
    case Card::Show: return "show";
    case Card::Off: return "off";
    case Card::Busy: return "busy";
    case Card::Spacing: return "spacing";
    case Card::Screen: return "screen";
    default: return "alert";
    }
}

} // namespace overhead
