// Host test for the config page's map section (docs/blipscope-map-client-steps.md, "Step D's other half", (b)):
// mapsettingshtml::Html is exactly what the page serves, so this grades the served markup.
//   DOC2  no map-layer key: its box is checked; a stored "false" drops it (the control)
//   DNC2  the same for map-names
//   D1's page row: no stored terrain selects strong; each of the four values selects its own option, and only it
//   no '%' anywhere (the page's %PLACEHOLDER% processing would eat it)
// Plants (test/host/sabotage_map_settings.py): S-DOC2 reads a literal "false" in place of ResolveToggle for the map,
// exactly DOC2 red; S-DNC2 the same for the names, exactly DNC2 red.
#include <cstdio>
#include <string>

#include "../../include/MapSettingsHtml.h"

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// The opening tag of the named input, or "" when it is not there.
static std::string InputTag(const std::string& h, const char* name)
{
    const std::string key = std::string("name=\"") + name + "\"";
    const size_t at = h.find(key);
    if (at == std::string::npos) return "";
    const size_t open = h.rfind('<', at);
    const size_t close = h.find('>', at);
    return h.substr(open, close - open + 1);
}

static bool Checked(const std::string& h, const char* name)
{
    return InputTag(h, name).find(" checked") != std::string::npos;
}

// The option values carrying " selected", in order.
static std::string Selected(const std::string& h)
{
    std::string out;
    size_t at = 0;
    while ((at = h.find("<option value=\"", at)) != std::string::npos) {
        const size_t v0 = at + 15, v1 = h.find('"', v0);
        const size_t close = h.find('>', v1);
        if (h.substr(v1, close - v1).find(" selected") != std::string::npos) out += (out.empty() ? "" : ",") + h.substr(v0, v1 - v0);
        at = close;
    }
    return out;
}

int main()
{
    const std::string unset = mapsettingshtml::Html(nullptr, nullptr, nullptr);
    const std::string empty = mapsettingshtml::Html("", "", "");

    // DOC2
    check(!InputTag(unset, "map-layer").empty(), "DOC2 the map's box is rendered");
    check(Checked(unset, "map-layer"), "DOC2 no map-layer key (nullptr): the box is checked");
    check(Checked(empty, "map-layer"), "DOC2 no map-layer key (\"\"): the box is checked");
    check(Checked(mapsettingshtml::Html("true", nullptr, nullptr), "map-layer"), "DOC2 a stored \"true\": checked");
    check(!Checked(mapsettingshtml::Html("false", nullptr, nullptr), "map-layer"),
          "DOC2 CONTROL a stored \"false\": not checked");

    // DNC2
    check(!InputTag(unset, "map-names").empty(), "DNC2 the names' box is rendered");
    check(Checked(unset, "map-names"), "DNC2 no map-names key (nullptr): the box is checked");
    check(Checked(empty, "map-names"), "DNC2 no map-names key (\"\"): the box is checked");
    check(Checked(mapsettingshtml::Html(nullptr, "true", nullptr), "map-names"), "DNC2 a stored \"true\": checked");
    check(!Checked(mapsettingshtml::Html(nullptr, "false", nullptr), "map-names"),
          "DNC2 CONTROL a stored \"false\": not checked");
    // the two boxes are independent
    check(Checked(mapsettingshtml::Html("false", nullptr, nullptr), "map-names"),
          "the map's \"false\" leaves the names' box checked");
    check(Checked(mapsettingshtml::Html(nullptr, "false", nullptr), "map-layer"),
          "the names' \"false\" leaves the map's box checked");

    // D1's page row
    check(Selected(unset) == "strong", "D1 page: no stored terrain selects strong, and only it");
    check(Selected(empty) == "strong", "D1 page: \"\" selects strong");
    const char* vals[] = {"off", "subtle", "strong", "xstrong"};
    for (const char* v : vals) {
        const std::string want = v;
        char what[96];
        std::snprintf(what, sizeof what, "D1 page: a stored \"%s\" selects its own option, and only it", v);
        check(Selected(mapsettingshtml::Html(nullptr, nullptr, v)) == want, what);
    }
    check(Selected(mapsettingshtml::Html(nullptr, nullptr, "loud")) == "strong", "D1 page: \"loud\" selects strong");
    check(Selected(mapsettingshtml::Html(nullptr, nullptr, "STRONG")) == "strong", "D1 page: \"STRONG\" selects strong");
    // Q-T4: the owner's labels, in his order
    const size_t a = unset.find(">Off<"), b = unset.find(">Subtle<"), c = unset.find(">Strong<"),
                 d = unset.find(">Extra strong<");
    check(a != std::string::npos && b != std::string::npos && c != std::string::npos && d != std::string::npos &&
              a < b && b < c && c < d,
          "Q-T4 the labels Off, Subtle, Strong, Extra strong, in that order");

    // the page's placeholder processing
    check(unset.find('%') == std::string::npos, "no '%' in the section");

    std::printf("test_map_settings_html: %s (%d failure(s))\n", failures ? "FAILED" : "all checks passed", failures);
    return failures ? 1 : 0;
}
