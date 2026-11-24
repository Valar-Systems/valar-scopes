#pragma once
// P6's host String (test/host/test_p6_labels.cpp only): the operations src/AircraftInfoFields.cpp's formatters and
// include/RouteLabel.h use, with Arduino's results (String(long) is decimal, as Arduino's ltoa base 10; + and += append;
// trim() strips whitespace; equalsIgnoreCase compares ASCII case-blind). Kept apart from test/host/arduino_shim so no
// other host test changes. A formatter that needs more fails to compile here, loudly.
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

class String {
  public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const std::string& s) : s_(s) {}
    String(long v) : s_(std::to_string(v)) {}
    String(int v) : s_(std::to_string(v)) {}

    unsigned int length() const { return static_cast<unsigned int>(s_.length()); }
    const char* c_str() const { return s_.c_str(); }
    bool isEmpty() const { return s_.empty(); }

    String& operator+=(const String& o) { s_ += o.s_; return *this; }
    String& operator+=(const char* c) { if (c) s_ += c; return *this; }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator!=(const String& o) const { return s_ != o.s_; }

    bool equalsIgnoreCase(const String& o) const
    {
        if (s_.size() != o.s_.size()) return false;
        for (size_t i = 0; i < s_.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(s_[i])) != std::tolower(static_cast<unsigned char>(o.s_[i])))
                return false;
        return true;
    }

    void trim()
    {
        size_t b = 0, e = s_.length();
        while (b < e && std::isspace(static_cast<unsigned char>(s_[b]))) b++;
        while (e > b && std::isspace(static_cast<unsigned char>(s_[e - 1]))) e--;
        s_ = s_.substr(b, e - b);
    }

    friend String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
    friend String operator+(const String& a, const char* b) { String r(a); r += b; return r; }
    friend String operator+(const char* a, const String& b) { String r(a); r += b; return r; }

  private:
    std::string s_;
};
