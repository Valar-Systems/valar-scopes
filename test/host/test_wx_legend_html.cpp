// WD8a (docs/wx-device.md): the config page's weather legend, include/WxLegendHtml.h, read as the HTML the device
// serves, in both looks. Rows are tagged [L1]-[L4] so test/host/sabotage_wx_legend_html.py can require exactly the
// sabotaged row to go red. Exit: 0 ok, 1 a check failed.
#include <cstdio>
#include <regex>
#include <string>
#include <vector>

#include "WxLegendHtml.h"

static int failures = 0;
static void row(bool ok, const char* tag, const std::string& what)
{
    std::printf("  %s  [%s] %s\n", ok ? "ok  " : "FAIL", tag, what.c_str());
    if (!ok) ++failures;
}

static std::string Rgb(uint32_t c)
{
    char b[24];
    std::snprintf(b, sizeof b, "rgb(%u,%u,%u)", (unsigned)((c >> 16) & 0xFF), (unsigned)((c >> 8) & 0xFF), (unsigned)(c & 0xFF));
    return b;
}

int main()
{
    std::printf("WxLegendHtml.h: the config page's weather legend\n");
    for (const wxs2::Look look : {wxs2::Look::A, wxs2::Look::B}) {
        const char* L = look == wxs2::Look::A ? "A" : "B";
        const std::string h = wxlegendhtml::Html(look);
        std::printf("== look %s (%u B)\n", L, (unsigned)h.size());

        // [L1] the rows: exactly the avionics look's six (ruled 2026-11-06: the drawn bands in words, no "Very light"
        // since drizzle is hidden, no snow), in order, each holding its codes.
        const char* want[6] = {"Light", "Moderate", "Heavy", "Intense", "No radar coverage", "No data"};
        std::vector<std::string> labels;
        std::regex lr("<span style=\"min-width:9.5rem\">([^<]*)</span>");
        for (std::sregex_iterator it(h.begin(), h.end(), lr), e; it != e; ++it) labels.push_back((*it)[1]);
        bool l1 = labels.size() == 6 && h.find("now") == std::string::npos;   // no "Snow"/"snow"
        for (int i = 0; l1 && i < 6; ++i) l1 = labels[i] == want[i];
        row(l1, "L1", std::string("rows: ") + std::to_string(labels.size()) + " labels, the avionics six without snow");

        // [L2] every drawn code's swatch (2-13) fills with the layer's colour for its code in this look, and code 1
        // (not drawn) has no swatch. Hand-written anchor: look A code 2 is green at 40 %, (0, 80, 0).
        int bad = h.find("data-code=\"1\"") == std::string::npos ? 0 : 1, seen = 0;
        for (int code = 2; code <= 13; ++code) {
            const std::string tag = "data-code=\"" + std::to_string(code) + "\"><rect width=\"12\" height=\"12\" fill=\"";
            const size_t at = h.find(tag);
            if (at == std::string::npos) { ++bad; continue; }
            ++seen;
            const std::string fill = h.substr(at + tag.size(), h.find('"', at + tag.size()) - (at + tag.size()));
            if (fill != Rgb(wxs2::Pixel888((uint8_t)code, 0, 0, look))) ++bad;
        }
        if (look == wxs2::Look::A && h.find("data-code=\"2\"><rect width=\"12\" height=\"12\" fill=\"rgb(0,80,0)\"") == std::string::npos) ++bad;
        row(bad == 0 && seen == 12, "L2", "echo swatches 2-13: " + std::to_string(seen) + " found, " + std::to_string(bad) +
                                          " wrong (not wxs2's colour, or a swatch for code 1)");

        // [L3] 14 and 15 are their dithers: the 2 x 2 cell of Pixel888, black where it is 0.
        int dbad = 0;
        for (int code = 14; code <= 15; ++code) {
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x) {
                    const std::string cell = "<rect x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) +
                                             "\" width=\"1\" height=\"1\" fill=\"" + Rgb(wxs2::Pixel888((uint8_t)code, x, y, look)) + "\"/>";
                    const size_t p = h.find("id=\"wxd" + std::to_string(code) + "\"");
                    const size_t q = p == std::string::npos ? p : h.find("</pattern>", p);
                    if (p == std::string::npos || h.substr(p, q - p).find(cell) == std::string::npos) ++dbad;
                }
        }
        row(dbad == 0, "L3", "dithers 14 and 15: " + std::to_string(dbad) + " cells not the device's pattern");

        // [L4] no literal '%': the page's %PLACEHOLDER% processing would eat it. 14 swatches in all (2-13, 14, 15).
        size_t sw = 0;
        for (size_t p = h.find("<svg"); p != std::string::npos; p = h.find("<svg", p + 1)) ++sw;
        row(h.find('%') == std::string::npos && sw == 14, "L4",
            "no literal '%' (" + std::to_string(h.find('%') == std::string::npos ? 0 : 1) + "), " + std::to_string(sw) + " swatches");
    }
    std::printf(failures ? "WX LEGEND HTML TESTS FAILED\n" : "all WX legend HTML checks passed\n");
    return failures ? 1 : 0;
}
