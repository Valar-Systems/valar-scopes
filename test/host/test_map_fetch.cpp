// The Blipscope's map fetch, step A's host rows (docs/blipscope-map-client-steps.md): include/MapQuery.h and
// include/MapPolicy.h held to CA1-CA11, frozen before the code (ruled 2026-11-08 ~20:28Z), with the cases, CA2's three
// rows and the policy's open choices registered on 2026-11-11 before any of this was written. CA12-CA14 are
// check_map_fetch.py's. Its sabotages: python -I test/host/sabotage_map_fetch.py, each failing exactly its set.
//
//     test_map_fetch <repo root>
//
// The route's bounds and the cap are read from proxy/src/basemap/query.ts and budget.ts, the other side of the
// contract, never typed here. Exit 0 all pass, 1 any fail, 2 the rig (a file or constant unreadable, or a case count
// other than 42). Prints no coordinate, no id, no hash and no spread delay: the synthetic ids are made at run time.

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "MapPolicy.h"
#include "MapQuery.h"
#include "RadarZoom.h"
#include "RebootCause.h"

namespace {

constexpr int kCases = 42;
constexpr uint32_t T0 = 1000000;   // a fixed boot clock, ms

int cases = 0, failures = 0;
bool rig = false;

void check(const std::string& name, bool ok, const std::string& what)
{
    ++cases;
    if (!ok) ++failures;
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", name.c_str(), what.c_str());
}

std::string fmt(const char* f, ...)
{
    char s[512];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(s, sizeof s, f, ap);
    va_end(ap);
    return s;
}

// ---- the other side of the contract: the route's constants, read from its source ---------------------------------

std::string ReadText(const std::string& path)
{
    std::string t;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        std::printf("the RIG: cannot read %s\n", path.c_str());
        rig = true;
        return t;
    }
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) t.append(buf, n);
    std::fclose(f);
    return t;
}

bool IdentChar(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; }

// `NAME = <digits>` or `NAME = <digits> * <digits>`, numeric separators allowed, defined exactly once. Anything else is
// the rig: a constant this cannot read is not a constant it may assume.
double Constant(const std::string& text, const char* name)
{
    const std::string pat = std::string(name) + " = ";
    size_t found = std::string::npos;
    int count = 0;
    for (size_t at = text.find(pat); at != std::string::npos; at = text.find(pat, at + 1))
        if (at == 0 || !IdentChar(text[at - 1])) {
            ++count;
            found = at;
        }
    if (count != 1) {
        std::printf("the RIG: %s defined %d times\n", name, count);
        rig = true;
        return 0;
    }
    size_t i = found + pat.size();
    auto number = [&](double* v) {
        std::string d;
        while (i < text.size() && ((text[i] >= '0' && text[i] <= '9') || text[i] == '_'))
            if (text[i++] != '_') d += text[i - 1];
        if (d.empty()) return false;
        *v = std::strtod(d.c_str(), nullptr);
        return true;
    };
    double a = 0, b = 1;
    bool ok = number(&a);
    while (i < text.size() && text[i] == ' ') ++i;
    if (ok && i < text.size() && text[i] == '*') {
        ++i;
        while (i < text.size() && text[i] == ' ') ++i;
        ok = number(&b);
        while (i < text.size() && text[i] == ' ') ++i;
    }
    if (!ok || i >= text.size() || (text[i] != ',' && text[i] != ';')) {
        std::printf("the RIG: %s is not a number this reads\n", name);
        rig = true;
        return 0;
    }
    return a * b;
}

struct Route {
    double maxRKm, epsMin, epsMax, lrMin, lrMax, nMax, levelsMax, terrainMax, cap;
};

// ---- the radar's own ladder, as AircraftManager::Initialise makes it ------------------------------------------------
// distanceKm = units::ToKm (double); radLat clamped to [0.001, 2.0] degrees; rangeRadiusDisplay = units::FromKm, whose
// km argument and division are float (DisplayUnits.h); then radarzoom::BuildLadder((float)rangeRadiusDisplay).
// EVERY VALUE THE DEVICE HOLDS AS A FLOAT GOES THROUGH A volatile float (Q-MC13, 2026-11-11). An i686 host (MinGW 5.1)
// does float arithmetic on the x87 at 80 bits and may keep the excess bits across an assignment or a cast; then 50.5
// mi read a hair above 50.5 and the ladder gained a 50 mi step the device never has. A store to a volatile float rounds
// to float, and for + - x / that one rounding of the x87's result is exactly the device's float result.

