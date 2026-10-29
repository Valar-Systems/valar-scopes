// include/DayNight.h, and its first test of include/SolarDim.h's SunElevationDeg (docs/weatherscope-stage3.md A11.4,
// A12.43, A12.45), held to 28 named checks and nothing else:
//   DN-E      SunElevationDeg against NOAA's Julian-century algorithm (tools/weatherscope/daynight_ref.py's fixture,
//             4,950 rows): max |diff| <= 0.02 deg, the maximum printed (predicted 0.002930 +- 1e-5);
//   DN-U1-8   SunElevationDeg's -0.833 deg crossing against USNO's transcribed sunrise and sunset, each within 1 min;
//   D1-D16    Decide's truth table;
//   Q1-Q3     three sequences: a dusk and a dawn (one switch each, at its band edge) and a hover (no switch).
// SY, the symbol scan, runs from run.sh (test/host/scan_daynight_symbols.sh). Its sabotages:
// python -I test/host/sabotage_daynight.py (6, each failing exactly its set).
//
//     test_daynight <test/fixtures/daynight>
//
// Exit 0 all pass, 1 any fail, 2 the rig (a fixture missing or short, or a check count other than 28).

// SolarDim.h uses M_PI, which the Arduino core provides on the board. MinGW's math.h hides it under -std=c++11
// (__STRICT_ANSI__) unless this is defined before the first include; glibc's g++ exposes it regardless. Set here, not
// in the shared header, which eight editions include.
#define _USE_MATH_DEFINES
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "DayNight.h"

namespace {

using daynight::Phase;

constexpr int kChecks = 28;
int checks = 0, failures = 0;

void check(const char* name, bool ok, const std::string& what)
{
    ++checks;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name, what.c_str());
}

const char* Name(Phase p) { return p == Phase::Day ? "Day" : p == Phase::Night ? "Night" : "Unknown"; }

