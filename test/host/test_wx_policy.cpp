// WD3 (docs/wx-device.md): the weather fetch policy (include/WxPolicy.h), as a table and as properties
// checked over random event sequences. One line per property, tagged (a)-(g), so the sabotage script
// (test/host/sabotage_wx_policy.py) can require that exactly the sabotaged property goes red.
// Exit: 0 ok, 1 a check failed.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "WxPolicy.h"

using namespace wxpolicy;

static int failures = 0;
static long viol[7] = {0, 0, 0, 0, 0, 0, 0};          // (a)..(g)
static void table(bool ok, char p, const char* what)
{
    std::printf("  %s  (%c) table: %s\n", ok ? "ok  " : "FAIL", p, what);
    if (!ok) { ++failures; ++viol[p - 'a']; }
}

static Query Q(int a, int b, int r) { Query q; q.lat100 = a; q.lon100 = b; q.rKm = r; return q; }

// The device's order (WD5 addendum 6): a request is SENT (OnSend records the attempt), then answered. The rows
// (a)-(f) send and answer at the same clock, as before; (g) below separates the two.
static Outcome Reply(State& s, const Query& asked, int status, uint32_t bytes, bool valid, uint32_t fe, bool all14,
                     const char* etag, uint32_t nowMs)
{
    const uint32_t seq = OnSend(s, nowMs);
    return OnResponse(s, seq, asked, status, bytes, valid, fe, all14, etag, nowMs);
}