struct Unit {
    const char* name;
    double factor;
};
const Unit kKm = { "km", 1.0 }, kMi = { "mi", 1.609344 }, kNmi = { "nmi", 1.852 };

radarzoom::Ladder RadarLadder(double distance, const Unit& u)
{
    const double distanceKm = distance * u.factor;
    double radLat = distanceKm / 111.0;
    if (radLat < 0.001) radLat = 0.001;
    if (radLat > 2.0) radLat = 2.0;
    volatile float km = (float)(radLat * 111.0);
    volatile float shown = u.factor == 1.0 ? km : km / (float)u.factor;
    const double rangeRadiusDisplay = shown;
    volatile float configured = (float)rangeRadiusDisplay;
    return radarzoom::BuildLadder(configured);
}

mapquery::Request Req(double distance, const Unit& u, int viewStep = -1)
{
    const radarzoom::Ladder l = RadarLadder(distance, u);
    mapquery::Request q;
    mapquery::Build(distance, u.factor, l, viewStep < 0 ? l.Top() : viewStep, &q);
    return q;
}

// ---- CA1-CA3: the request -------------------------------------------------------------------------------------------

void Ca1()
{
    struct Row {
        const char* name;
        double d;
        const Unit* u;
        const char* params;
        uint32_t cap;
    };
    const Row rows[] = {
        { "CA1", 100, &kMi,
          "r=162&eps=201168&layers=water,rivers,roads,places,terrain&levels=41239440:246,81808320:244,162275520:242",
          211408 },
        { "CA1b", 40, &kKm,
          "r=41&eps=50000&layers=water,rivers,roads,places,terrain&levels=10833333:260,25833333:248,41000000:246",
          222412 },
        { "CA1c", 25, &kMi,
          "r=42&eps=50292&layers=water,rivers,roads,places,terrain&levels=8851392:264,16898112:252,41239440:246",
          226508 },
        { "CA1d", 5, &kMi, "r=9&eps=10058&layers=water,rivers,roads,places,terrain&levels=8851392:264", 102472 },
        { "CA1e", 222, &kKm,
          "r=222&eps=277500&layers=water,rivers,roads,places,terrain&levels=25833333:248,50833333:244,223850000:242",
          212396 },
        { "CA1f", 0.1, &kMi, "r=1&eps=1000&layers=water,rivers,roads,places", 32768 },
    };
    for (const Row& r : rows) {
        const mapquery::Request q = Req(r.d, *r.u);
        const bool ok = std::strcmp(q.params, r.params) == 0 && q.capBytes == r.cap;
        check(r.name, ok, ok ? fmt("%g %s, cap %u", r.d, r.u->name, (unsigned)q.capBytes)
                             : fmt("got %s cap %u", q.params, (unsigned)q.capBytes));
    }
}

uint32_t RefMm(double km)   // the test's own mm, so a plant in the header's cannot move the expectation
{
    const double c = km < 222.0 ? km : 222.0;
    return (uint32_t)std::floor(c * 1e6 + 0.5);
}

