#include "HttpRequestManager.h"

namespace {
// Hard ceiling on any JSON body parsed off the socket. Every legitimate feed here is
// well under this (a wide OpenSky box is ~100-200 KB); without a cap, an oversized or
// hostile body (a plain-HTTP LAN endpoint is spoofable via mDNS) grows the elastic
// JsonDocument until internal heap hits zero MID-PARSE -- and with async_tcp, MQTT and
// loop-task Strings allocating from the same heap, that is a crash-reboot, not a clean
// NoMemory. Applied to Content-Length up front and to both socket streams cumulatively.
constexpr size_t MAX_JSON_BODY = 256 * 1024;
// ArduinoJson reads a stream a byte at a time. Doing that straight off the WiFi
// socket floods lwIP with tiny reads and, on the single-core C3, starves the
// async_tcp service task until its watchdog reboots the board (seen mid-parse of the
// OpenSky feed). This wrapper serves ArduinoJson from a small RAM buffer that is
// refilled from the socket in bulk, and yields while waiting for bytes so async_tcp
// keeps running. Memory stays low (no full-body String), unlike getString().
class BufferedSocketStream : public Stream {
public:
    BufferedSocketStream(Stream& src, size_t contentLength, uint32_t timeoutMs)
        : src_(src), remaining_(contentLength), timeoutMs_(timeoutMs) {}

    int available() override { return (int)(len_ - pos_) + src_.available(); }
    int read() override      { return Fill() ? buf_[pos_++] : -1; }
    int peek() override      { return Fill() ? buf_[pos_]   : -1; }
    size_t write(uint8_t) override { return 0; } // read-only

    // Diagnostics for truncated-feed debugging.
    size_t totalRead() const { return totalRead_; }
    bool   timedOut() const  { return timedOut_; }

private:
    // Guarantee at least one buffered byte; false at end-of-body or on timeout.
    bool Fill() {
        if (pos_ < len_) return true;
        if (remaining_ == 0) return false; // never read past Content-Length (no keep-alive stall)
        if (totalRead_ >= MAX_JSON_BODY) { truncated_ = true; return false; } // body over the ceiling: stop feeding the parser

        pos_ = len_ = 0;
        const uint32_t start = millis();
        while (src_.available() == 0) {
            if (millis() - start >= timeoutMs_) { timedOut_ = true; return false; }
            delay(1); // vTaskDelay: lets the higher-priority async_tcp task run + feed its WDT
        }

        size_t want = src_.available();
        if (want > remaining_)     want = remaining_;
        if (want > sizeof(buf_))   want = sizeof(buf_);
        len_ = src_.readBytes(buf_, want); // bulk read of already-available bytes; doesn't block
        remaining_ -= len_;
        totalRead_ += len_;

        // Yield periodically even when data is flowing steadily. The fetch/enrich tasks are pinned to
        // core 0, which the Task-WDT watches (idle_core_mask in setup()). A large feed (e.g. a wide
        // OpenSky box) read+parsed without ever blocking would keep core 0's idle task off the CPU and
        // trip the 10 s watchdog -> spontaneous reboot. A brief vTaskDelay every ~8 KB lets idle run
        // and feeds the WDT; the cost is negligible (a few ms across a whole response).
        if ((++fillCount_ & 0x0F) == 0) delay(1);
        return len_ > 0;
    }