// Days from 1970-01-01 to a civil date (Howard Hinnant's algorithm), so no time-zone library is involved.
long DaysFromCivil(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

int RigFail(const char* what)
{
    std::printf("FAIL: %s (the RIG, not a result)\n", what);
    return 2;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_daynight <test/fixtures/daynight>\n"); return 2; }
    const std::string dir = argv[1];

    // ---------------------------------------------------------------------------------------------- DN-E
    std::printf("== DN-E: SunElevationDeg against NOAA's Julian-century algorithm ==\n");
    {
        FILE* f = std::fopen((dir + "/noaa-elev.txt").c_str(), "r");
        if (!f) return RigFail("cannot read noaa-elev.txt");
        char line[256];
        int rows = 0;
        double worst = 0.0;
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            long long utc = 0;
            int lat = 0, lon = 0;
            long micro = 0;
            if (std::sscanf(line, "%lld %d %d %ld", &utc, &lat, &lon, &micro) != 4) { std::fclose(f); return RigFail("a malformed noaa-elev.txt row"); }
            const double got = SunElevationDeg(lat, lon, (time_t)utc);
            const double d = std::fabs(got - micro / 1e6);
            if (d > worst) worst = d;
            ++rows;
        }
        std::fclose(f);
        if (rows != 4950) return RigFail("noaa-elev.txt does not hold 4,950 rows");
        char t[128];
        std::snprintf(t, sizeof t, "%d rows, max |diff| %.6f deg (<= 0.02)", rows, worst);
        check("DN-E", worst <= 0.02, t);
    }

    // ---------------------------------------------------------------------------------------------- DN-U
    std::printf("== DN-U: the -0.833 deg crossing against USNO's sunrise and sunset (transcribed) ==\n");
    {
        FILE* f = std::fopen((dir + "/usno-2026.txt").c_str(), "r");
        if (!f) return RigFail("cannot read usno-2026.txt");
        char line[256];
        int n = 0;
        while (std::fgets(line, sizeof line, f)) {
            if (line[0] == '#' || line[0] == '\n') continue;
            int y = 0, mo = 0, d = 0, lat = 0, lon = 0, hh = 0, mm = 0;
            char phen[8] = {0};
            if (std::sscanf(line, "%d-%d-%d %d %d %7s %d:%d", &y, &mo, &d, &lat, &lon, phen, &hh, &mm) != 8) {
                std::fclose(f);
                return RigFail("a malformed usno-2026.txt row");
            }
            const bool rising = std::string(phen) == "Rise";
            const long long usno = (long long)DaysFromCivil(y, mo, d) * 86400 + hh * 3600 + mm * 60;
            // The first second at which the elevation is on the far side of the threshold, by bisection over USNO's
            // time +- 30 min. A bracket that does not hold is a failure of this row, not a crossing.
            auto above = [&](long long t) { return SunElevationDeg(lat, lon, (time_t)t) >= daynight::kThresholdDeg; };
            long long lo = usno - 1800, hi = usno + 1800;
            char name[16];   // room for any int: g++ 13's -Wformat-truncation cannot bound n
            std::snprintf(name, sizeof name, "DN-U%d", ++n);
            if (above(lo) == rising || above(hi) != rising) {
                check(name, false, std::string(phen) + ": no crossing within 30 min of USNO's");
                continue;
            }
            while (hi - lo > 1) {   // invariant: above(lo) != rising, above(hi) == rising
                const long long mid = lo + (hi - lo) / 2;
                (above(mid) == rising ? hi : lo) = mid;
            }
            const double dmin = (double)(hi - usno) / 60.0;
            char t[128];
            std::snprintf(t, sizeof t, "%04d-%02d-%02d %s USNO %02d:%02d, SunElevationDeg %+.2f min (within 1)", y, mo, d,
                          phen, hh, mm, dmin);
            check(name, std::fabs(dmin) <= 1.0, t);
        }
        std::fclose(f);
        if (n != 8) return RigFail("usno-2026.txt does not hold 8 events");
    }

    // ---------------------------------------------------------------------------------------------- DN-D
    std::printf("== DN-D: Decide's truth table (H = %.1f, threshold %.3f) ==\n", (double)daynight::kHysteresisDeg,
                (double)daynight::kThresholdDeg);
    {
        struct Row { const char* name; Phase prev; bool clock, loc; float elev; Phase want; };
        const Phase U = Phase::Unknown, D = Phase::Day, N = Phase::Night;
        const Row rows[16] = {
            {"D1", U, false, true, 10.0f, U},     {"D2", U, true, false, 10.0f, U},
            {"D3", D, false, true, -10.0f, D},    {"D4", N, true, false, 10.0f, N},
            {"D5", U, true, true, -0.833f, D},    {"D6", U, true, true, -0.834f, N},
            {"D7", U, true, true, -1.5f, N},      {"D8", U, true, true, 0.0f, D},
            {"D9", D, true, true, -1.832f, D},    {"D10", D, true, true, -1.834f, N},
            {"D11", N, true, true, 0.166f, N},    {"D12", N, true, true, 0.168f, D},
            {"D13", D, true, true, -0.5f, D},     {"D14", N, true, true, -0.5f, N},
            {"D15", D, true, true, 30.0f, D},     {"D16", N, true, true, -30.0f, N},
        };
        for (const Row& r : rows) {
            const Phase got = daynight::Decide(r.prev, r.clock, r.loc, r.elev);
            char t[128];
            std::snprintf(t, sizeof t, "prev %s, clock %s, location %s, %.3f deg: %s (want %s)", Name(r.prev),
                          r.clock ? "yes" : "no", r.loc ? "yes" : "no", (double)r.elev, Name(got), Name(r.want));
            check(r.name, got == r.want, t);
        }
    }

    // ---------------------------------------------------------------------------------------------- DN-Q
    std::printf("== DN-Q: a dusk, a dawn and a hover ==\n");
    {
        struct Seq { const char* name; Phase start; int switches; double at; };
        const Seq seqs[3] = {{"Q1", Phase::Day, 1, -1.84}, {"Q2", Phase::Night, 1, 0.17}, {"Q3", Phase::Day, 0, 0.0}};
        for (int s = 0; s < 3; ++s) {
            Phase p = seqs[s].start;
            int switches = 0;
            double at = 0.0;
            const int n = s < 2 ? 1001 : 200;
            for (int i = 0; i < n; ++i) {
                float e;
                if (s == 0) e = (float)(5.0 - i / 100.0);         // +5 down to -5 by 0.01
                else if (s == 1) e = (float)(-5.0 + i / 100.0);   // -5 up to +5
                else e = i % 2 == 0 ? -0.3f : -1.4f;               // inside the band, alternating
                const Phase q = daynight::Decide(p, true, true, e);
                if (q != p && switches++ == 0) at = e;
                p = q;
            }
            const bool atOk = seqs[s].switches == 0 || std::fabs(at - seqs[s].at) < 0.005;
            char t[128];
            if (switches) std::snprintf(t, sizeof t, "%d switch(es), the first at %.2f deg (want %d, at %.2f)", switches, at,
                                        seqs[s].switches, seqs[s].at);
            else std::snprintf(t, sizeof t, "0 switches (want %d)", seqs[s].switches);
            check(seqs[s].name, switches == seqs[s].switches && atOk, t);
        }
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    if (checks != kChecks) {
        std::printf("FAIL: %d checks ran, not %d (the RIG)\n", checks, kChecks);
        return 2;
    }
    return failures ? 1 : 0;
}
