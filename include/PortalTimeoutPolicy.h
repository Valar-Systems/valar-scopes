#pragma once

#include <stdint.h>

/* ============================================================================
 * HOW LONG THE SETUP PORTAL STAYS UP -- and why it is not one number.
 *
 * THE FAILURE THIS EXISTS FOR, observed 2026-09-13/14. A board that already
 * holds credentials for one network, carried to a DIFFERENT house, is very hard
 * to re-provision. The cycle is: try the saved SSID (~77 s), open the setup
 * hotspot for 180 s, nobody arrives, reboot, repeat. So the hotspot the new
 * owner is hunting for on their phone DISAPPEARS EVERY FIVE MINUTES, and the
 * device reads as broken.
 *
 * It was reported twice in this project before it was understood, both times in
 * the owner's own words -- "already on my network and I'm unable to disconnect
 * it from mine to connect to my friends" -- and once again as a board that
 * "didn't work" at a friend's house and then worked perfectly the next day,
 * after it had been credential-cleared in between. Same mechanism all three
 * times, never the firmware.
 *
 * WHY THE TIMEOUT CANNOT SIMPLY BE REMOVED. See WiFiManagerHelpers.h: the
 * timeout is what makes a power cut survivable. The board is up in ~10 s, the
 * router takes 1-3 minutes, the join fails -- and without a reboot-retry the
 * unit parks in setup mode forever, long after the network came back, with the
 * customer having done nothing wrong. Removing the timeout trades a rare, ugly
 * failure for a common one.
 *
 * SO IT ESCALATES RATHER THAN LATCHING. The two situations are indistinguishable
 * at the first failure and easy to tell apart after several:
 *
 *   a router rebooting          -> resolves within a couple of cycles
 *   a device in a NEW HOUSE     -> never resolves, no matter how long it waits
 *
 * Early cycles therefore keep the fast 180 s retry (self-heal intact). After
 * FAST_CYCLES consecutive failures the portal stays up far longer, so the
 * hotspot is reliably findable -- but it STILL times out and STILL retries, so
 * a network that comes back hours later is still rejoined with nobody present.
 *
 * NOT INFINITE, DELIBERATELY. An infinite portal would mean a board never again
 * retries its saved network, which re-creates the exact power-cut failure above
 * for anyone whose router is off longer than FAST_CYCLES cycles -- a holiday, a
 * house move, an ISP outage. The test asserts the patient path still terminates.
 * ========================================================================== */

namespace portaltimeout {

/// Portal timeout while the saved network might just be slow to return.
/// Matches the historical value exactly: early behaviour is unchanged.
constexpr uint16_t FAST_TIMEOUT_S = 180;

/// Portal timeout once repeated failures suggest the device has MOVED.
/// 15 minutes against a ~80 s join attempt leaves the hotspot up ~92% of the
/// time, which is the difference between "findable" and "keeps vanishing".
constexpr uint16_t PATIENT_TIMEOUT_S = 900;

/// Consecutive failed cycles before we stop assuming the router is coming back.
/// Three cycles is roughly 13 minutes of fast retry -- longer than any router
/// reboot, far shorter than a person's patience with a vanishing hotspot.
constexpr uint8_t FAST_CYCLES = 3;

/// WiFiManager's "never time out" value.
constexpr uint16_t NEVER = 0;

/**
 * How long the config portal should stay up this boot.
 *
 * @param hasCredentials     does the board have a saved network to retry?
 * @param consecutiveFailures  boots since the last SUCCESSFUL join, saturating.
 *
 * Returns seconds, or NEVER (0) to mean "do not time out at all".
 */
inline uint16_t TimeoutSeconds(bool hasCredentials, uint8_t consecutiveFailures)
{
    // NO CREDENTIALS -> NEVER TIME OUT. A reboot would retry nothing at all; it
    // would only yank the hotspot away from someone mid-setup. This is
    // out-of-box first setup, the one moment the portal must be rock steady,
    // and it is unchanged by this policy.
    if (!hasCredentials) return NEVER;

    return (consecutiveFailures < FAST_CYCLES) ? FAST_TIMEOUT_S : PATIENT_TIMEOUT_S;
}

/**
 * Next value of the consecutive-failure counter.
 *
 * A SUCCESSFUL JOIN CLEARS IT UNCONDITIONALLY -- including from the patient
 * state -- so a board that struggles once and then settles returns to the fast
 * self-heal path rather than staying degraded forever. Saturates instead of
 * wrapping: at 255 a wrap would silently drop a patient board back to the fast
 * path, which is the failure this whole file exists to prevent.
 */
inline uint8_t NextFailureCount(uint8_t current, bool joined)
{
    if (joined) return 0;
    return (current >= 255) ? 255 : (uint8_t)(current + 1);
}

} // namespace portaltimeout
