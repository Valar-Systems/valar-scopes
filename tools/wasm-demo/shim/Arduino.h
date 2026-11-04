#pragma once

// Blipscope web demo (tools/wasm-demo): a stand-in for the Arduino core, enough for the radar's
// translation units to compile under Emscripten. NOTHING here is on any device build's include
// path: platformio.ini never names tools/, so the firmware cannot see these headers.
//
// String follows the documented Arduino semantics for the calls the radar makes (substring clamps,
// indexOf returns -1, toInt stops at the first non-digit, String(float, n) rounds to n places).
// Where Arduino's behaviour at an edge differs from this one, the demo differs from the device;
// it is a demo, not a test.

#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>

#define ARDUINO 10819
#define HEX 16
#define DEC 10
#define OCT 8
#define BIN 2
#ifndef PI
#define PI 3.1415926535897932384626433832795
#endif
#define HALF_PI 1.5707963267948966192313216916398
#define TWO_PI 6.283185307179586476925286766559
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105
#define IRAM_ATTR
#define DRAM_ATTR
#define PROGMEM
#define PGM_P const char*
#define PSTR(s) (s)
#define F(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char*)(addr))
#define pgm_read_word(addr) (*(const unsigned short*)(addr))
#define pgm_read_dword(addr) (*(const unsigned long*)(addr))
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2

// The ESP32 core's Arduino.h brings these in (using std::min / std::max; radians/degrees/sq as macros).
using std::min;
using std::max;
using std::abs;
#define radians(deg) ((deg) * DEG_TO_RAD)
#define degrees(rad) ((rad) * RAD_TO_DEG)
#define sq(x) ((x) * (x))

typedef bool boolean;
typedef uint8_t byte;
typedef unsigned int word;
// Flash strings are ordinary memory here (ArduinoJson's PROGMEM path needs the names).
class __FlashStringHelper;
#define FPSTR(p) (reinterpret_cast<const __FlashStringHelper*>(p))
#define pgm_read_ptr(addr) (*(void* const*)(addr))
#define pgm_read_float(addr) (*(const float*)(addr))
#define strlen_P strlen
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcpy_P strcpy
#define strncpy_P strncpy
#define memcpy_P memcpy
#define memcmp_P memcmp

class String
{
  public:
    String() {}
    String(const char* s) : s_(s ? s : "") {}
    String(const char* s, unsigned int n) : s_(s ? std::string(s, n) : std::string()) {}
    String(const std::string& s) : s_(s) {}
    String(const String& o) = default;
    String(String&& o) = default;
    explicit String(char c) : s_(1, c) {}
    explicit String(unsigned char v, unsigned char base = 10) { s_ = itoa_(v, base); }
    explicit String(int v, unsigned char base = 10) { s_ = (base == 10) ? std::to_string(v) : itoa_((unsigned long)(unsigned)v, base); }
    explicit String(unsigned int v, unsigned char base = 10) { s_ = itoa_(v, base); }
    explicit String(long v, unsigned char base = 10) { s_ = (base == 10) ? std::to_string(v) : itoa_((unsigned long)v, base); }
    explicit String(unsigned long v, unsigned char base = 10) { s_ = itoa_(v, base); }
    explicit String(long long v, unsigned char base = 10) { s_ = (base == 10) ? std::to_string(v) : itoa_((unsigned long long)v, base); }
    explicit String(unsigned long long v, unsigned char base = 10) { s_ = itoa_(v, base); }
    explicit String(float v, unsigned int places = 2) { s_ = ftoa_(v, places); }
    explicit String(double v, unsigned int places = 2) { s_ = ftoa_(v, places); }
    String& operator=(const String& o) = default;
    String& operator=(String&& o) = default;
    String& operator=(const char* s) { s_ = s ? s : ""; return *this; }

    unsigned int length() const { return (unsigned int)s_.size(); }
    bool isEmpty() const { return s_.empty(); }
    const char* c_str() const { return s_.c_str(); }
    const std::string& str() const { return s_; }
    void reserve(unsigned int n) { s_.reserve(n); }
    char* begin() { return &s_[0]; }
    char* end() { return &s_[0] + s_.size(); }
    const char* begin() const { return s_.data(); }
    const char* end() const { return s_.data() + s_.size(); }

    char charAt(unsigned int i) const { return i < s_.size() ? s_[i] : 0; }
    void setCharAt(unsigned int i, char c) { if (i < s_.size()) s_[i] = c; }
    char operator[](unsigned int i) const { return charAt(i); }
    char& operator[](unsigned int i) { static char dummy; return i < s_.size() ? s_[i] : (dummy = 0); }

