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
#include <string.h>

#include "BasemapPackage.h"
#include "WxBasemap.h"
#include "WxCrop.h"
#include "WxLogic.h"
#include "WxScenarios.h"
#include "WxScreens.h"
#include "WxTowns.h"

namespace wxscenes {

// One stored scenario's screen at radiusMi. `order` is RankOrder over wxtowns::kTowns' populations. `offsetMin` is
// the scenario's own clock: Dallas's CDT unless given (3.5b's Bend scenarios pass Pacific, A10.4).
inline void FromScenario(const wxscen::Scenario& s, const wxcrop::Header& h, const uint8_t* codes, int radiusMi,
                         const int* order, wxscreens::ScreenIn* in, int offsetMin = wxscen::kOffsetMin)
{
    in->h = &h;
    in->codes = codes;
    in->radiusMi = radiusMi;
    in->towns = wxtowns::kTowns;
    in->nTowns = (int)wxtowns::kCount;
    in->order = order;
    wxscreens::ClockText(s.frameUtc, offsetMin, in->clock, sizeof in->clock);
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
                          int radiusMi, const int* order, wxscreens::ScreenIn* in, int offsetMin = wxscen::kOffsetMin)
{
    in->h = &h;
    in->codes = codes;
    in->radiusMi = radiusMi;
    in->towns = wxtowns::kTowns;
    in->nTowns = (int)wxtowns::kCount;
    in->order = order;
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

// ----------------------------------------------------------------------------------- 3.5b: a package's screen

constexpr int kMaxPlaces = 256;   // a package's places held at once; more refuses the package's towns, never truncates
constexpr int kMaxPkgPeaks = 16;  // format section 5.4 keeps the top 16; the screen places at most 4

// A package's places as the screen sees them: each anchored through the view by wxbase::Mapper, the name read in
// place (a package's names are not NUL-terminated) and copied whole, one at a time, when asked for. So nothing is
// truncated: Name(i) is valid until the next call, which is how PlaceLabels uses it (LabelText copies at once).
class PackagePlaces : public wxscreens::TownSet {
public:
    // False when the package holds more places than kMaxPlaces (the caller then shows no package towns).
    bool Load(const bmpk::Package& pk, const wxrender::View& v)
    {
        n_ = 0;
        total_ = 0;
        const wxbase::Mapper m(pk, v);
        bmpk::ForEachPlace(pk, [&](int32_t qx, int32_t qy, uint32_t pop, const char* name, uint32_t len) {
            if (total_++ >= kMaxPlaces) return;
            Item& it = items_[n_];
            m.ToScreen(qx, qy, &it.x, &it.y);
            it.name = name;
            it.len = (uint8_t)len;
            pops_[n_++] = pop;
        });
        return total_ <= kMaxPlaces;
    }
    int Count() const { return n_; }
    const uint32_t* Pops() const { return pops_; }
    const char* Name(int i) const override
    {
        memcpy(buf_, items_[i].name, items_[i].len);
        buf_[items_[i].len] = '\0';
        return buf_;
    }
    void At(int i, double* x, double* y) const override
    {
        *x = items_[i].x;
        *y = items_[i].y;
    }

private:
    struct Item {
        const char* name;
        uint8_t len;
        double x, y;
    };
    Item items_[kMaxPlaces];
    uint32_t pops_[kMaxPlaces];
    mutable char buf_[256];
    int n_ = 0, total_ = 0;
};

// A package's peaks, in rank order, anchored through the view; the first kMaxPkgPeaks are kept (the highest ranked:
// the screen places at most 4 of them), and Total() says how many the package held.
class PackagePeaks : public wxscreens::PeakSet {
public:
    void Load(const bmpk::Package& pk, const wxrender::View& v)
    {
        n_ = 0;
        total_ = 0;
        const wxbase::Mapper m(pk, v);
        bmpk::ForEachPeak(pk, [&](int32_t qx, int32_t qy, int32_t, const char* name, uint32_t len) {
            if (total_++ >= kMaxPkgPeaks) return;
            Item& it = items_[n_++];
            m.ToScreen(qx, qy, &it.x, &it.y);
            it.name = name;
            it.len = (uint8_t)len;
        });
    }
    int Total() const { return total_; }
    int Count() const override { return n_; }
    const char* Name(int i) const override
    {
        memcpy(buf_, items_[i].name, items_[i].len);
        buf_[items_[i].len] = '\0';
        return buf_;
    }
    void At(int i, double* x, double* y) const override
    {
        *x = items_[i].x;
        *y = items_[i].y;
    }

private:
    struct Item {
        const char* name;
        uint8_t len;
        double x, y;
    };
    Item items_[kMaxPkgPeaks];
    mutable char buf_[256];
    int n_ = 0, total_ = 0;
};

// The package's part of a screen (A10.2): its basemap image under the dry pixels, its places in place of WxTowns.h's
// (ranked by `placeOrder`, RankOrder over places.Pops()), its peaks when the mask has them, and its identity and mask
// for the layer's key. `base` was built by wxbase::Build for this view, mask and terrain.
inline void FromPackage(const bmpk::Package& pk, const uint8_t* base, uint8_t mask, bool terrain,
                        const PackagePlaces* places, const int* placeOrder, const PackagePeaks* peaks,
                        wxscreens::ScreenIn* in)
{
    in->base = base;
    in->terrain = terrain;
    in->mask = mask;
    in->pkgCrc = pk.crc32;
    in->pkgLen = pk.size;
    in->places = places;
    in->placeOrder = placeOrder;
    in->nPlaces = places ? places->Count() : 0;
    in->peaks = (mask & wxbase::kMaskPeaks) ? peaks : nullptr;
}

// k35.ref.txt's text token, so the board prints what the K reference holds (B1's text and label checks): printable
// ASCII as itself, every other byte and '%' as %XX, "-" when empty. Truncated at cap - 1 bytes, at a whole escape.
inline void Token(const char* s, char* out, size_t cap)
{
    if (cap == 0) return;
    size_t n = 0;
    if (!*s && cap > 1) out[n++] = '-';
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        const bool plain = *p >= 0x21 && *p <= 0x7E && *p != '%';
        if (n + (plain ? 1 : 3) >= cap) break;
        if (plain) {
            out[n++] = (char)*p;
        } else {
            static const char kHex[] = "0123456789ABCDEF";
            out[n++] = '%';
            out[n++] = kHex[*p >> 4];
            out[n++] = kHex[*p & 15];
        }
    }
    out[n] = '\0';
}

inline const char* InkName(wxraster::Rgb c)
{
    if (c == wxraster::kWhite) return "white";
    if (c == wxraster::kDim) return "dim";
    if (c == wxraster::kOrange) return "orange";
    return "other";
}

}  // namespace wxscenes
