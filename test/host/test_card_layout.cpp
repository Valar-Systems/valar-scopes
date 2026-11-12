// Host test for the detail card's no-photo layout (#670; docs/card-nophoto-fix.md, T-host).
//
// The defect: with no decoded photo the card uses its slot layout, whose flowed rows start at y 162. With the
// Follow control offered, the flow's floor is SCREEN_SIZE - 74, which on a 240 px panel is 166. A text-size-1
// row is 8 px tall and 162 + 8 = 170 > 166, so line() drops every row: route, type, operator and telemetry.
// The card shows only its callsign. Shipped in v9 through v19.
//
// The numbers live in include/CardLayout.h, and DrawDetailCard calls it, so this grades the card's own
// arithmetic. CL1 was written before the fix and read FAIL on the code before it (CI at 716edde93ce5: 0 rows of 3),
// so the check has been seen able to fail. CL2 and CL3 are the controls: the 480 px panel, and 240 px with no
// Follow control, held three rows before the fix, and CL6 holds them to the layout they had.
// The whole drawn card, end to end, is graded by T-web (tools/web-renderer/card_nophoto.mjs).
#include <cstdio>
#include "../../include/CardLayout.h"

static int failures = 0;
static void check(bool ok, const char* id, const char* what)
{
    std::printf("  %s %s %s\n", ok ? "ok  " : "FAIL", id, what);
    if (!ok) ++failures;
}

int main()
{
    std::printf("card layout: the no-photo card's rows\n");

    // The flowed rows are text size 1 in LovyanGFX's Font0: fontHeight() is 8 at size 1 on every panel (no
    // panel scales text), and DrawDetailCard's pitch is fontHeight() + 5.
    const int FONT_H = 8, LINE_H = FONT_H + 5;
    const int NEED = 3;   // route, type, operator: the card's identity rows
    char what[160];

    // CL0: the floors are today's. The fix may change only where the slot layout starts its flow, never the
    // Follow control's reservation or the bezel margin, so CL1 cannot pass by moving the floor.
    check(cardlayout::HardBottomY(240, true) == 166 && cardlayout::HardBottomY(240, false) == 228 &&
          cardlayout::HardBottomY(480, true) == 406 && cardlayout::HardBottomY(480, false) == 468,
          "CL0", "the flow's floor: 166 / 228 on 240 px, 406 / 468 on 480 px (Follow offered / not)");

    struct Case { const char* id; int screen; bool followCtl; const char* name; };
    const Case cases[] = {
        { "CL1", 240, true,  "240 px, Follow offered (THE BUG)" },
        { "CL2", 480, true,  "480 px, Follow offered (control)" },
        { "CL3", 240, false, "240 px, no Follow control (control)" },
    };
    for (const Case& c : cases) {
        const int y0 = cardlayout::SlotFlowStartY(c.screen, c.followCtl, FONT_H, LINE_H);
        const int bottom = cardlayout::HardBottomY(c.screen, c.followCtl);
        const int n = cardlayout::RowsThatFit(y0, bottom, FONT_H, LINE_H);
        std::snprintf(what, sizeof what, "%s: flow from y %d to %d holds %d row(s), needs %d",
                      c.name, y0, bottom, n, NEED);
        check(n >= NEED, c.id, what);
    }

    // CL6: only the broken case compacts. The fix may change the slot layout only where three rows did not fit;
    // the 480 px panel and 240 px without Follow keep the slot's flow at 162, so their cards are what they were.
    check(cardlayout::SlotCompact(240, true, FONT_H, LINE_H) &&
          !cardlayout::SlotCompact(480, true, FONT_H, LINE_H) && !cardlayout::SlotCompact(480, false, FONT_H, LINE_H) &&
          !cardlayout::SlotCompact(240, false, FONT_H, LINE_H) &&
          cardlayout::SlotFlowStartY(480, true, FONT_H, LINE_H) == cardlayout::SLOT_FLOW_Y &&
          cardlayout::SlotFlowStartY(240, false, FONT_H, LINE_H) == cardlayout::SLOT_FLOW_Y,
          "CL6", "only 240 px with Follow compacts; 480 px and 240 px without Follow keep the flow at 162");

    // The rule RowsThatFit counts with is line()'s, which calls RowFits: a row at y draws iff y + fontH <= bottom.
    check(cardlayout::RowFits(158, 166, 8) && !cardlayout::RowFits(159, 166, 8) &&
          cardlayout::RowsThatFit(158, 166, 8, 13) == 1 && cardlayout::RowsThatFit(159, 166, 8, 13) == 0,
          "CL4", "RowFits is line()'s test: 158 + 8 = 166 draws, 159 + 8 = 167 drops; RowsThatFit agrees");
    // An impossible pitch is refused, never counted as some plausible number of rows.
    check(cardlayout::RowsThatFit(70, 166, 8, 0) == -1, "CL5", "a pitch of 0 returns -1, not a count");

    if (failures == 0) std::printf("  ok\n");
    else               std::printf("  %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
