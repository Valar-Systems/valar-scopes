// Overhead alerts, the pure half: include/OverheadPolicy.h (docs/v17-overhead-card.md "Owner
// controls"; predictions frozen at ca6cfdda32f3).
//
// SABOTAGE S-b (the setting is the distance): make RadiusKm ignore `distance` and return 3 mi
// -> the "1 is smaller than 3" cases go red.
#include <cmath>
#include <cstdio>
#include "../../include/OverheadPolicy.h"

using namespace overhead;

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main()
{
    std::printf("overhead alerts (OverheadPolicy.h)\n");

    // the radius: the dropdown value in the device's unit, never converted on a unit change
    const double MI = 1.609344, KM = 1.0, NMI = 1.852;   // the firmware's units::ToKm(1, unit)
    check(Near(RadiusKm(3, MI), 3 * MI), "3 on a miles device is 3 mi (4.83 km) -- what COM18 measured as r_km=4.8");
    check(Near(RadiusKm(3, KM), 3.0), "3 on a km device is 3 km");
    check(Near(RadiusKm(3, NMI), 3 * NMI), "3 on a nautical-mile device is 3 nmi");
    check(RadiusKm(1, MI) < RadiusKm(2, MI) && RadiusKm(2, MI) < RadiusKm(3, MI) && RadiusKm(3, MI) < RadiusKm(5, MI),
          "1 < 2 < 3 < 5");
    check(Near(RadiusKm(3, KM) * MI, RadiusKm(3, MI)), "a unit change keeps the value 3 and reinterprets it");

    // the dropdown: stored values snap to 1/2/3/5, nearest, a tie to the smaller
    check(SnapDistance(1) == 1 && SnapDistance(2) == 2 && SnapDistance(3) == 3 && SnapDistance(5) == 5, "values on the dropdown are kept");
    check(SnapDistance(0.5) == 1, "0.5 -> 1");
    check(SnapDistance(2.5) == 2, "2.5 -> 2 (a tie between 2 and 3 goes to the smaller)");
    check(SnapDistance(4) == 3, "4 -> 3 (a tie between 3 and 5 goes to the smaller)");
    check(SnapDistance(4.5) == 5, "4.5 -> 5");
    check(SnapDistance(6) == 5 && SnapDistance(10) == 5, "6 and 10 -> 5");
    check(SnapDistance(0) == DEFAULT_DISTANCE && SnapDistance(-2) == DEFAULT_DISTANCE && SnapDistance(NAN) == DEFAULT_DISTANCE,
          "unreadable or non-positive -> the default, 3");

    // the card decision: OFF first, the flash wins, then the screen, idle, spacing
    const uint32_t never = 0xFFFFFFFFu;
    check(DecideCard(true, 40000, true, false, never) == Card::Show, "on, idle 40 s, a free face, no flash, no card yet -> SHOW");
    check(DecideCard(false, 40000, true, false, never) == Card::Off, "OFF -> no card, whatever else holds");
    check(DecideCard(true, 40000, true, true, never) == Card::Alert, "the emergency/military flash is up -> no card (it wins)");
    check(DecideCard(true, 40000, false, false, never) == Card::Screen, "Connect / a card open / Follow -> no card");
    check(DecideCard(true, CARD_IDLE_MS - 1, true, false, never) == Card::Busy, "touched 29.999 s ago -> no card (that pass's card is spent)");
    check(DecideCard(true, CARD_IDLE_MS, true, false, never) == Card::Show, "untouched for exactly 30 s -> SHOW");
    check(DecideCard(true, 40000, true, false, CARD_SPACING_MS - 1) == Card::Spacing, "59.999 s after the last card -> no card");
    check(DecideCard(true, 40000, true, false, CARD_SPACING_MS) == Card::Show, "60 s after the last card -> SHOW");

    // the phone push: the switch AND its own box AND a topic -- all 8 combinations
    check(!DEFAULT_PUSH, "the push box defaults OFF");
    for (int m = 0; m < 8; ++m) {
        const bool on = m & 1, box = m & 2, topic = m & 4;
        char what[96];
        std::snprintf(what, sizeof what, "push: switch=%d box=%d topic=%d -> %s", on, box, topic, (on && box && topic) ? "fires" : "silent");
        check(PushFires(on, box, topic) == (on && box && topic), what);
    }

    // the switch as read from NVS, and Home Assistant's state field (decision 2)
    check(SwitchOn(nullptr) == DEFAULT_ON && SwitchOn("") == DEFAULT_ON, "never saved: the switch reads the default, ON");
    check(SwitchOn("true") && !SwitchOn("false"), "a stored value is read as stored");
    check(HaStateFollowsSwitch(true) && !HaStateFollowsSwitch(false), "HA's state field follows the switch");

    std::printf(failures ? "FAILED: %d\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
