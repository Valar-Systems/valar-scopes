#pragma once
// P5 W (REG-W amendment 1): the aircraft marker's geometry, ONE body for two targets. It was
// AircraftManager::DrawAircraftTriangle's body, moved here with its text otherwise unchanged: the BandCanvas draws the
// marker through it, and the rain outline's mask (include/OutlineW.h) rasterises its 8 copies through it. Both targets
// take int32_t arguments, so every float expression is converted at the same boundary.
//
// Two compiled copies of one expression can still fuse differently (madd.s / msub.s; docs/speed-p5p6.md section 10).
// So REG-W's G-EXACT reads the built ELF: every expression's operations and fusing must match main's compiled
// DrawAircraftTriangle in both instantiations before any board row runs. Pure C++11: test/host/test_p5_outline.cpp
// compiles this same file.
#include <cmath>
#include <stdint.h>

#ifndef radians   // Arduino's (the device); the same expression for the host test
#define DEG_TO_RAD_MARKERDRAW 0.017453292519943295769236907684886
#define radians(deg) ((deg)*DEG_TO_RAD_MARKERDRAW)
#endif

namespace markerdraw {

// noinline: each instantiation stays one named function, as main's DrawAircraftTriangle was, so G-EXACT can read its
// float instructions by symbol (the mask's would otherwise dissolve into the contact loop, where no reader can find it).
template <typename Target>
__attribute__((noinline)) void Aircraft(Target& backbuffer, int x, int y, int cat, float trueTrack, int radarUpDeg,
                                        uint32_t color)
{
    // Type-aware marker, keyed off the ADS-B emitter category (normalised to
    // OpenSky's numbering for both the cloud and local feeds). Heading-less types
    // (rotorcraft, balloons) get a fixed glyph; everything else is a heading dart
    // whose size grows with the aircraft's weight class so a heavy reads
    // differently from a light single at a glance.

    // rotorcraft: a hub with two crossed rotor blades. Non-directional, since
    // helicopters routinely hover and yaw independently of their ground track.
    if (cat == 8) {
        backbuffer.drawLine(x - 5, y - 5, x + 5, y + 5, color);
        backbuffer.drawLine(x - 5, y + 5, x + 5, y - 5, color);
        backbuffer.fillCircle(x, y, 2, color);
        return;
    }

    // lighter-than-air (balloon / airship): a simple ring, also non-directional.
    if (cat == 10) {
        backbuffer.drawCircle(x, y, 4, color);
        return;
    }

    // heading unit vector (forward) and its perpendicular (right "wing").
    // Window-up subtracts the screen-top bearing so darts stay aligned with the
    // rotated picture (positions rotate inside ProjectCoordinateToScreen).
    const float headingScreen = radians(trueTrack - (float)radarUpDeg);
    const float dx = std::sin(headingScreen);
    const float dy = -std::cos(headingScreen);
    const float px = -dy;
    const float py = dx;

    // a dart pointing along the heading: tip ahead of the point, base behind it
    auto dart = [&](float tip, float base, float halfWidth) {
        const float tipX  = x + dx * tip,                 tipY  = y + dy * tip;
        const float leftX = x - dx * base + px * halfWidth, leftY = y - dy * base + py * halfWidth;
        const float rightX= x - dx * base - px * halfWidth, rightY= y - dy * base - py * halfWidth;
        backbuffer.fillTriangle(tipX, tipY, leftX, leftY, rightX, rightY, color);
    };

    // glider: a long, slender dart.
    if (cat == 9) {
        dart(9.0f, 2.0f, 1.0f);
        return;
    }

    // heavy / large jets (Large, High-vortex large, Heavy): a bigger dart with a
    // stub cross-wing so the airliners stand out from light traffic.
    const bool heavy = (cat == 4 || cat == 5 || cat == 6);
    if (heavy) {
        dart(8.0f, 4.0f, 2.5f);
        backbuffer.drawLine(x + px * 4.0f, y + py * 4.0f, x - px * 4.0f, y - py * 4.0f, color);
        return;
    }

    // everything else (light, small, high-performance, unknown): the standard dart
    dart(6.0f, 3.0f, 1.5f);
}

}  // namespace markerdraw
