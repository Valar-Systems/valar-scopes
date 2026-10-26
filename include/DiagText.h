#pragma once
// DIAG ONLY (docs/weatherscope-stage3.md A9.42): a text an app renders on request, for a web route to serve, so the
// route never reads an app's records from another task while the loop task is writing them.
//
//   the route:  n = Request(); wait, bounded, until Answer(n, &len) returns the text; copy it; check Answer(n) again
//               (a newer request may have replaced it during the copy); serve the copy.
//   the loop:   if (const uint32_t n = Pending()) Publish(n, text, len);   -- at most once a frame, on its own task.
//
// One buffer, in PSRAM, kept by main.cpp. A newer request supersedes an older one; the older route then times out and
// says so. Weatherscope is the only edition that defines the storage (main.cpp) and the route.
#include <cstddef>
#include <cstdint>

namespace diagtext {

constexpr size_t kCapacity = 32 * 1024;   // a 160-frame log, 8 builds and 64 writes come to about 16 KB

/// The route: ask for a fresh text. Returns the request's number (never 0).
uint32_t Request();
/// The loop: the newest unanswered request's number, or 0.
uint32_t Pending();
/// The loop: the text for request `n`, copied (truncated at kCapacity).
void Publish(uint32_t n, const char* text, size_t len);
/// The route: the text for request `n` once published, or nullptr. Valid until the next Publish.
const char* Answer(uint32_t n, size_t* len);

}  // namespace diagtext
