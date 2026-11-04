// Blipscope web demo: the Arduino/ESP-IDF calls that need a body -- clocks, Serial, the globals.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <LovyanGFX.hpp>
#include <emscripten.h>
#include <cstdarg>

HardwareSerial Serial;
EspClass ESP;
WiFiClass WiFi;

static double t0Ms()
{
    static const double t0 = emscripten_get_now();
    return t0;
}
unsigned long millis() { return (unsigned long)(emscripten_get_now() - t0Ms()); }
unsigned long micros() { return (unsigned long)((emscripten_get_now() - t0Ms()) * 1000.0); }
int64_t esp_timer_get_time() { return (int64_t)((emscripten_get_now() - t0Ms()) * 1000.0); }
// delay() on the frame loop cannot block the page; the radar's loop path never relies on it.
void delay(unsigned long) {}
void delayMicroseconds(unsigned int) {}

long random(long max) { return max > 0 ? (long)(std::rand() % max) : 0; }
long random(long min, long max) { return max > min ? min + random(max - min) : min; }
void randomSeed(unsigned long s) { std::srand((unsigned)s); }

void EspClass::restart() { EM_ASM({ console.warn("[demo] ESP.restart() requested -- ignored"); }); }
void esp_restart() { ESP.restart(); }

String IPAddress::toString() const
{
    char b[20];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", a_[0], a_[1], a_[2], a_[3]);
    return String(b);
}

// ---- Serial: line-buffered to the browser console --------------------------------------------
static std::string serialLine;
static void serialEmit(const char* s)
{
    for (; *s; ++s) {
        if (*s == '\n') {
            EM_ASM({ console.log("[device] " + UTF8ToString($0)); }, serialLine.c_str());
            serialLine.clear();
        } else if (*s != '\r') {
            serialLine += *s;
        }
    }
}
size_t HardwareSerial::print(const String& s) { serialEmit(s.c_str()); return s.length(); }
int HardwareSerial::printf(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    serialEmit(buf);
    return n;
}

size_t lgfx::LGFXBase::printf(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    print(buf);
    return n > 0 ? (size_t)n : 0;
}

namespace lgfx { namespace fonts {
    const IFont Font0{0, 6, 8}, Font2{2, 8, 16}, Font4{4, 14, 26}, FreeSans9pt7b{90, 11, 22},
        FreeSansBold9pt7b{91, 11, 22}, FreeSansBold12pt7b{120, 14, 29}, AsciiFont8x16{816, 8, 16};
} }
