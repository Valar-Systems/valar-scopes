#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>

#include "EamModels.h"

// Command-post watch ("ABNCP") data path, behind one firmware-side interface so the rest of the
// app is source-agnostic. Two implementations, user-selectable in web config:
//
//   1. BackendAbncpProvider (DEFAULT, no setup) -- GET {base}/status/abncp, already normalized
//      by the Valar feed (sourced from adsb.lol upstream). The firmware just relays it.
//   2. OpenSkyAbncpProvider (bring-your-own-account) -- queries OpenSky DIRECTLY from the device
//      with the USER's own OAuth credentials, filtered to an ICAO24 watchlist. It MUST NOT route
//      through the Valar backend, and there is NEVER a shared/baked-in OpenSky key: with blank
//      credentials the provider is inert (BuildRequest returns false, InertReason explains why).
//
// A provider only builds the worker request (the blocking token+GET runs off-loop); the worker
// parses the response by endpoint (ParseAbncpBackend vs ParseOpenSkyStates).
namespace eam {

class AbncpProvider {
public:
    virtual ~AbncpProvider() = default;

    // Poll cadence in ms. Never set below 60 s for OpenSky (their fair-use guidance).
    virtual uint32_t IntervalMs() const = 0;

    // Human label for the about/diagnostics ("adsb.lol via Valar feed", "OpenSky (your account)").
    virtual const char* Name() const = 0;

    // Non-null when the provider can't poll and the ABNCP screen should say so (and nothing is
    // called) -- e.g. OpenSky selected but credentials blank. Null when the provider is live.
    virtual const char* InertReason() const { return nullptr; }

    // Fill `req` for the next poll. Returns false if inert (no request should be sent).
    virtual bool BuildRequest(EamFetchRequest& req) const = 0;
};

// ------------------------------------------------------------------ adsb.lol via Valar feed
class BackendAbncpProvider : public AbncpProvider {
public:
    explicit BackendAbncpProvider(String baseUrl) : base(std::move(baseUrl)) {}

    uint32_t IntervalMs() const override { return 60000; } // ~60 s
    const char* Name() const override { return "adsb.lol via Valar feed"; }

    bool BuildRequest(EamFetchRequest& req) const override
    {
        req.endpoint = EamEndpoint::Abncp;
        req.url = base + "/status/abncp";
        return true;
    }

private:
    String base;
};

// ----------------------------------------------------------------- OpenSky (your account)
class OpenSkyAbncpProvider : public AbncpProvider {
public:
    OpenSkyAbncpProvider(String clientId, String clientSecret, std::vector<String> icaoWatch)
        : id(std::move(clientId)), secret(std::move(clientSecret)), watch(std::move(icaoWatch)) {}

    uint32_t IntervalMs() const override { return 90000; } // ~90 s, never faster than 60 s
    const char* Name() const override { return "OpenSky (your account)"; }

    const char* InertReason() const override
    {
        return (id.isEmpty() || secret.isEmpty()) ? "needs your OpenSky credentials" : nullptr;
    }

    bool BuildRequest(EamFetchRequest& req) const override
    {
        if (id.isEmpty() || secret.isEmpty())
            return false; // inert: never call OpenSky without the user's own credentials

        req.endpoint = EamEndpoint::AbncpOpenSky;
        req.url = "https://opensky-network.org/api/states/all";
        req.needsOpenSkyToken = true;
        req.openskyId = id;
        req.openskySecret = secret;
        // Filter to the watchlist hexes (E-4B can be anywhere, so no bbox). OpenSky accepts a
        // repeated icao24 param. extended=1 includes the category column.
        for (const String& h : watch)
            if (h.length()) req.params.push_back({"icao24", h});
        req.params.push_back({"extended", "1"});
        return true;
    }

private:
    String id;
    String secret;
    std::vector<String> watch;
};

} // namespace eam