    bool equals(const String& o) const { return s_ == o.s_; }
    bool equals(const char* o) const { return s_ == (o ? o : ""); }
    bool equalsIgnoreCase(const String& o) const {
        if (s_.size() != o.s_.size()) return false;
        for (size_t i = 0; i < s_.size(); i++)
            if (std::tolower((unsigned char)s_[i]) != std::tolower((unsigned char)o.s_[i])) return false;
        return true;
    }
    int compareTo(const String& o) const { return s_.compare(o.s_); }
    bool startsWith(const String& p) const { return s_.compare(0, p.s_.size(), p.s_) == 0 && s_.size() >= p.s_.size(); }
    bool startsWith(const String& p, unsigned int off) const { return off <= s_.size() && s_.compare(off, p.s_.size(), p.s_) == 0; }
    bool endsWith(const String& p) const { return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0; }

    int indexOf(char c, unsigned int from = 0) const { auto p = s_.find(c, from); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(const String& t, unsigned int from = 0) const { auto p = s_.find(t.s_, from); return p == std::string::npos ? -1 : (int)p; }
    int indexOf(const char* t, unsigned int from = 0) const { return indexOf(String(t), from); }
    int lastIndexOf(char c) const { auto p = s_.rfind(c); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(char c, unsigned int from) const { auto p = s_.rfind(c, from); return p == std::string::npos ? -1 : (int)p; }
    int lastIndexOf(const String& t) const { auto p = s_.rfind(t.s_); return p == std::string::npos ? -1 : (int)p; }
    String substring(unsigned int from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
    String substring(unsigned int from, unsigned int to) const {
        if (from > to) std::swap(from, to);
        if (from >= s_.size()) return String();
        if (to > s_.size()) to = (unsigned int)s_.size();
        return String(s_.substr(from, to - from));
    }

    void toLowerCase() { for (auto& c : s_) c = (char)std::tolower((unsigned char)c); }
    void toUpperCase() { for (auto& c : s_) c = (char)std::toupper((unsigned char)c); }
    void trim() {
        size_t a = 0, b = s_.size();
        while (a < b && std::isspace((unsigned char)s_[a])) a++;
        while (b > a && std::isspace((unsigned char)s_[b - 1])) b--;
        s_ = s_.substr(a, b - a);
    }
    void replace(const String& from, const String& to) {
        if (from.s_.empty()) return;
        size_t p = 0;
        while ((p = s_.find(from.s_, p)) != std::string::npos) { s_.replace(p, from.s_.size(), to.s_); p += to.s_.size(); }
    }
    void replace(char from, char to) { for (auto& c : s_) if (c == from) c = to; }
    void remove(unsigned int index) { if (index < s_.size()) s_.erase(index); }
    void remove(unsigned int index, unsigned int count) { if (index < s_.size()) s_.erase(index, count); }

    long toInt() const { return std::strtol(s_.c_str(), nullptr, 10); }
    float toFloat() const { return (float)std::strtod(s_.c_str(), nullptr); }
    double toDouble() const { return std::strtod(s_.c_str(), nullptr); }
    void getBytes(unsigned char* buf, unsigned int n, unsigned int index = 0) const {
        if (!n || !buf) return;
        unsigned int k = 0;
        for (; index + k < s_.size() && k + 1 < n; k++) buf[k] = (unsigned char)s_[index + k];
        buf[k] = 0;
    }
    void toCharArray(char* buf, unsigned int n, unsigned int index = 0) const { getBytes((unsigned char*)buf, n, index); }

    bool concat(const String& o) { s_ += o.s_; return true; }
    bool concat(const char* o) { if (o) s_ += o; return true; }
    bool concat(const char* o, unsigned int n) { if (o) s_.append(o, n); return true; }
    bool concat(char c) { s_ += c; return true; }
    bool concat(int v) { s_ += std::to_string(v); return true; }
    bool concat(unsigned int v) { s_ += std::to_string(v); return true; }
    bool concat(long v) { s_ += std::to_string(v); return true; }
    bool concat(unsigned long v) { s_ += std::to_string(v); return true; }
    bool concat(float v) { s_ += ftoa_(v, 2); return true; }
    bool concat(double v) { s_ += ftoa_(v, 2); return true; }
    template <typename T> String& operator+=(const T& v) { concat(v); return *this; }

    bool operator==(const String& o) const { return s_ == o.s_; }
    bool operator==(const char* o) const { return s_ == (o ? o : ""); }
    bool operator!=(const String& o) const { return s_ != o.s_; }
    bool operator!=(const char* o) const { return !(*this == o); }
    bool operator<(const String& o) const { return s_ < o.s_; }
    bool operator>(const String& o) const { return s_ > o.s_; }
    bool operator<=(const String& o) const { return s_ <= o.s_; }
    bool operator>=(const String& o) const { return s_ >= o.s_; }
    explicit operator bool() const { return true; }

  private:
    std::string s_;
    template <typename U> static std::string itoa_(U v, unsigned base) {
        if (base < 2 || base > 36) base = 10;
        if (v == 0) return "0";
        std::string r;
        while (v) { unsigned d = (unsigned)(v % base); r += (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; }
        std::reverse(r.begin(), r.end());
        return r;
    }
    static std::string ftoa_(double v, unsigned places) {
        if (std::isnan(v)) return "nan";
        if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.*f", (int)places, v);
        return buf;
    }
};

inline String operator+(const String& a, const String& b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, const char* b) { String r(a); r.concat(b); return r; }
inline String operator+(const char* a, const String& b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, char b) { String r(a); r.concat(b); return r; }
inline String operator+(char a, const String& b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, unsigned int b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, unsigned long b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, float b) { String r(a); r.concat(b); return r; }
inline String operator+(const String& a, double b) { String r(a); r.concat(b); return r; }
inline bool operator==(const char* a, const String& b) { return b == a; }
inline bool operator!=(const char* a, const String& b) { return !(b == a); }

// ---- time: millis/micros from the browser's clock; delay never blocks the page ----
unsigned long millis();
unsigned long micros();
void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);
inline void yield() {}

long random(long max);
long random(long min, long max);
void randomSeed(unsigned long);

template <typename T, typename L, typename H> inline T constrain(T x, L lo, H hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline long map(long x, long a, long b, long c, long d) { return (x - a) * (d - c) / (b - a) + c; }
inline float temperatureRead() { return 42.0f; }   // the S3's die sensor: a fixed reading
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }
inline int analogRead(int) { return 0; }

// ---- Serial: to the browser console ----
class HardwareSerial
{
  public:
    void begin(unsigned long) {}
    void end() {}
    int printf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    size_t print(const String& s);
    size_t print(const char* s) { return print(String(s)); }
    size_t print(const __FlashStringHelper* s) { return print(reinterpret_cast<const char*>(s)); }
    size_t print(char c) { return print(String(c)); }
    size_t print(int v) { return print(String(v)); }
    size_t print(unsigned v) { return print(String(v)); }
    size_t print(long v) { return print(String(v)); }
    size_t print(unsigned long v) { return print(String(v)); }
    size_t print(double v, int p = 2) { return print(String(v, (unsigned)p)); }
    size_t println() { return print(String("\n")); }
    template <typename T> size_t println(const T& v) { size_t n = print(v); print("\n"); return n + 1; }
    size_t write(uint8_t c) { return print((char)c); }
    void flush() {}
    int available() { return 0; }
    int read() { return -1; }
    void setDebugOutput(bool) {}
    explicit operator bool() const { return true; }
};
extern HardwareSerial Serial;

// ---- ESP: heap and identity queries answered with fixed, plausible values ----
class EspClass
{
  public:
    uint32_t getFreeHeap() { return 180000; }
    uint32_t getMinFreeHeap() { return 150000; }
    uint32_t getMaxAllocHeap() { return 110000; }
    uint32_t getHeapSize() { return 320000; }
    uint32_t getFreePsram() { return 7000000; }
    uint32_t getPsramSize() { return 8388608; }
    uint32_t getMaxAllocPsram() { return 7000000; }
    uint64_t getEfuseMac() { return 0x02DE0001ull; }   // the demo MAC, 02:...: never a real board's
    uint32_t getCpuFreqMHz() { return 240; }
    const char* getSdkVersion() { return "web"; }
    const char* getChipModel() { return "web"; }
    uint32_t getFlashChipSize() { return 16 * 1024 * 1024; }
    uint32_t getSketchSize() { return 0; }
    uint32_t getFreeSketchSpace() { return 0; }
    uint32_t getCycleCount() { return (uint32_t)micros() * 240u; }
    void restart();
};
extern EspClass ESP;

#include "IPAddress.h"