    Stream& src_;
    size_t remaining_;
    uint32_t timeoutMs_;
    uint8_t buf_[512];
    size_t pos_ = 0, len_ = 0;
    uint32_t fillCount_ = 0;
    size_t totalRead_ = 0;
    bool timedOut_ = false;
    bool truncated_ = false;
};

// Same idea as BufferedSocketStream, but for a Transfer-Encoding: chunked body (no
// Content-Length). It de-chunks straight off the socket, yields while waiting for bytes,
// and -- the part that matters -- STOPS at the 0-length terminator chunk. Raw
// HTTPClient::getString() on a chunked *keep-alive* response (the valar-eam-feed EAM
// backend sends exactly that) reads the body in a loop that never yields to core 0's idle
// task and then blocks in mbedtls_ssl_read past the body on the still-open keep-alive
// socket -- starving the 10 s Task-WDT into a permanent boot loop (the eam_fetch reboot).
// Chunk-size lines are read byte-wise (a few bytes per chunk); chunk DATA is bulk-read so
// lwIP isn't flooded with tiny reads. Heap stays low (no full-body String), like the
// Content-Length path.
class ChunkedSocketStream : public Stream {
public:
    ChunkedSocketStream(Stream& src, uint32_t timeoutMs)
        : src_(src), timeoutMs_(timeoutMs) {}

    int available() override { return (int)(len_ - pos_) + (done_ ? 0 : src_.available()); }
    int read() override      { return Fill() ? buf_[pos_++] : -1; }
    int peek() override      { return Fill() ? buf_[pos_]   : -1; }
    size_t write(uint8_t) override { return 0; } // read-only

    size_t totalRead() const { return totalRead_; }
    bool   timedOut() const  { return timedOut_; }

private:
    // Block (yielding) for one raw byte; -1 on stall timeout. The delay(1) yields keep
    // core 0's idle task fed, so even a slow transfer stays WDT-safe.
    int RawByte() {
        const uint32_t start = millis();
        while (src_.available() == 0) {
            if (millis() - start >= timeoutMs_) { timedOut_ = true; return -1; }
            delay(1);
        }
        return src_.read();
    }

    // Read a chunk-size line ("<hex>[;ext]\r\n") and return the size, or -1 on stall.
    long ReadChunkHeader() {
        char line[16];
        size_t n = 0;
        int c;
        while ((c = RawByte()) >= 0 && c != '\n') {
            if (c != '\r' && n < sizeof(line) - 1) line[n++] = (char)c;
        }
        if (c < 0) return -1;
        line[n] = '\0';
        return strtol(line, nullptr, 16); // stops at any ';' extension; 0 == terminator
    }

    // Guarantee at least one de-chunked byte in buf_; false at end-of-body or on timeout.
    bool Fill() {
        if (pos_ < len_) return true;
        if (done_) return false;
        if (totalRead_ >= MAX_JSON_BODY) { done_ = true; return false; } // body over the ceiling: stop feeding the parser
        pos_ = len_ = 0;

        // Advance to a chunk that still has data, consuming the CRLF that trails the
        // previous chunk's data before reading the next size line.
        while (chunkRemaining_ == 0) {
            if (sawData_) { RawByte(); RawByte(); } // the \r\n after the prior chunk's data
            const long sz = ReadChunkHeader();
            if (sz < 0)  { timedOut_ = true; done_ = true; return false; }
            if (sz == 0) { done_ = true; return false; } // terminator: never read into the idle keep-alive socket
            chunkRemaining_ = (size_t)sz;
            sawData_ = true;
        }

        // Bulk-read this chunk's data, capped at chunkRemaining_ so we never cross into
        // the next chunk's size line.
        const uint32_t start = millis();
        while (src_.available() == 0) {
            if (millis() - start >= timeoutMs_) { timedOut_ = true; done_ = true; return false; }
            delay(1);
        }
        size_t want = src_.available();
        if (want > chunkRemaining_) want = chunkRemaining_;
        if (want > sizeof(buf_))    want = sizeof(buf_);
        len_ = src_.readBytes(buf_, want);
        chunkRemaining_ -= len_;
        totalRead_ += len_;

        if ((++fillCount_ & 0x0F) == 0) delay(1); // yield even on a steady stream (see BufferedSocketStream)
        return len_ > 0;
    }

    Stream& src_;
    uint32_t timeoutMs_;
    uint8_t buf_[512];
    size_t pos_ = 0, len_ = 0;
    size_t chunkRemaining_ = 0;
    bool sawData_ = false;   // have we read a data chunk yet (so a CRLF precedes the next header)?
    bool done_ = false;
    bool timedOut_ = false;
    uint32_t fillCount_ = 0;
    size_t totalRead_ = 0;
};
} // namespace

