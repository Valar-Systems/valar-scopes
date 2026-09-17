#pragma once

/* ============================================================================
 * A MINIMAL Arduino String, so the host rig can grade include/CoordParse.h.
 *
 * WHY A SHIM RATHER THAN AN EXTRACTION. CoordParse.h is the one parser both the
 * device and a curl one-liner go through, and it traffics in Arduino `String`.
 * The alternatives were to transcribe its logic into a pure header -- which
 * would create a second implementation of the thing whose whole value is being
 * the only one -- or to give the host build just enough String to compile the
 * real file. This is the second. The test therefore grades the SHIPPING parser,
 * character for character, not a copy of it that can drift.
 *
 * WHAT THIS CANNOT CATCH, stated because a transcription that does not say so
 * is the weaker form pretending to be the stronger one:
 *
 *   - a difference between THIS String and Arduino's. If Arduino's substring()
 *     or trim() behaves differently at some edge, the host test agrees with the
 *     shim and the device does something else. Only the semantics CoordParse
 *     actually uses are implemented, and they are implemented to match the
 *     documented Arduino behaviour -- but "documented" is not "observed".
 *   - anything about the ESP32 toolchain. CoordParse.h is compiled for the
 *     device by the ORDINARY FIRMWARE BUILD, because ConfigurationWebServer.cpp
 *     includes it -- so a header that passes here and will not build for the
 *     board fails `pio run`, not this script. It is deliberately NOT added to
 *     run.sh's cross-compile block: that block builds src/game TUs against the
 *     narrow include path, which cannot reach Arduino.h by design.
 *
 * It is deliberately NOT on the shared INCLUDES path. One binary asks for it by
 * name, so no other host test can accidentally acquire an Arduino surface and
 * quietly stop being pure.
 * ==========================================================================*/

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

class String
{
  public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}

    void reserve(size_t n) { s_.reserve(n); }
    unsigned int length() const { return static_cast<unsigned int>(s_.length()); }
    const char* c_str() const { return s_.c_str(); }
    bool isEmpty() const { return s_.empty(); }

    char operator[](unsigned int i) const { return i < length() ? s_[i] : '\0'; }

    String& operator+=(char c) { s_ += c; return *this; }
    String& operator+=(const char* c) { if (c) s_ += c; return *this; }

    bool operator==(const char* o) const { return o && s_ == o; }
    bool operator!=(const char* o) const { return !(*this == o); }

    /// Arduino: [from, to). A `to` past the end clamps; from >= to gives "".
    String substring(unsigned int from, unsigned int to) const
    {
        if (from >= length() || from >= to) return String();
        if (to > length()) to = length();
        return String(s_.substr(from, to - from));
    }
    String substring(unsigned int from) const { return substring(from, length()); }

    int indexOf(char c) const
    {
        const size_t p = s_.find(c);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }

    bool endsWith(const char* suffix) const
    {
        if (!suffix) return false;
        const size_t n = std::strlen(suffix);
        return s_.length() >= n && s_.compare(s_.length() - n, n, suffix) == 0;
    }

    /// Arduino: remove from `index` to the end.
    void remove(unsigned int index) { if (index < length()) s_.erase(index); }

    /// Arduino: strip leading and trailing whitespace, in place.
    void trim()
    {
        size_t b = 0, e = s_.length();
        while (b < e && std::isspace(static_cast<unsigned char>(s_[b]))) b++;
        while (e > b && std::isspace(static_cast<unsigned char>(s_[e - 1]))) e--;
        s_ = s_.substr(b, e - b);
    }

  private:
    std::string s_;
};
