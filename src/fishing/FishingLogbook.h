#pragma once

#include <Arduino.h>
#include <Preferences.h>

// The Fishing edition (Reelscope) persistent catch log: lifetime tallies of fish the user has logged
// (tap the Catch Log screen to record one), how many landed while a solunar feeding window was active,
// best single day, and a fishing-day streak. Its own NVS namespace ("fi-log"), mirroring the other
// editions' logbooks.
//
// The "bite-window hit rate" (share of catches recorded while a major/minor period was active) is the
// payoff stat: it tells the angler whether the on-device solunar forecast is actually working for them.
//
// A "day" is a UTC calendar day on which at least one catch was logged; the streak is the run of
// consecutive such days, alive through today/yesterday and 0 once a day is missed.
class FishingLogbook {
public:
    void Begin();   // load counters from NVS (idempotent)

    // Record one logged catch. duringWindow = a solunar feeding period was active at the time.
    // nowUtc must be a real (NTP-synced) epoch; unsynced calls are ignored so the streak stays honest.
    void RecordCatch(long nowUtc, bool duringWindow);

    uint32_t Total() const      { return d.total; }
    uint32_t BiteWindow() const { return d.biteWindow; }   // caught during a feeding window
    uint32_t Best() const       { return d.best; }         // best single-day count
    uint32_t Days() const       { return d.days; }
    uint32_t BestStreak() const { return d.bestStreak; }
    long     LastCatch() const  { return d.lastCatch; }
    bool     Any() const        { return d.total > 0; }
    uint32_t CurrentStreak(long nowUtc) const;             // live streak, or 0 if broken
    uint32_t TodayCount(long nowUtc) const;                // catches logged today (0 if not today)
    int      BitePercent() const { return d.total ? (int)((d.biteWindow * 100 + d.total / 2) / d.total) : 0; }

private:
    struct Data {
        uint32_t total = 0, biteWindow = 0, best = 0, days = 0;
        uint32_t streak = 0, bestStreak = 0, today = 0;
        uint32_t lastDay = 0;    // UTC days-since-epoch of the most recent logged catch
        long     lastCatch = 0;  // UTC epoch of the most recent catch
    } d;
    Preferences prefs;
    void save();
};
