#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include "HttpRequestManager.h"

// Shared ntfy alert sender for the editions. Two properties the per-edition copies lacked:
//
// (1) DEFER, don't drop. The old SendNtfy dropped any alert that fired within 5 s of the
//     previous one -- but callers advance their edge-detection state unconditionally, so a
//     throttled alert was lost forever, and co-triggered alerts are common (a tsunami-flagged
//     big quake fires the big-quake alert, then the tsunami alert hits the throttle; a target
//     bird is usually also "notable"; the day's fastest pass is also a speeder). Here the second
//     alert is QUEUED and drained on a later tick, keeping the 5 s spacing between POSTs.
// (2) One place to fix, instead of seven identical copies.
//
// POSTs run on the loop task (Pump is called from the manager's Update) via the shared
// HttpRequestManager mutex -- the same discipline the radar uses for its ntfy alerts.
class NtfyAlerter {
public:
    void SetTopic(const String& topic) { topic_ = topic; }
    bool HasTopic() const { return !topic_.isEmpty(); }

    // Queue an alert (no-op with no topic). Never blocks and never sends here -- the manager's
    // Pump() drains the queue, so this is safe to call from CheckAlerts on the loop task.
    void Send(const String& title, const String& body, const String& tags, int priority) {
        if (topic_.isEmpty()) return;
        if (pending_.size() >= MAX_PENDING) {
            // Overflow (a storm of triggers): drop the OLDEST so the newest, most relevant
            // alerts still go out, and say so rather than truncating silently.
            Serial.println("[ntfy] pending queue full; dropping oldest alert");
            pending_.erase(pending_.begin());
        }
        pending_.push_back({title, body, tags, priority});
    }

    // Drain at most one queued alert, keeping >= THROTTLE_MS between POSTs. Call once per Update().
    void Pump(HttpRequestManager& http) {
        if (pending_.empty()) return;
        const unsigned long now = millis();
        // Wrap-safe: unsigned subtraction. lastMs_==0 means "never sent" -> send immediately.
        if (lastMs_ != 0 && now - lastMs_ < THROTTLE_MS) return;
        lastMs_ = now;

        const Alert a = pending_.front();
        pending_.erase(pending_.begin());
        const std::vector<std::pair<String, String>> headers = {
            {"Title", a.title}, {"Tags", a.tags}, {"Priority", String(a.priority)}
        };
        (void)http.Post(String("https://ntfy.sh/") + topic_, a.body, headers);
    }

private:
    static constexpr unsigned long THROTTLE_MS = 5000; // min spacing between POSTs (rate-limit ntfy.sh)
    static constexpr size_t MAX_PENDING = 8;           // bound the queue against a trigger storm

    struct Alert { String title, body, tags; int priority; };

    String topic_;                 // ntfy.sh topic; empty disables all alerts
    std::vector<Alert> pending_;   // FIFO of deferred alerts
    unsigned long lastMs_ = 0;     // millis() of the last POST (0 = none yet)
};
