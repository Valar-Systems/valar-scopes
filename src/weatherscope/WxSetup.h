#pragma once
// Weatherscope's setup states (docs/weatherscope-stage4.md 1.5, S4.32): a board with no Wi-Fi, no location or no key
// shows nothing live, and can fall back to nothing, so each state is a screen with its own words, decided here in ONE
// function. Live needs a frame. A refusal (the proxy's 401, or 403 `fake_device_id` / `forbidden`: proxy/src/index.ts
// :362-363, proxy/src/activation.ts:160) is said in words. A server or network failure keeps the last frame, which the
// screens age out as they always have (RADAR STALE past 30 min, Q15/Q18). Pure C++11, host-tested (test_wx_live.cpp).
#include <stdint.h>
#include <string.h>

namespace wxsetup {

enum class State : uint8_t { NoWifi, NoLocation, NotEnrolled, Refused, Waiting, Live };
enum class Refusal : uint8_t { None, KeyRejected, DeviceUnknown, NotActivated, Other };

struct Inputs {
    bool wifi = false;
    bool location = false;       // NVS latitude and longitude both set
    bool key = false;            // a device key in NVS
    int status = 0;              // the last /wx answer's HTTP status; 0 = none yet, or no answer
    const char* error = "";      // that answer's JSON "error", read only on a 401 or 403
    bool frame = false;          // a frame is held
};

inline Refusal RefusalOf(int status, const char* error)
{
    if (status == 401) return Refusal::KeyRejected;
    if (status != 403) return Refusal::None;
    if (error != nullptr && strcmp(error, "fake_device_id") == 0) return Refusal::DeviceUnknown;
    if (error != nullptr && strcmp(error, "forbidden") == 0) return Refusal::NotActivated;
    return Refusal::Other;
}

inline State Decide(const Inputs& in)
{
    if (!in.wifi) return State::NoWifi;
    if (!in.location) return State::NoLocation;
    if (!in.key) return State::NotEnrolled;
    if (RefusalOf(in.status, in.error) != Refusal::None) return State::Refused;
    if (!in.frame) return State::Waiting;
    return State::Live;
}

struct Words {
    const char* title;
    const char* line;
};

// The words each state shows (S4.33 freezes them). Live shows no setup screen, so its title is empty.
inline Words RefusedWords(Refusal r)
{
    switch (r) {
        case Refusal::KeyRejected: return {"KEY NOT ACCEPTED", "ENROL THIS BOARD AGAIN"};
        case Refusal::DeviceUnknown: return {"BOARD NOT RECOGNISED", "CONTACT SUPPORT"};
        case Refusal::NotActivated: return {"NOT ACTIVATED YET", "CONTACT SUPPORT"};
        case Refusal::Other:
        case Refusal::None: break;
    }
    return {"ACCESS REFUSED", "CONTACT SUPPORT"};
}

inline Words WordsOf(State s, Refusal r)
{
    switch (s) {
        case State::NoWifi: return {"NO WI-FI", "SET UP WI-FI FROM YOUR PHONE"};
        case State::NoLocation: return {"SET YOUR LOCATION", "ON THE SETUP PAGE"};
        case State::NotEnrolled: return {"NOT ENROLLED", "ENROL THIS BOARD ON THE SETUP PAGE"};
        case State::Waiting: return {"WAITING FOR RADAR", ""};
        case State::Refused: return RefusedWords(r);
        case State::Live: break;
    }
    return {"", ""};
}

}  // namespace wxsetup
