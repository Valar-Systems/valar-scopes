// W7's first slice, host-side (docs/weatherscope-stage4.md S4.32, S4.33): the live radar's frame ring (WxRing.h), the
// setup states and their words (WxSetup.h), and Weatherscope's /wx request (WxLiveUrl.h). Pure C++11; WxLiveUrl.h
// reads WireLocation.h, so this builds against test/host/arduino_shim. Its sabotages: test/host/sabotage_wx_live.py.
//
//   g++ -std=c++11 -Iinclude -Isrc/weatherscope -Itest/host/arduino_shim test/host/test_wx_live.cpp
//
// Prints ids, counts and the request strings only. The request's two points are synthetic, not a real place.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WxRing.h"
#include "WxSetup.h"
#include "WxLiveUrl.h"

static int failures = 0;

static void check(bool ok, const char* id, const std::string& what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what.c_str());
}

// ------------------------------------------------------------------------------------------------ the allocator
static int allocs = 0, frees = 0, failAt = -1;
static void* Alloc(size_t n)
{
    const int i = allocs++;
    if (i == failAt) return nullptr;
    return std::malloc(n);
}
static void Release(void* p)
{
    ++frees;
    std::free(p);
}
static void ResetAlloc(int fail)
{
    allocs = 0;
    frees = 0;
    failAt = fail;
}

// A frame whose every byte is `tag`, so History() can be read back as the tags it holds.
static std::vector<uint8_t> FrameOf(uint8_t tag, size_t n = 64) { return std::vector<uint8_t>(n, tag); }

static std::string Tags(const wxring::Ring& r)
{
    wx::CropBytes h[wxring::kSlots];
    const size_t n = r.History(h, wxring::kSlots);
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        char b[8];
        std::snprintf(b, sizeof b, "%s%u", i ? "," : "", (unsigned)h[i].data[0]);
        s += b;
    }
    return s;
}

static void Free(wxring::Ring& r)
{
    for (size_t i = 0; i < wxring::kSlots; ++i)
        if (r.slot[i]) Release(r.slot[i]);
}

