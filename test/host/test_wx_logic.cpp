// Weatherscope's screen logic (src/weatherscope/WxLogic.h) against its frozen truth tables: docs/weatherscope-stage3.md
// A8.2 and A8.8 -- 118 named cases and assertions, plus RK on the real KDFW crop. Each line prints its frozen name, so
// test/host/sabotage_wx_logic.py can require each sabotage to fail EXACTLY its named set.
//
//     test_wx_logic <test/fixtures/wx>
//
// Coordinates here are synthetic (around 0,0 and 45,0), not anyone's location; nothing prints a coordinate.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "WxCrop.h"
#include "WxLogic.h"

using namespace wxlogic;

namespace {

int checks = 0, failures = 0;
void check(const char* name, bool ok, const std::string& detail = "")
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name, detail.c_str());
}
std::string S(const char* s) { return s ? s : "(null)"; }

const double kDegPerMi = kKmPerMi / kKmPerDeg;   // degrees of latitude per mile, in the map's metric

// A synthetic crop: `rows` x `cols` cells, NW cell centre at (latNw, lonNw), and the given codes.
struct Crop {
    wxcrop::Header h;
    std::vector<uint8_t> codes;
};
Crop MakeCrop(int rows, int cols, double latNw, double lonNw, double cellLat, double cellLon, std::vector<uint8_t> codes)
{
    Crop c;
    c.h.version = 1;
    c.h.coarsen = 1;
    c.h.rows = (uint16_t)rows;
    c.h.cols = (uint16_t)cols;
    c.h.latNwMicro = (int32_t)lround(latNw * 1e6);
    c.h.lonNwMicro = (int32_t)lround(lonNw * 1e6);
    c.h.cellLatMicro = (uint32_t)lround(cellLat * 1e6);
    c.h.cellLonMicro = (uint32_t)lround(cellLon * 1e6);
    c.codes = codes;
    return c;
}
bool Within(const Crop& c, LatLon here) { return EchoWithin(c.h, c.codes.data(), here); }

// A crop with one cell `mi` miles north of (lat0, 0) and one at it.
Crop North(double mi, uint8_t northCode, uint8_t hereCode = 0)
{
    const double d = mi * kDegPerMi;
    return MakeCrop(2, 1, d, 0, d, 0.001, {northCode, hereCode});
}
// A crop at latitude lat0 with one cell `mi` miles east of (lat0, 0).
Crop East(double lat0, double mi, uint8_t code)
{
    const double e = mi * kKmPerMi / (kKmPerDeg * cos(lat0 * (kPi / 180.0)));
    return MakeCrop(1, 2, lat0, 0, 0.001, e, {0, code});
}

const LatLon kSquare[4] = {{-1, -1}, {-1, 1}, {1, 1}, {1, -1}};   // around (0, 0)
const LatLon kFar[4] = {{10, 10}, {10, 11}, {11, 11}, {11, 10}};
const int64_t kNow = 1000000;

Warn W(const char* event, int64_t expires, const LatLon* poly = kSquare, size_t n = 4)
{
    Warn w;
    w.event = event;
    w.messageType = "Alert";
    w.expiresUtc = expires;
    w.polygon = poly;
    w.polygonN = n;
    return w;
}

std::string MotionNorth(double mi) { char b[96]; snprintf(b, sizeof(b), "2024-05-21T21:58:00-00:00...storm...180DEG...10KT...%.6f,0.000000", mi * kDegPerMi); return b; }

Precip H(std::vector<const char*> entries, int64_t ageS = 60, bool valid = true)
{
    return HereNow(entries.data(), entries.size(), valid, kNow - ageS, kNow);
}
const char* PName(Precip p) { return p == Precip::Rain ? "Rain" : p == Precip::Snow ? "Snow" : "None"; }

std::string Join(const Tokens& t)
{
    std::string s = "[";
    for (int i = 0; i < t.n; ++i) s += std::string(i ? ", " : "") + "\"" + t.t[i] + "\"";
    return s + "]";
}
bool Is(const Tokens& t, std::vector<std::string> want)
{
    if (t.n != (int)want.size()) return false;
    for (int i = 0; i < t.n; ++i) if (want[i] != t.t[i]) return false;
    return true;
}

