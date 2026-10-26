#include "GameConfig.h"

#include <cstring>
#include <math.h>

namespace eam {

namespace {

// A non-negative INTEGER field. A float (the pre-ppm `weight` shape) is not one:
// is<uint32_t>() is false for 0.83, which is exactly the refusal wanted.
bool ReadU32(JsonVariantConst v, uint32_t& out)
{
    if (!v.is<uint32_t>()) return false;
    out = v.as<uint32_t>();
    return true;
}

} // namespace

bool ParseGameConfig(JsonObjectConst root, GameConfig& out)
{
    out = GameConfig();
    if (root.isNull()) return false;

    uint32_t epoch = 0;
    if (!ReadU32(root["epoch"], epoch)) return false;
    out.params.epoch = epoch;
    out.hold = root["hold"].is<bool>() && root["hold"].as<bool>();

    // decode.weights: ORDERED, integer ppm. Order is part of the derivation, so it
    // is copied exactly as served; an unknown class refuses the whole config.
    JsonArrayConst weights = root["decode"]["weights"].as<JsonArrayConst>();
    if (weights.isNull() || weights.size() == 0 || weights.size() > game::kMaxWeights) return false;
    for (JsonObjectConst w : weights) {
        const char* cls = w["class"] | "";
        game::MsgClass c;
        if (strcmp(cls, "nam") == 0) c = game::MsgClass::Nam;
        else if (strcmp(cls, "fdm") == 0) c = game::MsgClass::Fdm;
        else if (strcmp(cls, "execution") == 0) c = game::MsgClass::Execution;
        else return false;
        uint32_t ppm = 0;
        if (!ReadU32(w["ppm"], ppm)) return false;
        out.params.weights[out.params.n_weights++] = {c, ppm};
    }

    JsonObjectConst timing = root["timing"].as<JsonObjectConst>();
    JsonArrayConst tiers = timing["tOffset"].as<JsonArrayConst>();
    if (tiers.isNull() || tiers.size() == 0 || tiers.size() > game::kMaxTiers) return false;
    for (JsonObjectConst t : tiers) {
        const char* name = t["tier"] | "";
        game::Tier tier;
        if (strcmp(name, "normal") == 0) tier = game::Tier::Normal;
        else if (strcmp(name, "snap") == 0) tier = game::Tier::Snap;
        else return false;
        uint32_t ppm = 0, minS = 0, maxS = 0;
        if (!ReadU32(t["ppm"], ppm) || !ReadU32(t["minS"], minS) || !ReadU32(t["maxS"], maxS)) return false;
        out.params.tiers[out.params.n_tiers++] = {tier, ppm, minS, maxS};
    }

    if (!ReadU32(timing["ackCutoffS"], out.ackCutoffS)) return false;
    // Served from valar-eam-feed #71 on. Absent reads 0, which DrillPolicy treats as
    // "never": no auto-decode, and no clock is ever fresh enough to arm.
    ReadU32(timing["autoDecodeS"], out.autoDecodeS);
    ReadU32(timing["maxClockSyncAgeS"], out.maxClockSyncAgeS);

    // Seconds on the wire, microseconds in the drill. Rounded once, here.
    const float windowS = timing["executionWindowS"] | 0.0f;
    if (!(windowS > 0.0f)) return false;
    out.windowUs = (uint32_t)lroundf(windowS * 1e6f);
    const float bucketS = root["scoring"]["bucketS"] | 0.0f;
    out.bucketUs = bucketS > 0.0f ? (uint32_t)lroundf(bucketS * 1e6f) : 0;

    out.valid = true;
    return true;
}

} // namespace eam
