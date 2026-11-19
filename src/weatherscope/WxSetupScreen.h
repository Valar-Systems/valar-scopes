#pragma once
// Weatherscope's setup screens, on the Map page (docs/weatherscope-stage4.md S4.39 item 3, S4.40): Decide()'s state in
// its frozen words (WxSetup.h; S4.33), under the swipe frame's page dots. Pure; pixel-tested in
// test/host/test_wx_livemap.cpp, whose SETUP-FITS row holds every title and line inside the disc at its row.
#include "WxSetup.h"
#include "WxSwipe.h"

namespace wxsetupscreen {

constexpr int kTitleY = 104;   // the state's title and its line sit near the centre, where the disc is widest:
constexpr int kLineY = 120;    // "BOARD NOT RECOGNISED" is 119 px, wider than the frame's title row allows

// The setup screen for `s`, or nothing for Live (the live map draws instead). Returns whether it drew.
inline bool ComposeSetup(wxswipe::Canvas& c, wxsetup::State s, wxsetup::Refusal r)
{
    if (s == wxsetup::State::Live) return false;
    const wxsetup::Words w = wxsetup::WordsOf(s, r);
    wxswipe::Frame(c, "RADAR", wxswipe::Page::Map);
    c.Centred(w.title, kTitleY, 1, s == wxsetup::State::Refused ? c.th.uiWarn : c.th.uiFg);
    if (w.line[0] != 0) c.Centred(w.line, kLineY, 1, c.th.uiDim);
    return true;
}

}  // namespace wxsetupscreen
