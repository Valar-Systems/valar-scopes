#pragma once

#include <HTTPClient.h>
#include <ArduinoJson.h>
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

    // GET + JSON decode that deserializes straight from the response stream when the
    // server sends a Content-Length, so the raw body is never buffered into a String
    // alongside the parsed document. That simultaneous body+document is the worst heap
    // peak on the C3 (the OpenSky feed), and it's what fragments the heap below what
    // the config web server needs for its send buffer. Chunked/unknown-length replies
    // fall back to the buffered path so parsing stays correct. The decoded doc is
    // written into `doc`; HttpResult.response stays empty.
    [[nodiscard]] HttpResult GetJson(const String& url, JsonDocument& doc, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});

    // Non-blocking access to the same request mutex, so an UNRELATED consumer can run
    // exclusively against a network request without blocking if one is in flight. The
    // touch poll uses this: a touch I2C transfer that overlaps a TLS handshake on the
    // single-core C3 wedges the CST816 off the bus, so HandleTouch only polls when it can
    // take this lock (i.e. no GET/POST is mid-flight on any task) and skips the frame
    // otherwise. TryAcquireBus() returns true iff it took the lock; pair with ReleaseBus().
    bool TryAcquireBus() { return xSemaphoreTake(mutex, 0) == pdTRUE; }
    void ReleaseBus()    { xSemaphoreGive(mutex); }
};