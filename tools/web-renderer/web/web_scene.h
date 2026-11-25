#pragma once
#include <string>

// Blipscope web renderer: the fixed scene (web_scene.cpp). Synthetic, deterministic, looping: about 20
// aircraft on straight tracks across a disc centred on Bend, Oregon. No recorded position is used.
namespace scene {
constexpr double CENTRE_LAT = 44.06;     // Bend city centre (public)
constexpr double CENTRE_LON = -121.32;
// The /blips body the Worker would serve at this moment ({"v":1,"t":..,"a":[[...]]}).
std::string Blips(long nowEpoch, double nowSec);
// The /enrich/<hex> body for one of the scene's aircraft; false for any other hex (the demo's 404).
bool Enrich(const std::string& hex, std::string& out);
}
