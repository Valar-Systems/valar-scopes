#pragma once
// The detail card's vertical layout, as numbers a host test can read (#670; docs/card-nophoto-fix.md).
//
// These were literals inside AircraftManager::DrawDetailCard. They moved here UNCHANGED, and the card calls
// them, so the arithmetic that decides which rows a card can draw is graded on the host
// (test/host/test_card_layout.cpp) rather than trapped in a 300-line draw function. The move may not change a
// pixel: blipscope-s3-128 and blipscope-pro-s3-21 are rebuilt before and after it, and every differing byte must
// fall in the five rebuild regions (D-ref, in docs/card-nophoto-fix.md).
//
// Pure C++11 with no Arduino, LovyanGFX or ESP-IDF, so test/host/run.sh's include gate holds.
namespace cardlayout {

// ---- the slot layout: no decoded photo (a silhouette, or "Loading photo...") ----
constexpr int SLOT_SILHOUETTE_Y = 76;    // DrawAircraftSilhouette's centre, once the photo is known to be none
constexpr int SLOT_NOPHOTO_Y    = 120;   // "No photo available"
constexpr int SLOT_LOADING_Y    = 74;    // "Loading photo..."
constexpr int SLOT_TITLE_Y      = 136;   // the callsign, text size 2
constexpr int SLOT_FLOW_Y       = 162;   // the first flowed row

// ---- the data page: tapped over from a full-bleed photo ----
constexpr int DATA_TITLE_Y      = 36;    // the callsign, text size 2
constexpr int DATA_CAPTION_Y    = 58;    // "representative photo", for a stock photo
constexpr int DATA_FLOW_Y       = 70;    // the first flowed row

// ---- the flow's floor ----
constexpr int FOLLOW_RESERVE    = 74;    // kept clear of the Follow control: a control is reserved, a hint is not
constexpr int BEZEL_MARGIN      = 12;    // no row draws off the round panel

// The lowest y a flowed row's glyphs may reach.
constexpr int HardBottomY(int screen, bool followCtl)
{
    return followCtl ? screen - FOLLOW_RESERVE : screen - BEZEL_MARGIN;
}

// THE rule DrawDetailCard's line() applies, in one place: a row at y draws iff its glyphs end at or above the floor.
constexpr bool RowFits(int y, int bottom, int fontH)
{
    return y + fontH <= bottom;
}

// How many rows fontH tall, lineH apart, fit from y0 down to bottom. An impossible pitch (lineH <= 0) returns -1,
// never a plausible count.
constexpr int RowsThatFit(int y0, int bottom, int fontH, int lineH)
{
    return lineH <= 0 ? -1 : !RowFits(y0, bottom, fontH) ? 0 : (bottom - fontH - y0) / lineH + 1;
}

// THE FIX (#670). A card's identity is its route, type and operator: three rows. The slot layout spends the top of
// the card on a silhouette and a mid-card title, which leaves its flow 162 px down. On a 240 px panel with the Follow
// control offered, the floor is 166, so not one row fits and the card showed only its callsign. Identity outranks
// the silhouette (the slot's own promise: "with full telemetry beneath it").
constexpr int MIN_IDENTITY_ROWS = 3;

// Whether the slot layout must compact: true exactly when its flow cannot hold the identity rows. The compact slot
// drops the silhouette and lays out as the data page does: the title at DATA_TITLE_Y, the slot's caption ("Loading
// photo..." / "No photo available") at DATA_CAPTION_Y, the flow from DATA_FLOW_Y. Where the flow already holds three
// rows -- the 480 px panels, and 240 px without the Follow control -- nothing changes.
constexpr bool SlotCompact(int screen, bool followCtl, int fontH, int lineH)
{
    return RowsThatFit(SLOT_FLOW_Y, HardBottomY(screen, followCtl), fontH, lineH) < MIN_IDENTITY_ROWS;
}

// Where the slot layout's flow starts.
constexpr int SlotFlowStartY(int screen, bool followCtl, int fontH, int lineH)
{
    return SlotCompact(screen, followCtl, fontH, lineH) ? DATA_FLOW_Y : SLOT_FLOW_Y;
}

}  // namespace cardlayout
