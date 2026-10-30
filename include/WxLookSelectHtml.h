#pragma once
// The config page's weather-look select (docs/wx-device.md, "The look as a shipping setting", K1): a shipping setting
// since 2026-10-29, look A by default (Daniel's decision). Each option is named by what it does, in Daniel's words;
// the stored values stay "A" and "B", so nothing migrates. Pure C++11, no Arduino, so test/host/test_wx_look_select.cpp
// grades exactly the HTML the page serves. No '%' anywhere: the page's %PLACEHOLDER% processing would eat it
// (check-config-form.py).
#include <string>

#include "WxS2.h"

namespace wxlookselect {

constexpr const char* kLabelA = "Brighter rain, outlined planes";   // look A: 40 percent, the 1 px outline
constexpr const char* kLabelB = "Fainter rain, no outlines";        // look B: 25 percent

/** The labelled select for the stored value `stored` ("" when the key is absent), with the look it means selected. */
inline std::string Html(const char* stored)
{
    const wxs2::Look l = wxs2::LookFromStored(stored);
    std::string h = "<label>Weather look <select name=\"wx-look\">";
    h += std::string("<option value=\"A\"") + (l == wxs2::Look::A ? " selected" : "") + ">" + kLabelA + "</option>";
    h += std::string("<option value=\"B\"") + (l == wxs2::Look::B ? " selected" : "") + ">" + kLabelB + "</option>";
    h += "</select></label>";
    return h;
}

}  // namespace wxlookselect
