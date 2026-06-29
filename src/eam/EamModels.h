#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <ArduinoJson.h>

// Typed models for the valar-eam-feed backend plus the parsers that build them off the wire,
// and the request/result envelopes the background poller hands across the task boundary.
//
// Contract from the backend (all fields except a message's id/type/text are OPTIONAL; the
// firmware degrades gracefully when an endpoint is missing/empty). The firmware stays
// feed-agnostic: it only knows these normalized shapes, never the upstream HFGCS/ADS-B/SDR
// sources behind them. The sole exception is the device-side OpenSky command-post watch, whose
// raw state vectors are normalized into Abncp here (see ParseOpenSkyStates).
namespace eam {

// ----------------------------------------------------------------------------- messages
enum class MsgType : uint8_t { Eam, Skyking, Unknown };

// One EAM / Skyking broadcast. groups[] is the message broken into its phonetic groups,
// one per line for the monospace ticker; text is the raw string.
struct Msg {
    String id;                       // required; dedupe key
    MsgType type = MsgType::Unknown; // required
    String text;                     // required
    std::vector<String> groups;
    int charCount = 0;
    int groupCount = 0;
    String heardAt;                  // ISO-8601 (optional)
    long heardAtEpoch = 0;           // parsed from heardAt, 0 if absent/unparseable
    int frequencyKhz = 0;            // 0 = unknown
    String callsign;
    String codeword;                 // Skyking codeword (optional)
    bool malformed = false;          // "± copy?" -- a questionable copy
};

// --------------------------------------------------------------------------------- tempo
// One HFGCS frequency's count today (for the Tempo screen's activity strip).
struct FreqCount {
    int khz = 0;
    int count = 0;
};

struct Tempo {
    bool valid = false;
    int countToday = 0;
    float baselineMedian = 0.0f;
    float ratio = 0.0f;
    String level = "normal";         // "normal" | "elevated" | "high"
    int windowDays = 0;
    // Extended stats (all optional; carried on the same tempo/stats response). The firmware
    // degrades to the plain dial when they're absent.
    std::vector<FreqCount> byFreq;   // today's count per HFGCS frequency
    int byHour[24] = {0};            // today's count per UTC hour-of-day
    bool hasByHour = false;          // a by_hour array was present (even if all zero)
    int longestQuietMin = -1;        // longest gap today with no EAM, minutes (-1 = unknown)
};

// ----------------------------------------------------------------------------- codewords
struct Codeword {
    String codeword;
    String lastSeen;                 // ISO-8601 (optional)
    int count = 0;
};

// --------------------------------------------------------------------------- propagation
struct PropBand {
    String band;                     // e.g. "40m"
    String day;                      // qualitative condition strings from the source
    String night;
};

// Space-weather summary carried on /propagation (also available raw at /spaceweather). Drives the
// restrained banner on the propagation screen and the fourth ntfy trigger.
struct SpaceWeather {
    bool valid = false;
    int kp = -1;                     // planetary K index 0..9 (-1 = unknown)
    String rScale;                   // NOAA radio-blackout scale, e.g. "R2" ("" = none)
    String gScale;                   // NOAA geomagnetic-storm scale, e.g. "G2" ("" = none)
    String xrayClass;                // peak X-ray flux class, e.g. "X1.2" / "M5" ("" = unknown)
    bool hfDegraded = false;         // backend's "HF comms degraded right now" flag
    String note;                     // short human note (optional)
};

struct Propagation {
    bool valid = false;
    String updatedAt;
    int sfi = 0;                     // solar flux index
    int aIndex = 0;
    int kIndex = 0;
    int sunspots = 0;
    std::vector<PropBand> bands;
    std::vector<int> freqsKhz;       // HFGCS primary frequencies
    int suggestedKhz = 0;            // best HFGCS freq now (0 = none)
    String suggestedReason;
    String source;                   // credit line, e.g. "N0NBH"
    SpaceWeather space;              // optional; .valid only when the feed carries it
};

// ------------------------------------------------------------------------------- launches
struct Launch {
    String designation;              // e.g. "Glory Trip 252"
    String windowStart;              // ISO-8601
    String windowEnd;                // ISO-8601
    long windowStartEpoch = 0;       // parsed, 0 if absent
    String site;
    String source;
};

// ----------------------------------------------------------------------------- ABNCP watch
struct AbncpAircraft {
    String type;                     // "E-4B" / "E-6B" / "" (best-effort)
    String callsign;
    String hex;                      // ICAO24
    bool hasPos = false;
    double lat = 0.0;
    double lon = 0.0;
    String heardAt;                  // ISO-8601 (optional)
};

struct Abncp {
    bool valid = false;              // a successful check has populated this (even if empty)
    bool airborne = false;
    std::vector<AbncpAircraft> aircraft;
    String checkedAt;                // ISO-8601 (optional)
};

// ------------------------------------------------------------------------------- mil air
// Notable military aircraft up now (tankers, command, recon...) from /status/milair. Like the
// command-post watch, this only sees aircraft transmitting ADS-B.
struct MilAircraft {
    String type;                     // e.g. "KC-135" (best-effort)
    String callsign;
    String hex;                      // ICAO24
    bool hasPos = false;
    double lat = 0.0;
    double lon = 0.0;
    String heardAt;                  // ISO-8601 (optional)
    String category;                 // backend free-form tag, e.g. "tanker" (optional)
};

struct MilAir {
    bool valid = false;              // a successful fetch populated this (even if count 0)
    int count = 0;
    std::vector<MilAircraft> aircraft;
    String source;                   // credit line (optional)
};

// ----------------------------------------------------------------- poller request/result
// Which endpoint a worker fetch targets. ABNCP has two sources (backend vs device-side
// OpenSky) that produce the same Abncp shape via different URLs/parsers.
enum class EamEndpoint : uint8_t {
    Latest, Skykings, Tempo, Codewords, Propagation, Icbm, Abncp, AbncpOpenSky, MilAir
};

// Loop -> worker: a single GET to perform, fully built on the loop task. For AbncpOpenSky the
// worker first exchanges the user's OpenSky credentials for a bearer token (off-loop), so the
// id/secret travel here; they are never logged and never sent anywhere but OpenSky's auth host.
struct EamFetchRequest {
    EamEndpoint endpoint = EamEndpoint::Latest;
    String url;
    std::vector<std::pair<String, String>> params;
    std::vector<std::pair<String, String>> headers;
    bool needsOpenSkyToken = false;
    String openskyId;
    String openskySecret;
};

// Worker -> loop: the parsed payload (only the field matching `endpoint` is populated). ok is
// the HTTP-level outcome; an ok-but-empty result is a valid "no data", not a failure.
struct EamFetchResult {
    EamEndpoint endpoint = EamEndpoint::Latest;
    bool ok = false;
    std::vector<Msg> messages;       // Latest / Skykings
    Tempo tempo;
    std::vector<Codeword> codewords;
    int codewordWindowDays = 0;
    Propagation propagation;
    std::vector<Launch> launches;
    Abncp abncp;
    MilAir milair;
};

// -------------------------------------------------------------------------------- parsers
// All take an already-decoded ArduinoJson value and fill the typed struct, tolerating missing
// optional fields. Caps bound the retained vectors so a hostile/huge response can't exhaust RAM.
bool ParseMsg(JsonObjectConst o, Msg& out);                                    // false if id/type/text missing
void ParseMessages(JsonObjectConst root, std::vector<Msg>& out, size_t cap);   // reads root["messages"]
bool ParseTempo(JsonObjectConst root, Tempo& out);
void ParseCodewords(JsonObjectConst root, std::vector<Codeword>& out, int& windowDays, size_t cap);
bool ParsePropagation(JsonObjectConst root, Propagation& out);
void ParseLaunches(JsonObjectConst root, std::vector<Launch>& out, size_t cap);
bool ParseAbncpBackend(JsonObjectConst root, Abncp& out);                       // {base}/status/abncp shape
bool ParseOpenSkyStates(JsonObjectConst root, Abncp& out);                      // normalize states/all vectors
bool ParseMilAir(JsonObjectConst root, MilAir& out, size_t cap);               // {base}/status/milair shape

// Best-effort classification of an ABNCP aircraft from its hex/callsign (used by the OpenSky
// path, which has no type field). Returns "E-4B", "E-6B", or "".
String ClassifyAbncp(const String& hex, const String& callsign);

// Default ICAO24 seed for the OpenSky command-post watch: the four E-4B "Nightwatch" hexes.
// PLACEHOLDERS -- verify against a live tracker before relying on them. The user can edit the
// list in web config; allow adding E-6B by hex/callsign.
std::vector<String> DefaultAbncpWatch();

// Parse "YYYY-MM-DDThh:mm:ss" (UTC, trailing zone/fraction ignored) to a Unix epoch; 0 on failure.
long Iso8601ToEpoch(const String& s);

} // namespace eam
