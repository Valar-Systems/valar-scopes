#include "ClaudescopeModels.h"

namespace claudescope {

namespace {

// Read one {"pct": N, "resets_at": E} object into a UsageWindow. Tolerates either a numeric pct or
// a missing field (left NaN -> the screen shows "--"). `label` is optional (per-model windows).
void ReadWindow(JsonObjectConst obj, UsageWindow& out, const char* label = "")
{
    if (obj.isNull()) return;
    if (obj["pct"].is<float>() || obj["pct"].is<int>()) {
        float p = obj["pct"].as<float>();
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        out.pct = p;
    }
    out.resetEpoch = obj["resets_at"] | 0L;
    if (label && label[0]) out.label = label;
    else                   out.label = obj["label"] | "";
    out.valid = !isnan(out.pct);
}

} // namespace

void ParseUsage(JsonObjectConst root, UsageState& out, size_t maxModels)
{
    out = UsageState();
    if (root.isNull()) return;

    out.planName = root["plan"] | "";
    out.extraUsage = root["extra_usage"] | false;
    out.updatedEpoch = root["updated_at"] | 0L;

    ReadWindow(root["session"].as<JsonObjectConst>(), out.session);
    ReadWindow(root["week_all"].as<JsonObjectConst>(), out.weekAll);

    JsonArrayConst models = root["week_models"].as<JsonArrayConst>();
    if (!models.isNull()) {
        for (JsonObjectConst m : models) {
            if (out.weekModels.size() >= maxModels) break;
            UsageWindow w;
            ReadWindow(m, w);
            if (w.valid) out.weekModels.push_back(w);
        }
    }

    // A snapshot is "valid" if at least one window landed -- the screens read whichever exist.
    out.valid = out.session.valid || out.weekAll.valid || !out.weekModels.empty();
}

} // namespace claudescope