String HttpRequestManager::BuildQueryString(const std::vector<std::pair<String, String>>& params) const
{
    if (params.empty())
        return "";

    String queryStream = "?";

    bool first = true;
    for (const auto& [key, value] : params)
    {
        if (!first)
            queryStream += "&";

        queryStream += key + "=" + value;

        first = false;
    }

    return queryStream;
}

String HttpRequestManager::ReadBodyYielding(HTTPClient& http)
{
    // A thumbnail is ~10-30 KB; the cap guards heap and bounds the read time. STALL_MS is the
    // no-progress timeout and stays well under the 10 s Task-WDT (the loop yields, so even a
    // stalled host is WDT-safe). A truncated/short read just makes drawJpg fail -> no photo,
    // never a crash.
    constexpr size_t   MAX_BODY = 64 * 1024;
    constexpr uint32_t STALL_MS = 8000;

    String out;
    auto* stream = http.getStreamPtr(); // WiFiClient* (alias varies across core versions)
    if (stream == nullptr) return out;

    const int contentLen = http.getSize(); // Content-Length, or -1 when chunked/unknown
    if (contentLen > 0) out.reserve((size_t)(contentLen < (int)MAX_BODY ? contentLen : MAX_BODY));

    uint8_t buf[512];
    size_t total = 0;
    uint32_t lastProgress = millis();
    uint32_t reads = 0;

    while (http.connected() || stream->available() > 0) {
        const size_t avail = (size_t)stream->available();
        if (avail == 0) {
            if (millis() - lastProgress > STALL_MS) break; // stalled host: bail (WDT-safe)
            delay(2);                                       // yield to idle + async_tcp
            continue;
        }
        size_t want = avail < sizeof(buf) ? avail : sizeof(buf);
        if (total + want > MAX_BODY) want = MAX_BODY - total;
        const int got = stream->readBytes(buf, want);
        if (got > 0) {
            out.concat(reinterpret_cast<const char*>(buf), (unsigned int)got);
            total += got;
            lastProgress = millis();
        }
        // Let core 0's priority-0 idle task run every few reads so a steady stream can't
        // monopolise the core and trip the Task-WDT (this is what getString() fails to do).
        if ((++reads & 0x07) == 0) delay(1);
        if (total >= MAX_BODY) break;
        if (contentLen > 0 && total >= (size_t)contentLen) break;
    }
    return out;
}

HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpResult result{ false, 0, "", "" };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    xSemaphoreTake(mutex, portMAX_DELAY); // exclusive access to the shared HTTPClient
    HTTPClient& http = ClientFor(fullUrl); // scheme-pinned instance (see header)

    // One request, with a single retry on a STALE KEEP-ALIVE socket. The TLS client
    // holds its connection open for reuse (see the header note), but Cloudflare closes
    // an idle keep-alive after only a few seconds -- so a socket we reuse can be
    // half-open, and the GET then fails at the transport layer (read timeout / connection
    // refused) rather than returning a status. When that happens on a socket we REUSED
    // (not one we just handshaked), the connection is the suspect: drop it and retry once
    // on a fresh handshake. A failure on a freshly opened socket is a real network error,
    // so don't loop. This is what was intermittently blanking cloud enrichment cards.
    //
    // setFollowRedirects: adsbdb's photo thumbnails are served from airport-data.com
    // behind a redirect; without it the GET stops at the redirect and hands back the empty
    // 3xx body -- a blank photo from a "successful" fetch. Only GETs use this client, so
    // re-issuing on a redirect is safe. setConnectTimeout/setTimeout bound how long one
    // request can block: these run synchronously and a half-open host that stalls for many
    // seconds would starve the watchdog-fed async_tcp service task into a reboot.
    int responseCode = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        http.begin(fullUrl);
        result.reusedConnection = http.connected(); // pre-existing socket -> no TLS handshake this request
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        http.setConnectTimeout(5000); // TCP connect
        http.setTimeout(5000);        // per-read stream timeout
        for (const auto& header : headers)
            http.addHeader(header.first, header.second);
        responseCode = http.GET();
        if (responseCode > 0 || !result.reusedConnection)
            break;
        Serial.printf("[GET] stale keep-alive (%d) on reused socket; retrying fresh\n", responseCode);
        http.end(); // close the dead socket so the retry's begin() handshakes anew
    }
    result.statusCode = responseCode;

    if (responseCode > 0) {
        result.success = true;
        // Yielding, size-capped body read -- getString() here starved core 0's idle task on a
        // slow photo download and tripped the Task-WDT into a reboot (see ReadBodyYielding).
        result.response = ReadBodyYielding(http);
    }
    else {
        result.success = false;
        result.errorMessage = http.errorToString(responseCode);
        Serial.print("[GET] HTTP Error (");
        Serial.print(responseCode);
        Serial.print("): ");
        Serial.println(result.errorMessage);
    }

    http.end();
    xSemaphoreGive(mutex);
    return result;
}

