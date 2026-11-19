#pragma once
// W7 slice 3b's live map, pure (docs/weatherscope-stage4.md S4.39, S4.40): the screen a live frame draws, and whether
// Bend's stored package may sit under it. Host-tested in test/host/test_wx_livemap.cpp; its sabotages are
// test/host/sabotage_wx_livemap.py.
//
// THE BASEMAP RULING (the reviewer, S4.39): outside Bend, a live board draws its radar on plain ground. The package is
// used only when the WHOLE view lies inside its disc, so a map never ends mid-screen and never sits under another place.
// And nothing the stored scenarios carry reaches a live screen: no towns (WxTowns.h is Dallas's), no warning, no
// temperature (Q126 (a): the obs feed is unavailable in a live image).
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "BasemapPackage.h"
#include "WxBasemap.h"
#include "WxCrop.h"
#include "WxRender.h"
#include "WxScenarios.h"
#include "WxScenes.h"
#include "WxScreens.h"

namespace wxlivemap {

constexpr double kKmPerMi = 1.609344;

// The view's disc against the package's, in the package's own planar metric (WxBasemap.h's Mapper: km east and south of
// its origin). True only when the view's centre lies at least viewKm inside the package's radius.
inline bool Covers(const bmpk::Package& pk, double lat, double lon, double viewKm)
{
    const double lat0 = pk.lat0Micro / 1e6, lon0 = pk.lon0Micro / 1e6;
    const double c0 = fmax(cos(lat0 * (wxraster::kPi / 180)), 0.01);
    const double e = (lon - lon0) * wxbase::kKmPerDeg * c0, s = (lat0 - lat) * wxbase::kKmPerDeg;
    return sqrt(e * e + s * s) + viewKm <= pk.radiusMm / 1e6;
}

// A live frame's screen: the frame's own clock, its age by the board's clock, and the nearest echo in view.
inline void FromLive(const wxcrop::Header& h, const uint8_t* codes, int radiusMi, int64_t nowUtc, int offsetMin,
                     wxscreens::ScreenIn* in)
{
    in->h = &h;
    in->codes = codes;
    in->radiusMi = radiusMi;
    in->towns = nullptr;   // a live frame has no towns: WxTowns.h is Dallas's
    in->nTowns = 0;
    in->order = nullptr;
    wxscreens::ClockText(h.frameEpoch, offsetMin, in->clock, sizeof in->clock);
    in->ageS = nowUtc - (int64_t)h.frameEpoch;
    in->loop = false;
    in->warning = false;
    in->arrow = false;
    wxscreens::StateIn st;
    st.echo = wxscreens::NearestEchoInView(h, codes, wxscen::Centre(h), wxscreens::kInViewMi);
    st.ageS = in->ageS;
    wxscreens::StateLine(st, in->state, sizeof in->state, &in->stateInk);
}

// A live loop frame: WxScenes.h's FromLoopFrame, step for step, without its towns.
inline void FromLiveLoopFrame(const wxcrop::Header& h, const uint8_t* codes, int index, uint32_t newestEpoch,
                              int radiusMi, int offsetMin, wxscreens::ScreenIn* in)
{
    in->h = &h;
    in->codes = codes;
    in->radiusMi = radiusMi;
    in->towns = nullptr;   // the loop's frames have no towns either
    in->nTowns = 0;
    in->order = nullptr;
    wxscreens::ClockText(h.frameEpoch, offsetMin, in->clock, sizeof in->clock);
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

// A package's working set for one view (the manager's Basemap holds one).
struct PackageView {
    wxscenes::PackagePlaces places;
    wxscenes::PackagePeaks peaks;
    int order[wxscenes::kMaxPlaces];
};

// The package under a live screen, by the ruling: nothing unless Covers(). When it covers, the base is built for this
// view as the stored path builds it (test_wx_k35b.cpp's Prepare; the manager's EnsureBase) and the package's fields are
// set. False leaves every package field of `in` untouched: plain ground.
inline bool LivePackage(const bmpk::Package& pk, const wxcrop::Header& h, int radiusMi, bool terrain,
                        const wxbase::Scratch& sc, uint8_t* image, PackageView* pv, wxscreens::ScreenIn* in)
{
    const auto c = wxscen::Centre(h);
    if (!Covers(pk, c.lat, c.lon, radiusMi * kKmPerMi)) return false;
    wxrender::View v;
    wxrender::BuildViewKm(h, &v, radiusMi * kKmPerMi);   // the view Compose builds for this radius
    if (!pv->places.Load(pk, v)) return false;
    wxscreens::RankOrder(pv->places.Pops(), pv->places.Count(), pv->order);
    pv->peaks.Load(pk, v);
    if (!wxbase::Build(pk, v, radiusMi, wxbase::kMaskAll, terrain, sc, image)) return false;
    wxscenes::FromPackage(pk, image, wxbase::kMaskAll, terrain, &pv->places, pv->order, &pv->peaks, in);
    return true;
}

}  // namespace wxlivemap
