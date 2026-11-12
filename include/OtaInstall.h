#pragma once
// THE INSTALL DECISION: read the image's head from the stream, decide, and only
// then touch flash (docs/weatherscope-stage3.md A1.2, A12.104).
//
// The design this replaces wrote the whole image, let Update.end() switch the
// boot partition, read the identity back and switched it again on a mismatch --
// and a power loss between the two switches booted the foreign image. Here a
// refusal makes NO flash call at all: the inactive slot is never erased, the boot
// partition never touched, and the connection is closed after 352 bytes.
//
// Pure and header-only (C++11). `Source` and `Flash` are template parameters so the
// host tests drive this exact code with a fake flash that keeps a boot HISTORY: the
// withdrawn design reverts the boot partition, so a final-state check would pass it
// (A1.3), and the history is what tells the two apart.
//
//   Source: size_t read(uint8_t* buf, size_t n)       up to n bytes; 0 at the end or a timeout
//   Flash:  bool begin(size_t total)                  the Updater's begin (nothing written yet)
//           size_t write(const uint8_t* buf, size_t n)
//           size_t writeRest(Source& src, size_t n)   the rest of the stream, n bytes
//           bool end()                                verify + switch the boot partition
//           int error()                               why the last call failed

#include <stdio.h>

#include "OtaIdentity.h"

namespace ota {

enum class Outcome : uint8_t {
    Accepted,      // internal: the decision said yes; the flash comes next
    Installed,     // written and switched; the caller restarts
    NotAnImage,    // the first byte is not 0xE9: today's fail--106, kept (A12.104)
    Truncated,     // fewer than kHeadBytes arrived
    NoRecord,      // no identity record: every image built before the check
    BadRecord,     // a record this version does not know
    Foreign,       // a record, but not what was asked for
    FlashFailed,   // the Updater refused: fail-<its error>
};

struct Result {
    Outcome outcome;
    int error;              // FlashFailed: the Updater's error; NotAnImage: -106
    bool imageKnown;        // the head held a readable record
    Identity image;
};

inline bool IsRefusal(Outcome o)
{
    return o == Outcome::Truncated || o == Outcome::NoRecord || o == Outcome::BadRecord || o == Outcome::Foreign;
}

// The OTA-mem record's `res` for an outcome (A12.104's mapping). A refusal is never
// written as a failure, and a failure never as a refusal.
inline void Token(const Result& r, char* buf, size_t n)
{
    switch (r.outcome) {
        case Outcome::Truncated:   snprintf(buf, n, "refused-truncated"); break;
        case Outcome::NoRecord:    snprintf(buf, n, "refused-no-record"); break;
        case Outcome::BadRecord:   snprintf(buf, n, "refused-bad-record"); break;
        case Outcome::Foreign:     snprintf(buf, n, "refused-foreign"); break;
        case Outcome::NotAnImage:  snprintf(buf, n, "fail--106"); break;
        case Outcome::FlashFailed: snprintf(buf, n, "fail-%d", r.error); break;
        case Outcome::Accepted:
        case Outcome::Installed:   snprintf(buf, n, "incomplete"); break;   // finalised as ok by the new image
    }
}

// THE PATIENCE (docs/ota-head-patience.md). On the TLS download a read that returns 0 means EITHER "nothing has
// arrived yet" OR "the stream ended": NetworkClientSecure::read returns -1 with nothing buffered, and
// NetworkClient::readBytes turns that into an immediate 0 (framework-arduinoespressif32 3.3.9). So an empty read is
// waited out, never taken as the end: kEmptyReads empty reads IN A ROW, kEmptyWaitMs apart, end it, and any bytes
// restart the count. writeRest always had this patience; the head read, the first read of the same stream, did not,
// and refused a late body as truncated on 13 of 19 bench checks (docs/v19-cut.md, window 3).
constexpr unsigned kEmptyReads = 300;
constexpr unsigned kEmptyWaitMs = 100;

// One read that brings bytes, waiting out empty ones: that read's count, or 0 after kEmptyReads empty reads in a row.
// The last empty read ends it without a wait, as writeRest's own loop always did. The source supplies waitEmpty():
// delay(kEmptyWaitMs) on the device, a counter in the host tests. Both reads of the stream go through here, so the
// head and the body can never again disagree about what an empty read means.
template <class Source>
size_t ReadSome(Source& src, uint8_t* buf, size_t n)
{
    for (unsigned empty = 0;;) {
        const size_t got = src.read(buf, n);
        if (got != 0) return got;
        if (++empty >= kEmptyReads) return 0;
        src.waitEmpty();
    }
}

template <class Source>
size_t ReadHead(Source& src, uint8_t* head, size_t want)
{
    size_t got = 0;
    while (got < want) {
        const size_t n = ReadSome(src, head + got, want - got);
        if (n == 0) break;
        got += n;
    }
    return got;
}

// THE DECISION. No flash call is made above it or inside it.
inline Result Decide(const uint8_t* head, size_t got, const Identity& requested)
{
    Result res;
    memset(&res, 0, sizeof res);
    if (got >= 1 && head[0] != kImageMagic) { res.outcome = Outcome::NotAnImage; res.error = -106; return res; }
    if (got < kHeadBytes) { res.outcome = Outcome::Truncated; return res; }
    const Parsed p = Parse(head, got, res.image);
    if (p == Parsed::NoRecord) { res.outcome = Outcome::NoRecord; return res; }
    if (p == Parsed::BadRecord) { res.outcome = Outcome::BadRecord; return res; }
    res.imageKnown = true;
    res.outcome = Accept(requested, res.image) ? Outcome::Accepted : Outcome::Foreign;
    return res;
}

template <class Flash>
Result Failed(Flash& flash, Result res)
{
    res.outcome = Outcome::FlashFailed;
    res.error = flash.error();
    return res;
}

// begin + the head: the first flash calls of an accepted image.
template <class Flash>
bool StartImage(Flash& flash, size_t total, const uint8_t* head, size_t got)
{
    return flash.begin(total) && flash.write(head, got) == got;
}

// the rest of the stream, then end() -- which switches the boot partition.
template <class Source, class Flash>
Result FinishImage(Source& src, Flash& flash, size_t total, size_t got, Result res)
{
    if (flash.writeRest(src, total - got) != total - got || !flash.end()) return Failed(flash, res);
    res.outcome = Outcome::Installed;
    return res;
}

// `total` is the server's Content-Length. `requested` is what the device asked for:
// Request(its own board, the scope in the URL it fetched).
template <class Source, class Flash>
Result Install(Source& src, Flash& flash, size_t total, const Identity& requested)
{
    uint8_t head[kHeadBytes];
    const size_t got = ReadHead(src, head, total < kHeadBytes ? total : kHeadBytes);
    Result res = Decide(head, got, requested);
    if (res.outcome != Outcome::Accepted) return res;
    if (!StartImage(flash, total, head, got)) return Failed(flash, res);
    return FinishImage(src, flash, total, got, res);
}

}  // namespace ota
