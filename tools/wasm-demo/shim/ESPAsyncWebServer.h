#pragma once
#include "Arduino.h"

// Blipscope web demo: only the TYPE, so src/ConfigurationWebServer.h compiles. The demo serves no page;
// its ConfigurationWebServer (web/web_config.cpp) answers GetStoredString from a fixed map.
class AsyncWebServer
{
  public:
    explicit AsyncWebServer(uint16_t) {}
};
