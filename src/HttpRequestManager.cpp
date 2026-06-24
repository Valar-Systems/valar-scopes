#include "HttpRequestManager.h"

namespace {
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

private:
    // Guarantee at least one buffered byte; false at end-of-body or on timeout.
    bool Fill() {
        if (pos_ < len_) return true;
        if (remaining_ == 0) return false; // never read past Content-Length (no keep-alive stall)

        pos_ = len_ = 0;
        const uint32_t start = millis();
        while (src_.available() == 0) {
            if (millis() - start >= timeoutMs_) return false;
            delay(1); // vTaskDelay: lets the higher-priority async_tcp task run + feed its WDT
        }

        size_t want = src_.available();
        if (want > remaining_)     want = remaining_;
        if (want > sizeof(buf_))   want = sizeof(buf_);
        len_ = src_.readBytes(buf_, want); // bulk read of already-available bytes; doesn't block
        remaining_ -= len_;
        return len_ > 0;
    }

    Stream& src_;
    size_t remaining_;
    uint32_t timeoutMs_;
    uint8_t buf_[512];
    size_t pos_ = 0, len_ = 0;
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

HttpResult HttpRequestManager::Get(const String& url, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers) {
    HttpResult result{ false, 0, "", "" };

    const String queryParams = BuildQueryString(params);
    const String fullUrl = url + queryParams;

    xSemaphoreTake(mutex, portMAX_DELAY); // exclusive access to the shared HTTPClient
    http.begin(fullUrl);

    // Follow 3xx redirects. adsbdb's photo thumbnails are served from airport-data.com
    // behind a redirect; without this the GET stops at the redirect and hands back the
    // empty 3xx body -- which, because the status code is still > 0, was treated as a
    // successful response. That is exactly the "[photo] ... bytes=0 decoded=0" failure:
    // a blank photo from a "successful" fetch. Only GETs use this client, so always
    // re-issuing on a redirect is safe.
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);

    // Bound how long one request can block. These run synchronously on the loop task,
    // and a slow or half-open host that stalls for many seconds monopolises the single
    // core long enough to starve the watchdog-fed async_tcp service task into a reboot.
    http.setConnectTimeout(5000); // TCP connect
    http.setTimeout(5000);        // per-read stream timeout

    // add headers to request
    for (const auto& header : headers) {
        http.addHeader(header.first, header.second);
    }

    // send request and handle response
    int responseCode = http.GET();
    result.statusCode = responseCode;

    if (responseCode > 0) {
        result.success = true;
        result.response = http.getString();
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

HttpResult HttpRequestManager::GetJson(const String& url, JsonDocument& doc, const std::vector<std::pair<String, String>>& params, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    const String fullUrl = url + BuildQueryString(params);

    xSemaphoreTake(mutex, portMAX_DELAY); // exclusive access to the shared HTTPClient
    http.begin(fullUrl);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setConnectTimeout(5000);
    http.setTimeout(5000);

    for (const auto& header : headers)
        http.addHeader(header.first, header.second);

    const int responseCode = http.GET();
    result.statusCode = responseCode;

    if (responseCode > 0) {
        DeserializationError err;
        const int bodyLen = http.getSize();
        if (bodyLen >= 0) {
            // Content-Length known (not chunked): parse straight off the socket so the
            // body is never copied into a String. Wrap it so reads are bulk + yielding
            // (raw byte-at-a-time reads here starve async_tcp into a watchdog reboot).
            BufferedSocketStream body(http.getStream(), (size_t)bodyLen, 5000);
            err = deserializeJson(doc, body);
        } else {
            // chunked / unknown length: getStream() would include chunk markers that
            // ArduinoJson can't parse, so use the de-chunked String (higher heap peak,
            // but correct). Rare for these JSON endpoints.
            err = deserializeJson(doc, http.getString());
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

HttpResult HttpRequestManager::Post(const String& url, const String& body, const std::vector<std::pair<String, String>>& headers)
{
    HttpResult result{ false, 0, "", "" };

    xSemaphoreTake(mutex, portMAX_DELAY); // exclusive access to the shared HTTPClient
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
