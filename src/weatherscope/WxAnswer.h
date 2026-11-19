#pragma once
// What one /wx answer does on Weatherscope (docs/weatherscope-stage4.md S4.32, S4.37): to the fetch policy, to the
// refusal latch, and to the frame ring. Pure; the fetch itself (W7 slice 3b, ProxyWxFeed) sends each request and hands
// each answer here. WxPolicy is used for FETCHING only -- its rules (a), (b), (d), (e), (f) and (g) -- and never for
// what the screen shows (S4.33's correction; w7_static.py's W7-NOSHOW): the screens keep the owner's Q15/Q18 display.
// Host-tested in test/host/test_wx_answer.cpp; its sabotages are test/host/sabotage_wx_answer.py.
#include <math.h>
#include <stdint.h>

#include "WireLocation.h"
#include "WxPolicy.h"
#include "WxRing.h"
#include "WxSetup.h"

namespace wxanswer {

constexpr int32_t kRadiusKm = 97;   // the r that WxLiveUrl.h's WeatherscopeWxQuery sends

// The query a request carries, built as the radar builds its own (AircraftManager.cpp 3470-3474): the wire's 2-dp
// rounding, so the policy compares exactly what the URL sends (WeatherscopeWxQuery, through wireloc::Center).
inline wxpolicy::Query QueryOf(double lat, double lon)
{
    wxpolicy::Query q;
    q.lat100 = (int32_t)lround(wireloc::Round2(lat) * 100.0);
    q.lon100 = (int32_t)lround(wireloc::Round2(lon) * 100.0);
    q.rKm = kRadiusKm;
    return q;
}

// WxPolicy (a): no request at all without Wi-Fi, a location and a key.
inline bool Enabled(const wxsetup::Inputs& in) { return in.wifi && in.location && in.key; }

struct Live {
    wxpolicy::State policy;
    wxsetup::AuthLatch latch;
};

// The current query. A change drops the held crop and its ETag (WxPolicy (d)) AND empties the ring, so the loop never
// plays one place's frames at another.
inline void SetQuery(Live& L, wxring::Ring& ring, const wxpolicy::Query& q)
{
    if (L.policy.query == q) return;
    wxpolicy::OnQuery(L.policy, q);
    ring.Clear();
}

struct Reply {
    int status = 0;                  // <= 0: no answer
    const uint8_t* body = nullptr;
    uint32_t bytes = 0;
    bool valid = false;              // wxcrop::Validate() == Ok on the body
    uint32_t frameEpoch = 0;
    bool allNoCoverage = false;
    const char* etag = nullptr;
    const char* maxAge = nullptr;    // X-Wx-Max-Age: the policy records it (h); nothing here shows by it
    const char* error = nullptr;     // the JSON "error" of a 401 or 403
};

enum class Took : uint8_t { Stray, Replaced, NotModified, Kept, Error };

struct Result {
    Took took = Took::Stray;
    bool kept = false;               // the ring kept the frame (every other new frame)
};

// One answer to request `seq`, made for `asked`, arriving at `nowMs`. An answer to anything but the request in flight
// is STRAY and touches nothing: not the policy, not the latch, not the ring.
inline Result OnReply(Live& L, wxring::Ring& ring, uint32_t seq, const wxpolicy::Query& asked, const Reply& r,
                      uint32_t nowMs)
{
    Result res;
    if (!L.policy.inFlight || seq != L.policy.seq) return res;
    const wxpolicy::Outcome o = wxpolicy::OnResponse(L.policy, seq, asked, r.status, r.bytes, r.valid, r.frameEpoch,
                                                     r.allNoCoverage, r.etag, r.maxAge, nowMs);
    wxsetup::OnAnswer(L.latch, r.status, r.error, nowMs);
    switch (o) {
        case wxpolicy::Outcome::Replaced:
            res.took = Took::Replaced;
            res.kept = ring.Offer(r.body, r.bytes, r.frameEpoch);
            break;
        case wxpolicy::Outcome::NotModified: res.took = Took::NotModified; break;
        case wxpolicy::Outcome::Kept: res.took = Took::Kept; break;
        case wxpolicy::Outcome::Error: res.took = Took::Error; break;
    }
    return res;
}

}  // namespace wxanswer
