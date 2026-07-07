#pragma once

#include "HttpRequestManager.h"

class OpenSkyAuthTokenHandler
{
private:
    HttpRequestManager& http;

    String bearerToken = "";
    unsigned long tokenIssuedMs = 0; // millis() when the cached token was fetched;
                                     // age is compared with wrap-safe subtraction
    String tokenClientId = "";      // credentials the cached token was issued under;
    String tokenClientSecret = "";  // a change invalidates the cache so new keys apply at once

    String FetchBearerToken(const String& url, const String& clientId, const String& clientSecret);

public:
    OpenSkyAuthTokenHandler(HttpRequestManager& httpRequestManager) : http(httpRequestManager) {}
    ~OpenSkyAuthTokenHandler() = default;

    [[nodiscard]] const String GetValidToken(const String& clientId, const String& clientSecret);

    // Drop the cached token so the next GetValidToken refetches -- called when a
    // states fetch comes back 401/403 (e.g. the token expired server-side, or the
    // cached-age bookkeeping was wrong), so auth failures self-heal within one cycle.
    void Invalidate() { bearerToken = ""; }
};