void Ca2(const Route& rt)
{
    int configs = 0, badBounds = 0, badCover = 0, badLevels = 0, own = 0, none = 0, below = 0;
    std::string firstBounds, firstCover, firstLevels;
    for (const Unit* u : { &kKm, &kMi, &kNmi })
        for (int i = 1; i <= 2220; ++i) {
            const double d = i / 10.0;
            const radarzoom::Ladder l = RadarLadder(d, *u);
            mapquery::Request q;
            mapquery::Build(d, u->factor, l, l.Top(), &q);
            ++configs;
            // the route's bounds, each read from its source
            bool ok = q.rKm >= 1 && q.rKm <= rt.maxRKm && q.epsMm >= rt.epsMin && q.epsMm <= rt.epsMax &&
                      q.nLevels <= rt.levelsMax;
            double terrain = 0;
            for (int a = 0; a < q.nLevels; ++a) {
                const mapquery::Level& v = q.levels[a];
                ok = ok && v.radiusMm >= rt.lrMin && v.radiusMm <= rt.lrMax && v.n >= 1 && v.n <= rt.nMax;
                for (int b = 0; b < a; ++b) ok = ok && q.levels[b].radiusMm != v.radiusMm;
                terrain += 8.0 + (double)v.n * v.n;
            }
            ok = ok && terrain <= rt.terrainMax;
            ok = ok && ((std::strstr(q.params, "terrain") != nullptr) == (q.nLevels > 0));
            if (!ok && ++badBounds == 1) firstBounds = fmt("%g %s: %s", d, u->name, q.params);
            // r covers the view plus the 2-dp offset, unless 222 caps it
            if (q.rKm < 222 && q.rKm < q.rvMm / 1e6 + 0.555 && ++badCover == 1)
                firstCover = fmt("%g %s: r %d", d, u->name, q.rKm);
            // the radar's top three steps: each with its own level (pixel within 1 % of step/120), covering step + 0.785
            const int top3 = l.n < 3 ? l.n : 3;
            below += l.n - top3;
            for (int s = l.n - top3; s < l.n; ++s) {
                const double sKm = (double)l.step[s] * u->factor;
                const double px = RefMm(sKm) / 120.0;
                const double nOwn = 240 + 2 * std::ceil(800000.0 / px);
                const double rOwn = std::floor(nOwn * px / 2 + 0.5);
                if (!(nOwn <= 1024 && rOwn >= 1e6 && rOwn <= 2.5e8)) {
                    ++none;
                    continue;
                }
                const double want = sKm * 1e6 / 120;
                double smallest = -1;
                for (int a = 0; a < q.nLevels; ++a) {
                    const double p = 2.0 * q.levels[a].radiusMm / q.levels[a].n;
                    if (std::fabs(p - want) <= 0.01 * want && (smallest < 0 || q.levels[a].radiusMm < smallest))
                        smallest = q.levels[a].radiusMm;
                }
                if (smallest >= 0 && smallest >= (sKm < 222.0 ? sKm : 222.0) * 1e6 + 785000) {
                    ++own;
                } else if (++badLevels == 1) {
                    firstLevels = fmt("%g %s: step %d", d, u->name, s);
                }
            }
        }
    check("CA2-bounds", badBounds == 0,
          badBounds ? fmt("%d of %d configs out of bounds; first %s", badBounds, configs, firstBounds.c_str())
                    : fmt("%d configs within the route's bounds", configs));
    check("CA2-rcover", badCover == 0,
          badCover ? fmt("%d configs short; first %s", badCover, firstCover.c_str())
                   : fmt("%d configs: r covers the view + 0.555 km, or is 222", configs));
    check("CA2-levels", badLevels == 0,
          fmt("%d configs; %d steps with their own level; %d with none; %d below the top three%s", configs, own, none,
              below, badLevels ? fmt("; %d short, first %s", badLevels, firstLevels.c_str()).c_str() : ""));
}

void Ca3(const Route& rt)
{
    check("CA3", rt.cap == mapquery::kCapBytes,
          fmt("CAP_BYTES in budget.ts %.0f, the request's base %u", rt.cap, (unsigned)mapquery::kCapBytes));
}

// ---- CA4-CA11: the policy -------------------------------------------------------------------------------------------

mappolicy::Key KeyOf(const mapquery::Request& q, int32_t lat100 = 1234, int32_t lon100 = -5678)
{
    mappolicy::Key k;
    k.lat100 = lat100;
    k.lon100 = lon100;
    std::snprintf(k.params, sizeof k.params, "%s", q.params);
    return k;
}

const mappolicy::Key& KeyA()
{
    static const mappolicy::Key k = KeyOf(Req(100, kMi));
    return k;
}
const mappolicy::Key& KeyB()   // a radius change
{
    static const mappolicy::Key k = KeyOf(Req(50, kMi));
    return k;
}
const mappolicy::Key& KeyC()   // a location change
{
    static const mappolicy::Key k = KeyOf(Req(100, kMi), 1235, -5678);
    return k;
}

mappolicy::State Fresh()   // a boot that asks at once, holding key A
{
    mappolicy::State s;
    mappolicy::Begin(s, false, 0, T0);
    mappolicy::OnQuery(s, KeyA());
    return s;
}

bool Due(mappolicy::State& s, uint32_t t) { return mappolicy::ShouldFetch(s, true, t); }

// Not due one ms before `t0 + secs`, due at it.
bool WaitsExactly(mappolicy::State& s, uint32_t t0, uint32_t secs)
{
    return !Due(s, t0 + secs * 1000 - 1) && Due(s, t0 + secs * 1000);
}

