// Weatherscope's four scenarios (src/weatherscope/WxScenarios.h) through Choose and the text functions, against the
// predictions frozen in docs/weatherscope-stage3.md A8.10 and A8.15 (K3: 12 checks). The frames, and the warning's
// distance, bearing and UNTIL, come from test/fixtures/wx/loop/scenarios.ref.json -- scenarios.py's independent
// Python computation over the same frames. The text checks read the BUILT warning directly, so a wrong screen fails
// only its `choose` check.
//
//     test_wx_scenarios <test/fixtures/wx/loop>
//
// Its sabotages: python -I test/host/sabotage_wx_scenarios.py. Prints no coordinate.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "WxCrop.h"
#include "WxLogic.h"
#include "WxScenarios.h"

using namespace wxlogic;

namespace {

int checks = 0, failures = 0;
void check(const char* name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name, what.c_str());
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
    std::fclose(f);
    return true;
}

// The value of "key" in the object that starts at the first `"section"` -- a string or a bare number.
std::string ValueIn(const std::string& j, const char* section, const char* key)
{
    const size_t s = j.find(std::string("\"") + section + "\"");
    if (s == std::string::npos) return "";
    const size_t e = j.find('}', s);
    const std::string pat = std::string("\"") + key + "\": ";
    const size_t p = j.find(pat, s);
    if (p == std::string::npos || p > e) return "";
    size_t v = p + pat.size();
    if (j[v] == '"') { const size_t q = j.find('"', v + 1); return j.substr(v + 1, q - v - 1); }
    size_t w = v;
    while (w < j.size() && j[w] != ',' && j[w] != '\n' && j[w] != '\r' && j[w] != '}') ++w;
    return j.substr(v, w - v);
}

struct Frame {
    wxcrop::Header h;
    std::vector<uint8_t> codes;
};
bool Load(const std::string& path, Frame* f)
{
    std::vector<uint8_t> b;
    if (!ReadFile(path, &b)) return false;
    if (wxcrop::Validate(b.data(), b.size(), &f->h) != wxcrop::Status::Ok) return false;
    f->codes.assign(f->h.Cells(), 0);
    return wxcrop::DecodeInto(b.data(), b.size(), f->codes.data(), f->codes.size()) == wxcrop::Status::Ok;
}

