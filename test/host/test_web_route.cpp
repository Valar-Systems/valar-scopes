// Which requests the web build forwards (tools/web-renderer/web/web_route.h; docs/web-kiosk-airfield.md). Rows AF1 and AF2,
// one line each, for test/host/sabotage_web_kiosk.py. AF3, the draw, runs on the wasm (tools/web-renderer/airports_remote.mjs).
//
//   test_web_route.exe      exit 0 all pass, 1 a check failed
#include <cstdio>

#include "web_route.h"

static int failures = 0;
static void row(bool ok, const char* id, const char* what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what);
}

int main()
{
    const char* const kept[] = {"/api/v1/blipscope/blips", "/api/v1/blipscope/enrich/a1b2c3"};
    const char* const never[] = {"/api/v1/blipscope/config", "/api/v1/blipscope/photo/x", "/api/v1/blipscope/leaderboard",
                                 "/api/v1/blipscope/wx/cref", "/api/v1/blipscope/basemap", "/elsewhere"};
    const char* const airports = "/api/v1/blipscope/airports";

    bool af1 = !webroute::Forwarded(airports, false);
    for (const char* p : kept) af1 &= webroute::Forwarded(p, false);
    for (const char* p : never) af1 &= !webroute::Forwarded(p, false);
    af1 &= !webroute::AirportsRemoteOn("") && !webroute::AirportsRemoteOn("false") && !webroute::AirportsRemoteOn("1");
    row(af1, "AF1", "key off: /airports is not forwarded; /blips and /enrich/ are; nothing else is");

    bool af2 = webroute::Forwarded(airports, true) && webroute::AirportsRemoteOn("true");
    for (const char* p : kept) af2 &= webroute::Forwarded(p, true);
    for (const char* p : never) af2 &= !webroute::Forwarded(p, true);
    row(af2, "AF2", "key on: /airports is forwarded too, and nothing else is added");

    std::printf("%s\n", failures ? "WEB ROUTE TESTS FAILED" : "all web route checks passed");
    return failures ? 1 : 0;
}
