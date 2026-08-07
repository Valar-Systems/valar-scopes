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

    // --- per-request measurement (see MEASUREMENT in AircraftManager) ----------
    // The proxy has always returned X-Cache (HIT/STALE/MISS) and X-Upstream, and
    // the device has always thrown them away -- so "is the feed dry or is the
    // device not asking?" needed a cross-system log join to answer. Kept here it
    // is answerable per poll, on the device, with no correlation step at all.
    String cacheState;             // X-Cache: HIT | STALE | MISS ("" if absent)
    String upstream;               // X-Upstream: which source actually served it
    size_t bodyBytes = 0;          // response body size (Content-Length, or bytes read)
    unsigned long parseMs = 0;     // JSON deserialize time only, excluding transfer
    unsigned long requestMs = 0;   // whole call: connect + transfer + parse
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

    // GET over https validated against a PINNED root chain, on the SAME shared client.
    //
    // Every other request here goes through HTTPClient::begin(url). For an https URL that
    // falls through to begin(url, nullptr) -> TLSTraits(nullptr) -> setInsecure(): the feeds
    // are NOT certificate-validated. That is a defensible trade for data whose worst case is
    // a wrong number on a screen. It is not defensible for the update channel, where a lie
    // about the latest version silently pins a fleet on an old build, undetectably.
    //
    // begin(url, ca) installs TLSTraits(ca) and connect() applies it, via verify(), to the
    // client this object ALREADY owns -- so a pinned request costs no additional TLS context.
    // That is the point: the OTA check used to stand up its own WiFiClientSecure alongside
    // this one, which is the one-client invariant the rest of the system is built on.
    // Redirects keep the pin: setURL() preserves _transportTraits, so the CDN hop GitHub
    // issues is validated against the same roots.
    [[nodiscard]] HttpResult GetSecure(const String& url, const char* caCert,
                                       const std::vector<std::pair<String, String>>& headers = {});

    // Drop the shared https client's live TLS session. CALL WITH THE BUS HELD.
    //
    // HTTPClient::end() does not reliably do this. disconnect(false) only releases the
    // transport when the socket is currently connected AND not marked reusable, and the
    // feeds run keep-alive, so between polls this object holds a live mbedTLS session --
    // context plus buffers, the largest contiguous allocation on the device. Anything that
    // then wants its own TLS context is asking for a second one simultaneously.
    //
    // That is what the OTA download does, and it is the most likely reading of the soak's
    // failed 16,717 B allocation. Takes no lock, because the caller must already hold the
    // bus for the whole window it is protecting -- otherwise a background fetch re-opens a
    // session in the gap between the release and the download.
    void ReleaseTlsLocked();

    // Non-blocking access to the same request mutex, so an UNRELATED consumer can run
    // exclusively against a network request without blocking if one is in flight. The
    // touch poll uses this: a touch I2C transfer that overlaps a TLS handshake on the
    // single-core C3 wedges the CST816 off the bus, so HandleTouch only polls when it can
    // take this lock (i.e. no GET/POST is mid-flight on any task) and skips the frame
    // otherwise. TryAcquireBus() returns true iff it took the lock; pair with ReleaseBus().
    bool TryAcquireBus() { return xSemaphoreTake(mutex, 0) == pdTRUE; }
    void ReleaseBus()    { xSemaphoreGive(mutex); }

    // TLS connection accounting (https only; plain-http LAN requests never
    // handshake). A FRESH https connection needs a large contiguous block for the
    // handshake, and on this heap that -- not parsing a sub-KB body -- is the
    // expensive part of a request. Alternating between two hosts forces a new
    // handshake every switch, because the keep-alive socket only helps when the
    // next request goes to the same place.
    //
    // Counting handshakes vs reuses turns "does routing detail lookups through one
    // host instead of two reduce heap pressure?" into a measured number rather than
    // an argument. Both counters are only ever touched while holding the request
    // mutex, so they need no separate synchronisation.
    uint32_t TlsHandshakes() const { return tlsHandshakes; }
    uint32_t TlsReuses() const     { return tlsReuses; }

private:
    uint32_t tlsHandshakes = 0;
    uint32_t tlsReuses = 0;
    // Call with the mutex held, after reusedConnection is known.
    void NoteTls(const String& url, bool reused) {
        if (url.startsWith("http://")) return; // plain http: no TLS context at all
        if (reused) ++tlsReuses; else ++tlsHandshakes;
    }
};