void Ca4()
{
    for (int status : { 202, 429 }) {
        mappolicy::State s = Fresh();
        const bool atOnce = Due(s, T0);
        const uint32_t seq = mappolicy::OnSend(s, T0);
        const uint32_t reply = T0 + 2000;
        const mappolicy::Outcome o = mappolicy::OnResponse(s, seq, status, "15", mappolicy::Gate::NotRun, reply);
        const bool ok = atOnce && o == mappolicy::Outcome::Wait && WaitsExactly(s, reply, 15);
        check(fmt("CA4-%d", status), ok, "Retry-After 15: not due at +14,999 ms, due at +15,000");
    }
    bool ok = true;
    std::string bad;
    const struct {
        const char* v;
        uint32_t secs;
    } edges[] = { { "1", 1 }, { "3600", 3600 }, { "0015", 15 } };
    for (const auto& e : edges) {
        mappolicy::State s = Fresh();
        const uint32_t seq = mappolicy::OnSend(s, T0);
        const bool one = mappolicy::OnResponse(s, seq, 202, e.v, mappolicy::Gate::NotRun, T0) ==
                             mappolicy::Outcome::Wait &&
                         WaitsExactly(s, T0, e.secs);
        if (!one && bad.empty()) bad = std::string("\"") + e.v + "\"";
        ok = ok && one;
    }
    check("CA4-edges", ok, ok ? "\"1\", \"3600\", \"0015\": exactly 1, 3,600 and 15 s" : "wrong for " + bad);
}

bool RefusedWaits60(const char* v)
{
    mappolicy::State s = Fresh();
    const uint32_t seq = mappolicy::OnSend(s, T0);
    return mappolicy::OnResponse(s, seq, 202, v, mappolicy::Gate::NotRun, T0) ==
               mappolicy::Outcome::RetryAfterRefused &&
           WaitsExactly(s, T0, 60);
}

void Ca5()
{
    check("CA5-absent", RefusedWaits60(nullptr), "no Retry-After: refused, 60 s");
    const char* refused[] = { "", "0", "3601", "-5", "15.5", " 15", "15 ", "abc", "00015",
                              "Wed, 21 Oct 2015 07:28:00 GMT" };
    int bad = 0;
    std::string first;
    for (const char* v : refused)
        if (!RefusedWaits60(v) && ++bad == 1) first = std::string("\"") + v + "\"";
    check("CA5-values", bad == 0, bad ? fmt("%d of 10 not refused; first %s", bad, first.c_str())
                                      : "10 values refused, each 60 s");
}

void Ca6()
{
    for (int status : { 404, 422, 400 }) {
        mappolicy::State s = Fresh();
        uint32_t seq = mappolicy::OnSend(s, T0);
        bool ok = mappolicy::OnResponse(s, seq, status, nullptr, mappolicy::Gate::NotRun, T0) ==
                  mappolicy::Outcome::NoPackage;
        ok = ok && !Due(s, T0 + 60000) && !Due(s, T0 + 3600000) && !Due(s, T0 + 86400000);
        const uint32_t t = T0 + 86400000;
        ok = ok && mappolicy::OnQuery(s, KeyB()) && Due(s, t);   // a radius change: one attempt, at once
        seq = mappolicy::OnSend(s, t);
        ok = ok && mappolicy::OnResponse(s, seq, status, nullptr, mappolicy::Gate::NotRun, t) ==
                       mappolicy::Outcome::NoPackage;
        ok = ok && !Due(s, t + 3600000);
        check(fmt("CA6-%d", status), ok, "no package; nothing asked for 24 h; a radius change asks once");
    }
}

// Successive failures of `status`, each answered 1 s after its send at the moment it fell due: their waits must be
// exactly `secs` in order.
bool Backs(int status, const std::vector<uint32_t>& secs, mappolicy::State& s, uint32_t& t)
{
    bool ok = true;
    for (uint32_t w : secs) {
        ok = ok && Due(s, t);
        const uint32_t seq = mappolicy::OnSend(s, t);
        const uint32_t reply = t + 1000;
        mappolicy::OnResponse(s, seq, status, nullptr, mappolicy::Gate::NotRun, reply);
        ok = ok && WaitsExactly(s, reply, w);
        t = reply + w * 1000;
    }
    return ok;
}

