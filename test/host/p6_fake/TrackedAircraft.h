#pragma once
// P6's host stand-in for src/models/TrackedAircraft.h (test/host/test_p6_labels.cpp): ONLY the members the info
// fields' formatters read. The host build copies src/AircraftInfoFields.{h,cpp} next to this file as
// models/TrackedAircraft.h, so the REAL formatters and the REAL LabelKey compile against it. A formatter that reads a
// member missing here fails to compile -- add it to the lists below, and P6-COVER then moves it too, because the
// struct and the test's mutations are both generated from these lists.
#include <Arduino.h>

#define P6_STATE_STRINGS(X) X(icao24) X(callsign) X(originCountry) X(squawk)
#define P6_STATE_FLOATS(X) X(baroAltitude) X(velocity) X(trueTrack) X(verticalRate) X(geoAltitude)
#define P6_STATE_INTS(X) X(positionSource) X(category)
#define P6_TOP_STRINGS(X) X(typeCode) X(operatorName) X(registration) X(routeOrigin) X(routeDest)

struct P6State {
#define P6_S(m) String m;
#define P6_F(m) float m = 0.0f;
#define P6_I(m) int m = 0;
    P6_STATE_STRINGS(P6_S)
    P6_STATE_FLOATS(P6_F)
    P6_STATE_INTS(P6_I)
};

struct TrackedAircraft {
    P6State state;
    P6_TOP_STRINGS(P6_S)
#undef P6_S
#undef P6_F
#undef P6_I
};
