#pragma once
// The config page's weather legend (docs/wx-device.md, WD8; ruled 2026-10-28): what the layer draws, in the look in
// use, under the weather switch in the Display tab's Weather section. Pure C++11 (no Arduino), so
// test/host/test_wx_legend_html.cpp reads exactly the HTML the device serves.
//
// The colours are NEVER typed here: every swatch is drawn from wxs2::Pixel888, the layer's own function, at serve time
// -- an echo code's fill is its colour in the look; 14 and 15 are their dithers, the 2 x 2 cell of Pixel888 itself.
// The rows are Weatherscope's legend labels (src/weatherscope/WxLegend.h), without its snow row (the layer cannot draw
// snow until WXC1 v2 brings the type). Code 13, the 75+ white, is the last swatch of "Hail possible", as there.
//
// The HTML holds no literal '%': the page's %PLACEHOLDER% processing would eat it (check-config-form.py refuses one).
#include <stdint.h>
#include <string>

#include "WxS2.h"

namespace wxlegendhtml {

struct Row { const char* label; uint8_t first, last; };
constexpr Row kRows[6] = {{"Light rain", 1, 4},    {"Moderate", 5, 7},          {"Heavy", 8, 10},
                          {"Hail possible", 11, 13}, {"No radar coverage", 14, 14}, {"No data", 15, 15}};
constexpr int kSwatch = 12;   // px, as Weatherscope's legend draws them

inline std::string Rgb(uint32_t c)
{
    return "rgb(" + std::to_string((c >> 16) & 0xFF) + "," + std::to_string((c >> 8) & 0xFF) + "," +
           std::to_string(c & 0xFF) + ")";
}

/** One swatch. An echo code is one fill; a dither (14, 15) is a 2 x 2 pattern of Pixel888, black where it is 0. */
inline std::string Swatch(uint8_t code, wxs2::Look look)
{
    const std::string n = std::to_string(code), s = std::to_string(kSwatch);
    std::string out = "<svg width=\"" + s + "\" height=\"" + s + "\" data-code=\"" + n + "\">";
    if (code >= 1 && code <= 13) {
        out += "<rect width=\"" + s + "\" height=\"" + s + "\" fill=\"" + Rgb(wxs2::Pixel888(code, 0, 0, look)) + "\"/>";
    } else {
        out += "<defs><pattern id=\"wxd" + n + "\" width=\"2\" height=\"2\" patternUnits=\"userSpaceOnUse\">";
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                out += "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" width=\"1\" height=\"1\" fill=\"" +
                       Rgb(wxs2::Pixel888(code, x, y, look)) + "\"/>";
        out += "</pattern></defs><rect width=\"" + s + "\" height=\"" + s + "\" fill=\"url(#wxd" + n + ")\"/>";
    }
    return out + "</svg>";
}

/** The whole legend, in the look in use. */
inline std::string Html(wxs2::Look look)
{
    std::string out = "<div class=\"wxlegend\" data-look=\"";
    out += look == wxs2::Look::A ? "A" : "B";
    out += "\">";
    for (const Row& r : kRows) {
        out += "<div class=\"wxrow\" style=\"display:flex;align-items:center;gap:.35rem;margin:.2rem 0\">"
               "<span style=\"min-width:9.5rem\">";
        out += r.label;
        out += "</span>";
        for (int c = r.first; c <= r.last; ++c) out += Swatch((uint8_t)c, look);
        out += "</div>";
    }
    return out + "</div>";
}

}  // namespace wxlegendhtml