void Ca7()
{
    const struct {
        const char* name;
        int status;
    } rows[] = { { "CA7-500", 500 }, { "CA7-503", 503 }, { "CA7-429", 429 }, { "CA7-transport", -1 } };
    for (const auto& r : rows) {
        mappolicy::State s = Fresh();
        uint32_t t = T0;
        check(r.name, Backs(r.status, { 60, 120 }, s, t), "two in a row: 60 s, then 120 s");
    }
    {
        mappolicy::State s = Fresh();
        bool ok = Due(s, T0);
        const uint32_t seq1 = mappolicy::OnSend(s, T0);
        ok = ok && !Due(s, T0 + 59999) && !Due(s, T0 + 60000);   // the second call ends it: a failed attempt
        ok = ok && WaitsExactly(s, T0 + 60000, 60);
        const uint32_t t2 = T0 + 120000;
        ok = ok && mappolicy::OnResponse(s, seq1, 200, nullptr, mappolicy::Gate::Passed, t2) ==
                       mappolicy::Outcome::Ignored && !s.held;
        mappolicy::OnSend(s, t2);
        ok = ok && !Due(s, t2 + 60000) && WaitsExactly(s, t2 + 60000, 120);
        check("CA7-timeout", ok, "unanswered 60 s: a failure, 60 s then 120 s; the late reply ignored");
    }
    {
        mappolicy::State s = Fresh();
        uint32_t t = T0;
        check("CA7-cap", Backs(500, { 60, 120, 240, 480, 960, 1920, 3600, 3600 }, s, t),
              "60, 120, 240, 480, 960, 1,920, 3,600, 3,600 s");
    }
    {
        mappolicy::State s = Fresh();
        uint32_t t = T0;
        bool ok = Backs(500, { 60, 120, 240 }, s, t);
        const uint32_t seq = mappolicy::OnSend(s, t);
        ok = ok && mappolicy::OnResponse(s, seq, 200, nullptr, mappolicy::Gate::Passed, t + 1000) ==
                       mappolicy::Outcome::Accepted;
        t += 2000;
        ok = ok && mappolicy::OnQuery(s, KeyB());
        ok = ok && Backs(500, { 60, 120 }, s, t);
        check("CA7-reset", ok, "three failures, a package, a radius change: 60 s, then 120 s");
    }
    for (int status : { 401, 403 }) {
        mappolicy::State s = Fresh();
        uint32_t t = T0;
        check(fmt("CA7b-%d", status), Backs(status, { 3600, 3600 }, s, t), "3,600 s, twice");
    }
}

void Ca8()
{
    const uint32_t H = 3600000;
    {
        mappolicy::State s = Fresh();
        const uint32_t seq = mappolicy::OnSend(s, T0);
        const bool ok = mappolicy::OnResponse(s, seq, 200, nullptr, mappolicy::Gate::Refused, T0) ==
                            mappolicy::Outcome::Refused &&
                        !s.held && WaitsExactly(s, T0, 60);
        check("CA8-first", ok, "one refusal: an error, 60 s, no package");
    }
    {
        mappolicy::State s = Fresh();
        uint32_t t = T0;
        bool ok = true;
        const mappolicy::Gate gates[] = { mappolicy::Gate::Refused, mappolicy::Gate::Refused, mappolicy::Gate::Passed };
        mappolicy::Outcome last = mappolicy::Outcome::Ignored;
        for (mappolicy::Gate g : gates) {
            ok = ok && Due(s, t);
            const uint32_t seq = mappolicy::OnSend(s, t);
            last = mappolicy::OnResponse(s, seq, 200, nullptr, g, t);
            t += H;
        }
        ok = ok && last == mappolicy::Outcome::Accepted && s.held && !Due(s, t);
        check("CA8-recover", ok, "two refusals, then a pass: held, and nothing more asked");
    }
    {
        mappolicy::State s = Fresh();
        uint32_t t = T0;
        bool ok = true;
        mappolicy::Outcome o[3];
        for (int i = 0; i < 3; ++i) {
            ok = ok && Due(s, t);
            const uint32_t seq = mappolicy::OnSend(s, t);
            o[i] = mappolicy::OnResponse(s, seq, 200, nullptr, mappolicy::Gate::Refused, t);
            t += H;
        }
        ok = ok && o[0] == mappolicy::Outcome::Refused && o[1] == mappolicy::Outcome::Refused &&
             o[2] == mappolicy::Outcome::NoPackage && !Due(s, t) && !Due(s, t + 24 * H);
        t += 24 * H;
        ok = ok && mappolicy::OnQuery(s, KeyB()) && Due(s, t);
        for (int i = 0; i < 2; ++i) {
            ok = ok && Due(s, t);
            const uint32_t seq = mappolicy::OnSend(s, t);
            ok = ok && mappolicy::OnResponse(s, seq, 200, nullptr, mappolicy::Gate::Refused, t) ==
                           mappolicy::Outcome::Refused;
            t += H;
        }
        check("CA8-stop", ok, "the third refusal stops; a radius change starts the count again");
    }
}

