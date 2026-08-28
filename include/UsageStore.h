#pragma once

// Usage counters -- the device half: persistence, cadence, and the one-shot
// take. The arithmetic and the wire format are in UsageReport.h, which is pure
// and is where the host tests live.
//
// Namespace `usage`, its own NVS namespace, for the same reason the follow log
// has one: it is not configuration, nobody edits it from the web page, and the
// `config` namespace is deliberately kept to a small known set of writers (see
// docs/nvs-config-flip-2026-08-27.md).
//
// =============================================================================
// FLASH WRITES: ONE PER REPORT, NOT ONE PER EVENT
//
// Counters live in RAM and are persisted only when a report is taken -- 24
// writes a day. Persisting on every card open would be thousands, and the
// benefit would be recovering counts across an unplanned reboot, which is
// exactly the bounded loss the delta design already accepts.
//
// The consequence, stated rather than discovered: events since the last report
// are lost on a power cut. Same direction as a dropped request -- undercount,
// never overcount.

#include <Arduino.h>
#include <Preferences.h>

#include "UsageReport.h"

namespace usage {

class Store {
public:
    static constexpr const char* NS = "usage";

    /// At boot. A namespace that does not exist yet is a device that has never
    /// reported, which is every device until this ships.
    void Load()
    {
        Preferences p;
        if (!p.begin(NS, /*readOnly=*/true)) return;
        total.cardOpens     = p.getUInt("t_card", 0);
        total.screenRadar   = p.getUInt("t_radar", 0);
        total.screenList    = p.getUInt("t_list", 0);
        total.screenStats   = p.getUInt("t_stats", 0);
        total.screenFollow  = p.getUInt("t_follow", 0);
        total.logbookClaims = p.getUInt("t_claim", 0);
        reported.cardOpens     = p.getUInt("r_card", 0);
        reported.screenRadar   = p.getUInt("r_radar", 0);
        reported.screenList    = p.getUInt("r_list", 0);
        reported.screenStats   = p.getUInt("r_stats", 0);
        reported.screenFollow  = p.getUInt("r_follow", 0);
        reported.logbookClaims = p.getUInt("r_claim", 0);
        p.end();
    }

    // The count sites. Deliberately one named method each rather than a generic
    // Bump(enum): a call site reads as what it counts, and grep finds every
    // place a counter can move.
    void CardOpened()    { ++total.cardOpens; }
    void LogbookClaim()  { ++total.logbookClaims; }
    void ScreenRadar()   { ++total.screenRadar; }
    void ScreenList()    { ++total.screenList; }
    void ScreenStats()   { ++total.screenStats; }
    void ScreenFollow()  { ++total.screenFollow; }

    /// Whether Follow is configured. A BOOLEAN, never the target -- §17.
    void SetFollowEnabled(bool on) { followEnabled = on; }

    bool Due(uint32_t nowMs) const { return usage::Due(nowMs, lastReportMs); }

    /// The header value, or "" when nothing is due.
    ///
    /// COMMITS ON TAKE, like OtaUpdater::TakeOtaMemReport and for the same
    /// reason: the alternative is a device that keeps retrying telemetry, and a
    /// dropped sample is worth less than that. See UsageReport.h on why the loss
    /// direction is the safe one.
    ///
    /// LOOP TASK ONLY -- it writes NVS. The caller hands the string to the fetch
    /// task inside the request, exactly like cloudBase/cloudKey/otaMem.
    String Take(uint32_t nowMs)
    {
        if (!Due(nowMs)) return String();

        Report r;
        r.delta = Delta(reported, total);
        r.followEnabled = followEnabled;
        r.uptimeHours = UptimeHours(nowMs);

        char buf[MAX_LEN];
        if (Format(r, buf, sizeof(buf)) == 0) return String();

        reported = total;
        lastReportMs = nowMs ? nowMs : 1u;   // 0 means "never reported"
        Persist();
        return String(buf);
    }

private:
    void Persist()
    {
        Preferences p;
        if (!p.begin(NS, /*readOnly=*/false)) return;
        p.putUInt("t_card",   total.cardOpens);
        p.putUInt("t_radar",  total.screenRadar);
        p.putUInt("t_list",   total.screenList);
        p.putUInt("t_stats",  total.screenStats);
        p.putUInt("t_follow", total.screenFollow);
        p.putUInt("t_claim",  total.logbookClaims);
        p.putUInt("r_card",   reported.cardOpens);
        p.putUInt("r_radar",  reported.screenRadar);
        p.putUInt("r_list",   reported.screenList);
        p.putUInt("r_stats",  reported.screenStats);
        p.putUInt("r_follow", reported.screenFollow);
        p.putUInt("r_claim",  reported.logbookClaims);
        p.end();
    }

    Counters total{};
    Counters reported{};
    bool     followEnabled = false;
    uint32_t lastReportMs  = 0;
};

} // namespace usage
