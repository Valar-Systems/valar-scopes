#pragma once
#include "Arduino.h"
#include "WiFiClient.h"

// Blipscope web demo: MQTT never connects (the demo's config leaves it off, and there is no network).
class PubSubClient
{
  public:
    explicit PubSubClient(WiFiClient&) {}
    PubSubClient& setServer(const char*, uint16_t) { return *this; }
    PubSubClient& setBufferSize(uint16_t) { return *this; }
    PubSubClient& setKeepAlive(uint16_t) { return *this; }
    PubSubClient& setSocketTimeout(uint16_t) { return *this; }
    bool connect(const char*) { return false; }
    bool connect(const char*, const char*, const char*) { return false; }
    bool connect(const char*, const char*, const char*, const char*, uint8_t, bool, const char*) { return false; }
    bool connect(const char*, const char*, const char*, const char*, uint8_t, bool, const char*, bool) { return false; }
    bool connected() { return false; }
    bool publish(const char*, const char*) { return false; }
    bool publish(const char*, const char*, bool) { return false; }
    bool publish(const char*, const uint8_t*, unsigned int, bool) { return false; }
    bool beginPublish(const char*, unsigned int, bool) { return false; }
    size_t write(const uint8_t*, size_t) { return 0; }
    bool endPublish() { return false; }
    bool loop() { return false; }
    void disconnect() {}
    int state() { return -1; }
};
