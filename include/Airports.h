#pragma once

// Baked table of major airports for the radar's airport overlay (config
// "airports"). Header-only and included ONLY by AircraftManager.cpp, so the
// editions' build_src_filter arrangement is untouched and no edition links the
// data. ~250 entries x 13 B is ~3 KB of flash.
//
// Coordinates are airport reference points rounded to ~0.01 deg (~1 km) --
// plenty at radar scale, where a pixel is a few hundred metres at the tightest
// zoom. Codes are IATA (what a spotter recognises). The set is the world's
// major internationals plus large regionals; it is deliberately curated rather
// than exhaustive -- a missing strip is a data gap, a WRONG position is a bug,
// so only well-verified fields belong here. The cloud path can supersede this
// table later with a /v1/airports endpoint (see ROADMAP.md) without touching
// the draw code.

#include <cstdint>

struct Airport {
    float lat;
    float lon;
    char code[4]; // IATA, NUL-terminated
};

static constexpr Airport AIRPORTS[] = {
    // --- United States ---
    { 33.64f,  -84.43f, "ATL" }, { 33.94f, -118.41f, "LAX" }, { 41.98f,  -87.90f, "ORD" },
    { 32.90f,  -97.04f, "DFW" }, { 39.86f, -104.67f, "DEN" }, { 40.64f,  -73.78f, "JFK" },
    { 37.62f, -122.38f, "SFO" }, { 47.45f, -122.31f, "SEA" }, { 36.08f, -115.15f, "LAS" },
    { 28.43f,  -81.31f, "MCO" }, { 25.79f,  -80.29f, "MIA" }, { 35.21f,  -80.94f, "CLT" },
    { 33.44f, -112.01f, "PHX" }, { 29.98f,  -95.34f, "IAH" }, { 42.36f,  -71.01f, "BOS" },
    { 44.88f,  -93.22f, "MSP" }, { 42.21f,  -83.35f, "DTW" }, { 26.07f,  -80.15f, "FLL" },
    { 40.69f,  -74.17f, "EWR" }, { 40.78f,  -73.87f, "LGA" }, { 40.79f, -111.98f, "SLC" },
    { 38.85f,  -77.04f, "DCA" }, { 38.95f,  -77.46f, "IAD" }, { 39.18f,  -76.67f, "BWI" },
    { 32.73f, -117.19f, "SAN" }, { 27.98f,  -82.53f, "TPA" }, { 30.19f,  -97.67f, "AUS" },
    { 36.12f,  -86.68f, "BNA" }, { 41.79f,  -87.75f, "MDW" }, { 32.85f,  -96.85f, "DAL" },
    { 21.32f, -157.92f, "HNL" }, { 45.59f, -122.60f, "PDX" }, { 38.75f,  -90.37f, "STL" },
    { 35.88f,  -78.79f, "RDU" }, { 29.65f,  -95.28f, "HOU" }, { 38.70f, -121.59f, "SMF" },
    { 29.99f,  -90.26f, "MSY" }, { 37.36f, -121.93f, "SJC" }, { 33.68f, -117.87f, "SNA" },
    { 37.72f, -122.22f, "OAK" }, { 39.30f,  -94.71f, "MCI" }, { 29.53f,  -98.47f, "SAT" },
    { 40.49f,  -80.23f, "PIT" }, { 41.41f,  -81.85f, "CLE" }, { 39.05f,  -84.66f, "CVG" },
    { 40.00f,  -82.89f, "CMH" }, { 39.72f,  -86.29f, "IND" }, { 42.95f,  -87.90f, "MKE" },
    { 30.49f,  -81.69f, "JAX" }, { 26.54f,  -81.76f, "RSW" }, { 26.68f,  -80.10f, "PBI" },
    { 61.17f, -149.99f, "ANC" }, { 20.90f, -156.43f, "OGG" }, { 35.04f, -106.61f, "ABQ" },
    { 34.20f, -118.36f, "BUR" }, { 34.06f, -117.60f, "ONT" }, { 39.87f,  -75.24f, "PHL" },
    { 35.04f,  -89.98f, "MEM" }, { 35.39f,  -97.60f, "OKC" }, { 36.20f,  -95.89f, "TUL" },
    { 41.30f,  -95.89f, "OMA" }, { 43.56f, -116.22f, "BOI" }, { 47.62f, -117.53f, "GEG" },
    { 32.12f, -110.94f, "TUS" }, { 31.81f, -106.38f, "ELP" }, { 42.94f,  -78.73f, "BUF" },
    { 43.12f,  -77.67f, "ROC" }, { 43.11f,  -76.11f, "SYR" }, { 42.75f,  -73.80f, "ALB" },
    { 41.94f,  -72.68f, "BDL" }, { 41.73f,  -71.43f, "PVD" }, { 42.93f,  -71.44f, "MHT" },
    { 43.65f,  -70.31f, "PWM" }, { 44.47f,  -73.15f, "BTV" }, { 37.51f,  -77.32f, "RIC" },
    { 36.89f,  -76.20f, "ORF" }, { 32.90f,  -80.04f, "CHS" }, { 32.13f,  -81.20f, "SAV" },
    { 34.90f,  -82.22f, "GSP" }, { 35.81f,  -83.99f, "TYS" }, { 38.17f,  -85.74f, "SDF" },
    { 39.90f,  -84.22f, "DAY" }, { 42.88f,  -85.52f, "GRR" }, { 41.53f,  -93.66f, "DSM" },
    { 34.73f,  -92.22f, "LIT" }, { 37.65f,  -97.43f, "ICT" }, { 38.81f, -104.70f, "COS" },
    // Western-US scheduled-service regionals (the table's first user-reported
    // gap was RDM, 2026-07-16):
    { 44.25f, -121.15f, "RDM" }, { 44.12f, -123.21f, "EUG" }, { 42.37f, -122.87f, "MFR" },
    { 39.50f, -119.77f, "RNO" }, { 36.78f, -119.72f, "FAT" }, { 46.26f, -119.12f, "PSC" },
    { 45.78f, -111.15f, "BZN" }, { 46.92f, -114.09f, "MSO" },
    // Bend Municipal (KBDN; FAA "BDN", no IATA) -- busy GA field, user request:
    { 44.09f, -121.20f, "BDN" },
    // --- Canada ---
    { 43.68f,  -79.63f, "YYZ" }, { 49.19f, -123.18f, "YVR" }, { 45.47f,  -73.74f, "YUL" },
    { 51.13f, -114.01f, "YYC" }, { 53.31f, -113.58f, "YEG" }, { 45.32f,  -75.67f, "YOW" },
    { 49.91f,  -97.24f, "YWG" }, { 44.88f,  -63.51f, "YHZ" },
    // --- Mexico / Central America / Caribbean ---
    { 19.44f,  -99.07f, "MEX" }, { 21.04f,  -86.87f, "CUN" }, { 20.52f, -103.31f, "GDL" },
    { 25.78f, -100.11f, "MTY" }, { 18.44f,  -66.00f, "SJU" }, {  9.07f,  -79.38f, "PTY" },
    {  9.99f,  -84.21f, "SJO" }, { 22.99f,  -82.41f, "HAV" }, { 18.50f,  -77.91f, "MBJ" },
    { 18.57f,  -68.36f, "PUJ" }, { 18.43f,  -69.67f, "SDQ" },
    // --- South America ---
    { -23.43f, -46.47f, "GRU" }, { -22.81f, -43.25f, "GIG" }, { -15.87f, -47.92f, "BSB" },
    { -23.63f, -46.66f, "CGH" }, { -34.82f, -58.54f, "EZE" }, { -34.56f, -58.42f, "AEP" },
    { -33.39f, -70.79f, "SCL" }, { -12.02f, -77.11f, "LIM" }, {   4.70f, -74.15f, "BOG" },
    {   6.16f, -75.42f, "MDE" }, {  -0.13f, -78.36f, "UIO" }, { -34.84f, -56.03f, "MVD" },
    // --- United Kingdom & Ireland ---
    { 51.47f,   -0.46f, "LHR" }, { 51.15f,   -0.18f, "LGW" }, { 51.89f,    0.24f, "STN" },
    { 51.87f,   -0.37f, "LTN" }, { 51.51f,    0.05f, "LCY" }, { 53.35f,   -2.28f, "MAN" },
    { 55.95f,   -3.37f, "EDI" }, { 55.87f,   -4.43f, "GLA" }, { 52.45f,   -1.75f, "BHX" },
    { 51.38f,   -2.72f, "BRS" }, { 55.04f,   -1.69f, "NCL" }, { 53.43f,   -6.24f, "DUB" },
    { 51.84f,   -8.49f, "ORK" }, { 52.70f,   -8.92f, "SNN" }, { 54.66f,   -6.22f, "BFS" },
    // --- Western / Central Europe ---
    { 49.01f,    2.55f, "CDG" }, { 48.72f,    2.38f, "ORY" }, { 43.66f,    7.22f, "NCE" },
    { 45.73f,    5.08f, "LYS" }, { 43.44f,    5.22f, "MRS" }, { 43.63f,    1.37f, "TLS" },
    { 52.31f,    4.76f, "AMS" }, { 50.90f,    4.48f, "BRU" }, { 49.63f,    6.20f, "LUX" },
    { 50.03f,    8.57f, "FRA" }, { 48.35f,   11.79f, "MUC" }, { 52.36f,   13.50f, "BER" },
    { 53.63f,   10.00f, "HAM" }, { 51.29f,    6.77f, "DUS" }, { 50.87f,    7.14f, "CGN" },
    { 48.69f,    9.19f, "STR" }, { 47.46f,    8.55f, "ZRH" }, { 46.24f,    6.11f, "GVA" },
    { 47.60f,    7.52f, "BSL" }, { 48.11f,   16.57f, "VIE" }, { 50.10f,   14.26f, "PRG" },
    { 52.17f,   20.97f, "WAW" }, { 50.08f,   19.78f, "KRK" }, { 54.38f,   18.47f, "GDN" },
    { 47.44f,   19.26f, "BUD" }, { 44.57f,   26.09f, "OTP" }, { 42.70f,   23.41f, "SOF" },
    { 37.94f,   23.94f, "ATH" }, { 40.52f,   22.97f, "SKG" },
    // --- Iberia / Italy ---
    { 40.47f,   -3.56f, "MAD" }, { 41.30f,    2.08f, "BCN" }, { 39.55f,    2.74f, "PMI" },
    { 36.67f,   -4.50f, "AGP" }, { 38.28f,   -0.56f, "ALC" }, { 39.49f,   -0.48f, "VLC" },
    { 37.42f,   -5.90f, "SVQ" }, { 43.30f,   -2.91f, "BIO" }, { 38.77f,   -9.13f, "LIS" },
    { 41.24f,   -8.68f, "OPO" }, { 37.01f,   -7.97f, "FAO" }, { 41.80f,   12.24f, "FCO" },
    { 45.63f,    8.72f, "MXP" }, { 45.45f,    9.28f, "LIN" }, { 45.67f,    9.70f, "BGY" },
    { 45.51f,   12.35f, "VCE" }, { 40.88f,   14.29f, "NAP" }, { 37.47f,   15.07f, "CTA" },
    { 38.18f,   13.10f, "PMO" }, { 44.53f,   11.29f, "BLQ" }, { 43.68f,   10.39f, "PSA" },
    // --- Nordics / Baltics ---
    { 60.19f,   11.10f, "OSL" }, { 60.29f,    5.22f, "BGO" }, { 63.46f,   10.92f, "TRD" },
    { 58.88f,    5.63f, "SVG" }, { 59.65f,   17.92f, "ARN" }, { 57.66f,   12.28f, "GOT" },
    { 55.62f,   12.65f, "CPH" }, { 60.32f,   24.96f, "HEL" }, { 63.98f,  -22.62f, "KEF" },
    { 56.92f,   23.97f, "RIX" }, { 59.41f,   24.83f, "TLL" }, { 54.63f,   25.29f, "VNO" },
    // --- Turkey / Eastern Europe ---
    { 41.26f,   28.74f, "IST" }, { 40.90f,   29.32f, "SAW" }, { 36.90f,   30.80f, "AYT" },
    { 40.13f,   33.00f, "ESB" }, { 38.29f,   27.16f, "ADB" }, { 34.88f,   33.62f, "LCA" },
    { 35.86f,   14.48f, "MLA" }, { 45.74f,   16.07f, "ZAG" }, { 42.56f,   18.27f, "DBV" },
    { 44.82f,   20.29f, "BEG" }, { 41.42f,   19.72f, "TIA" }, { 50.34f,   30.89f, "KBP" },
    { 55.97f,   37.41f, "SVO" }, { 55.41f,   37.90f, "DME" }, { 59.80f,   30.26f, "LED" },
    // --- Middle East ---
    { 25.25f,   55.36f, "DXB" }, { 24.44f,   54.65f, "AUH" }, { 25.27f,   51.61f, "DOH" },
    { 29.24f,   47.97f, "KWI" }, { 26.27f,   50.63f, "BAH" }, { 24.96f,   46.70f, "RUH" },
    { 21.68f,   39.16f, "JED" }, { 26.47f,   49.80f, "DMM" }, { 23.59f,   58.28f, "MCT" },
    { 31.72f,   35.99f, "AMM" }, { 33.82f,   35.49f, "BEY" }, { 32.01f,   34.89f, "TLV" },
    { 30.11f,   31.41f, "CAI" }, { 27.18f,   33.80f, "HRG" }, { 35.42f,   51.15f, "IKA" },
    // --- Africa ---
    { -26.14f,  28.25f, "JNB" }, { -33.96f,  18.60f, "CPT" }, { -29.61f,  31.12f, "DUR" },
    {  -1.32f,  36.93f, "NBO" }, {   8.98f,  38.80f, "ADD" }, {   6.58f,   3.32f, "LOS" },
    {   9.01f,   7.26f, "ABV" }, {   5.61f,  -0.17f, "ACC" }, {  33.37f,  -7.59f, "CMN" },
    {  31.61f,  -8.04f, "RAK" }, {  36.85f,  10.23f, "TUN" }, {  36.69f,   3.22f, "ALG" },
    {   0.04f,  32.44f, "EBB" }, {  -6.88f,  39.20f, "DAR" }, { -20.43f,  57.68f, "MRU" },
    // --- East Asia ---
    { 35.55f,  139.78f, "HND" }, { 35.76f,  140.39f, "NRT" }, { 34.43f,  135.24f, "KIX" },
    { 34.79f,  135.44f, "ITM" }, { 34.86f,  136.81f, "NGO" }, { 42.78f,  141.69f, "CTS" },
    { 33.59f,  130.45f, "FUK" }, { 26.20f,  127.65f, "OKA" }, { 37.46f,  126.44f, "ICN" },
    { 37.56f,  126.79f, "GMP" }, { 35.18f,  128.94f, "PUS" }, { 33.51f,  126.49f, "CJU" },
    { 40.08f,  116.58f, "PEK" }, { 39.51f,  116.41f, "PKX" }, { 31.14f,  121.81f, "PVG" },
    { 31.20f,  121.34f, "SHA" }, { 23.39f,  113.30f, "CAN" }, { 22.64f,  113.81f, "SZX" },
    { 29.72f,  106.64f, "CKG" }, { 25.10f,  102.93f, "KMG" }, { 34.44f,  108.75f, "XIY" },
    { 30.78f,  114.21f, "WUH" }, { 30.23f,  120.43f, "HGH" }, { 24.54f,  118.13f, "XMN" },
    { 22.31f,  113.91f, "HKG" }, { 22.15f,  113.59f, "MFM" }, { 25.08f,  121.23f, "TPE" },
    { 25.07f,  121.55f, "TSA" }, { 22.58f,  120.35f, "KHH" },
    // --- South / Southeast Asia ---
    { 14.51f,  121.02f, "MNL" }, { 10.31f,  123.98f, "CEB" }, {  1.36f,  103.99f, "SIN" },
    {  2.75f,  101.71f, "KUL" }, {  5.30f,  100.28f, "PEN" }, { 13.69f,  100.75f, "BKK" },
    { 13.91f,  100.60f, "DMK" }, {  8.11f,   98.31f, "HKT" }, { 18.77f,   98.96f, "CNX" },
    { 10.82f,  106.66f, "SGN" }, { 21.21f,  105.80f, "HAN" }, { 16.04f,  108.20f, "DAD" },
    { 11.55f,  104.84f, "PNH" }, { 16.91f,   96.13f, "RGN" }, { -6.13f,  106.66f, "CGK" },
    { -8.75f,  115.17f, "DPS" }, { -7.38f,  112.79f, "SUB" }, { 28.57f,   77.10f, "DEL" },
    { 19.09f,   72.87f, "BOM" }, { 13.20f,   77.71f, "BLR" }, { 12.99f,   80.17f, "MAA" },
    { 17.24f,   78.43f, "HYD" }, { 22.65f,   88.45f, "CCU" }, { 10.15f,   76.40f, "COK" },
    { 15.38f,   73.83f, "GOI" }, { 23.07f,   72.63f, "AMD" }, { 18.58f,   73.92f, "PNQ" },
    { 23.84f,   90.40f, "DAC" }, {  7.18f,   79.88f, "CMB" }, { 27.70f,   85.36f, "KTM" },
    { 33.55f,   72.83f, "ISB" }, { 24.90f,   67.16f, "KHI" }, { 31.52f,   74.40f, "LHE" },
    { 41.26f,   69.28f, "TAS" }, { 43.35f,   77.04f, "ALA" },
    // --- Oceania ---
    { -33.95f, 151.18f, "SYD" }, { -37.67f, 144.84f, "MEL" }, { -27.38f, 153.12f, "BNE" },
    { -31.94f, 115.97f, "PER" }, { -34.94f, 138.53f, "ADL" }, { -35.31f, 149.19f, "CBR" },
    { -28.16f, 153.50f, "OOL" }, { -16.88f, 145.75f, "CNS" }, { -42.84f, 147.51f, "HBA" },
    { -12.41f, 130.88f, "DRW" }, { -37.01f, 174.79f, "AKL" }, { -41.33f, 174.81f, "WLG" },
    { -43.49f, 172.53f, "CHC" }, { -45.02f, 168.74f, "ZQN" }, { -17.76f, 177.44f, "NAN" },
    { 13.48f,  144.80f, "GUM" },
};

static constexpr size_t AIRPORT_COUNT = sizeof(AIRPORTS) / sizeof(AIRPORTS[0]);
