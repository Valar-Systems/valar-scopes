#pragma once

#include "HttpRequestManager.h"

class OpenSkyAuthTokenHandler
{
private:
    HttpRequestManager& http;

    String bearerToken = "";
    unsigned long tokenExpiry = 0;  // millis() timestamp
    String tokenClientId = "";      // credentials the cached token was issued under;
    String tokenClientSecret = "";  // a change invalidates the cache so new keys apply at once

    String FetchBearerToken(const String& url, const String& clientId, const String& clientSecret);

public:
    OpenSkyAuthTokenHandler(HttpRequestManager& httpRequestManager) : http(httpRequestManager) {}
    ~OpenSkyAuthTokenHandler() = default;

    [[nodiscard]] const String GetValidToken(const String& clientId, const String& clientSecret);
};