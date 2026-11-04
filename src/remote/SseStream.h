#pragma once

/* ===========================================================================
 * AN ESPHOME EVENT STREAM, BYTES TO EVENTS: the pure half (docs/remote-edition.md section 5).
 *
 * ESPHome 2026.6.5's ESP-IDF web server (components/web_server_idf/web_server_idf.cpp) sends /events as HTTP
 * CHUNKED transfer encoding, one chunk per event, the size line "%08x", and inside it the SSE fields
 * ("retry: N", "id: N", "event: <type>", "data: <json>", CRLF each) and a blank line. Two incremental stages,
 * fed whatever the socket has, so a stream that lives for hours never blocks the task reading it:
 *
 *   Dechunk  chunked bytes -> body bytes. A malformed size line or a missing chunk CRLF BREAKS the stream:
 *            the link drops it and reconnects. It never skips ahead looking for something plausible --
 *            resynchronising on a guess is how a half event becomes a whole one.
 *   Framer   body bytes -> (type, data) events. A line over LINE_MAX drops its WHOLE event (counted). It is
 *            never truncated: half a JSON object is a different object.
 *
 * The repo's other chunked decoder, ChunkedSocketStream (src/HttpRequestManager.cpp), is a blocking Stream
 * adapter for one-shot fetches. What it establishes -- hex size with an optional ";extension", CRLF after
 * each chunk's data, size 0 ends the body -- this one establishes too; on garbage this one breaks instead.
 * C++11, like every host-tested header.
 * ======================================================================== */

#include <cstddef>
#include <cstdint>
#include <string>

namespace sse {

constexpr size_t LINE_MAX = 3072;   // ESPHome's largest state event here is ~400 B; a log line can be longer
constexpr int    SIZE_DIGITS_MAX = 8;   // the IDF server writes "%08x"

class Dechunk {
public:
    bool Broken() const { return st == St::Broken; }
    bool Done() const { return st == St::Done; }

    /// Feed raw socket bytes; every decoded body byte goes to out(char). Stops at Broken or Done.
    template <typename Out>
    void Feed(const char* p, size_t n, Out out)
    {
        for (size_t i = 0; i < n; ++i) {
            const char c = p[i];
            switch (st) {
            case St::Size: {
                const int v = Hex(c);
                if (v >= 0 && digits < SIZE_DIGITS_MAX) { size = size * 16 + (uint32_t)v; ++digits; break; }
                if (c == ';' && digits > 0) { st = St::Ext; break; }
                if (c == '\r' && digits > 0) { st = St::SizeLF; break; }
                st = St::Broken;   // not hex, too long, or empty: never resync
                return;
            }
            case St::Ext:
                if (c == '\r') st = St::SizeLF;
                break;
            case St::SizeLF:
                if (c != '\n') { st = St::Broken; return; }
                if (size == 0) { st = St::Done; return; }
                remaining = size;
                st = St::Data;
                break;
            case St::Data:
                out(c);
                if (--remaining == 0) st = St::DataCR;
                break;
            case St::DataCR:
                if (c != '\r') { st = St::Broken; return; }
                st = St::DataLF;
                break;
            case St::DataLF:
                if (c != '\n') { st = St::Broken; return; }
                st = St::Size;
                size = 0;
                digits = 0;
                break;
            default:
                return;   // Broken or Done: nothing more is delivered
            }
        }
    }

private:
    enum class St : uint8_t { Size, Ext, SizeLF, Data, DataCR, DataLF, Done, Broken };
    St       st = St::Size;
    uint32_t size = 0;
    int      digits = 0;
    uint32_t remaining = 0;

    static int Hex(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
};

class Framer {
public:
    uint32_t Dropped() const { return dropped; }

    /// Feed body bytes; each complete event goes to onEvent(type, data). An event with no data is not
    /// dispatched (the SSE rule), and neither is one that had an oversize line.
    template <typename OnEvent>
    void Feed(const char* p, size_t n, OnEvent onEvent)
    {
        for (size_t i = 0; i < n; ++i) {
            const char c = p[i];
            if (c == '\r') continue;   // CRLF and LF frame alike; ESPHome's JSON never holds a bare CR
            if (c != '\n') {
                if (line.size() < LINE_MAX) line.push_back(c);
                else overflow = true;
                continue;
            }
            if (overflow) {
                bad = true;   // this line was cut off: the event it belongs to is dropped whole
            } else if (line.empty()) {
                if (bad) ++dropped;
                else if (hasData) onEvent(type, data);
                type.clear();
                data.clear();
                hasData = false;
                bad = false;
            } else {
                Field();
            }
            line.clear();
            overflow = false;
        }
    }

private:
    std::string line, type, data;
    bool     hasData = false;
    bool     overflow = false;   // the current line passed LINE_MAX
    bool     bad = false;        // the current event had such a line
    uint32_t dropped = 0;

    void Field()
    {
        if (line[0] == ':') return;   // a comment
        const size_t colon = line.find(':');
        const std::string name = line.substr(0, colon);
        std::string value = colon == std::string::npos ? std::string() : line.substr(colon + 1);
        if (!value.empty() && value[0] == ' ') value.erase(0, 1);
        if (name == "event") {
            type = value;
        } else if (name == "data") {
            if (hasData) data.push_back('\n');
            data += value;
            hasData = true;
        }
        // "id" and "retry" are the server's bookkeeping; nothing here needs them.
    }
};

} // namespace sse