std::vector<int> Placed(std::vector<LabelBox> boxes)
{
    int drawn[64];
    const int n = PlaceLabels(boxes.data(), (int)boxes.size(), drawn);
    std::vector<int> v(drawn, drawn + n);
    std::sort(v.begin(), v.end());
    return v;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2) { std::printf("usage: test_wx_logic <test/fixtures/wx>\n"); return 2; }
    const LatLon o{0, 0};

    std::printf("== R: approaching (1b) ==\n");
    check("R1", !Within(MakeCrop(2, 2, 0, 0, 0.01, 0.01, {0, 0, 0, 0}), o), "all dry");
    check("R2", Within(North(14.9, 1), o), "code 1 at 14.9 mi N");
    check("R3", !Within(North(15.1, 13), o), "code 13 at 15.1 mi N");
    check("R4", Within(MakeCrop(1, 1, 0, 0, 0.01, 0.01, {1}), o), "code 1 at the centre");
    check("R5", !Within(MakeCrop(2, 1, 5 * kDegPerMi, 0, 5 * kDegPerMi, 0.001, {14, 14}), o), "only code 14 within");
    check("R6", !Within(MakeCrop(2, 1, 5 * kDegPerMi, 0, 5 * kDegPerMi, 0.001, {15, 15}), o), "only code 15 within");
    check("R7", Within(East(45, 14.9, 5), LatLon{45, 0}), "lat 45, code 5 at 14.9 mi E");
    check("R8", !Within(East(45, 15.1, 5), LatLon{45, 0}), "lat 45, code 5 at 15.1 mi E");

    {   // RK: the real KDFW crop, at its centre, against the independent Python reference (ring_ref.py)
        const std::string dir = argv[1];
        FILE* f = std::fopen((dir + "/kdfw-60mi.wxc").c_str(), "rb");
        FILE* r = std::fopen((dir + "/kdfw-60mi.ring.txt").c_str(), "r");
        if (!f || !r) { std::printf("cannot read the KDFW crop or its ring reference\n"); return 2; }
        std::vector<uint8_t> bytes;
        uint8_t buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
        std::fclose(f);
        double ref = -2;
        if (std::fscanf(r, "%lf", &ref) != 1) { std::printf("the ring reference is unreadable\n"); return 2; }
        std::fclose(r);
        wxcrop::Header h;
        std::vector<uint8_t> codes(40000);
        if (wxcrop::DecodeInto(bytes.data(), bytes.size(), codes.data(), codes.size(), &h) != wxcrop::Status::Ok) {
            std::printf("the KDFW crop did not decode\n");
            return 2;
        }
        const LatLon centre{h.latNwMicro / 1e6 - (h.rows - 1) / 2.0 * (h.cellLatMicro / 1e6),
                            h.lonNwMicro / 1e6 + (h.cols - 1) / 2.0 * (h.cellLonMicro / 1e6)};
        const double got = NearestEchoMi(h, codes.data(), centre);
        char d[96];
        snprintf(d, sizeof(d), "nearest echo %.4f mi, reference %.4f mi", got, ref);
        check("RK", fabs(got - ref) <= 0.01, d);
    }

    std::printf("== P: which screen (1a) ==\n");
    {
        const Warn tor = W("Tornado Warning", kNow + 3600);
        const Warn ended = W("Tornado Warning", kNow - 60);
        const Warn far = W("Tornado Warning", kNow + 3600, kFar, 4);
        auto C = [&](const Warn* w, size_t n, Precip p, bool ring) { return Choose(w, n, o, kNow, p, ring); };
        Choice c = C(&tor, 1, Precip::Rain, true);
        check("P1", c.screen == Screen::Warning && c.warning == 0, "covering active warning wins");
        c = C(nullptr, 0, Precip::Rain, true);
        check("P2", c.screen == Screen::PrecipNow && c.type == Precip::Rain, "here now (rain) before the ring");
        c = C(nullptr, 0, Precip::None, true);
        check("P3", c.screen == Screen::PrecipApproaching, "ring only");
        c = C(nullptr, 0, Precip::None, false);
        check("P4", c.screen == Screen::Dry, "nothing");
        c = C(&ended, 1, Precip::None, true);
        check("P5", c.screen == Screen::PrecipApproaching, "a covering warning that has ended doesn't count");
        c = C(&far, 1, Precip::None, false);
        check("P6", c.screen == Screen::Dry, "an active warning elsewhere doesn't count");
        c = C(nullptr, 0, Precip::Snow, false);
        check("P7", c.screen == Screen::PrecipNow && c.type == Precip::Snow, "snow here now, no ring");
        c = C(nullptr, 0, Precip::Rain, false);
        check("P8", c.screen == Screen::PrecipNow && c.type == Precip::Rain, "rain here now, no ring");
    }

    std::printf("== C: a warning covers the location (1d) ==\n");
    {
        const LatLon sq[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};
        const LatLon sqClosed[5] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}, {0, 0}};
        const LatLon u[8] = {{0, 0}, {0, 3}, {3, 3}, {3, 2}, {1, 2}, {1, 1}, {3, 1}, {3, 0}};   // a U, notch to the north
        check("C1", Covers(sq, 4, LatLon{0.5, 0.5}), "inside a square");
        check("C2", !Covers(sq, 4, LatLon{2, 2}), "outside it");
        check("C3", !Covers(u, 8, LatLon{2, 1.5}), "in a concave polygon's notch");
        check("C4", Covers(u, 8, LatLon{0.5, 1.5}), "in its body");
        check("C5", !Covers(nullptr, 0, LatLon{0.5, 0.5}), "no polygon");
        check("C6", !Covers(sq, 2, LatLon{0, 0.5}), "two vertices");
        check("C7", Covers(sq, 4, LatLon{0.5, 1.0}), "on the right edge");
        check("C8", Covers(sqClosed, 5, LatLon{0.5, 0.5}), "closing vertex repeated");
    }

    std::printf("== W: which warnings, and their order (3a, 3d, Q2) ==\n");
    {
        check("W1", WarningRank("Tornado Warning") == 0);
        check("W2", WarningRank("Snow Squall Warning") == 1);
        check("W3", WarningRank("Severe Thunderstorm Warning") == 2);
        check("W4", WarningRank("Flash Flood Warning") == 3);
        check("W5", WarningRank("Tornado Watch") == -1, "watch");
        check("W6", WarningRank("Winter Storm Warning") == -1);
        check("W7", WarningRank("Extreme Wind Warning") == -1);
        check("W8", WarningRank("Special Weather Statement") == -1);
        check("W9", WarningRank("") == -1, "empty");
        const Warn four[4] = {W("Flash Flood Warning", kNow + 100), W("Severe Thunderstorm Warning", kNow + 100),
                              W("Tornado Warning", kNow + 100), W("Snow Squall Warning", kNow + 100)};
        int ord[4] = {-1, -1, -1, -1};
        const int n = Order(four, 4, o, kNow, ord, 4);
        check("W10", n == 4 && ord[0] == 2 && ord[1] == 3 && ord[2] == 1 && ord[3] == 0, "TOR, SS, SVR, FF");

        const std::string m5 = MotionNorth(5), m12 = MotionNorth(12), m40 = MotionNorth(40), m2 = MotionNorth(2);
        Warn a = W("Severe Thunderstorm Warning", kNow + 2000), b = W("Severe Thunderstorm Warning", kNow + 6000);
        a.motion = m5.c_str();
        b.motion = m12.c_str();
        Warn pair[2] = {b, a};   // B listed first, so a passing W11 can't be the input order
        int o2[2] = {-1, -1};
        Order(pair, 2, o, kNow, o2, 2);
        check("W11", o2[0] == 1, "the nearer storm (A, 5 mi) first, though B expires later");
        pair[1].motion = nullptr;   // A lacks a position
        Order(pair, 2, o, kNow, o2, 2);
        check("W12", o2[0] == 0, "A lacks a position: the later expiry (B) first");
        pair[0].motion = nullptr;
        Order(pair, 2, o, kNow, o2, 2);
        check("W13", o2[0] == 0, "both lack positions: the later expiry (B) first");
        Warn tor = W("Tornado Warning", kNow + 100), svr = W("Severe Thunderstorm Warning", kNow + 100);
        tor.motion = m40.c_str();
        svr.motion = m2.c_str();
        const Warn mixed[2] = {svr, tor};
        Order(mixed, 2, o, kNow, o2, 2);
        check("W14", o2[0] == 1, "type before distance: the Tornado Warning (40 mi) before SVR (2 mi)");
    }

    std::printf("== E: when a warning ends (3c) ==\n");
    {
        Warn w = W("Tornado Warning", 1000);
        check("E1", !Ended(w, 999), "before expires");
        check("E2", Ended(w, 1000), "at expires");
        check("E3", Ended(w, 1001), "after expires");
        w.messageType = "Cancel";
        check("E4", Ended(w, 500), "a Cancel before expires");
        w.messageType = "Update";
        check("E5", !Ended(w, 500), "an Update before expires");
    }

    std::printf("== M: storm motion (§3, Q4) ==\n");
    {
        const Motion m1 = ParseMotion("2024-05-21T21:58:00-00:00...storm...211DEG...37KT...0.5,-0.5");
        check("M1", m1.valid && m1.fromDeg == 211 && m1.speedKt == 37 && m1.nPos == 1 && m1.pos[0].lat == 0.5 &&
                        m1.pos[0].lon == -0.5, "parsed");
        Motion m = m1;
        check("M2", strcmp(TravelCompass(m), "NE") == 0, "from 211 -> " + S(TravelCompass(m)));
        m.fromDeg = 0;
        check("M3", strcmp(TravelCompass(m), "S") == 0, "from 0 -> " + S(TravelCompass(m)));
        m.fromDeg = 90;
        check("M4", strcmp(TravelCompass(m), "W") == 0, "from 90 -> " + S(TravelCompass(m)));
        check("M5", !ParseMotion("2024-05-21T21:58:00-00:00...storm...211DEG...0.5,-0.5").valid, "KT missing");
        check("M6", !ParseMotion("").valid, "empty");
        const Motion m7 = ParseMotion("t...storm...211DEG...37KT...-0.1,-0.1");
        const MotionText t7 = MotionLine(m7, o);
        char d7[64];
        snprintf(d7, sizeof(d7), "bearing %s, %.4f mi", t7.bearing, t7.distanceMi);
        check("M7", t7.valid && strcmp(t7.bearing, "SW") == 0 && fabs(t7.distanceMi - 9.75) <= 0.01, d7);
        const MotionText t8 = MotionLine(ParseMotion("t...storm...211DEG...0KT...-0.1,-0.1"), o);
        check("M8", t8.valid && strcmp(t8.travel, "STATIONARY") == 0, "travel " + S(t8.travel));
        char s9[96];
        snprintf(s9, sizeof(s9), "t...storm...211DEG...10KT...%.8f,0", 0.3 * kDegPerMi);
        const MotionText t9 = MotionLine(ParseMotion(s9), o);
        check("M9", t9.valid && strcmp(t9.distance, "<1 MI") == 0, "distance " + S(t9.distance));
        snprintf(s9, sizeof(s9), "t...storm...211DEG...10KT...%.8f,0", (0.5 + 1e-6) * kDegPerMi);   // +1e-6: see below
        const MotionText t10 = MotionLine(ParseMotion(s9), o);
        // 0.5 mi plus a millionth, so floating point can't land the boundary case below 0.5.
        check("M10", t10.valid && strcmp(t10.distance, "1 MI") == 0, "distance " + S(t10.distance));
        char s11[160];
        snprintf(s11, sizeof(s11), "t...storm...200DEG...10KT...%.6f,0 %.6f,0 %.6f,0", 30 * kDegPerMi, 9 * kDegPerMi,
                 20 * kDegPerMi);
        const MotionText t11 = MotionLine(ParseMotion(s11), o);
        check("M11", t11.valid && strcmp(t11.distance, "9 MI") == 0 && strcmp(t11.bearing, "N") == 0,
              "distance " + S(t11.distance) + ", bearing " + S(t11.bearing));
    }

    std::printf("== U: units (6a) ==\n");
    {
        char d[48];
        check("U1", KtToMph(37) == 43, (snprintf(d, sizeof(d), "%d", KtToMph(37)), d));
        check("U2", KtToMph(0) == 0);
        check("U3", KtToMph(10) == 12, (snprintf(d, sizeof(d), "%d", KtToMph(10)), d));
        check("U4", KtToMph(13) == 15, (snprintf(d, sizeof(d), "%d", KtToMph(13)), d));
        check("U5", fabs(KmToMi(24.140) - 15.00) <= 0.01);
        check("U6a", strcmp(Compass8(0), "N") == 0);
        check("U6b", strcmp(Compass8(22.4), "N") == 0);
        check("U6c", strcmp(Compass8(22.5), "NE") == 0);
        check("U6d", strcmp(Compass8(67.5), "E") == 0);
        check("U6e", strcmp(Compass8(359.9), "N") == 0);
    }

    std::printf("== L: label text (5d) ==\n");
    {
        char b[64];
        LabelText("Bend city", b, sizeof(b));
        check("L1", strcmp(b, "BEND") == 0, S(b));
        LabelText("La Pine city", b, sizeof(b));
        check("L2", strcmp(b, "LA PINE") == 0, S(b));
        LabelText("Winston-Salem city", b, sizeof(b));
        check("L3", strcmp(b, "WINSTON-SALEM") == 0, S(b));
        LabelText("Oklahoma City city", b, sizeof(b));
        check("L4", strcmp(b, "OKLAHOMA CITY") == 0, S(b));
    }

    std::printf("== H: here now (1c, Q1) ==\n");
    {
        check("H1", H({"-RA"}, 60, false) == Precip::None, "invalid observation");
        check("H2", H({}) == Precip::None, "no entries");
        check("H3", H({"-RA"}) == Precip::Rain, PName(H({"-RA"})));
        check("H4", H({"SN"}) == Precip::Snow, PName(H({"SN"})));
        check("H5", H({"BR"}) == Precip::None, PName(H({"BR"})));
        check("H6", H({"+TSRA"}) == Precip::Rain, PName(H({"+TSRA"})));
        check("H7", H({"-SHSN"}) == Precip::Snow, PName(H({"-SHSN"})));
        check("H8", H({"BR", "-DZ"}) == Precip::Rain, PName(H({"BR", "-DZ"})));
        check("H9", H({"RASN"}) == Precip::Snow, PName(H({"RASN"})));
        check("H10", H({"PL"}) == Precip::Snow, PName(H({"PL"})));
        check("H11", H({"IC"}) == Precip::Snow, PName(H({"IC"})));
        check("H12", H({"-SG"}) == Precip::Snow, PName(H({"-SG"})));
        check("H13", H({"FZRA"}) == Precip::Rain, PName(H({"FZRA"})));
        check("H14", H({"GR"}) == Precip::Rain, PName(H({"GR"})));
        check("H15", H({"GS"}) == Precip::Rain, PName(H({"GS"})));
        check("H16", H({"UP"}) == Precip::Rain, PName(H({"UP"})));
        check("H17", H({"TS"}) == Precip::None, PName(H({"TS"})));
        check("H18", H({"VCSH"}) == Precip::None, PName(H({"VCSH"})));
        check("H19", H({"VCSN"}) == Precip::None, PName(H({"VCSN"})));
        check("H20", H({"-RA", "SN"}) == Precip::Snow, PName(H({"-RA", "SN"})));
        check("H21", H({"-RA"}, 7201) == Precip::None, "2 h and 1 s old");
        check("H22", H({"-RA"}, 7200) == Precip::Rain, "exactly 2 h old");
    }

    std::printf("== T: the Tornado Warning's first line (Q3) ==\n");
    {
        Warn w = W("Tornado Warning", kNow + 100);
        w.tornadoDetection = "RADAR INDICATED";
        w.maxHailSize = "1.00";
        check("T1", Is(LineA(w), {"RADAR INDICATED", "HAIL 1 IN"}), Join(LineA(w)));
        w = W("Tornado Warning", kNow + 100);
        w.tornadoDetection = "OBSERVED";
        check("T2", Is(LineA(w), {"OBSERVED"}), Join(LineA(w)));
        w.tornadoDamageThreat = "CATASTROPHIC";
        w.maxHailSize = "2.75";
        check("T3", Is(LineA(w), {"TORNADO EMERGENCY", "HAIL 2.75 IN"}), Join(LineA(w)));
        w = W("Tornado Warning", kNow + 100);
        w.maxHailSize = "1.75";
        check("T4", Is(LineA(w), {"HAIL 1.75 IN"}), Join(LineA(w)));
        w = W("Tornado Warning", kNow + 100);
        w.tornadoDetection = "POSSIBLE";
        check("T5", Is(LineA(w), {}), Join(LineA(w)));
        w = W("Tornado Warning", kNow + 100);
        w.tornadoDamageThreat = "CONSIDERABLE";
        w.tornadoDetection = "RADAR INDICATED";
        check("T6", Is(LineA(w), {"RADAR INDICATED"}), Join(LineA(w)));
    }

    std::printf("== B: the first line for the other types (3b, 3e) ==\n");
    {
        Warn w = W("Severe Thunderstorm Warning", kNow + 100);
        w.maxHailSize = "1.00";
        w.maxWindGust = "60 MPH";
        check("B1", Is(LineA(w), {"HAIL 1 IN", "WIND 60 MPH"}), Join(LineA(w)));
        w = W("Severe Thunderstorm Warning", kNow + 100);
        w.maxHailSize = "1.75";
        check("B2", Is(LineA(w), {"HAIL 1.75 IN"}), Join(LineA(w)));
        w = W("Severe Thunderstorm Warning", kNow + 100);
        w.maxWindGust = "70 MPH";
        check("B3", Is(LineA(w), {"WIND 70 MPH"}), Join(LineA(w)));
        w = W("Severe Thunderstorm Warning", kNow + 100);
        check("B4", Is(LineA(w), {}), Join(LineA(w)));
        w = W("Flash Flood Warning", kNow + 100);
        w.flashFloodDetection = "RADAR INDICATED";
        check("B5", Is(LineA(w), {"RADAR INDICATED"}), Join(LineA(w)));
        w = W("Flash Flood Warning", kNow + 100);
        check("B6", Is(LineA(w), {}), Join(LineA(w)));
        w = W("Snow Squall Warning", kNow + 100);
        w.maxHailSize = "1.00";   // even with a field present, Snow Squall's first line is empty (3e)
        check("B7", Is(LineA(w), {}), Join(LineA(w)));
        char b[32];
        // Each call is made BEFORE check(): a function's arguments are evaluated in an unspecified order (GCC goes
        // right to left), so a detail read from the buffer in the same call printed the PREVIOUS value at 25bd7fa.
        bool r = HailText("0.75", b, sizeof(b));
        check("B8a", r && strcmp(b, "HAIL 0.75 IN") == 0, S(b));
        r = HailText("1.50", b, sizeof(b));
        check("B8b", r && strcmp(b, "HAIL 1.5 IN") == 0, S(b));
    }

    std::printf("== HL: headlines (Q5) ==\n");
    {
        check("HL1", strcmp(Headline(Choice{Screen::PrecipNow, Precip::Rain, -1}), "RAIN NOW") == 0);
        check("HL2", strcmp(Headline(Choice{Screen::PrecipNow, Precip::Snow, -1}), "SNOW NOW") == 0);
        check("HL3", strcmp(Headline(Choice{Screen::PrecipApproaching, Precip::None, -1}), "PRECIP WITHIN 15 MI") == 0,
              S(Headline(Choice{Screen::PrecipApproaching, Precip::None, -1})));
        check("HL4", Headline(Choice{Screen::Warning, Precip::None, 0})[0] == '\0' &&
                         Headline(Choice{Screen::Dry, Precip::None, -1})[0] == '\0', "none for Warning and Dry");
    }

    std::printf("== LP: label placement (Q6a) ==\n");
    {
        check("LP1", Placed({{100, 100, 30, 8, 1}, {100, 140, 30, 8, 5}}) == std::vector<int>({0, 1}), "both drawn");
        check("LP2", Placed({{100, 100, 30, 8, 1}, {110, 104, 30, 8, 2}}) == std::vector<int>({0}), "the nearer drawn");
        check("LP3", Placed({{100, 100, 20, 8, 1}, {115, 104, 20, 8, 2}, {130, 108, 20, 8, 3}}) == std::vector<int>({0, 2}),
              "A and C drawn");
        check("LP4", Placed({{210, 118, 30, 8, 1}}).empty(), "leaves the 112 px circle: skipped");
    }

    std::printf("== X: UNTIL (Q7) ==\n");
    {
        char b[32];
        auto U = [&](const char* iso) { b[0] = '\0'; return UntilText(iso, b, sizeof(b)); };
        bool r = U("2026-10-20T18:15:00-06:00");   // called before check(), as in B8
        check("X1", r && strcmp(b, "UNTIL 6:15 PM") == 0, S(b));
        r = U("2026-10-20T00:05:00-05:00");
        check("X2", r && strcmp(b, "UNTIL 12:05 AM") == 0, S(b));
        r = U("2026-10-20T12:00:00-05:00");
        check("X3", r && strcmp(b, "UNTIL 12:00 PM") == 0, S(b));
        r = U("2026-10-20T18:15:00Z");
        check("X4", r && strcmp(b, "UNTIL 6:15 PM") == 0, S(b));
        check("X5", !U("2026-10-20 18:15"), "malformed");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
