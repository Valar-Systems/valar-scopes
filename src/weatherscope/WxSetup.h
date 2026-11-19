#pragma once
// Weatherscope's setup states (docs/weatherscope-stage4.md 1.5, S4.32): a board with no Wi-Fi, no location or no key
// shows nothing live, and can fall back to nothing, so each state is a screen with its own words, decided here in ONE
// function. Live needs a frame. A refusal (the proxy's 401, or 403 `fake_device_id` / `forbidden`: proxy/src/index.ts
// :362-363, proxy/src/activation.ts:160) is said in words. A server or network failure keeps the last frame, which the
// screens age out as they always have (RADAR STALE past 30 min, Q15/Q18). Pure C++11, host-tested (test_wx_live.cpp,
// test_wx_answer.cpp).
//
// THE REFUSED SCREEN AND THE SETUP PAGE READ ONE BIT (S4.36, 5c's answers): a refusal is shown only once the AuthLatch
// below has latched, by the radar's own thresholds, and NeedsReverify() -- what the page's %REFUSED% shows -- is the
// same Shown() that Decide() is given. Never a second evaluation of the last answer.
#include <stdint.h>
#include <string.h>

namespace wxsetup {

enum class State : uint8_t { NoWifi, NoLocation, NotEnrolled, Refused, Waiting, Live };
enum class Refusal : uint8_t { None, KeyRejected, DeviceUnknown, NotActivated, Other };

struct Inputs {
    bool wifi = false;
    bool location = false;       // NVS latitude and longitude both set
    bool key = false;            // a request key by RequestKey()'s precedence
    Refusal refused = Refusal::None;   // Shown(the latch): None unless it has latched
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
    if (in.refused != Refusal::None) return State::Refused;
    if (!in.frame) return State::Waiting;
    return State::Live;
}

// The request key, by the radar's precedence (AircraftManager.cpp 1486-1489; S4.36 ruling (a)): the pasted
// "cloud-key", else the factory "cloud-key-fac". A pasted key with an empty factory slot is a key: the device draws
// Live, while the setup page, which reads the factory slot alone, says "not enrolled" (the named pasted-key row).
inline const char* RequestKey(const char* pasted, const char* factory)
{
    if (pasted != nullptr && pasted[0] != 0) return pasted;
    if (factory != nullptr && factory[0] != 0) return factory;
    return "";
}

// The refusal latch: the radar's debounce (AircraftManager.cpp 4168-4196), the same thresholds. It latches once the
// proxy has refused this board's key at least kLatchStreak times in a row AND for at least kLatchMs since the streak's
// first refusal, so no transient explains it. Any 200 or 304 is proof the key works and clears everything at once. An
// answer that is neither (no answer, a 5xx, a 429, another 4xx) is not evidence either way: it neither advances nor
// clears the streak. Held in RAM since boot, never stored: a stored flag would outlive the owner's fix (5c's answer).
constexpr uint8_t kLatchStreak = 5;                       // AircraftManager.cpp: REVERIFY_MIN_STREAK
constexpr uint32_t kLatchMs = 15UL * 60UL * 1000UL;       // AircraftManager.cpp: REVERIFY_MIN_MS, 15 min

struct AuthLatch {
    uint8_t streak = 0;          // consecutive refusals, at most 255
    uint32_t firstMs = 0;        // when the streak's first refusal arrived
    bool latched = false;
    Refusal kind = Refusal::None;   // the latest refusal's kind: the device's own screen says which
};

inline void OnAnswer(AuthLatch& l, int status, const char* error, uint32_t nowMs)
{
    const Refusal r = RefusalOf(status, error);
    if (r != Refusal::None) {
        if (l.streak == 0) l.firstMs = nowMs;
        if (l.streak < 255) ++l.streak;
        l.kind = r;
        if (!l.latched && l.streak >= kLatchStreak && (uint32_t)(nowMs - l.firstMs) >= kLatchMs) l.latched = true;
    } else if (status == 200 || status == 304) {
        l = AuthLatch();
    }
}

// What the screen is given (Inputs::refused) and, through NeedsReverify, what the page shows: one evaluation.
inline Refusal Shown(const AuthLatch& l) { return l.latched ? l.kind : Refusal::None; }
inline bool NeedsReverify(const AuthLatch& l) { return Shown(l) != Refusal::None; }

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
