#include "FishingLogbook.h"

namespace { constexpr long DAY = 86400; }

void FishingLogbook::Begin()
{
    prefs.begin("fi-log", false);
    d.total      = prefs.getUInt("total", 0);
    d.biteWindow = prefs.getUInt("bite", 0);
    d.best       = prefs.getUInt("best", 0);
    d.days       = prefs.getUInt("days", 0);
    d.streak     = prefs.getUInt("streak", 0);
    d.bestStreak = prefs.getUInt("beststreak", 0);
    d.today      = prefs.getUInt("today", 0);
    d.lastDay    = prefs.getUInt("lastday", 0);
    d.lastCatch  = (long)prefs.getUInt("lastcatch", 0);
}

void FishingLogbook::save()
{
    prefs.putUInt("total", d.total);
    prefs.putUInt("bite", d.biteWindow);
    prefs.putUInt("best", d.best);
    prefs.putUInt("days", d.days);
    prefs.putUInt("streak", d.streak);
    prefs.putUInt("beststreak", d.bestStreak);
    prefs.putUInt("today", d.today);
    prefs.putUInt("lastday", d.lastDay);
    prefs.putUInt("lastcatch", (uint32_t)d.lastCatch);
}

void FishingLogbook::RecordCatch(long nowUtc, bool duringWindow)
{
    if (nowUtc < 1600000000) return;   // ignore an unsynced clock so the streak stays honest
    const uint32_t day = (uint32_t)(nowUtc / DAY);
    if (day != d.lastDay) {
        d.streak = (day == d.lastDay + 1) ? d.streak + 1 : 1;   // consecutive vs reset
        if (d.streak > d.bestStreak) d.bestStreak = d.streak;
        d.days++;
        d.today = 0;
        d.lastDay = day;
    }
    d.total++;
    d.today++;
    if (d.today > d.best) d.best = d.today;
    if (duringWindow) d.biteWindow++;
    d.lastCatch = nowUtc;
    save();
}

uint32_t FishingLogbook::CurrentStreak(long nowUtc) const
{
    const uint32_t day = (uint32_t)(nowUtc / DAY);
    return (day >= d.lastDay && day - d.lastDay <= 1) ? d.streak : 0;
}

uint32_t FishingLogbook::TodayCount(long nowUtc) const
{
    const uint32_t day = (uint32_t)(nowUtc / DAY);
    return (day == d.lastDay) ? d.today : 0;
}
