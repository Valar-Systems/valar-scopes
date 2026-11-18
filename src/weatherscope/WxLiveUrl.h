#pragma once
// Weatherscope's /wx request (docs/weatherscope-stage4.md S4.32; Q125 (a), ruled: Weatherscope asks `shsr`, through its
// OWN call). F1 (scripts/check_wx_device_cref.py, uk-data's) will recognise it by name (S4.33's interface): every
// `WeatherscopeWxUrl(` call site under src/ passes "shsr", and no Weatherscope file calls the radar's `WeatherUrl(`.
// The location goes through WireLocation.h (2 dp on the wire). r = 97 km: one crop covers both zooms (1.2).
#include <Arduino.h>
#include <stdio.h>

#include "WireLocation.h"

namespace wxlive {

constexpr const char* kPath = "/api/v1/blipscope/wx/";
constexpr int kRadiusKm = 97;

inline String WeatherscopeWxUrl(const String& base, const char* product)
{
    String u = base;
    u += kPath;
    u += product;
    return u;
}

inline String WeatherscopeWxQuery(double lat, double lon)
{
    String q("?lat=");
    q += wireloc::Center(lat).c_str();
    q += "&lon=";
    q += wireloc::Center(lon).c_str();
    char r[8];
    snprintf(r, sizeof r, "&r=%d", kRadiusKm);
    q += r;
    return q;
}

}  // namespace wxlive