// Synthetic ids in the id's own shape, 16 hex digits, from splitmix64. Made here, never written as literals, never
// printed.
std::string SyntheticId(uint64_t i)
{
    uint64_t z = i * 0x9E3779B97F4A7C15ull + 0x5DEECE66Dull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", (unsigned long long)z);
    return b;
}

void Ca9()
{
    uint32_t d = 0;   // the first synthetic id whose D is at least 1 s, so "not due at D - 1 ms" is a real moment
    for (uint64_t i = 0; d == 0; ++i) d = mappolicy::SpreadS(SyntheticId(i).c_str());
    const struct {
        const char* name;
        uint8_t cause;
        bool followsUpdate;
        bool spreads;
    } rows[] = {
        { "CA9-ota", rebootcause::NONE, true, true },
        { "CA9-nightly", rebootcause::OTA_CHECK, false, true },
        { "CA9-netwedge", rebootcause::NET_WEDGE, false, true },
        { "CA9-poweron", rebootcause::NONE, false, false },
        { "CA9-firstrun", rebootcause::NONE, false, false },
        { "CA9-config", rebootcause::NONE, false, false },
        { "CA9-touchwedge", rebootcause::TOUCH_WEDGE, false, false },
    };
    for (const auto& r : rows) {
        mappolicy::State s;
        mappolicy::Begin(s, mappolicy::Spreads(r.cause, r.followsUpdate), d, T0);
        mappolicy::OnQuery(s, KeyA());
        const bool ok = r.spreads ? WaitsExactly(s, T0, d) : Due(s, T0);
        check(r.name, ok, r.spreads ? "waits D, then asks" : "asks at once");
    }
    const int kIds = 10000;
    int bins[12] = { 0 };
    double sum = 0;
    bool inRange = true;
    for (int i = 0; i < kIds; ++i) {
        const uint32_t v = mappolicy::SpreadS(SyntheticId((uint64_t)i).c_str());
        inRange = inRange && v < mappolicy::kSpreadS;
        sum += v;
        if (v < 7200) ++bins[v / 600];
    }
    const double mean = sum / kIds;
    int lo = kIds, hi = 0;
    for (int b : bins) {
        lo = b < lo ? b : lo;
        hi = b > hi ? b : hi;
    }
    const double each = kIds / 12.0;
    const bool ok = inRange && std::fabs(mean - 3600) <= 72 && lo >= each * 0.85 && hi <= each * 1.15;
    check("CA9-dist", ok, fmt("10,000 synthetic ids: all in [0, 7,200) %s; mean %.1f; ten-minute bins %d-%d", inRange ? "yes" : "NO",
                              mean, lo, hi));
}

