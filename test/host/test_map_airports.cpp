// Step C's scope addition (docs/blipscope-map-client-steps.md, "Step C's scope addition: airportsGen moves only when the
// airports CONTENT changes", and its amendment): include/MapAirportsKey.h held to its 13 registered rows.
//     test_map_airports
// Exit 0 all pass, 1 any fail, 2 the rig (a case count other than 13). Prints row names only: the list below is made up
// (codes AAA-DDD, positions at two decimals at most), and no row prints a value.
//
// Every row but AP-BOOT sets the held hash explicitly, so S-AP-BOOT (Held's default changed) can reach AP-BOOT alone, and
// AP-EMPTY holds the hash of an EMPTY LIST rather than kEmptyHash, so S-AP-EMPTYEQ (kEmptyHash changed) reaches
// AP-EMPTYEQ alone. The composite "cleared, then an empty arrival, is no change" is AP-EMPTYEQ and AP-EMPTY together.
#include <cmath>
#include <cstdio>
#include <string>

#include "MapAirportsKey.h"

namespace {

struct Row {   // CloudFeed::CloudAirport's four fields, as the names' obstacles read them
    float lat;
    float lon;
    char code[5];
    char kind;
};

const Row kList[3] = {{40.5f, -73.25f, "AAA", 'L'}, {41.0f, -72.5f, "BBB", 'M'}, {39.75f, -74.0f, "CCC", 'S'}};

int g_checks = 0, g_fails = 0;

void Check(bool ok, const std::string& name)
{
    ++g_checks;
    if (ok) {
        std::printf("  ok   %s\n", name.c_str());
    } else {
        ++g_fails;
        std::printf("  FAIL %s\n", name.c_str());
    }
}

struct List {
    Row r[4];
    size_t n;
};

List Base()
{
    List l = List();   // value-initialized: the unused fourth row is zeros, never read uninitialized
    for (size_t i = 0; i < 3; ++i) l.r[i] = kList[i];
    l.n = 3;
    return l;
}

uint32_t Hash(const List& l) { return mapairports::ContentHash(l.r, l.n); }

uint32_t EmptyListHash() { return mapairports::ContentHash<Row>(nullptr, 0); }

// held at `from`, the arrival `to`: what Changed answers
bool ChangedFrom(uint32_t from, uint32_t to)
{
    mapairports::Held held;
    held.h = from;
    return mapairports::Changed(held, to);
}

}  // namespace

int main()
{
    const List base = Base();

    Check(!ChangedFrom(EmptyListHash(), EmptyListHash()), "AP-EMPTY");
    Check(!ChangedFrom(Hash(base), Hash(Base())), "AP-SAME");
    {
        Row none[1] = {kList[0]};
        Check(EmptyListHash() == mapairports::kEmptyHash && mapairports::ContentHash(none, 0) == mapairports::kEmptyHash,
              "AP-EMPTYEQ");
    }
    {
        mapairports::Held boot;   // the only default-constructed Held in the suite
        Check(boot.h == mapairports::kEmptyHash, "AP-BOOT");
    }
    Check(ChangedFrom(mapairports::kEmptyHash, Hash(base)), "AP-FIRST");
    Check(ChangedFrom(Hash(base), EmptyListHash()), "AP-GONE");
    {
        List l = Base();
        l.r[1].lat = std::nextafter(l.r[1].lat, 1000.0f);   // one float ulp
        Check(ChangedFrom(Hash(base), Hash(l)), "AP-LAT");
    }
    {
        List l = Base();
        l.r[1].lon = std::nextafter(l.r[1].lon, 1000.0f);
        Check(ChangedFrom(Hash(base), Hash(l)), "AP-LON");
    }
    {
        List l = Base();
        l.r[1].code[1] = 'D';   // one code byte
        Check(ChangedFrom(Hash(base), Hash(l)), "AP-CODE");
    }
    {
        List l = Base();
        l.r[2].kind = 'M';   // S to M
        Check(ChangedFrom(Hash(base), Hash(l)), "AP-KIND");
    }
    {
        List l = Base();
        const Row t = l.r[0];
        l.r[0] = l.r[1];
        l.r[1] = t;
        Check(ChangedFrom(Hash(base), Hash(l)), "AP-ORDER");
    }
    {
        List l = Base();
        const Row extra = {38.5f, -75.0f, "DDD", 'S'};
        l.r[3] = extra;
        l.n = 4;
        Check(ChangedFrom(Hash(base), Hash(l)), "AP-COUNT");
    }
    {
        mapairports::Held held;
        held.h = Hash(base);                   // the list the map last drew from
        held.h = mapairports::kEmptyHash;      // the config save's clear (AircraftManager.cpp, the clear site)
        Check(mapairports::Changed(held, Hash(base)), "AP-CLEAR");   // the same list, re-fetched after the clear
    }

    std::printf("%d checks, %d failures\n", g_checks, g_fails);
    if (g_checks != 13) {
        std::printf("the RIG: %d cases, not 13\n", g_checks);
        return 2;
    }
    return g_fails ? 1 : 0;
}
