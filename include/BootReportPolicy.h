#pragma once

/* ===========================================================================
 * ONE BOOT, ONE REPORT, DELIVERED -- NOT MERELY SENT.
 *
 * The delivery rule for the X-Blip-Boot reason, as a pure state machine so it
 * can be proved on a host. The VALUE (esp_reset_reason plus the deferred-reboot
 * cause) is composed in OtaUpdater.cpp, which needs the chip; the decision of
 * WHETHER TO ATTACH IT lives here, where it needs nothing.
 *
 * WHY IT DIFFERS FROM ITS NEIGHBOURS, ON PURPOSE. UsageStore::Take commits its
 * delta before the request leaves; TakeOtaMemReport clears on read and says so
 * ("a report lost to a failed request is not retried"). Both are best-effort and
 * both are right: a lost usage delta heals next hour, a lost OTA report is
 * re-covered by the next update cycle.
 *
 * THIS FIELD CANNOT BE, AND THE REASON IS SAMPLING BIAS RATHER THAN TIDINESS.
 * A boot reason happens once per boot and is gone forever if dropped -- and the
 * boots most worth seeing are the ones that FOLLOW A NETWORK PROBLEM, whose
 * first check-in is therefore the likeliest to fail. Best-effort here would
 * systematically lose exactly the population the field exists to measure, and
 * would look like a healthy fleet while doing it.
 *
 * TWO FLAGS, NOT ONE, and the second is the one that is easy to leave out.
 * `pending` says there is still something to report. `inFlight` says a request
 * is already carrying it. Without `inFlight`, a check-in issued before the
 * previous result came back would attach the reason twice and produce two rows
 * for one boot -- converting a dropped sample into a double-counted one, which
 * is strictly worse than the bug being fixed.
 * ======================================================================== */

namespace bootreport {

struct State {
    bool pending  = true;  ///< every boot begins with exactly one report owed
    bool inFlight = false; ///< a request is already carrying it
};

/**
 * Should this request carry the boot reason?
 *
 * True at most once at a time, and never again after a delivered report.
 */
inline bool Take(State& s)
{
    if (!s.pending || s.inFlight) return false;
    s.inFlight = true;
    return true;
}

/**
 * A carrying request finished. `delivered` = the server acknowledged it (2xx).
 *
 * Always releases the in-flight guard; retires the report only on delivery.
 * Safe to call on every result -- a no-op when nothing of ours was attached,
 * which is what lets the caller wire it unconditionally rather than tracking
 * which request was the carrier.
 */
inline void Ack(State& s, bool delivered)
{
    if (!s.inFlight) return;
    s.inFlight = false;
    if (delivered) s.pending = false;
}

} // namespace bootreport
