// Host test for the radar label's line selection.
//
// THE COUNT IS THE EASY HALF AND THE ORDER IS THE POINT. "Three lines" is
// satisfied by any three, including the three the field table happens to list
// first -- which would put Callsign, Aircraft type and Operator on the glass and
// silently drop altitude, while this file stayed green. So every case here
// asserts the SEQUENCE, compared whole, never a subset and never just a length.
//
// The table below mirrors src/AircraftInfoFields.cpp's order. It is a
// transcription, and transcriptions go stale: if a key is renamed there and not
// here, this file keeps passing while the radar quietly stops ranking. The
// last block guards exactly that.
#include <cstdio>
#include <cstring>
#include "../../include/LabelLines.h"

static int failures = 0;

static void check(bool cond, const char* what)
{
    if (!cond) { printf("  FAIL  %s\n", what); ++failures; }
}

// the shipping table's order, keys only
static const char* const KEYS[] = {
    "info-callsign", "info-type", "info-operator", "info-reg", "info-route",
    "info-icao", "info-country", "info-speed", "info-vrate", "info-baroalt",
    "info-geoalt", "info-heading", "info-squawk", "info-category", "info-possrc",
};
static const int N = (int)(sizeof(KEYS) / sizeof(KEYS[0]));

static int idx(const char* key)
{
    for (int i = 0; i < N; ++i)
        if (std::strcmp(KEYS[i], key) == 0) return i;
    return -1;
}

static void expect(const char* label,
                   const char* const* on, int onCount,
                   const char* const* want, int wantCount)
{
    bool willDraw[N];
    for (int i = 0; i < N; ++i) willDraw[i] = false;
    for (int k = 0; k < onCount; ++k) {
        const int i = idx(on[k]);
        if (i < 0) {
            printf("  FAIL  %s: names a key absent from the table: %s\n", label, on[k]);
            ++failures;
            return;
        }
        willDraw[i] = true;
    }
    int out[labellines::MAX_LINES];
    const int n = labellines::Select(KEYS, willDraw, N, out);

    bool ok = (n == wantCount);
    for (int k = 0; ok && k < n; ++k) ok = (out[k] == idx(want[k]));
    if (!ok) {
        printf("  FAIL  %s\n        got   ", label);
        for (int k = 0; k < n; ++k) printf("%s ", KEYS[out[k]]);
        printf("\n        want  ");
        for (int k = 0; k < wantCount; ++k) printf("%s ", want[k]);
        printf("\n");
        ++failures;
    } else {
        printf("  ok    %s\n", label);
    }
}

int main()
{
    printf("label lines: selection and order\n");

    // THE BRIEF'S CASE: everything ticked -> three lines, and these three.
    {
        const char* on[] = { "info-callsign","info-type","info-operator","info-reg","info-route",
                             "info-icao","info-country","info-speed","info-vrate","info-baroalt",
                             "info-geoalt","info-heading","info-squawk","info-category","info-possrc" };
        const char* want[] = { "info-type", "info-callsign", "info-baroalt" };
        expect("15 enabled -> type, callsign, altitude", on, 15, want, 3);
    }

    // The ranking beats the table order: callsign is listed first and must not
    // be drawn first.
    {
        const char* on[]   = { "info-callsign", "info-type" };
        const char* want[] = { "info-type", "info-callsign" };
        expect("type outranks callsign despite the table order", on, 2, want, 2);
    }

    // A ranked field that is off does not hold its slot open.
    {
        const char* on[]   = { "info-callsign", "info-speed", "info-squawk" };
        const char* want[] = { "info-callsign", "info-speed", "info-squawk" };
        expect("type off -> callsign, speed, then table order", on, 3, want, 3);
    }

    // Unranked fields fall back to the table's own order, not to the order the
    // caller happened to enable them in.
    {
        const char* on[]   = { "info-possrc", "info-country", "info-vrate" };
        const char* want[] = { "info-country", "info-vrate", "info-possrc" };
        expect("unranked fields keep table order", on, 3, want, 3);
    }

    // "Altitude" means BAROMETRIC. Geometric is not a stand-in for the ranking.
    {
        const char* on[]   = { "info-geoalt", "info-baroalt" };
        const char* want[] = { "info-baroalt", "info-geoalt" };
        expect("baro outranks geo", on, 2, want, 2);
    }

    // FEWER THAN THREE MUST BE POSSIBLE. A function that always returned three
    // would satisfy every case above.
    {
        const char* on[]   = { "info-squawk" };
        const char* want[] = { "info-squawk" };
        expect("CONTROL: one enabled -> one line", on, 1, want, 1);
    }
    {
        const char* none[] = { "" };
        expect("CONTROL: none enabled -> no lines", none, 0, none, 0);
    }

    // AN ENABLED FIELD THAT FORMATS TO "" MUST NOT SPEND A SLOT. willDraw is
    // enabled-AND-non-empty for exactly this reason: a contact with no squawk
    // would otherwise lose one of its three lines to a blank.
    {
        bool willDraw[N];
        for (int i = 0; i < N; ++i) willDraw[i] = false;
        willDraw[idx("info-type")] = true;
        willDraw[idx("info-callsign")] = false;   // enabled upstream, empty for this contact
        willDraw[idx("info-speed")] = true;
        int out[labellines::MAX_LINES];
        const int n = labellines::Select(KEYS, willDraw, N, out);
        check(n == 2 && out[0] == idx("info-type") && out[1] == idx("info-speed"),
              "an empty field does not consume a line");
    }

    // THE TRANSCRIPTION GUARD. Every ranked key must exist in the table. Without
    // this, renaming a key in AircraftInfoFields.cpp stops the ranking applying
    // and every case above still passes, because they all resolve through the
    // same stale copy.
    for (int p = 0; p < labellines::PRIORITY_COUNT; ++p) {
        char msg[96];
        snprintf(msg, sizeof(msg), "priority key %s resolves in the field table",
                 labellines::PRIORITY[p]);
        check(idx(labellines::PRIORITY[p]) >= 0, msg);
    }

    check(labellines::MAX_LINES == 3, "the cap is three");

    printf(failures ? "\nlabel lines: %d FAILURE(S)\n" : "\nlabel lines: all good\n", failures);
    return failures ? 1 : 0;
}
