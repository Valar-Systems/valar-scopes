#pragma once
#include "Arduino.h"

// Blipscope web demo: only the TYPE, so src/HttpRequestManager.h compiles. Nothing calls it: the demo's
// HttpRequestManager (web/web_http.cpp) answers every request from the fixed scene, with no socket.
class WiFiClient
{
  public:
    void setConnectionTimeout(uint32_t) {}
    void setTimeout(uint32_t) {}
    void stop() {}
    bool connected() { return false; }
};
class HTTPClient
{
  public:
    void end() {}
};
#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTP_CODE_OK 200
