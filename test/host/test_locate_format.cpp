// "Use my location": the helper's formatted position must be accepted by the FIRMWARE's parser.
//
// The helper (proxy/src/locatepage.ts) formats the position in JavaScript; the device parses
// what lands in the boxes with include/CoordParse.h on Save. The input is taken from the other
// side of that contract: test/fixtures/locate-format.txt is written by
// proxy/scripts/locate-fixture.mjs from the helper's own formatCoord (CI fails if it is
// stale), and every line must parse here and land within 0.00005 of its source position.
//
// CONTROL: a line with latitude 90.0001 is appended in-test and must be REJECTED, so this test
// proves it can fail.
//
// Usage: test_locate_format <fixture>     Exit: 0 ok, 1 a line failed, 2 blind (no lines read)
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#include "../../include/CoordParse.h"

static int failures = 0;

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: test_locate_format <fixture>\n"); return 2; }
    std::printf("locate helper format -> CoordParse::SplitPair\n");
    std::ifstream f(argv[1]);
    std::vector<std::string> lines;
    for (std::string l; std::getline(f, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty() && l[0] != '#') lines.push_back(l);
    }
    if (lines.size() < 8) { std::printf("  BLIND: read %zu fixture lines from %s\n", lines.size(), argv[1]); return 2; }

    for (const std::string& l : lines) {
        const size_t a = l.find('|'), b = l.find('|', a + 1);
        const std::string pair = l.substr(0, a);
        const double srcLat = std::stod(l.substr(a + 1, b - a - 1)), srcLon = std::stod(l.substr(b + 1));
        double la = NAN, lo = NAN;
        const bool ok = CoordParse::SplitPair(String(pair.c_str()), la, lo);
        const bool close = ok && std::fabs(la - srcLat) <= 0.00005 + 1e-9 && std::fabs(lo - srcLon) <= 0.00005 + 1e-9;
        if (!close) { std::printf("  FAIL: \"%s\" -> ok=%d %.6f, %.6f (source %.7f, %.7f)\n", pair.c_str(), ok, la, lo, srcLat, srcLon); ++failures; }
    }
    std::printf("  %zu helper-formatted positions parsed by the firmware\n", lines.size());

    // CONTROL: the parser can refuse -- an out-of-range latitude must not be accepted.
    double la = 0, lo = 0;
    if (CoordParse::SplitPair(String("90.0001, 10.0000"), la, lo)) { std::printf("  FAIL: CONTROL lat 90.0001 was accepted\n"); ++failures; }

    std::printf(failures ? "FAILED (%d)\n" : "ok\n", failures);
    return failures ? 1 : 0;
}