const char* ScreenName(Screen s)
{
    switch (s) {
        case Screen::Warning: return "Warning";
        case Screen::PrecipNow: return "PrecipNow";
        case Screen::PrecipApproaching: return "PrecipApproaching";
        case Screen::Dry: return "Dry";
    }
    return "?";
}
const char* PrecipName(Precip p) { return p == Precip::Rain ? "Rain" : p == Precip::Snow ? "Snow" : "None"; }
std::string Q(const char* s) { return std::string("\"") + s + "\""; }

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: test_wx_scenarios <test/fixtures/wx/loop>\n"); return 2; }
    const std::string dir = argv[1];
    std::vector<uint8_t> rj;
    if (!ReadFile(dir + "/scenarios.ref.json", &rj)) { std::printf("cannot read scenarios.ref.json\n"); return 2; }
    const std::string j(rj.begin(), rj.end());
    const std::string appName = ValueIn(j, "approaching", "frame"), dryName = ValueIn(j, "dry", "frame");
    const std::string refDist = ValueIn(j, "warning", "stormDistanceMi"), refBear = ValueIn(j, "warning", "stormBearingDeg");
    const std::string refCompass = ValueIn(j, "warning", "stormCompass"), refUntil = ValueIn(j, "warning", "until");
    if (appName.empty() || dryName.empty() || refDist.empty() || refBear.empty() || refCompass.empty() || refUntil.empty()) {
        std::printf("scenarios.ref.json is missing a field\n");
        return 2;
    }
    Frame app, dry;
    if (!Load(dir + "/" + appName, &app) || !Load(dir + "/" + dryName, &dry)) {
        std::printf("cannot load %s or %s\n", appName.c_str(), dryName.c_str());
        return 2;
    }
    std::printf("frames: approaching %s, dry %s (from scenarios.ref.json)\n", appName.c_str(), dryName.c_str());

    std::printf("== K3: dry ==\n");
    {
        wxscen::Scenario s;
        if (!wxscen::Build(wxscen::Kind::Dry, dry.h, dry.codes.data(), &s)) { std::printf("Build failed\n"); return 2; }
        const Choice c = s.Choose();
        check("K3.dry.choose", c.screen == Screen::Dry, ScreenName(c.screen));
        check("K3.dry.headline", std::strcmp(Headline(c), "") == 0, Q(Headline(c)));
    }
    std::printf("== K3: approaching ==\n");
    {
        wxscen::Scenario s;
        if (!wxscen::Build(wxscen::Kind::Approaching, app.h, app.codes.data(), &s)) { std::printf("Build failed\n"); return 2; }
        const Choice c = s.Choose();
        check("K3.app.choose", c.screen == Screen::PrecipApproaching, ScreenName(c.screen));
        check("K3.app.headline", std::strcmp(Headline(c), "PRECIP WITHIN 15 MI") == 0, Q(Headline(c)));
    }
    std::printf("== K3: here now (synthetic -RA) ==\n");
    {
        wxscen::Scenario s;
        if (!wxscen::Build(wxscen::Kind::HereNow, app.h, app.codes.data(), &s)) { std::printf("Build failed\n"); return 2; }
        const Choice c = s.Choose();
        check("K3.here.choose", c.screen == Screen::PrecipNow && c.type == Precip::Rain,
              std::string(ScreenName(c.screen)) + ", " + PrecipName(c.type));
        check("K3.here.headline", std::strcmp(Headline(c), "RAIN NOW") == 0, Q(Headline(c)));
    }
    std::printf("== K3: warning (synthetic SVR) ==\n");
    {
        wxscen::Scenario s;
        if (!wxscen::Build(wxscen::Kind::Warning, app.h, app.codes.data(), &s)) { std::printf("Build failed\n"); return 2; }
        const Choice c = s.Choose();
        char d[160];
        std::snprintf(d, sizeof d, "%s, index %d", ScreenName(c.screen), c.warning);
        check("K3.warn.choose", c.screen == Screen::Warning && c.warning == 0, d);

        const Tokens k = LineA(s.warn);
        std::string got;
        for (int i = 0; i < k.n; ++i) got += (i ? ", " : "") + Q(k.t[i]);
        check("K3.warn.lineA", k.n == 2 && std::strcmp(k.t[0], "HAIL 1 IN") == 0 && std::strcmp(k.t[1], "WIND 60 MPH") == 0,
              "[" + got + "]");

        const Motion m = ParseMotion(s.warn.motion);
        const MotionText t = MotionLine(m, s.here);
        check("K3.warn.travel", t.valid && std::strcmp(t.travel, "MOVING NE 35 MPH") == 0, Q(t.travel));

        const double rd = std::strtod(refDist.c_str(), nullptr), rb = std::strtod(refBear.c_str(), nullptr);
        char want[24];
        std::snprintf(want, sizeof want, "%ld MI", std::lround(rd));
        std::snprintf(d, sizeof d, "%.7f mi vs reference %s (|d| %.1e); text %s vs %s", t.distanceMi, refDist.c_str(),
                      std::fabs(t.distanceMi - rd), Q(t.distance).c_str(), Q(want).c_str());
        check("K3.warn.distance", m.valid && std::fabs(t.distanceMi - rd) <= 5e-7 && std::strcmp(t.distance, want) == 0, d);

        const double bearing = m.valid ? BearingDeg(s.here, m.pos[NearestPos(m, s.here)]) : -1;
        std::snprintf(d, sizeof d, "%.5f deg vs reference %s (|d| %.1e); %s vs %s", bearing, refBear.c_str(),
                      std::fabs(bearing - rb), Q(t.bearing).c_str(), Q(refCompass.c_str()).c_str());
        check("K3.warn.bearing", m.valid && std::fabs(bearing - rb) <= 5e-5 && refCompass == t.bearing, d);

        char until[40] = "";
        const bool okU = UntilText(s.warn.expiresIso, until, sizeof until);
        check("K3.warn.until", okU && refUntil == until, Q(until) + " vs " + Q(refUntil.c_str()));
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
