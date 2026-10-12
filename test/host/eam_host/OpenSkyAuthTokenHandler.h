#pragma once

// Stands in for src/OpenSkyAuthTokenHandler.h. The E6 test configures the
// command-post watch from the BACKEND, so no token is ever asked for; if one is,
// the empty answer makes EamFeedClient::Fetch report the fetch failed, which is
// the shipping behaviour for "no token".
#include "HttpRequestManager.h"

class OpenSkyAuthTokenHandler {
public:
    explicit OpenSkyAuthTokenHandler(HttpRequestManager&) {}
    const String GetValidToken(const String&, const String&) { return String(); }
};