HttpResult HttpRequestManager::GetJsonImpl(const String& url, JsonDocument& doc, const JsonDocument* filter,
                                           const std::vector<std::pair<String, String>>& params,
                                           const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    const String fullUrl = url + BuildQueryString(params);

    xSemaphoreTake(mutex, portMAX_DELAY); // exclusive access to the shared HTTPClient
    HTTPClient& http = ClientFor(fullUrl); // scheme-pinned instance (see header)

    // Single retry on a stale keep-alive socket (see Get() for the full rationale): a
    // reused connection Cloudflare has already closed fails at the transport layer, so on
    // a failure that hit a REUSED socket we drop it and retry once on a fresh handshake.
    // This is the fix for the intermittent cloud-enrich blanks (read timeout / connection
    // refused on the shared TLS client). collectHeaders keeps the Transfer-Encoding so the
    // body-read below can tell a chunked reply from a close-delimited one.
    static const char* kCollectHeaders[] = { "Transfer-Encoding" };
    int responseCode = 0;
    for (int attempt = 0; attempt < 2; ++attempt) {
        http.begin(fullUrl);
        result.reusedConnection = http.connected(); // pre-existing socket -> no TLS handshake this request
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        http.setConnectTimeout(5000);
        http.setTimeout(5000);
        http.collectHeaders(kCollectHeaders, 1);
        for (const auto& header : headers)
            http.addHeader(header.first, header.second);
        responseCode = http.GET();
        if (responseCode > 0 || !result.reusedConnection)
            break;
        Serial.printf("[GET] stale keep-alive (%d) on reused socket; retrying fresh\n", responseCode);
        http.end(); // close the dead socket so the retry's begin() handshakes anew
    }
    result.statusCode = responseCode;

    if (responseCode > 0) {
        DeserializationError err;
        const int bodyLen = http.getSize();
        if (bodyLen >= 0 && (size_t)bodyLen > MAX_JSON_BODY) {
            // Refuse up front: parsing it would exhaust the heap mid-stream.
            Serial.printf("[GET] body too large (%d > %u); refusing\n", bodyLen, (unsigned)MAX_JSON_BODY);
            result.errorMessage = "body too large";
            http.end();
            xSemaphoreGive(mutex);
            return result;
        }
        if (bodyLen >= 0) {
            // Content-Length known (not chunked): parse straight off the socket so the
            // body is never copied into a String. Wrap it so reads are bulk + yielding
            // (raw byte-at-a-time reads here starve async_tcp into a watchdog reboot).
            // 15 s stall tolerance (not 5 s): a wide OpenSky box is a large body, and on the
            // S3 the 480x480 panel shares the PSRAM bus, slowing the socket drain enough that
            // OpenSky pauses mid-transfer. The wait loop yields, so a long timeout is WDT-safe.
            BufferedSocketStream body(http.getStream(), (size_t)bodyLen, 15000);
            err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                         : deserializeJson(doc, body);
            if (err)
                Serial.printf("[GET] diag: Content-Length path, CL=%d read=%u timedOut=%d err=%s\n",
                              bodyLen, (unsigned)body.totalRead(), (int)body.timedOut(), err.c_str());
        } else if (http.header("Transfer-Encoding").indexOf("chunked") >= 0) {
            // Chunked, no Content-Length (the valar-eam-feed EAM endpoints). De-chunk off
            // the socket with the same yielding/stall-bounded discipline as the path above,
            // stopping at the terminator chunk. The raw http.getString() that used to be here
            // never yielded and then blocked past the body on the keep-alive socket, starving
            // the 10 s Task-WDT into a boot loop (eam_fetch). See ChunkedSocketStream.
            ChunkedSocketStream body(http.getStream(), 15000);
            err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                         : deserializeJson(doc, body);
            if (err)
                Serial.printf("[GET] diag: chunked path, read=%u timedOut=%d err=%s\n",
                              (unsigned)body.totalRead(), (int)body.timedOut(), err.c_str());
        } else {
            // Unknown length and not chunked (close-delimited): the server signals end-of-body
            // by closing the connection, so getString() reads to a clean EOF (no keep-alive
            // over-read to block on). Rare for these JSON endpoints.
            const String s = http.getString();
            if (s.length() > MAX_JSON_BODY) {
                Serial.printf("[GET] close-delimited body too large (%u); refusing\n", (unsigned)s.length());
                result.errorMessage = "body too large";
                http.end();
                xSemaphoreGive(mutex);
                return result;
            }
            err = filter ? deserializeJson(doc, s, DeserializationOption::Filter(*filter))
                         : deserializeJson(doc, s);
            if (err)
                Serial.printf("[GET] diag: close-delimited path, got=%u bytes err=%s\n",
                              (unsigned)s.length(), err.c_str());
        }

        if (err) {
            result.errorMessage = err.c_str();
            Serial.printf("[GET] JSON parse failed (%d): %s\n", responseCode, err.c_str());
        } else {
            result.success = true;
        }
    } else {
        result.errorMessage = http.errorToString(responseCode);
        Serial.printf("[GET] HTTP Error (%d): %s\n", responseCode, result.errorMessage.c_str());
    }

    http.end();
    xSemaphoreGive(mutex);
    return result;
}

