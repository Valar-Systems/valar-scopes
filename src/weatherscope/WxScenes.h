// Weatherscope 3.5a: what a stored scenario or a loop frame puts on the screen, as one wxscreens::ScreenIn -- the
// firmware's path AND the K rows' (test/host/test_wx_k35.cpp), so the host grades the code the board runs
// (docs/weatherscope-stage3.md A9.10). Pure C++11. No coordinate is stored here.
//
// The scenario's frame time is what the top strip shows; its age (the clock less the frame) drives the orange time
// and RADAR STALE (Q15, Q18). A warning shows its title and band lines (and the arrow when its storm is within the
// 60 mi view, Q17); every other screen shows the state line. A loop frame shows its own time, dim, and the loop's
// band (A9.10) in place of the state line.

#pragma once

#include <stdint.h>
#include <stdio.h>

#include "WxCrop.h"
#include "WxLogic.h"
#include "WxScenarios.h"
#include "WxScreens.h"
#include "WxTowns.h"

namespace wxscenes {

// One stored scenario's screen at radiusMi. `order` is RankOrder over wxtowns::kTowns' populations.
inline void FromScenario(const wxscen::Scenario& s, const wxcrop::Header& h, const uint8_t* codes, int radiusMi,
                         const int* order, wxscreens::ScreenIn* in)
{
    in->h = &h;
    in->codes = codes;
    in->radiusMi = radiusMi;
    in->towns = wxtowns::kTowns;
    in->nTowns = (int)wxtowns::kCount;
    in->order = order;
    wxscreens::ClockText(s.frameUtc, wxscen::kOffsetMin, in->clock, sizeof in->clock);
    in->ageS = s.nowUtc - s.frameUtc;
    in->loop = false;
    in->arrow = false;
    in->state[0] = '\0';
    const wxlogic::Choice c = s.Choose();
    in->warning = c.screen == wxlogic::Screen::Warning && c.warning >= 0 && (&s.warn)[c.warning].polygonN >= 4;
    if (in->warning) {
        const wxlogic::Warn& w = (&s.warn)[c.warning];
        for (int i = 0; i < 4; ++i) in->polygon[i] = w.polygon[i];   // the stored square (A8.10); a ring's 5th = 1st
        const wxlogic::Motion m = wxlogic::ParseMotion(w.motion);
        in->wt = wxscreens::MakeWarningTexts(w, wxlogic::MotionLine(m, s.here));
        if (m.valid) {
            in->storm = m.pos[wxlogic::NearestPos(m, s.here)];
            in->fromDeg = m.fromDeg;
            in->arrow = wxscreens::ArrowVisible(s.here, in->storm);
        }
        return;
    }
    wxscreens::StateIn st;
    st.hereNow = s.HereNowPrecip();
    st.echo = wxscreens::NearestEchoInView(h, codes, s.here, wxscreens::kInViewMi);
    st.hasTemp = s.hasTemp;
    st.tempF = s.tempF;
    st.ageS = in->ageS;
    wxscreens::StateLine(st, in->state, sizeof in->state, &in->stateInk);
}

// Loop frame `index` (0 = oldest) of the past hour, at radiusMi (Q19): its own time, dim, and the loop's band --
// "56 MIN AGO" against the newest frame, or "NOW" -- with the track and the knob.
inline void FromLoopFrame(const wxcrop::Header& h, const uint8_t* codes, int index, uint32_t newestEpoch,
                          int radiusMi, const int* order, wxscreens::ScreenIn* in)
{
    in->h = &h;
    in->codes = codes;
    in->radiusMi = radiusMi;
    in->towns = wxtowns::kTowns;
    in->nTowns = (int)wxtowns::kCount;
    in->order = order;
    wxscreens::ClockText(h.frameEpoch, wxscen::kOffsetMin, in->clock, sizeof in->clock);
    in->ageS = 0;
    in->warning = false;
    in->arrow = false;
    in->state[0] = '\0';
    in->loop = true;
    in->loopIndex = index;
    const long mins = ((long)newestEpoch - (long)h.frameEpoch) / 60;
    if (mins <= 0) snprintf(in->loopAge, sizeof in->loopAge, "NOW");
    else snprintf(in->loopAge, sizeof in->loopAge, "%ld MIN AGO", mins);
}

}  // namespace wxscenes
