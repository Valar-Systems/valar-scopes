#pragma once

/* ============================================================================
 * Stands in for src/HttpRequestManager.h: the network ends here, and so does
 * the instrument's reach.
 *
 * GetJson() deserializes the body the test has staged into the caller's
 * JsonDocument -- the same `deserializeJson(doc, stream)` call the real one makes,
 * fed through a reader that hands ArduinoJson the body 512 bytes at a time, as
 * BufferedSocketStream/ChunkedSocketStream do (their buf_ is 512 B). It does NOT
 * run those socket streams, HTTPClient, TLS or lwIP: they move bytes and never
 * look inside the JSON, so a long `text` is to them a longer body -- 6.5 KB for
 * the 500-character fixture against MAX_JSON_BODY = 256 KB. That premise is
 * stated, not tested; the bench step runs the real path. Implemented in
 * test_eam_long_message.cpp.
 * ==========================================================================*/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <utility>
#include <vector>

struct HttpResult {
    bool success = false;
    int statusCode = 0;
    String response;
    String errorMessage;
    bool reusedConnection = false;
    String cacheState;
    String upstream;
    String cacheControl;
    size_t bodyBytes = 0;
    unsigned long parseMs = 0;
    unsigned long requestMs = 0;
};

class HttpRequestManager {
public:
    HttpResult GetJson(const String& url, JsonDocument& doc,
                       const std::vector<std::pair<String, String>>& params = {},
                       const std::vector<std::pair<String, String>>& headers = {});
};