void Ca10()
{
    for (int loc = 0; loc < 2; ++loc) {
        const mappolicy::Key& other = loc ? KeyC() : KeyB();
        bool ok = true;
        {   // a held package
            mappolicy::State s = Fresh();
            const uint32_t seq = mappolicy::OnSend(s, T0);
            ok = ok && mappolicy::OnResponse(s, seq, 200, nullptr, mappolicy::Gate::Passed, T0) ==
                           mappolicy::Outcome::Accepted;
            ok = ok && mappolicy::OnQuery(s, other) && !s.held && Due(s, T0 + 1);
        }
        {   // inside a spread wait
            mappolicy::State s;
            mappolicy::Begin(s, true, 3600, T0);
            ok = ok && !mappolicy::OnQuery(s, KeyA()) && !Due(s, T0);
            ok = ok && mappolicy::OnQuery(s, other) && Due(s, T0);
        }
        {   // inside a backoff wait
            mappolicy::State s = Fresh();
            const uint32_t seq = mappolicy::OnSend(s, T0);
            mappolicy::OnResponse(s, seq, 500, nullptr, mappolicy::Gate::NotRun, T0);
            ok = ok && !Due(s, T0) && mappolicy::OnQuery(s, other) && Due(s, T0);
        }
        check(loc ? "CA10-location" : "CA10-change", ok,
              "the package dropped and asked at once: held, in a spread wait, in a backoff wait");
    }
    {
        const radarzoom::Ladder l = RadarLadder(100, kMi);
        const mapquery::Request top = Req(100, kMi);
        mappolicy::State s = Fresh();
        const uint32_t seq = mappolicy::OnSend(s, T0);
        bool ok = mappolicy::OnResponse(s, seq, 200, nullptr, mappolicy::Gate::Passed, T0) ==
                  mappolicy::Outcome::Accepted;
        for (int v = 0; v <= l.Top(); ++v) {
            const mapquery::Request q = Req(100, kMi, v);
            ok = ok && std::strcmp(q.params, top.params) == 0 && q.capBytes == top.capBytes;
            ok = ok && !mappolicy::OnQuery(s, KeyOf(q)) && s.held && !Due(s, T0 + 1000);
        }
        check("CA10-zoom", ok, fmt("%d view steps: one request, one key, nothing asked", l.n));
    }
}

void Ca11()
{
    mappolicy::State s = Fresh();
    const uint32_t seq = mappolicy::OnSend(s, T0);
    bool ok = mappolicy::OnQuery(s, KeyB());
    ok = ok && mappolicy::OnResponse(s, seq, 200, nullptr, mappolicy::Gate::Passed, T0 + 3000) ==
                   mappolicy::Outcome::Dropped;
    ok = ok && !s.held && Due(s, T0 + 3000);
    check("CA11", ok, "an answer to the config before a change is dropped; the new one asks at once");
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: test_map_fetch <repo root>\n");
        return 2;
    }
    const std::string root = argv[1];
    const std::string query = ReadText(root + "/proxy/src/basemap/query.ts");
    const std::string budget = ReadText(root + "/proxy/src/basemap/budget.ts");
    if (rig) return 2;
    Route rt;
    rt.maxRKm = Constant(query, "MAX_R_KM");
    rt.epsMin = Constant(query, "EPS_MIN_MM");
    rt.epsMax = Constant(query, "EPS_MAX_MM");
    rt.lrMin = Constant(query, "LEVEL_R_MIN_MM");
    rt.lrMax = Constant(query, "LEVEL_R_MAX_MM");
    rt.nMax = Constant(query, "LEVEL_N_MAX");
    rt.levelsMax = Constant(query, "LEVELS_MAX");
    rt.terrainMax = Constant(query, "TERRAIN_BYTES_MAX");
    rt.cap = Constant(budget, "CAP_BYTES");
    if (rig) return 2;
    std::printf("the route (query.ts, budget.ts): r <= %.0f km; eps %.0f-%.0f mm; level R %.0f-%.0f mm, N <= %.0f, "
                "at most %.0f, terrain <= %.0f B; CAP_BYTES %.0f\n",
                rt.maxRKm, rt.epsMin, rt.epsMax, rt.lrMin, rt.lrMax, rt.nMax, rt.levelsMax, rt.terrainMax, rt.cap);
    std::printf("CA1 the default and five more\n");
    Ca1();
    std::printf("CA2 every configured radius, 0.1-222 by 0.1, in km, mi and nmi\n");
    Ca2(rt);
    std::printf("CA3 the cap\n");
    Ca3(rt);
    std::printf("CA4 Retry-After\n");
    Ca4();
    std::printf("CA5 a refused Retry-After\n");
    Ca5();
    std::printf("CA6 no package\n");
    Ca6();
    std::printf("CA7 the backoff, and CA7b the credential hour\n");
    Ca7();
    std::printf("CA8 the gate\n");
    Ca8();
    std::printf("CA9 F6's spread\n");
    Ca9();
    std::printf("CA10 a change, and a zoom\n");
    Ca10();
    std::printf("CA11 an answer to an older config\n");
    Ca11();
    std::printf("%d checks, %d failures\n", cases, failures);
    if (cases != kCases) {
        std::printf("the RIG: %d cases, not %d\n", cases, kCases);
        return 2;
    }
    return failures ? 1 : 0;
}
