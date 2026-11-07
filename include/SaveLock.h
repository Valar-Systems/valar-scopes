#pragma once
// ONE GATE between a settings save and a touch-wedge reboot (docs/weatherscope-stage3.md A12.119, A12.120).
//
// The config page's /save, /location and /enroll-key handlers write NVS on the async web server's
// task, while the touch-wedge reboot runs on the loop task. Before this gate, a reboot could land
// mid-/save and leave a partial form -- one coordinate stored and not the other. Now:
//   - a save holds the gate across its writes (SaveScope, opened after the cross-origin check);
//   - the reboot takes the gate only when no save holds it (TryHoldForRestart), and never gives it
//     back: the restart ends the boot. A reboot that falls due during a save waits for the next loop;
//   - a save that arrives after that is REFUSED OUT LOUD: 503 and a plain sentence the page shows,
//     never a silent failure (ruling 4, A12.120). If the restart beats the refusal onto the wire,
//     the page's own "save failed - device unreachable" is what the user sees.
//
// The transitions are pure and host-tested (test_touch_wedge_cap.cpp, CR29-CR32). On a device they
// run inside a critical section; on the host there is one thread and no lock.
#include <stdint.h>
#include <stdio.h>
#if defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#endif

namespace savelock {

constexpr int REFUSED_STATUS = 503;
// The page's own "NOT SAVED - ..." style. No quote or backslash, so it sits in JSON unescaped.
constexpr const char* REFUSED_TEXT = "NOT SAVED - the scope is restarting. Try again in a minute.";

// How the endpoint answers: /save and /enroll-key read text, /location reads JSON.
enum class Shape : uint8_t { Text, Json };

struct Gate {
    uint8_t saving = 0;           // saves in progress (one task serves them, but count anyway)
    bool heldForRestart = false;  // taken by the reboot; never released
};

inline bool EnterSave(Gate& g)
{
    if (g.heldForRestart) return false;
    ++g.saving;
    return true;
}
inline void ExitSave(Gate& g)
{
    if (g.saving) --g.saving;
}
inline bool HoldForRestart(Gate& g)
{
    if (g.heldForRestart) return true;
    if (g.saving) return false;
    g.heldForRestart = true;
    return true;
}

// The device's one gate, and the lock around its transitions.
inline Gate& Global()
{
    static Gate g;
    return g;
}
#if defined(ESP_PLATFORM)
inline portMUX_TYPE& Mux()
{
    static portMUX_TYPE m = portMUX_INITIALIZER_UNLOCKED;
    return m;
}
struct Critical {
    Critical() { portENTER_CRITICAL(&Mux()); }
    ~Critical() { portEXIT_CRITICAL(&Mux()); }
};
#else
struct Critical {};
#endif

/// The touch-wedge reboot's side: true once the gate is the reboot's, for good.
inline bool TryHoldForRestart(Gate& g = Global())
{
    Critical c;
    (void)c;
    return HoldForRestart(g);
}

/// The refusal, on the request that asked. Request is AsyncWebServerRequest on a device, a
/// recording fake on the host; both take send(int, const char*, const char*).
template <class Request>
inline void SendRefusal(Request* request, Shape shape)
{
    if (shape == Shape::Json) {
        char body[128];
        snprintf(body, sizeof body, "{\"ok\":false,\"error\":\"%s\"}", REFUSED_TEXT);
        request->send(REFUSED_STATUS, "application/json", body);
    } else {
        request->send(REFUSED_STATUS, "text/plain", REFUSED_TEXT);
    }
}

/// A handler's save, admitted or refused. Admitted: the gate is held until the scope ends, on
/// every return path. Refused: the refusal has been sent, and the handler returns at once.
///     savelock::SaveScope<AsyncWebServerRequest> save(request, savelock::Shape::Text);
///     if (!save.Admitted()) return;
template <class Request>
class SaveScope {
public:
    SaveScope(Request* request, Shape shape, Gate& g = Global()) : g_(g)
    {
        {
            Critical c;
            (void)c;
            admitted_ = EnterSave(g_);
        }
        if (!admitted_) SendRefusal(request, shape);
    }
    ~SaveScope()
    {
        if (!admitted_) return;
        Critical c;
        (void)c;
        ExitSave(g_);
    }
    bool Admitted() const { return admitted_; }

private:
    SaveScope(const SaveScope&);
    SaveScope& operator=(const SaveScope&);
    Gate& g_;
    bool admitted_ = false;
};

} // namespace savelock