int main()
{
    // ---------------------------------------------------------------------------------------------------- the ring
    {
        ResetAlloc(-1);
        wxring::Ring r;
        const bool ok = r.Init(Alloc, Release);
        check(ok && r.ok && allocs == (int)wxring::kSlots && r.count == 0 && !r.CanLoop(), "RING-ALLOC",
              "15 slots allocated once, empty, no loop: allocs " + std::to_string(allocs));
        Free(r);
    }
    {
        ResetAlloc(7);   // the 8th allocation fails
        wxring::Ring r;
        const bool ok = r.Init(Alloc, Release);
        bool nulls = true;
        for (size_t i = 0; i < wxring::kSlots; ++i) nulls &= r.slot[i] == nullptr;
        const std::vector<uint8_t> f = FrameOf(1);
        const bool offered = r.Offer(f.data(), f.size(), 1000);
        check(!ok && !r.ok && frees == 7 && nulls && !offered && r.count == 0, "RING-ALLOC-NONE",
              "all or none: the 7 allocated freed (" + std::to_string(frees) + "), every slot null, Offer refused");
    }
    {
        ResetAlloc(-1);
        wxring::Ring r;
        r.Init(Alloc, Release);
        std::string kept;
        for (int i = 0; i < 10; ++i) {
            const std::vector<uint8_t> f = FrameOf((uint8_t)i);
            kept += r.Offer(f.data(), f.size(), 1000 + 120 * i) ? "K" : ".";
        }
        check(kept == "K.K.K.K.K." && r.count == 5 && Tags(r) == "0,2,4,6,8", "RING-EVERY-OTHER",
              "every other new frame, from the first: " + kept + "; history " + Tags(r));
        Free(r);
    }
    {
        ResetAlloc(-1);
        wxring::Ring r;
        r.Init(Alloc, Release);
        const std::vector<uint8_t> a = FrameOf(1), b = FrameOf(2), c = FrameOf(3), d = FrameOf(4);
        const bool k1 = r.Offer(a.data(), a.size(), 1000);
        const bool k2 = r.Offer(b.data(), b.size(), 1000);   // not newer: not new
        const bool k3 = r.Offer(c.data(), c.size(), 1120);   // the 2nd new: skipped
        const bool k4 = r.Offer(d.data(), d.size(), 1240);   // the 3rd new: kept
        check(k1 && !k2 && !k3 && k4 && r.count == 2 && Tags(r) == "1,4", "RING-REPEAT",
              "a frame no newer than the newest seen adds nothing and does not count: history " + Tags(r));
        Free(r);
    }
    {
        ResetAlloc(-1);
        wxring::Ring r;
        r.Init(Alloc, Release);
        for (int i = 0; i < 40; ++i) {
            const std::vector<uint8_t> f = FrameOf((uint8_t)i);
            r.Offer(f.data(), f.size(), 1000 + 120 * i);
        }
        check(r.count == wxring::kSlots && Tags(r) == "10,12,14,16,18,20,22,24,26,28,30,32,34,36,38", "RING-WRAP",
              "40 new frames keep 20; the newest 15, oldest first: " + Tags(r));
        Free(r);
    }
    {
        ResetAlloc(-1);
        wxring::Ring r;
        r.Init(Alloc, Release);
        std::string can;
        for (int i = 0; i < 6; ++i) {
            const std::vector<uint8_t> f = FrameOf((uint8_t)i);
            r.Offer(f.data(), f.size(), 1000 + 120 * i);
            can += r.CanLoop() ? "Y" : "n";
        }
        check(can == "nnnnYY", "RING-LOOP-MIN", "a tap loops only with 3 kept frames, after each of 6 new frames: " + can);
        Free(r);
    }
    {
        ResetAlloc(-1);
        wxring::Ring r;
        r.Init(Alloc, Release);
        std::vector<uint8_t> f = FrameOf(7, 100);
        r.Offer(f.data(), f.size(), 1000);
        f[0] = 99;   // the caller's buffer moves on
        wx::CropBytes h[1];
        const size_t n = r.History(h, 1);
        check(n == 1 && h[0].size == 100 && h[0].data[0] == 7 && h[0].data != f.data(), "RING-COPY",
              "a kept frame is a copy of its bytes, at the size offered");
        Free(r);
    }
    {
        ResetAlloc(-1);
        wxring::Ring r;
        r.Init(Alloc, Release);
        const std::vector<uint8_t> big = FrameOf(5, wxcrop::kMaxBytes + 1), ok1 = FrameOf(6), ok2 = FrameOf(8);
        const bool kb = r.Offer(big.data(), big.size(), 1000);
        const bool k1 = r.Offer(ok1.data(), ok1.size(), 1120);   // still the FIRST new frame: kept
        const bool k2 = r.Offer(ok2.data(), ok2.size(), 1240);   // the 2nd: skipped
        check(!kb && k1 && !k2 && r.count == 1 && Tags(r) == "6", "RING-OVERSIZE",
              "a frame over kMaxBytes is refused and never counts as new: history " + Tags(r));
        Free(r);
    }

    // -------------------------------------------------------------------------------------------- the setup states
    using wxsetup::State;
    using wxsetup::Refusal;
    {
        // S4.37: a refusal reaches Decide as the latch's shown kind (WxSetup.h AuthLatch), not as the last answer.
        struct Case { bool wifi, loc, key; Refusal refused; bool frame; State want; };
        const Case cases[] = {
            {false, false, false, Refusal::None, false, State::NoWifi},
            {false, true, true, Refusal::None, true, State::NoWifi},
            {true, false, true, Refusal::None, true, State::NoLocation},
            {true, true, false, Refusal::None, false, State::NotEnrolled},
            {true, true, true, Refusal::KeyRejected, true, State::Refused},
            {true, true, true, Refusal::None, false, State::Waiting},
            {true, true, true, Refusal::None, true, State::Live},
            {true, true, true, Refusal::NotActivated, false, State::Refused},
        };
        int bad = 0;
        for (const Case& c : cases) {
            wxsetup::Inputs in;
            in.wifi = c.wifi;
            in.location = c.loc;
            in.key = c.key;
            in.refused = c.refused;
            in.frame = c.frame;
            if (wxsetup::Decide(in) != c.want) ++bad;
        }
        check(bad == 0, "STATE-PRECEDENCE", "no Wi-Fi, then no location, then not enrolled, then refused, then waiting, "
                                            "then live: 8 cases, " + std::to_string(bad) + " wrong");
    }
    {
        const Refusal kinds[] = {Refusal::None, Refusal::KeyRejected, Refusal::DeviceUnknown, Refusal::NotActivated,
                                 Refusal::Other};
        int combos = 0, liveNoFrame = 0;
        for (int m = 0; m < 16; ++m)
            for (Refusal k : kinds) {
                wxsetup::Inputs in;
                in.wifi = m & 1;
                in.location = m & 2;
                in.key = m & 4;
                in.frame = m & 8;
                in.refused = k;
                ++combos;
                if (wxsetup::Decide(in) == State::Live && !in.frame) ++liveNoFrame;
            }
        check(combos == 80 && liveNoFrame == 0, "STATE-LIVE-NEEDS-FRAME",
              "over all 80 inputs, live never without a frame: " + std::to_string(liveNoFrame) + " violations");
    }
    {
        auto at = [](Refusal refused, bool frame) {
            wxsetup::Inputs in;
            in.wifi = in.location = in.key = true;
            in.refused = refused;
            in.frame = frame;
            return wxsetup::Decide(in);
        };
        const bool ok = wxsetup::RefusalOf(401, "unauthorized") == Refusal::KeyRejected &&
                        wxsetup::RefusalOf(403, "fake_device_id") == Refusal::DeviceUnknown &&
                        wxsetup::RefusalOf(403, "forbidden") == Refusal::NotActivated &&
                        wxsetup::RefusalOf(403, "other") == Refusal::Other &&
                        wxsetup::RefusalOf(500, "") == Refusal::None &&
                        at(Refusal::KeyRejected, true) == State::Refused && at(Refusal::None, true) == State::Live &&
                        at(Refusal::None, false) == State::Waiting && at(Refusal::Other, false) == State::Refused;
        check(ok, "STATE-REFUSED", "401 and both 403s map to their refusal; a latched refusal is said in words; without "
                                   "one the last frame is live, or the screen waits (no frame)");
    }
    {
        const char* t[] = {wxsetup::WordsOf(State::NoWifi, Refusal::None).title,
                           wxsetup::WordsOf(State::NoLocation, Refusal::None).title,
                           wxsetup::WordsOf(State::NotEnrolled, Refusal::None).title,
                           wxsetup::WordsOf(State::Waiting, Refusal::None).title,
                           wxsetup::WordsOf(State::Refused, Refusal::KeyRejected).title,
                           wxsetup::WordsOf(State::Refused, Refusal::DeviceUnknown).title,
                           wxsetup::WordsOf(State::Refused, Refusal::NotActivated).title,
                           wxsetup::WordsOf(State::Refused, Refusal::Other).title};
        bool ok = std::strlen(wxsetup::WordsOf(State::Live, Refusal::None).title) == 0;
        for (int i = 0; i < 8; ++i) {
            ok &= t[i] && std::strlen(t[i]) > 0;
            for (int j = 0; j < i; ++j) ok &= std::strcmp(t[i], t[j]) != 0;
        }
        check(ok, "STATE-WORDS", "each setup state and each refusal has its own non-empty title; live has none");
    }

    // ------------------------------------------------------------------------------------------------ the request
    {
        const String u = wxlive::WeatherscopeWxUrl(String("https://proxy.example"), "shsr");
        check(u == "https://proxy.example/api/v1/blipscope/wx/shsr", "URL-PATH", std::string("the path: ") + u.c_str());
    }
    {
        const String a = wxlive::WeatherscopeWxQuery(10.1265, -20.6789);   // synthetic points, no real place
        const String b = wxlive::WeatherscopeWxQuery(-0.004, 0.004);
        check(a == "?lat=10.13&lon=-20.68&r=97" && b == "?lat=0.00&lon=0.00&r=97", "URL-QUERY",
              std::string("2 dp through WireLocation.h, r = 97 km: ") + a.c_str() + " | " + b.c_str());
    }

    std::printf("%s\n", failures ? "WX LIVE TESTS FAILED" : "all WX live checks passed");
    return failures ? 1 : 0;
}