static void Table()
{
    const Query q1 = Q(3930, -9471, 80), q2 = Q(3931, -9471, 80);
    { State s; OnQuery(s, q1); table(!ShouldFetch(s, false, 0) && !ShouldFetch(s, false, 999999), 'a', "disabled: no request at any time"); }
    { State s; OnQuery(s, q1); table(ShouldFetch(s, true, 0), 'e', "enabled, never attempted: fetch at once"); }
    { State s; OnQuery(s, q1);
      Reply(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      table(!ShouldFetch(s, true, 59999) && ShouldFetch(s, true, 60000), 'e', "after a 200: the next fetch at +60 s, not before"); }
    { State s; OnQuery(s, q1);
      const Outcome o = Reply(s, q1, 200, kMaxBytes + 1, true, 1000, false, "\"e1\"", 0);
      table(o == Outcome::Kept && !s.haveCrop, 'b', "a 200 of 32,769 bytes is not taken"); }
    { State s; OnQuery(s, q1);
      table(Reply(s, q1, 200, 5000, false, 1000, false, "\"e1\"", 0) == Outcome::Kept && !s.haveCrop, 'b', "an invalid 200 is not taken"); }
    { State s; OnQuery(s, q1); Reply(s, q1, 200, 5000, true, 2000, false, "\"e2\"", 0);
      const Outcome o = Reply(s, q1, 200, 5000, true, 1880, false, "\"e1\"", 60000);
      table(o == Outcome::Kept && s.frameEpoch == 2000 && std::strcmp(s.etag, "\"e2\"") == 0, 'b', "an OLDER frame never replaces a newer one"); }
    { State s; OnQuery(s, q1); OnQuery(s, q2);
      table(Reply(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0) == Outcome::Kept && !s.haveCrop, 'b', "a 200 answering the old query is not taken"); }
    { State s; OnQuery(s, q1); Reply(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      table(WhatToShow(s, true, true, 1600) == Show::Crop && WhatToShow(s, true, true, 1601) == Show::Nothing, 'c', "shown at 600 s old, hidden at 601 s"); }
    { State s; OnQuery(s, q1); Reply(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      table(WhatToShow(s, true, false, 0) == Show::Nothing && WhatToShow(s, true, false, 1100) == Show::Nothing, 'c', "never shown without a set clock"); }
    { State s; OnQuery(s, q1); Reply(s, q1, 200, 5000, true, 1000, false, "\"e1\"", 0);
      const bool same = IfNoneMatch(s, q1) && std::strcmp(IfNoneMatch(s, q1), "\"e1\"") == 0;
      OnQuery(s, q2);
      table(same && IfNoneMatch(s, q2) == nullptr && !s.haveCrop, 'd', "the ETag is sent for its own query and dropped on a change"); }
    { State s; OnQuery(s, q1); uint32_t t = 0; uint32_t seen[6];
      for (int i = 0; i < 6; ++i) { Reply(s, q1, 503, 0, false, 0, false, nullptr, t); seen[i] = s.intervalMs; t += s.intervalMs; }
      const bool ladder = seen[0] == 120000 && seen[1] == 240000 && seen[2] == 480000 && seen[3] == 600000 && seen[5] == 600000;
      Reply(s, q1, 304, 0, false, 0, false, nullptr, t);
      table(ladder && s.intervalMs == kPollMs, 'e', "errors back off 120, 240, 480, 600, 600 s; a 304 resets to 60 s"); }
    { State s; OnQuery(s, q1); Reply(s, q1, 200, 36, true, 1000, true, "\"e1\"", 0);
      table(s.haveCrop && WhatToShow(s, true, true, 1100) == Show::OutsideCoverage, 'f', "an all-14 crop is held and shown as outside coverage"); }
}

// ---- (g): the attempt is the send; one in flight; no refetch on a reply; a lost reply times out ------
static void TableG()
{
    const Query q1 = Q(3930, -9471, 80);
    { State s; OnQuery(s, q1); const uint32_t n = OnSend(s, 0);
      const bool before = !ShouldFetch(s, true, 0) && !ShouldFetch(s, true, 399);
      OnResponse(s, n, q1, 304, 0, false, 0, false, nullptr, 400);
      table(before && !ShouldFetch(s, true, 400) && !ShouldFetch(s, true, 59999) && ShouldFetch(s, true, 60000), 'g',
            "sent at 0, a 304 at +0.4 s: not due until +60 s from the SEND, and the reply triggers nothing"); }
    { State s; OnQuery(s, q1); OnSend(s, 0);
      const bool held = !ShouldFetch(s, true, 29999);
      const bool ended = !ShouldFetch(s, true, 30000) && !s.inFlight && s.intervalMs == 120000;
      table(held && ended && !ShouldFetch(s, true, 119999) && ShouldFetch(s, true, 120000), 'g',
            "a lost reply: in flight until +30 s, then a failed attempt -- 120 s, due at +120 s from the send"); }
    { State s; OnQuery(s, q1); const uint32_t n = OnSend(s, 0); ShouldFetch(s, true, 30000);
      const Outcome o = OnResponse(s, n, q1, 200, 5000, true, 1000, false, "\"e1\"", 31000);
      table(o == Outcome::Kept && !s.haveCrop && s.intervalMs == 120000 && !s.inFlight, 'g',
            "a reply after its timeout changes nothing"); }
    { State s; OnQuery(s, q1); const uint32_t n = OnSend(s, 0);
      const Outcome o = OnResponse(s, n + 1, q1, 304, 0, false, 0, false, nullptr, 400);
      table(o == Outcome::Kept && s.inFlight && !ShouldFetch(s, true, 401), 'g', "a stray reply changes nothing"); }
}

// G3: the policy driven as AircraftManager::Update() drives it. Each 50 ms pass takes `now` FIRST; a completed
// fetch is consumed after it, at the device's later millis() (now + 2 ms); then one fetch slot (fetchInFlight) is
// shared with the feed (every 5 s, 300 ms), weather only on a pass where the feed is not due. Weather replies take
// 400 ms, every 4th 2,500 ms; every 12th is LOST (the policy never hears; the slot frees at +10 s); the 1st and
// every 6th are 200, the rest 304. Today's wiring -- the attempt recorded at the reply -- bursts here.
struct LoopCount { long wx = 0, feed = 0, maxIn5 = 0; };
static LoopCount RunLoop()
{
    State s; const Query q = Q(4406, -12132, 100); OnQuery(s, q);
    const uint32_t kPass = 50, kEnd = 30u * 60u * 1000u, kFeedEvery = 5000, kFeedTakes = 300;
    bool slotBusy = false, feedEver = false, replyPending = false, replyLost = false;
    uint32_t slotFreeAt = 0, lastFeed = 0, replySeq = 0; int replyStatus = 0; long k = 0;
    std::vector<uint32_t> sends; LoopCount out;
    for (uint32_t now = 0; now < kEnd; now += kPass) {
        if (slotBusy && now >= slotFreeAt) {
            slotBusy = false;
            if (replyPending) {
                replyPending = false;
                if (!replyLost)
                    OnResponse(s, replySeq, q, replyStatus, 5000, true, 1000 + (uint32_t)k, false, "\"e\"", now + 2);
            }
        }
        if (slotBusy) continue;
        if (!feedEver || now - lastFeed >= kFeedEvery) {
            feedEver = true; lastFeed = now; slotBusy = true; slotFreeAt = now + kFeedTakes; ++out.feed;
        } else if (ShouldFetch(s, true, now)) {
            ++k; replySeq = OnSend(s, now); sends.push_back(now); ++out.wx;
            replyLost = (k % 12 == 0);
            slotBusy = true; slotFreeAt = now + (replyLost ? 10000u : (k % 4 == 0 ? 2500u : 400u));
            replyPending = true; replyStatus = (k == 1 || k % 6 == 0) ? 200 : 304;
        }
    }
    size_t j = 0;
    for (size_t i = 0; i < sends.size(); ++i) {
        while (sends[i] - sends[j] >= 5000) ++j;
        if ((long)(i - j + 1) > out.maxIn5) out.maxIn5 = (long)(i - j + 1);
    }
    return out;
}

static void Loop()
{
    const LoopCount c = RunLoop();
    std::printf("  info (g) loop, 30 min: %ld weather requests, at most %ld in any 5 s, %ld feed requests\n",
                c.wx, c.maxIn5, c.feed);
    table(c.wx >= 27 && c.wx <= 33, 'g', "loop: 27-33 weather requests in 30 min (WD5c)");
    table(c.maxIn5 <= 1, 'g', "loop: at most 1 weather request in any 5 s window");
    table(c.feed >= 355, 'g', "loop: the feed keeps its cadence (>= 355 of 360)");
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
            const Outcome o = Reply(s, qs[askedIdx], status, bytes, valid, fe, all14, tag, nowMs);
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
    TableG();
    Loop();
    const long steps = 200000;
    Random(steps);
    const char* name[7] = {"disabled means 0 requests", "a 200 replaces only when small, valid, current and not older",
                           "never shown older than 600 s or without a clock", "If-None-Match only with the current query's ETag",
                           "60 s polling; errors back off to a 600 s cap; 200/304 reset", "an all-14 crop is shown as outside coverage",
                           "the attempt is the send; one in flight; a lost reply times out (table + loop)"};
    for (int p = 0; p < 7; ++p) {
        const bool ok = viol[p] == 0;
        std::printf("  %s  (%c) %ld random steps: %s -- %ld violations\n", ok ? "ok  " : "FAIL", 'a' + p, steps, name[p], viol[p]);
        if (!ok) ++failures;
    }
    std::printf(failures ? "WX POLICY TESTS FAILED\n" : "all WX policy checks passed\n");
    return failures ? 1 : 0;
}
