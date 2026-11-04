#pragma once
#include "Arduino.h"
#include "IPAddress.h"

// Blipscope web renderer: Wi-Fi, always connected to a network called "demo". The address is from the
// documentation range (RFC 5737), never a real one.
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_CONNECTION_LOST = 5,
               WL_DISCONNECTED = 6 } wl_status_t;
typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
#define WIFI_PS_NONE 0

class WiFiClass
{
  public:
    wl_status_t status() { return WL_CONNECTED; }
    bool isConnected() { return true; }
    IPAddress localIP() { return IPAddress(192, 0, 2, 10); }
    IPAddress gatewayIP() { return IPAddress(192, 0, 2, 1); }
    IPAddress dnsIP(int = 0) { return IPAddress(192, 0, 2, 1); }
    String SSID() { return String("demo"); }
    String BSSIDstr() { return String("00:00:00:00:00:00"); }
    int8_t RSSI() { return -55; }
    int32_t channel() { return 6; }
    String macAddress() { return String("02:00:00:00:00:00"); }
    const char* getHostname() { return "blipscope-demo"; }
    bool setHostname(const char*) { return true; }
    bool mode(wifi_mode_t) { return true; }
    wifi_mode_t getMode() { return WIFI_STA; }
    bool setSleep(bool) { return true; }
    bool disconnect(bool = false, bool = false) { return true; }
    bool reconnect() { return true; }
    void begin() {}
    void begin(const char*, const char* = nullptr) {}
    int hostByName(const char*, IPAddress& out) { out = IPAddress(192, 0, 2, 20); return 1; }
};
extern WiFiClass WiFi;
