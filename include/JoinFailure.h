#pragma once

#include <stdint.h>

/* ============================================================================
 * WHY A JOIN FAILED, SAID TO THE PERSON STANDING THERE.
 *
 * THE INCIDENT. A customer entered the wrong Wi-Fi password on the setup page.
 * The page said "saving" and raised nothing. The device then failed to join and
 * showed:
 *
 *     No Wi-Fi
 *     Retrying...
 *
 * for 1.5 s, and rebooted. It did that forever. Two days of investigation
 * followed -- a suspected antenna fault, a suspected hostile mesh, a trip to
 * another house with four boards and a printed test plan -- and the answer was
 * a typo the device had diagnosed correctly within seconds of the first attempt
 * and told nobody.
 *
 * THE DIAGNOSIS ALREADY EXISTED. The WiFi event handler in main.cpp has
 * separated these causes for a long time, carefully, including the genuinely
 * hard distinction between the two timeouts that look alike:
 *
 *     AUTH_FAIL / 4WAY_HANDSHAKE_TIMEOUT / AUTH_EXPIRE -> key mismatch
 *     HANDSHAKE_TIMEOUT (204)                          -> RF, NOT a key mismatch
 *
 * It printed all of it to a serial port that, on a customer's shelf, nobody has
 * attached. This is the repo's own "instrument firing into a void": the signal
 * was perfect, the code was right, and nothing read it.
 *
 * SO THE SURFACE IS THE GLASS, NOT THE PORTAL. The obvious fix -- an error on
 * the setup page -- is the one that cannot be relied on. When the board tests
 * new credentials it brings up STA, the phone's link to the setup hotspot
 * typically drops, and iOS in particular abandons a network with no internet.
 * The error would be delivered exactly when it cannot arrive. The device's own
 * screen is four inches from the person's face and cannot disconnect.
 *
 * PURE ON PURPOSE. No Arduino, no esp_wifi: the mapping is the part worth
 * testing on a host, and test/host/test_join_failure.cpp does. The numeric
 * reason codes are duplicated here rather than included, and main.cpp
 * static_asserts them against the SDK's own values -- so if the IDF ever
 * renumbers one, the BUILD fails instead of the message quietly becoming wrong.
 * ========================================================================== */

namespace joinfail {

// esp_wifi_types.h wifi_err_reason_t, duplicated so this header stays pure.
// main.cpp static_asserts every one of these against the real enum.
constexpr uint8_t R_AUTH_EXPIRE            = 2;
constexpr uint8_t R_4WAY_HANDSHAKE_TIMEOUT = 15;
constexpr uint8_t R_NO_AP_FOUND            = 201;
constexpr uint8_t R_AUTH_FAIL              = 202;
constexpr uint8_t R_HANDSHAKE_TIMEOUT      = 204;

enum class Cause : uint8_t {
    Unknown = 0,
    WrongPassword,    ///< the key was rejected -- a typo, almost always
    NetworkNotFound,  ///< the SSID was not on the air at all
    WeakSignal,       ///< frames lost mid-handshake: RF, NOT the password
};

/// Three short lines for DrawSplash. Kept narrow on purpose: this is a 240x240
/// ROUND disc, and a line that overruns the chord is unreadable exactly when it
/// matters most.
struct Advice {
    const char* l0;  ///< what happened, in two words
    const char* l1;  ///< what to do
    const char* l2;  ///< ...continued
};

inline Cause Classify(uint8_t reason)
{
    // THE TWO TIMEOUTS ARE DIFFERENT FAULTS AND MUST NOT BE MERGED.
    // 4WAY_HANDSHAKE_TIMEOUT (15) is what a wrong WPA2 passphrase actually
    // produces -- the key fails during the 4-way exchange, not at auth, so
    // "wrong password" rarely arrives as AUTH_FAIL. HANDSHAKE_TIMEOUT (204) is
    // frames going missing, which is an antenna or a supply, and telling that
    // owner to re-check a correct password sends them down a two-day hole.
    switch (reason) {
        case R_AUTH_FAIL:
        case R_AUTH_EXPIRE:
        case R_4WAY_HANDSHAKE_TIMEOUT: return Cause::WrongPassword;
        case R_NO_AP_FOUND:            return Cause::NetworkNotFound;
        case R_HANDSHAKE_TIMEOUT:      return Cause::WeakSignal;
        default:                       return Cause::Unknown;
    }
}

/**
 * What to put on the glass.
 *
 * EVERY CASE NAMES AN ACTION, including Unknown. A screen that states a fault
 * and no next step leaves the owner exactly where the old "No Wi-Fi /
 * Retrying..." left them -- which is the failure this file exists to end.
 */
inline Advice AdviceFor(Cause c)
{
    switch (c) {
        case Cause::WrongPassword:
            return { "WRONG PASSWORD", "Rejoin the setup", "page and retype it" };
        case Cause::NetworkNotFound:
            return { "NETWORK NOT FOUND", "Check the name, or", "move nearer to it" };
        case Cause::WeakSignal:
            return { "WEAK SIGNAL", "Not the password.", "Move nearer router" };
        case Cause::Unknown:
        default:
            // Deliberately NOT "wrong password". Guessing the commonest cause
            // would be right most of the time and would send the unlucky owner
            // to re-type a password that was already correct.
            return { "COULD NOT CONNECT", "Rejoin the setup", "page and try again" };
    }
}

/* ---- the cross-task store -------------------------------------------------
 * The reason arrives on the WiFi event task; the screen is drawn on the loop
 * task. One byte plus a flag, written once and read once -- no queue, no lock,
 * no allocation, matching how JoinDiag hands data across the same boundary.
 * Nothing here calls into LovyanGFX, which is the actual constraint.
 */
inline volatile uint8_t& LastReasonRef() { static volatile uint8_t r = 0; return r; }
inline volatile bool&    HaveReasonRef() { static volatile bool h = false; return h; }

/// WiFi event task only.
inline void RecordReason(uint8_t reason)
{
    LastReasonRef() = reason;
    HaveReasonRef() = true;
}

/// Loop task only. Unknown when nothing was ever recorded -- which is itself a
/// real state (the portal timed out with nobody there, so no attempt was made).
inline Cause LastCause()
{
    if (!HaveReasonRef()) return Cause::Unknown;
    return Classify(LastReasonRef());
}

inline uint8_t LastReason() { return HaveReasonRef() ? LastReasonRef() : 0; }

} // namespace joinfail
