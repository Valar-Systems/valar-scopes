// WD3 (docs/wx-device.md): the weather fetch policy (include/WxPolicy.h), as a table and as properties
// checked over random event sequences. One line per property, tagged (a)-(f), so the sabotage script
// (test/host/sabotage_wx_policy.py) can require that exactly the sabotaged property goes red.
// Exit: 0 ok, 1 a check failed.
#include <cstdio>
#include <cstring>
#include <string>

#include "WxPolicy.h"

using namespace wxpolicy;

static int failures = 0;
static long viol[6] = {0, 0, 0, 0, 0, 0};             // (a)..(f) over the random sequences
static void table(bool ok, char p, const char* what)
{
    std::printf("  %s  (%c) table: %s\n", ok ? "ok  " : "FAIL", p, what);
    if (!ok) { ++failures; ++viol[p - 'a']; }
}

static Query Q(int a, int b, int r) { Query q; q.lat100 = a; q.lon100 = b; q.rKm = r; return q; }

static void Table()
{
    const Query q1 = Q(3930, -9471, 80), q2 = Q(3931, -9471, 80);
    { State s; OnQuery(s, q1); table(!ShouldFetch(s, false, 0) && !ShouldFetch(s, false, 999999), 'a', "disabled: no request at any time"); }
    { State s; OnQuery(s, q1); table(ShouldFetch(s, true, 0), 'e', "enabled, never attempted: fetch at once"); }
    { State s; OnQuery(s, q1);
      OnResponse(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      table(!ShouldFetch(s, true, 59999) && ShouldFetch(s, true, 60000), 'e', "after a 200: the next fetch at +60 s, not before"); }
    { State s; OnQuery(s, q1);
      const Outcome o = OnResponse(s, q1, 200, kMaxBytes + 1, true, 1000, false, "\"e1\"", 0);
      table(o == Outcome::Kept && !s.haveCrop, 'b', "a 200 of 32,769 bytes is not taken"); }
    { State s; OnQuery(s, q1);
      table(OnResponse(s, q1, 200, 5000, false, 1000, false, "\"e1\"", 0) == Outcome::Kept && !s.haveCrop, 'b', "an invalid 200 is not taken"); }
    { State s; OnQuery(s, q1); OnResponse(s, q1, 200, 5000, true, 2000, false, "\"e2\"", 0);
      const Outcome o = OnResponse(s, q1, 200, 5000, true, 1880, false, "\"e1\"", 60000);
      table(o == Outcome::Kept && s.frameEpoch == 2000 && std::strcmp(s.etag, "\"e2\"") == 0, 'b', "an OLDER frame never replaces a newer one"); }
    { State s; OnQuery(s, q1); OnQuery(s, q2);
      table(OnResponse(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0) == Outcome::Kept && !s.haveCrop, 'b', "a 200 answering the old query is not taken"); }
    { State s; OnQuery(s, q1); OnResponse(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      table(WhatToShow(s, true, true, 1600) == Show::Crop && WhatToShow(s, true, true, 1601) == Show::Nothing, 'c', "shown at 600 s old, hidden at 601 s"); }
    { State s; OnQuery(s, q1); OnResponse(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      table(WhatToShow(s, true, false, 0) == Show::Nothing && WhatToShow(s, true, false, 1100) == Show::Nothing, 'c', "never shown without a set clock"); }
    { State s; OnQuery(s, q1); OnResponse(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      const bool same = IfNoneMatch(s, q1) && std::strcmp(IfNoneMatch(s, q1), "\"e1\"") == 0;
      OnQuery(s, q2);
      table(same && IfNoneMatch(s, q2) == nullptr && !s.haveCrop, 'd', "the ETag is sent for its own query and dropped on a change"); }
    { State s; OnQuery(s, q1); uint32_t t = 0; uint32_t seen[6];
      for (int i = 0; i < 6; ++i) { OnResponse(s, q1, 503, 0, false, 0, false, nullptr, t); seen[i] = s.intervalMs; t += s.intervalMs; }
      const bool ladder = seen[0] == 120000 && seen[1] == 240000 && seen[2] == 480000 && seen[3] == 600000 && seen[5] == 600000;
      OnResponse(s, q1, 304, 0, false, 0, false, nullptr, t);
      table(ladder && s.intervalMs == kPollMs, 'e', "errors back off 120, 240, 480, 600, 600 s; a 304 resets to 60 s"); }
    { State s; OnQuery(s, q1); OnResponse(s, q1, 200, 36, true, 1000, true, "\"e1\"", 0);
      table(s.haveCrop && WhatToShow(s, true, true, 1100) == Show::OutsideCoverage, 'f', "an all-14 crop is held and shown as outside coverage"); }
}

// ---- random event sequences, with a model of what each property requires --------------------------
static uint32_t g_seed = 20261020;
static uint32_t rnd() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }

static void Random(long steps)
{
    const Query qs[3] = {Q(3930, -9471, 80), Q(3930, -9471, 160), Q(4188, -8790, 80)};
    State s;
    int qi = 0;
    OnQuery(s, qs[0]);
    bool enabled = true, clockSet = true;
    uint32_t nowMs = 1000, nowEpoch = 1761000000, frame = 1761000000;
    std::string earnedEtag; int earnedFor = -1;          // the ETag the model has seen earned, and its query
    uint32_t expectInterval = kPollMs;
    for (long i = 0; i < steps; ++i) {
        const uint32_t dt = 1000 + rnd() % 200000;
        nowMs += dt; nowEpoch += dt / 1000; if (rnd() % 3 == 0) frame += 120;
        const uint32_t e = rnd() % 100;
        if (e < 3) enabled = !enabled;
        else if (e < 6) { qi = (int)(rnd() % 3); OnQuery(s, qs[qi]); if (earnedFor != qi) { earnedEtag.clear(); earnedFor = -1; } }
        else if (e < 8) clockSet = !clockSet;
        if (ShouldFetch(s, enabled, nowMs)) {
            if (!enabled) ++viol[0];                                                  // (a)
            const char* inm = IfNoneMatch(s, qs[qi]);
            if (inm && (earnedFor != qi || earnedEtag != inm)) ++viol[3];             // (d)
            const int askedIdx = (rnd() % 10 == 0) ? (int)(rnd() % 3) : qi;          // sometimes the query moved
            const uint32_t prevEpoch = s.frameEpoch; const bool had = s.haveCrop; const std::string prevEtag = s.etag;
            const uint32_t r = rnd() % 100;
            int status = 200; uint32_t bytes = 1000 + rnd() % 30000; bool valid = true;
            uint32_t fe = frame - (rnd() % 4 == 0 ? 240 : 0); bool all14 = rnd() % 10 == 0;
            if (r < 10) bytes = kMaxBytes + 1 + rnd() % 1000;
            else if (r < 18) valid = false;
            else if (r < 35) status = 304;
            else if (r < 45) status = 503;
            else if (r < 50) status = 404;
            else if (r < 55) status = -1;
            char tag[32]; std::snprintf(tag, sizeof tag, "\"t%lu\"", (unsigned long)i);
            const uint32_t before = s.intervalMs;
            const Outcome o = OnResponse(s, qs[askedIdx], status, bytes, valid, fe, all14, tag, nowMs);
            if (o == Outcome::Replaced) {
                if (bytes > kMaxBytes || !valid || askedIdx != qi || (had && fe < prevEpoch)) ++viol[1];   // (b)
                earnedEtag = tag; earnedFor = qi;
            } else if (status == 200 && (s.frameEpoch != prevEpoch || s.haveCrop != had || prevEtag != s.etag)) {
                ++viol[1];                                                           // (b) a refused 200 changed the held crop
            }
            if (status == 200 || status == 304) expectInterval = kPollMs;
            else expectInterval = before * 2 > kMaxBackoffMs ? kMaxBackoffMs : before * 2;
            if (s.intervalMs != expectInterval) ++viol[4];                           // (e)
        }
        const Show sh = WhatToShow(s, enabled, clockSet, clockSet ? nowEpoch : 0);
        if (sh != Show::Nothing && (!clockSet || (nowEpoch > s.frameEpoch && nowEpoch - s.frameEpoch > kMaxAgeS))) ++viol[2];   // (c)
        if (sh == Show::Crop && s.allNoCoverage) ++viol[5];                         // (f)
    }
}

int main()
{
    std::printf("WxPolicy.h: the weather fetch policy\n");
    Table();
    const long steps = 200000;
    Random(steps);
    const char* name[6] = {"disabled means 0 requests", "a 200 replaces only when small, valid, current and not older",
                           "never shown older than 600 s or without a clock", "If-None-Match only with the current query's ETag",
                           "60 s polling; errors back off to a 600 s cap; 200/304 reset", "an all-14 crop is shown as outside coverage"};
    for (int p = 0; p < 6; ++p) {
        const bool ok = viol[p] == 0;
        std::printf("  %s  (%c) %ld random steps: %s -- %ld violations\n", ok ? "ok  " : "FAIL", 'a' + p, steps, name[p], viol[p]);
        if (!ok) ++failures;
    }
    std::printf(failures ? "WX POLICY TESTS FAILED\n" : "all WX policy checks passed\n");
    return failures ? 1 : 0;
}
