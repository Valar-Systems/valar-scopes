#pragma once
// Which of the firmware's requests the web build forwards to the page's apiBase (docs/web-renderer.md,
// docs/web-kiosk-airfield.md). Everything else is answered here, with no request.
//
// /airports is forwarded only when the page sets "airports-remote" = "true": the app then serves the venue's own field,
// and its reply replaces the baked table. Off -- the default, and the public website -- nothing new leaves.
//
// /photo/ is the card's photo, fetched on a card open from the path the enrich reply names (the owner, 2026-11-24:
// the website and the screens show full cards). Only this one prefix: a near miss (/v1/photo/, /photos/, /photo with
// no slash, an absolute URL) is answered here like everything else (_handoffs/p4/web-photos/REG-web-photos.txt, C1).
// std::string only, so test/host/test_web_route.cpp can compile it on its own.
#include <string>

namespace webroute {

constexpr const char* kBlips = "/api/v1/blipscope/blips";
constexpr const char* kEnrich = "/api/v1/blipscope/enrich/";
constexpr const char* kAirports = "/api/v1/blipscope/airports";
constexpr const char* kPhoto = "/api/v1/blipscope/photo/";

inline bool StartsWith(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

inline bool IsBlips(const std::string& path) { return StartsWith(path, kBlips); }

inline bool IsPhoto(const std::string& path) { return StartsWith(path, kPhoto); }

inline bool AirportsRemoteOn(const std::string& stored) { return stored == "true"; }

inline bool Forwarded(const std::string& path, bool airportsRemote)
{
    if (StartsWith(path, kBlips) || StartsWith(path, kEnrich) || IsPhoto(path)) return true;
    return airportsRemote && StartsWith(path, kAirports);
}

}  // namespace webroute
