#pragma once

// Stands in for include/DeviceIdentity.h (esp_mac + mbedtls). EamFeedClient sends
// LeaderboardId() as X-Missileer-Device on /eam/latest; any 8-32 hex id will do
// for the server, and the test asserts the header is sent.
#include <Arduino.h>

namespace DeviceIdentity {
inline const String& LeaderboardId()
{
    static const String id("0000000000000000");   // EXAMPLE -- not a real device
    return id;
}
}  // namespace DeviceIdentity
