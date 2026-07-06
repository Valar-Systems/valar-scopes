#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <math.h>
#include <ArduinoJson.h>

// Typed models for the Claudescope edition plus the parser that builds them off the wire, and the
// request/result envelopes the background poller hands across the task boundary. Mirrors the
// Seismic/Space models' shape (feed-agnostic structs + a free-function parser + a fetch envelope).
//
// Claudescope reads ONE endpoint: a small normalized JSON served by the user's "claudescope-sidecar"
// on their LAN (tools/claudescope-sidecar/). The sidecar holds the Claude OAuth token, reads the
// undocumented usage window state, and republishes it as the stable shape below -- so the device
// never sees the token and never talks to Anthropic directly. There is NO baked-in backend; the
// "cl-base-url" config (the sidecar's address) is required and empty by default. Expected body:
//
//   {
//     "plan": "Max (20x)",
//     "session":  { "pct": 34, "resets_at": 1751824000 },
//     "week_all": { "pct": 16, "resets_at": 1752000000 },
//     "week_models": [ { "label": "Fable", "pct": 2, "resets_at": 1752000000 } ],
//     "extra_usage": false,
//     "updated_at": 1751820000
//   }
//
// `pct` is 0..100 percent-used; `resets_at`/`updated_at` are Unix seconds (the sidecar converts the
// upstream ISO timestamps). All fields are best-effort; the screens degrade to "--" when a poll
// hasn't landed or a field is absent.
namespace claudescope {

// One usage limit window (a session cap or a weekly cap). `pct` is percent USED (so headroom is
// 100 - pct); `resetEpoch` is when it fully replenishes.
struct UsageWindow {
    bool  valid       = false;
    float pct         = NAN;  // 0..100 percent used
    long  resetEpoch  = 0;    // Unix seconds; 0 = unknown
    String label;             // model name for per-model weekly windows ("" for session/all)
};

// The full normalized usage snapshot the sidecar publishes.
struct UsageState {
    bool  valid = false;
    String planName;                        // e.g. "Max (20x)"
    UsageWindow session;                    // rolling ~5 h session cap
    UsageWindow weekAll;                    // weekly all-models cap
    std::vector<UsageWindow> weekModels;    // per-model weekly caps (e.g. Fable), bounded
    bool  extraUsage = false;               // usage-credits toggle is on
    long  updatedEpoch = 0;                 // when the sidecar last refreshed (Unix s)
};

// ------------------------------------------------------------------ poller request / result
// Single logical endpoint; the enum keeps parity with the sibling editions' envelope shape and
// leaves room for a second window source later.
enum class ClaudeEndpoint : uint8_t { Usage };

// Loop -> worker: a single request, fully built on the loop task.
struct ClaudeFetchRequest {
    ClaudeEndpoint endpoint = ClaudeEndpoint::Usage;
    String url;
    std::vector<std::pair<String, String>> params;
    std::vector<std::pair<String, String>> headers;
};

// Worker -> loop: the parsed snapshot. `ok` is the HTTP/parse-level outcome.
struct ClaudeFetchResult {
    ClaudeEndpoint endpoint = ClaudeEndpoint::Usage;
    bool ok = false;
    UsageState usage;
};

// -------------------------------------------------------------------------------- parser
// Read the sidecar's normalized shape into `out`, tolerating missing fields. The per-model weekly
// list is capped so a chatty sidecar can't grow the store unbounded.
void ParseUsage(JsonObjectConst root, UsageState& out, size_t maxModels = 6);

} // namespace claudescope
