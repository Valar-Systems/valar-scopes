#pragma once

// Stands in for include/DeviceIdentity.h (esp_mac + mbedtls). EamFeedClient sends
// LeaderboardId() as X-Missileer-Device on /eam/latest; any 8-32 hex id will do
// for the server, and the test asserts the header is sent.
#include <Arduino.h>

namespace DeviceIdentity {
inline const String& LeaderboardId()
{
    static const String id("e6e6e6e6e6e6e6e6");
    return id;
}
}  // namespace DeviceIdentity
