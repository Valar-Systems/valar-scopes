#pragma once

// "ON WATCH": how many Missileers polled the feed in the last 10 minutes, shown under the
// Zulu clock. Pure (no Arduino, no ArduinoJson) so test/host/test_on_watch.cpp grades the
// shipping code as-is.
//
// Ruling (2026-09-25): fetched with the feed poll; shows the LAST value if a fetch fails, "--"
// only if it never had one; the device itself is in the count (a lone unit shows 1).
//
// WHERE THE "ITSELF" COMES FROM -- a premise about the server, named here and in the server:
// valar-eam-feed counts distinct X-Missileer-Device ids seen on GET /eam/latest in the last
// 600 s, and records the calling device BEFORE it answers, so `on_watch` already includes
// this unit. Nothing is added locally: a local +1 would double-count every device whose
// header the server accepted. If the header is missing or malformed the server still answers
// but counts only the others -- which is why EamFeedClient sends it on every /eam/latest.
//
// WHAT IS NOT A READING. A response without the field (an older server), or with a value
// that is not a non-negative integer, is not "zero on watch": it is no answer, and the last
// good value stays. Nothing is clamped or truncated: a number too wide to draw would be a
// display problem, never a reason to show a different number.
namespace onwatch {

constexpr int kNever = -1;   // never had a value: the screen shows "--"

struct Count {
    long value = kNever;

    // One /eam/latest response that parsed. `present` = the body carried an integer
    // `on_watch`; `v` its value. Returns true if the shown value was (re)set.
    bool Apply(bool present, long v)
    {
        if (!present || v < 0) return false;   // no answer: keep what we had
        value = v;
        return true;
    }
    // A fetch that failed (network, non-2xx, wrong shape) never reaches Apply: EamFeedClient
    // only applies a Latest result that parsed, so a failure leaves `value` exactly as it was.

    bool Known() const { return value != kNever; }
};

// The digits drawn: "--" until the first good value, else the decimal count. `buf` must hold
// at least 12 bytes; returns buf.
inline const char* Text(const Count& c, char* buf, unsigned n)
{
    if (n < 3) { if (n) buf[0] = '\0'; return buf; }
    if (!c.Known()) { buf[0] = '-'; buf[1] = '-'; buf[2] = '\0'; return buf; }
    // Plain decimal, no printf: this header stays free of <cstdio> for the host rig.
    char tmp[24];
    int i = 0;
    unsigned long v = (unsigned long)c.value;
    do { tmp[i++] = (char)('0' + (v % 10)); v /= 10; } while (v && i < (int)sizeof(tmp));
    // Too small a buffer draws NOTHING rather than "--": "--" means "never had one", and
    // saying that about a value we hold would be a plausible-looking lie.
    if ((unsigned)i + 1 > n) { buf[0] = '\0'; return buf; }
    for (int k = 0; k < i; ++k) buf[k] = tmp[i - 1 - k];
    buf[i] = '\0';
    return buf;
}

} // namespace onwatch