HttpResult HttpRequestManager::GetJson(const String& url, JsonDocument& doc, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers)
{
    return GetJsonImpl(url, doc, nullptr, params, headers);
}

HttpResult HttpRequestManager::GetJson(const String& url, JsonDocument& doc, const JsonDocument& filter, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers)
{
    return GetJsonImpl(url, doc, &filter, params, headers);
}

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    xSemaphoreTake(mutex, portMAX_DELAY); // exclusive access to the shared HTTPClient
    HTTPClient& http = ClientFor(url); // scheme-pinned instance (see header)
    http.begin(url);

    // Bound the request the same way Get() does: a POST that runs on the loop task (the
    // ntfy alerts) must not be able to hang the device long enough to trip the async_tcp
    // watchdog. No forced redirect-follow here -- a redirect must not silently re-POST.
    http.setConnectTimeout(5000);
    http.setTimeout(5000);

    // add headers to request
    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    // send request and handle response
    int responseCode = http.POST(body);
    result.statusCode = responseCode;

    if (responseCode > 0) {
        result.success = true;
        result.response = http.getString();
    }
    else {
        result.success = false;
        result.errorMessage = http.errorToString(responseCode);
        Serial.print("[POST] HTTP Error (");
        Serial.print(responseCode);
        Serial.print("): ");
        Serial.println(result.errorMessage);
    }

    http.end();
    xSemaphoreGive(mutex);
    return result;
}
