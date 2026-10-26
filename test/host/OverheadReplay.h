#pragma once
// A host replay of overhead EDGES (the first frame of a pass inside the radius) through the firmware's
// own pure rules -- configmigration::MigrateOverheadSwitch, overhead::SwitchOn, DecideCard, PushFires and
// HaStateFollowsSwitch -- for a v16 configuration migrated to rev 7. Used by test_overhead_replay.cpp
// (synthetic edges, in run.sh) and by the workstation replay of real captured edges, which is never
// committed. It models the WIRING (which predicate each consumer asks); bench check (c) checks the wiring.
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "../../include/ConfigMigration.h"
#include "../../include/OverheadPolicy.h"

namespace ovhreplay {

struct Prefs {   // records every write, like test_config_migration's
    std::map<std::string, std::string> kv;
    std::vector<std::string> writes;
    bool isKey(const char* k) const { return kv.count(k) != 0; }
    std::string getString(const char* k, const char* def) const { auto it = kv.find(k); return it == kv.end() ? std::string(def) : it->second; }
    void putString(const char* k, const char* v) { kv[k] = v; writes.push_back(k); }
};

struct V16 { const char* lookup; const char* lookupAlert; bool topicSet; bool mqttOn; };   // nullptr = key absent
struct Edge { uint32_t tMs; bool idle; };

struct Result {
    std::string ovhOn, lookupAlertAfter;   // "(absent)" when the key is absent
    bool on = false, box = false;
    int edges = 0, rings = 0, cards = 0, pushes = 0, haStateRises = 0, haEvents = 0;
};

inline Result Replay(const V16& v, const std::vector<Edge>& edges)
{
    Prefs p;
    if (v.lookup) p.kv["lookup"] = v.lookup;
    if (v.lookupAlert) p.kv["lookup-alert"] = v.lookupAlert;
    configmigration::MigrateOverheadSwitch(p);

    Result r;
    r.ovhOn = p.isKey("ovh-on") ? p.kv["ovh-on"] : std::string("(absent)");
    r.lookupAlertAfter = p.isKey("lookup-alert") ? p.kv["lookup-alert"] : std::string("(absent)");
    r.on = overhead::SwitchOn(p.isKey("ovh-on") ? p.kv["ovh-on"].c_str() : nullptr);    // the firmware's load
    r.box = p.isKey("lookup-alert") && p.kv["lookup-alert"] == "true";

    bool anyCard = false;
    uint32_t lastCard = 0;
    for (const Edge& e : edges) {
        ++r.edges;
        if (r.on) ++r.rings;                                              // the ring: showOverhead && IsOverhead
        const uint32_t sinceCard = anyCard ? e.tMs - lastCard : 0xFFFFFFFFu;
        if (overhead::DecideCard(r.on, e.idle ? overhead::CARD_IDLE_MS : 0, true, false, sinceCard) == overhead::Card::Show) {
            ++r.cards;
            anyCard = true;
            lastCard = e.tMs;
        }
        if (overhead::PushFires(r.on, r.box, v.topicSet)) ++r.pushes;
        if (v.mqttOn && overhead::HaStateFollowsSwitch(r.on)) ++r.haStateRises;   // each edge is its own episode here
        if (v.mqttOn) ++r.haEvents;                                       // per-aircraft events: never gated on the switch
    }
    return r;
}

} // namespace ovhreplay
