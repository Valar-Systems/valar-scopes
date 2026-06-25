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
};