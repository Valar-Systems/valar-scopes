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
    bool reusedConnection = false; // socket was already open at request start (keep-alive, no TLS handshake)
};

class HttpRequestManager
{
private:
    // One HTTPClient per URL scheme. The scheme-only begin(String) API never lets go of its
    // internal client object: after an https request, end()/disconnect() keeps the
    // NetworkClientSecure around (for keep-alive reuse, and the not-connected branch keeps it
    // too), and connect() only builds a new transport when that client is null. So a single
    // instance that alternates https (OpenSky / adsbdb / ntfy) with plain http (a local
    // dump1090/readsb receiver, a MiniSpeedCam) re-uses the stale TLS client for the http URL
    // and tries an SSL handshake against port 80 -- "invalid SSL record" on every local fetch
    // until reboot. Pinning each scheme to its own instance keeps every transport consistent
    // for life. Only httpTls ever owns a TLS context and the mutex below still serializes ALL
    // requests across both, so the C3-era "one TLS session at a time" budget is unchanged.
    HTTPClient httpTls;   // https:// consumers
    HTTPClient httpPlain; // http:// consumers (LAN devices; no TLS context ever)
    HTTPClient& ClientFor(const String& url) { return url.startsWith("http://") ? httpPlain : httpTls; }

    // HTTPClient (and its single TLS context) is not reentrant, and the C3 hasn't
    // the heap for a second TLS session. The background OpenSky fetch task and the
    // loop task share these instances, so every request cycle holds this mutex --
    // each begin()/GET()/end() runs to completion before another task can start one.
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

    String BuildQueryString(const std::vector<std::pair<String, String>>& params) const;

    // Drain an HTTP response body into a String with periodic yields + a size/time cap.
    // HTTPClient::getString() reads in a loop that never lets core 0's priority-0 idle task
    // run, so a slow or large body on the (core-0-pinned) fetch/enrich tasks starves the
    // Task-WDT into a reboot. The photo fetch (an airport-data.com thumbnail behind a
    // redirect) hits exactly that. This replaces getString() in Get(); GetJson() already
    // streams via the yielding BufferedSocketStream.
    String ReadBodyYielding(HTTPClient& http);

    // Shared body for the two GetJson overloads below. When `filter` is non-null it is applied
    // as a DeserializationOption::Filter, so only whitelisted fields are pulled off the stream.
    HttpResult GetJsonImpl(const String& url, JsonDocument& doc, const JsonDocument* filter,
                           const std::vector<std::pair<String, String>>& params,
                           const std::vector<std::pair<String, String>>& headers);

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

    // As above, but deserialize through an ArduinoJson filter so only the whitelisted fields are
    // pulled from a large response (e.g. a busy local dump1090/readsb aircraft.json that lists many
    // aircraft with many fields each) -- keeps the parsed document small on a tight heap.
    [[nodiscard]] HttpResult GetJson(const String& url, JsonDocument& doc, const JsonDocument& filter, const std::vector<std::pair<String, String>>& params = {}, const std::vector<std::pair<String, String>>& headers = {});

    // Non-blocking access to the same request mutex, so an UNRELATED consumer can run
    // exclusively against a network request without blocking if one is in flight. The
    // touch poll uses this: a touch I2C transfer that overlaps a TLS handshake on the
    // single-core C3 wedges the CST816 off the bus, so HandleTouch only polls when it can
    // take this lock (i.e. no GET/POST is mid-flight on any task) and skips the frame
    // otherwise. TryAcquireBus() returns true iff it took the lock; pair with ReleaseBus().
    bool TryAcquireBus() { return xSemaphoreTake(mutex, 0) == pdTRUE; }
    void ReleaseBus()    { xSemaphoreGive(mutex); }
};