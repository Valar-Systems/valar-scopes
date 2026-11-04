#pragma once
#include <cstdint>
#include <cstdio>

class String;

// Blipscope web demo: Arduino's IPAddress, as a value. The demo has no network; WiFi.localIP()
// answers a documentation address (RFC 5737), never a real one.
class IPAddress
{
  public:
    IPAddress() : a_{0, 0, 0, 0} {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : a_{a, b, c, d} {}
    IPAddress(uint32_t v) : a_{(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)} {}
    uint8_t operator[](int i) const { return a_[i & 3]; }
    operator uint32_t() const { return a_[0] | (a_[1] << 8) | (a_[2] << 16) | ((uint32_t)a_[3] << 24); }
    bool operator==(const IPAddress& o) const { return (uint32_t)*this == (uint32_t)o; }
    bool operator!=(const IPAddress& o) const { return !(*this == o); }
    String toString() const;

  private:
    uint8_t a_[4];
};
