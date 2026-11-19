// W7 slice 3a, host-side (docs/weatherscope-stage4.md S4.37): the refusal latch and the key precedence (WxSetup.h),
// and what one /wx answer does to the policy, the latch and the ring (WxAnswer.h). Pure C++11; WxAnswer.h reads
// WireLocation.h, so this builds against test/host/arduino_shim. Its sabotages: test/host/sabotage_wx_answer.py.
//
//   g++ -std=c++11 -Iinclude -Isrc/weatherscope -Itest/host/arduino_shim test/host/test_wx_answer.cpp
//
// Prints ids, counts and times only. The query points are synthetic, not a real place.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WxAnswer.h"
#include "WxLiveUrl.h"

using wxsetup::AuthLatch;
using wxsetup::Refusal;
using wxsetup::State;

static int failures = 0;

static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

static void* Alloc(size_t n) { return std::malloc(n); }
static void Release(void* p) { std::free(p); }

struct Rig {
    wxanswer::Live L;
    wxring::Ring ring;
    wxpolicy::Query q;
    std::vector<uint8_t> body;
    Rig() : body(100, 7)
    {
        ring.Init(Alloc, Release);
        q = wxanswer::QueryOf(10.13, -20.68);
        wxanswer::SetQuery(L, ring, q);
    }
    ~Rig()
    {
        for (size_t i = 0; i < wxring::kSlots; ++i) Release(ring.slot[i]);
    }
    // Send now, and answer at once.
    wxanswer::Result Answer(uint32_t ms, const wxanswer::Reply& r)
    {
        const uint32_t seq = wxpolicy::OnSend(L.policy, ms);
        return wxanswer::OnReply(L, ring, seq, q, r, ms);
    }
    wxanswer::Reply Frame(uint32_t epoch, const char* etag = nullptr)
    {
        wxanswer::Reply r;
        r.status = 200;
        r.body = body.data();
        r.bytes = (uint32_t)body.size();
        r.valid = true;
        r.frameEpoch = epoch;
        r.etag = etag;
        return r;
    }
};

static wxanswer::Reply Status(int status, const char* error = nullptr)
{
    wxanswer::Reply r;
    r.status = status;
    r.error = error;
    return r;
}

static const uint32_t kMin = 60000;

static void Refuse(AuthLatch& l, uint32_t ms, const char* error = "unauthorized")
{
    wxsetup::OnAnswer(l, error && std::strcmp(error, "unauthorized") == 0 ? 401 : 403, error, ms);
}

