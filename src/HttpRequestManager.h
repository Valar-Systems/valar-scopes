#pragma once

#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

struct HttpResult {
    bool success;           // Whether the request succeeded
    int statusCode;         // HTTP status code (0 if network error)
    String response;        // Response body (empty on error)
    String errorMessage;    // Error description if success == false
};

class HttpRequestManager
{
private:
    HTTPClient http;

    // HTTPClient (and its single TLS context) is not reentrant, and the C3 hasn't
    // the heap for a second TLS session. The background OpenSky fetch task and the
    // loop task share this one instance, so every request cycle holds this mutex --
    // each begin()/GET()/end() runs to completion before another task can start one.
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    String BuildQueryString(const std::vector<std::pair<String, String>>& params) const;

public:
    HttpRequestManager() = default;
    ~HttpRequestManager() = default;

    [[nodiscard]] HttpResult Get(const String& url, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});
    [[nodiscard]] HttpResult Post(const String& url, const String& body = "", const std::vector<std::pair<String, String>>& headers = {});
};