int main()
{
    // ---------------------------------------------------------------------------------------------- the latch
    {
        // Consecutive 401s, each answered at its send, the sends paced by WxPolicy's backoff from a 60 s poll.
        Rig g;
        std::vector<uint32_t> sends;
        size_t latchedAt = 0;
        for (uint32_t t = 0; t <= 3600 && sends.size() < 5; ++t) {
            if (!wxpolicy::ShouldFetch(g.L.policy, true, t * 1000)) continue;
            sends.push_back(t);
            g.Answer(t * 1000, Status(401, "unauthorized"));
            if (latchedAt == 0 && wxsetup::NeedsReverify(g.L.latch)) latchedAt = sends.size();
        }
        std::string s;
        for (size_t i = 0; i < sends.size(); ++i) s += (i ? "," : "") + std::to_string(sends[i]);
        check(sends == std::vector<uint32_t>{0, 120, 360, 840, 1440} && latchedAt == 5, "LATCH-BACKOFF",
              "401s paced by the backoff are sent at " + s + " s; latched at refusal " + std::to_string(latchedAt));
    }
    {
        AuthLatch l;
        for (uint32_t i = 0; i < 10; ++i) Refuse(l, i * 1000);
        const bool a = wxsetup::NeedsReverify(l);
        Refuse(l, 899999);
        const bool b = wxsetup::NeedsReverify(l);
        Refuse(l, 900000);
        const bool c = wxsetup::NeedsReverify(l);
        check(!a && !b && c && l.streak == 12, "LATCH-STREAK-NEEDS-TIME",
              "10 refusals in 9 s do not latch, nor one at 899.999 s; one at 900 s does (streak " +
                  std::to_string(l.streak) + ")");
    }
    {
        AuthLatch l;
        for (uint32_t m = 0; m <= 30; m += 10) Refuse(l, m * kMin);
        const bool a = wxsetup::NeedsReverify(l);
        Refuse(l, 31 * kMin);
        const bool b = wxsetup::NeedsReverify(l);
        check(!a && b, "LATCH-TIME-NEEDS-STREAK", "4 refusals over 30 min do not latch; the 5th, at 31 min, does");
    }
    {
        AuthLatch l;
        for (uint32_t m = 0; m <= 16; m += 4) Refuse(l, m * kMin);
        const bool a = wxsetup::NeedsReverify(l);
        wxsetup::OnAnswer(l, 200, nullptr, 17 * kMin);
        const bool b = !wxsetup::NeedsReverify(l) && l.streak == 0 && wxsetup::Shown(l) == Refusal::None;
        for (uint32_t m = 18; m <= 34; m += 4) Refuse(l, m * kMin);
        const bool c = wxsetup::NeedsReverify(l);
        wxsetup::OnAnswer(l, 304, nullptr, 35 * kMin);
        const bool d = !wxsetup::NeedsReverify(l) && l.streak == 0;
        Refuse(l, 36 * kMin);
        const bool e = !wxsetup::NeedsReverify(l) && l.streak == 1;
        check(a && b && c && d && e, "LATCH-CLEAR",
              "a 200 and a 304 each clear a latched refusal and its streak at once; the next refusal starts again");
    }
    {
        AuthLatch l;
        for (uint32_t m = 0; m <= 3; ++m) Refuse(l, m * kMin);
        const int quiet[] = {0, 500, 503, 429, 404};
        for (int i = 0; i < 5; ++i) wxsetup::OnAnswer(l, quiet[i], nullptr, (4 + i) * kMin);
        const bool a = l.streak == 4 && !wxsetup::NeedsReverify(l);
        Refuse(l, 16 * kMin);
        const bool b = wxsetup::NeedsReverify(l) && l.streak == 5;
        for (int i = 0; i < 5; ++i) wxsetup::OnAnswer(l, quiet[i], nullptr, (17 + i) * kMin);
        const bool c = wxsetup::NeedsReverify(l);
        check(a && b && c, "LATCH-NOT-EVIDENCE",
              "no answer, 500, 503, 429 and 404 neither advance nor clear the streak, before or after it latches");
    }
    {
        AuthLatch l;
        for (uint32_t m = 0; m <= 3; ++m) Refuse(l, m * kMin);
        const bool a = wxsetup::Shown(l) == Refusal::None;
        Refuse(l, 15 * kMin, "forbidden");
        const bool b = wxsetup::Shown(l) == Refusal::NotActivated &&
                       std::strcmp(wxsetup::WordsOf(State::Refused, wxsetup::Shown(l)).title, "NOT ACTIVATED YET") == 0;
        Refuse(l, 16 * kMin, "fake_device_id");
        const bool c = wxsetup::Shown(l) == Refusal::DeviceUnknown;
        Refuse(l, 17 * kMin);
        const bool d = wxsetup::Shown(l) == Refusal::KeyRejected;
        Refuse(l, 18 * kMin, "other");
        const bool e = wxsetup::Shown(l) == Refusal::Other;
        check(a && b && c && d && e, "LATCH-KIND",
              "nothing is shown before it latches; then the latest refusal's own words (NOT ACTIVATED YET first)");
    }
    {
        const uint32_t f = 0xFFFF0000u;   // 65.5 s before millis() wraps
        AuthLatch l;
        for (uint32_t i = 0; i < 4; ++i) Refuse(l, f + i * 1000);
        const bool a = !wxsetup::NeedsReverify(l);
        Refuse(l, f + 900000u);           // past the wrap
        const bool b = wxsetup::NeedsReverify(l);
        check(a && b, "LATCH-WRAP", "a streak that crosses millis()'s wrap latches at 900 s, as any other");
    }
    {
        // The screen and the page read ONE bit: over random answer sequences, the refused screen (Decide, given
        // Shown) and NeedsReverify (what the page's %REFUSED% shows) never disagree.
        const int st[] = {0, 200, 304, 401, 403, 403, 403, 500, 503};
        const char* er[] = {nullptr, nullptr, nullptr, "unauthorized", "fake_device_id", "forbidden", "other",
                            nullptr, nullptr};
        uint32_t seed = 12345;
        auto rnd = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
        long checks = 0, disagree = 0, latchedSeen = 0, streakOnly = 0;
        for (int run = 0; run < 2000; ++run) {
            AuthLatch l;
            uint32_t t = 0;
            for (int k = 0; k < 50; ++k) {
                const int i = (int)(rnd() % 9);
                t += (uint32_t)(rnd() % 601) * 1000u;
                wxsetup::OnAnswer(l, st[i], er[i], t);
                wxsetup::Inputs in;
                in.wifi = in.location = in.key = true;
                in.refused = wxsetup::Shown(l);
                in.frame = (rnd() % 2) != 0;
                const bool screen = wxsetup::Decide(in) == State::Refused;
                ++checks;
                if (screen != wxsetup::NeedsReverify(l)) ++disagree;
                if (wxsetup::NeedsReverify(l)) ++latchedSeen;
                if (l.streak >= wxsetup::kLatchStreak && !l.latched) ++streakOnly;
            }
        }
        check(checks == 100000 && disagree == 0 && latchedSeen > 0 && streakOnly > 0, "LATCH-ONE-BIT",
              std::to_string(checks) + " answers, " + std::to_string(disagree) + " disagreements; latched in " +
                  std::to_string(latchedSeen) + ", a streak of 5+ not yet latched in " + std::to_string(streakOnly));
    }

    // ------------------------------------------------------------------------------------------------ the key
    {
        using wxsetup::RequestKey;
        const bool ok = std::strcmp(RequestKey("P", "F"), "P") == 0 && std::strcmp(RequestKey("", "F"), "F") == 0 &&
                        std::strcmp(RequestKey(nullptr, "F"), "F") == 0 && std::strcmp(RequestKey("P", ""), "P") == 0 &&
                        RequestKey("", "")[0] == 0 && RequestKey(nullptr, nullptr)[0] == 0;
        check(ok, "KEY-PRECEDENCE", "the pasted cloud-key, else cloud-key-fac, else none (the radar's order)");
    }
    {
        wxsetup::Inputs in;
        in.wifi = in.location = in.frame = true;
        in.key = wxsetup::RequestKey("P", "")[0] != 0;
        const bool a = wxsetup::Decide(in) == State::Live;
        in.key = wxsetup::RequestKey("", "")[0] != 0;
        const bool b = wxsetup::Decide(in) == State::NotEnrolled;
        check(a && b, "KEY-PASTED-ROW",
              "ruling (a): a pasted key with an empty cloud-key-fac draws Live (the page says not enrolled: "
              "W7-ENROLLED-SIDE); no key at all is NOT ENROLLED");
    }

    // ---------------------------------------------------------------------------------------------- the answer
    {
        Rig g;
        g.Answer(0, Status(401, "unauthorized"));
        const wxanswer::Result r = g.Answer(120000, g.Frame(1000));
        check(r.took == wxanswer::Took::Replaced && r.kept && g.ring.count == 1 && g.L.policy.haveCrop &&
                  g.L.latch.streak == 0 && g.L.policy.intervalMs == 60000,
              "ANSWER-200", "a valid 200 replaces the crop, the ring keeps it, the poll resets to 60 s, the streak clears");
    }
    {
        Rig g;
        g.Answer(0, g.Frame(1000));
        g.Answer(60000, Status(401, "unauthorized"));
        const wxanswer::Result r = g.Answer(180000, Status(304));
        check(r.took == wxanswer::Took::NotModified && g.ring.count == 1 && g.L.latch.streak == 0 &&
                  g.L.policy.intervalMs == 60000 && g.L.policy.haveCrop,
              "ANSWER-304", "a 304 keeps the crop and the ring as they are, resets the poll and clears the streak");
    }
    {
        Rig g;
        const wxanswer::Result r = g.Answer(0, Status(401, "unauthorized"));
        check(r.took == wxanswer::Took::Error && g.ring.count == 0 && g.L.policy.intervalMs == 120000 &&
                  g.L.latch.streak == 1 && !wxsetup::NeedsReverify(g.L.latch) && !g.L.policy.inFlight,
              "ANSWER-401", "a 401 is an error: the poll backs off to 120 s and the streak is 1, not latched");
    }
    {
        Rig g;
        g.Answer(0, g.Frame(1000));
        const uint32_t seq = wxpolicy::OnSend(g.L.policy, 60000);
        auto snap = [&g]() {
            char b[512];
            std::snprintf(b, sizeof b, "%u %d %u %s %d %u | %u %u %d %d | %u %u %u %u", (unsigned)g.L.policy.intervalMs,
                          (int)g.L.policy.haveCrop, (unsigned)g.L.policy.frameEpoch, g.L.policy.etag,
                          (int)g.L.policy.inFlight, (unsigned)g.L.policy.seq, (unsigned)g.L.latch.streak,
                          (unsigned)g.L.latch.firstMs, (int)g.L.latch.latched, (int)g.L.latch.kind,
                          (unsigned)g.ring.count, (unsigned)g.ring.next, (unsigned)g.ring.newSeen,
                          (unsigned)g.ring.newestSeen);
            return std::string(b);
        };
        const std::string s0 = snap();
        const wxanswer::Result r1 = wxanswer::OnReply(g.L, g.ring, seq - 1, g.q, Status(401, "unauthorized"), 61000);
        const wxanswer::Result r2 = wxanswer::OnReply(g.L, g.ring, seq + 5, g.q, g.Frame(2000), 62000);
        const bool unchanged = snap() == s0;
        const wxanswer::Result real = wxanswer::OnReply(g.L, g.ring, seq, g.q, Status(304), 63000);
        const std::string s1 = snap();
        const wxanswer::Result r3 = wxanswer::OnReply(g.L, g.ring, seq, g.q, Status(401, "unauthorized"), 64000);
        check(r1.took == wxanswer::Took::Stray && r2.took == wxanswer::Took::Stray && unchanged &&
                  real.took == wxanswer::Took::NotModified && r3.took == wxanswer::Took::Stray && snap() == s1,
              "ANSWER-STRAY", "answers to another request, or with none in flight, touch nothing; the real one is taken");
    }
    {
        Rig g;
        bool ok = true;
        g.Answer(0, Status(401, "unauthorized"));
        wxanswer::Reply bad = g.Frame(1000);
        bad.valid = false;
        ok &= g.Answer(60000, bad).took == wxanswer::Took::Kept && g.ring.count == 0 && g.L.latch.streak == 0;
        g.Answer(120000, Status(401, "unauthorized"));
        std::vector<uint8_t> big(wxpolicy::kMaxBytes + 1, 7);
        wxanswer::Reply over = g.Frame(1000);
        over.body = big.data();
        over.bytes = (uint32_t)big.size();
        ok &= g.Answer(240000, over).took == wxanswer::Took::Kept && g.ring.count == 0 && g.L.latch.streak == 0;
        g.Answer(300000, Status(401, "unauthorized"));
        const uint32_t seq = wxpolicy::OnSend(g.L.policy, 420000);
        const wxpolicy::Query other = wxanswer::QueryOf(10.50, -20.68);
        ok &= wxanswer::OnReply(g.L, g.ring, seq, other, g.Frame(1000), 420000).took == wxanswer::Took::Kept &&
              g.ring.count == 0 && g.L.latch.streak == 0;
        g.Answer(480000, g.Frame(2000));
        const size_t before = g.ring.count;
        g.Answer(540000, Status(401, "unauthorized"));
        ok &= g.Answer(660000, g.Frame(1000)).took == wxanswer::Took::Kept && g.ring.count == before &&
              g.L.latch.streak == 0;
        check(ok, "ANSWER-KEPT", "an invalid 200, one over 32,768 bytes, one for another query and one older than the "
                                 "held frame are kept out of the ring; each still clears the streak");
    }
    {
        Rig g;
        const bool a = wxpolicy::IfNoneMatch(g.L.policy, g.q) == nullptr;
        g.Answer(0, g.Frame(1000, "e1"));
        const char* e1 = wxpolicy::IfNoneMatch(g.L.policy, g.q);
        const bool b = e1 && std::strcmp(e1, "e1") == 0;
        g.Answer(60000, g.Frame(2000));
        const bool c = wxpolicy::IfNoneMatch(g.L.policy, g.q) == nullptr;
        g.Answer(120000, g.Frame(3000, "e3"));
        const char* e3 = wxpolicy::IfNoneMatch(g.L.policy, g.q);
        const bool d = e3 && std::strcmp(e3, "e3") == 0;
        const wxpolicy::Query q2 = wxanswer::QueryOf(10.50, -20.68);
        wxanswer::SetQuery(g.L, g.ring, q2);
        const bool e = wxpolicy::IfNoneMatch(g.L.policy, q2) == nullptr;
        check(a && b && c && d && e, "ANSWER-ETAG",
              "If-None-Match only with an ETag from the latest 200 for this query; none after a query change");
    }
    {
        Rig g;
        for (uint32_t i = 0; i < 5; ++i) g.Answer(i * 60000, g.Frame(1000 * (i + 1)));
        const bool a = g.ring.count == 3 && g.ring.CanLoop() && g.L.policy.haveCrop;
        wxanswer::SetQuery(g.L, g.ring, g.q);
        const bool b = g.ring.count == 3 && g.L.policy.haveCrop;
        const wxpolicy::Query q2 = wxanswer::QueryOf(10.50, -20.68);
        wxanswer::SetQuery(g.L, g.ring, q2);
        wx::CropBytes out[wxring::kSlots];
        const bool c = g.ring.count == 0 && !g.ring.CanLoop() && g.ring.History(out, wxring::kSlots) == 0 &&
                       !g.L.policy.haveCrop && g.L.policy.etag[0] == 0;
        wxsetup::Inputs in;
        in.wifi = in.location = in.key = true;
        in.frame = g.L.policy.haveCrop;
        const bool d = wxsetup::Decide(in) == State::Waiting;
        g.q = q2;
        const wxanswer::Result r = g.Answer(300000, g.Frame(7000));
        const bool e = r.kept && g.ring.count == 1;
        check(a && b && c && d && e, "ANSWER-QUERY-CHANGE",
              "a new query empties the ring (3 frames to 0) and drops the crop, so the screen waits; the same query "
              "changes nothing; the new place's first frame is kept");
    }
    {
        long sends[4] = {0, 0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            Rig g;
            wxsetup::Inputs in;
            in.wifi = k != 0;
            in.location = k != 1;
            in.key = k != 2;
            for (uint32_t t = 0; t < 3600; ++t) {
                if (!wxpolicy::ShouldFetch(g.L.policy, wxanswer::Enabled(in), t * 1000)) continue;
                ++sends[k];
                g.Answer(t * 1000, Status(304));
            }
        }
        check(sends[0] == 0 && sends[1] == 0 && sends[2] == 0 && sends[3] == 60, "ANSWER-DISABLED",
              "requests in an hour: no Wi-Fi " + std::to_string(sends[0]) + ", no location " + std::to_string(sends[1]) +
                  ", no key " + std::to_string(sends[2]) + ", all three set " + std::to_string(sends[3]));
    }
    {
        // The policy's query and the URL's coordinates are the same numbers: a synthetic sweep of every 0.001 deg.
        long n = 0, mismatch = 0;
        for (long i = -180000; i <= 180000; ++i) {
            const double v = i / 1000.0;
            const wxpolicy::Query qq = wxanswer::QueryOf(v, v);
            std::string c = wireloc::Center(v).c_str();
            c.erase(std::remove(c.begin(), c.end(), '.'), c.end());
            const long w = std::atol(c.c_str());
            ++n;
            if (w != qq.lat100 || w != qq.lon100) ++mismatch;
        }
        const std::string query = wxlive::WeatherscopeWxQuery(0.0, 0.0).c_str();
        const size_t at = query.find("&r=");
        const long r = at == std::string::npos ? -1 : std::atol(query.c_str() + at + 3);
        check(n == 360001 && mismatch == 0 && r == wxanswer::kRadiusKm, "QUERY-MATCH",
              std::to_string(n) + " points, " + std::to_string(mismatch) + " where the policy's query and the URL's "
              "2 dp differ; the URL's r " + std::to_string(r) + " is the query's");
    }

    std::printf("%s\n", failures ? "WX ANSWER TESTS FAILED" : "all WX answer checks passed");
    return failures ? 1 : 0;
}
