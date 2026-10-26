#include "WeatherscopeManager.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <new>
#include <stdarg.h>
#include <algorithm>
#include <time.h>

#include <stdlib.h>   // before WxLogic.h, which calls strtod without including it (A8.15)

#include "DeviceIdentity.h"
#include "FrameBuffer.h"
#include "Layout.h"
#include "SolarDim.h"
#include "TouchPoll.h"
#include "WxCrop.h"
#include "WxLegend.h"
#include "WxLogic.h"
#include "WxPalette.h"
#include "WxRaster.h"
#include "WxRender.h"
#include "WxScenarios.h"
#include "WxScenes.h"
#include "WxScreens.h"
#include "WxTowns.h"

namespace {

constexpr unsigned long SPLASH_MS = 4000;  // the splash, once per boot
constexpr int64_t kLoopPeriodUs = 70000;   // 14.29 fps (A8.20)
constexpr int kFrameDim = wxpalette::kDim55; // rain code 1 at 55 %, as the bench (final palette): the legend's

const uint32_t kFg     = lgfx::color888(230, 237, 234);
const uint32_t kDim    = lgfx::color888(130, 150, 160);
const uint32_t kAccent = lgfx::color888(90, 200, 255);
const uint32_t kWarn   = lgfx::color888(255, 176, 40);

const char* const kScenarioName[wxscreens::kScenarios] = {"dry", "approaching", "herenow", "warning", "stale", "clear"};

const char* ScreenName(wxlogic::Screen s)
{
    switch (s) {
    case wxlogic::Screen::Warning: return "Warning";
    case wxlogic::Screen::PrecipNow: return "PrecipNow";
    case wxlogic::Screen::PrecipApproaching: return "PrecipApproaching";
    default: return "Dry";
    }
}

// "212400": a frame's UTC clock, the name its file carries.
void Hhmmss(uint32_t epoch, char out[7])
{
    const uint32_t d = epoch % 86400;
    snprintf(out, 7, "%02u%02u%02u", (unsigned)(d / 3600), (unsigned)(d / 60 % 60), (unsigned)(d % 60));
}

void HexSha(const uint8_t d[32], char out[65])
{
    for (int i = 0; i < 32; ++i) snprintf(out + 2 * i, 3, "%02x", d[i]);
}

// WxRaster's pixels, straight into a 16-bit sprite's buffer. LovyanGFX keeps that buffer as big-endian RGB565 --
// the bytes /diag/fb serves and the K rows hash -- so each pixel is color565's truncation, byte-swapped.
class SpriteSink : public wxraster::Sink {
public:
    explicit SpriteSink(LGFX_Sprite& s) : buf_((uint16_t*)s.getBuffer()), w_(s.width()) {}
    void Put(int x, int y, wxraster::Rgb c) override
    {
        const uint16_t v = (uint16_t)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
        buf_[y * w_ + x] = (uint16_t)((v >> 8) | (v << 8));
    }

private:
    uint16_t* buf_;
    int w_;
};

// B2's code hash: every row of codes as the 60 mi map layer is drawn, into SHA-256 -- the code buffer the map was
// drawn FROM, without a buffer for it (A8.20) -- timed apart from the drawing.
class RowHash : public wxscreens::CodeRows {
public:
    RowHash()
    {
        mbedtls_sha256_init(&ctx_);
        mbedtls_sha256_starts(&ctx_, 0);
    }
    ~RowHash() override { mbedtls_sha256_free(&ctx_); }
    void Row(const uint8_t* codes, int n) override
    {
        const int64_t t = esp_timer_get_time();
        mbedtls_sha256_update(&ctx_, codes, (size_t)n);
        us += (uint32_t)(esp_timer_get_time() - t);
    }
    void Finish(uint8_t out[32])
    {
        const int64_t t = esp_timer_get_time();
        mbedtls_sha256_finish(&ctx_, out);
        us += (uint32_t)(esp_timer_get_time() - t);
    }
    uint32_t us = 0;

private:
    mbedtls_sha256_context ctx_;
};

// The profile's clock (A9.22): esp_timer's microseconds, truncated (a frame is far shorter than the wrap).
uint32_t ProfClock() { return (uint32_t)esp_timer_get_time(); }

bool FullScreen(LGFX_Sprite& s)
{
    return s.getBuffer() && s.width() == wxrender::kSize && s.height() == wxrender::kSize && s.getColorDepth() == 16;
}

}  // namespace

// ----------------------------------------------------------------------------- set-up

void WeatherscopeManager::Initialise()
{
    // Called again after every config save (main.cpp): the splash, the buffers and the zoom belong to boot only.
    if (!splashStarted) {
        splashStarted = true;
        splashStartMs = millis();
        zoomLadder = wxscreens::ZoomLadder();
        zoomIdx = zoomLadder.Top();
        LogFeeds();
        MakeBuffers();
        PickFrames();
    }

    const String latStr = configServer.GetStoredString("latitude");
    const String lonStr = configServer.GetStoredString("longitude");
    hasLatLon = latStr.length() && lonStr.length();
    deviceLat = latStr.toDouble();
    deviceLon = lonStr.toDouble();

    const String br = configServer.GetStoredString("brightness");
    configuredBrightness = br.isEmpty() ? 255 : (uint8_t)constrain(br.toInt(), 10, 255);
    const String ad = configServer.GetStoredString("autodim");
    autoDim = ad.isEmpty() || ad == "true";

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // Flags only: the location itself is never logged.
    Serial.printf("[wx] init; location=%s autodim=%d brightness=%u stored=%d\n",
                  hasLatLon ? "set" : "unset", (int)autoDim, (unsigned)configuredBrightness, (int)feeds.stored);
}

// A PSRAM object, constructed in place: nullptr when the allocation fails.
template <typename T>
static T* NewInPsram()
{
    void* p = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM);
    return p ? new (p) T() : nullptr;
}

// The frame path, once: the cached screen sprite, the grid, the scenario, the screen's input and output, the view,
// the town ranking, the cycle and the loop's records. Its PSRAM cost is printed as one number (A9.5's sizes).
void WeatherscopeManager::MakeBuffers()
{
    const size_t psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    nHistory = feeds.radar.History(history, kLoopFrames);
    size_t cells = 0;
    for (size_t i = 0; i <= nHistory; ++i) {
        const wx::CropBytes& c = i < nHistory ? history[i] : feeds.clear;
        wxcrop::Header h;
        if (!c.Empty() && wxcrop::ParseHeader(c.data, c.size, &h) == wxcrop::Status::Ok && h.Cells() > cells)
            cells = h.Cells();
    }
    screenSprite = new LGFX_Sprite(&tft);
    screenSprite->setPsram(true);
    screenSprite->setColorDepth(16);
    const bool spriteOk = screenSprite->createSprite(wxrender::kSize, wxrender::kSize) != nullptr;
    grid = cells ? (uint8_t*)heap_caps_malloc(cells, MALLOC_CAP_SPIRAM) : nullptr;
    gridCells = grid ? cells : 0;
    scenario = NewInPsram<wxscen::Scenario>();
    screenIn = NewInPsram<wxscreens::ScreenIn>();
    screenOut = NewInPsram<wxscreens::ScreenOut>();
    view = NewInPsram<wxrender::View>();
    cycle = NewInPsram<wxscreens::Cycle>();
    layer = NewInPsram<wxscreens::Layer>();
    townOrder = (int*)heap_caps_malloc(sizeof(int) * wxtowns::kCount, MALLOC_CAP_SPIRAM);
    records = (LoopRecord*)heap_caps_malloc(sizeof(LoopRecord) * kLoopFrames, MALLOC_CAP_SPIRAM);
    if (townOrder) {
        uint32_t pops[wxtowns::kCount];
        for (unsigned i = 0; i < wxtowns::kCount; ++i) pops[i] = wxtowns::kTowns[i].pop;
        wxscreens::RankOrder(pops, (int)wxtowns::kCount, townOrder);
    }
    const size_t psramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    psramUsed = psramBefore - psramAfter;
    buffersReady = spriteOk && FullScreen(*screenSprite) && grid && scenario && screenIn && screenOut && view && cycle &&
                   layer && townOrder && records;
    Serial.printf("[wx] psram_used=%u sprite=%d grid=%u in=%u out=%u layer=%u history=%u clear=%u ready=%d\n",
                  (unsigned)psramUsed, (int)spriteOk, (unsigned)gridCells, (unsigned)sizeof(wxscreens::ScreenIn),
                  (unsigned)sizeof(wxscreens::ScreenOut), (unsigned)sizeof(wxscreens::Layer), (unsigned)nHistory,
                  (unsigned)feeds.clear.size, (int)buffersReady);
}

bool WeatherscopeManager::DecodeCrop(const wx::CropBytes& crop, wxcrop::Header* h)
{
    if (crop.Empty() || !grid) return false;
    return wxcrop::DecodeInto(crop.data, crop.size, grid, gridCells, h) == wxcrop::Status::Ok;
}

// A8.10's rule, on the device: approaching = the LATEST frame whose nearest echo is within 15 mi of the crop's
// centre; dry = the LATEST beyond 15 mi, or with none. Printed, and graded (B1).
void WeatherscopeManager::PickFrames()
{
    if (!buffersReady) return;
    for (size_t i = 0; i < nHistory; ++i) {
        wxcrop::Header h;
        if (!DecodeCrop(history[i], &h)) continue;
        if (wxlogic::EchoWithin(h, grid, wxscen::Centre(h))) pickApproaching = (int)i;
        else pickDry = (int)i;
    }
    wxcrop::Header h;
    if (pickApproaching >= 0 && wxcrop::ParseHeader(history[pickApproaching].data, history[pickApproaching].size, &h) == wxcrop::Status::Ok)
        Hhmmss(h.frameEpoch, pickApproachingName);
    if (pickDry >= 0 && wxcrop::ParseHeader(history[pickDry].data, history[pickDry].size, &h) == wxcrop::Status::Ok)
        Hhmmss(h.frameEpoch, pickDryName);
    Serial.printf("[wx] pick approaching=%s dry=%s psram_used=%u\n", pickApproachingName, pickDryName,
                  (unsigned)psramUsed);
}

int WeatherscopeManager::ZoomMiles() const { return wxscreens::ZoomMiles(zoomLadder, zoomIdx); }

// The cycle's scenario at the current zoom: built from its frame, composed ONCE into the cached sprite, and printed
// in k35.ref.txt's own lines (A9.10 B1: the frame's hash, its time, its band, its labels) under "<name>-<miles>".
void WeatherscopeManager::BuildScenario()
{
    const int kindIndex = cycle->index;
    const wxscen::Kind kind = (wxscen::Kind)kindIndex;
    const wx::CropBytes& crop = kind == wxscen::Kind::Clear ? feeds.clear
                              : history[kind == wxscen::Kind::Dry ? pickDry : pickApproaching];
    wxcrop::Header h;
    if (!DecodeCrop(crop, &h) || !wxscen::Build(kind, h, grid, scenario)) {
        Out("[wx] scn=%s FAILED: its crop does not decode or build\n", kScenarioName[kindIndex]);
        return;
    }
    PrintCopyStats();   // the hold that ended (A9.22's P4)
    const int R = ZoomMiles();
    *screenIn = wxscreens::ScreenIn();
    wxscenes::FromScenario(*scenario, h, grid, R, townOrder, screenIn);
    *screenOut = wxscreens::ScreenOut();
    SpriteSink sink(*screenSprite);
    wxscreens::Profile prof;
    prof.clock = ProfClock;
    const int64_t t0 = esp_timer_get_time();
    wxscreens::Compose(sink, *screenIn, *view, screenOut, nullptr, &prof);
    const uint32_t composeUs = (uint32_t)(esp_timer_get_time() - t0);

    uint8_t sha[32];
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0);
    mbedtls_sha256_update(&ctx, (const uint8_t*)screenSprite->getBuffer(), (size_t)wxrender::kSize * wxrender::kSize * 2);
    mbedtls_sha256_finish(&ctx, sha);
    mbedtls_sha256_free(&ctx);

    char hh[7], hex[65], row[24], tok[160], tok2[160];
    Hhmmss(h.frameEpoch, hh);
    HexSha(sha, hex);
    snprintf(row, sizeof row, "%s-%d", kScenarioName[kindIndex], R);
    // the boot-time pick and PSRAM figure again, so a trace that opens after boot still has them
    Out("[wx] pick approaching=%s dry=%s psram_used=%u\n", pickApproachingName, pickDryName,
                  (unsigned)psramUsed);
    Out("[wx] scn=%s R=%d frame=%s screen=%s synthetic=%d compose_us=%lu\n", kScenarioName[kindIndex], R, hh,
                  ScreenName(scenario->Choose().screen), (int)scenario->synthetic, (unsigned long)composeUs);
    {
        char line[200];
        int n = snprintf(line, sizeof line, "[wx] prof %s", row);
        for (int i = 0; i < wxscreens::Profile::kPhases && n > 0 && n < (int)sizeof line; ++i)
            n += snprintf(line + n, sizeof line - n, " %s=%lu", wxscreens::Profile::Name(i), (unsigned long)prof.Us(i));
        Out("%s sum=%lu compose_us=%lu\n", line, (unsigned long)prof.Sum(), (unsigned long)composeUs);
    }
    Out("[wx] k ROW %s %s\n", row, hex);
    wxscenes::Token(screenOut->time, tok, sizeof tok);
    Out("[wx] k TIME %s %s %s\n", row, tok, wxscenes::InkName(screenOut->timeInk));
    if (screenIn->warning) {
        wxscenes::Token(screenIn->wt.band1, tok, sizeof tok);
        wxscenes::Token(screenIn->wt.band2, tok2, sizeof tok2);
        Out("[wx] k BAND %s 1 %s\n", row, tok);
        Out("[wx] k BAND %s 2 %s\n", row, tok2);
    } else {
        wxscenes::Token(screenIn->state, tok, sizeof tok);
        Out("[wx] k BAND %s S %s %s\n", row, tok, wxscenes::InkName(screenIn->stateInk));
    }
    for (int i = 0; i < screenOut->nLabels; ++i) {
        const wxscreens::Placed& p = screenOut->labels[i].at;
        wxscenes::Token(screenOut->labels[i].label, tok, sizeof tok);
        Out("[wx] k LABEL %s %s %c %d %d %d %d\n", row, tok, p.side, p.box.x0, p.box.y0, p.box.x1, p.box.y1);
    }
    Out("[wx] screen=scenario %s R=%d\n", kScenarioName[kindIndex], R);
    if (fireT0) {   // BUILD M (A9.30): the fire's work is done; its first push closes the record
        const int64_t total = esp_timer_get_time() - fireT0;
        FireRecord& r = fires[nFires % kFireRecords];
        snprintf(r.why, sizeof r.why, "%s", fireWhy);
        r.printUs = (uint32_t)outUs;
        r.buildUs = (uint32_t)(total - outUs);
        r.shortWrites = outShort;
        r.prints = outN;
        r.gapUs = r.baseUs = 0;
        openFire = nFires % kFireRecords;
        ++nFires;
        fireT0 = 0;
    }
}

// One line at boot, proving the feeds answer through the factory. Sizes, counts and times only: no coordinate.
void WeatherscopeManager::LogFeeds() const
{
    const wx::CropBytes crop = feeds.radar.Latest();
    wxcrop::Header h;
    const wxcrop::Status s = wxcrop::Validate(crop.data, crop.size, &h);
    const wx::Observation o = feeds.obs.Latest();
    const wx::AlertList a = feeds.alerts.Active();
    wx::CropBytes hist[kLoopFrames];
    const size_t nh = feeds.radar.History(hist, kLoopFrames);
    size_t histBytes = 0;
    for (size_t i = 0; i < nh; ++i) histBytes += hist[i].size;
    Serial.printf("[wx] feeds: crop %u B %s %ux%u frame=%lu | history %u frames %u B | clear %u B | obs valid=%d "
                  "t=%ld present=%u | alerts=%u%s%s\n",
                  (unsigned)crop.size, wxcrop::StatusName(s), (unsigned)h.rows, (unsigned)h.cols,
                  (unsigned long)h.frameEpoch, (unsigned)nh, (unsigned)histBytes, (unsigned)feeds.clear.size,
                  (int)o.valid, (long)o.timeUtc, (unsigned)o.presentCount, (unsigned)a.count, a.count ? " first=" : "",
                  a.count ? a.items[0].event : "");
}

// ----------------------------------------------------------------------------- the loop task

void WeatherscopeManager::Update()
{
    UpdateBrightness();

    if (mode == Mode::Splash) {
        if (millis() - splashStartMs < SPLASH_MS) return;
        if (feeds.stored && buffersReady && pickApproaching >= 0 && pickDry >= 0 && !feeds.clear.Empty()) {
            mode = Mode::Scenario;
            lastTouchMs = millis();
            wxscreens::Step(*cycle, millis(), feeds.stored);   // starts the hold clock
            BuildScenario();
        } else {
            mode = Mode::Status;
        }
        return;
    }

    HandleTouch();
    HandleSerialGestures();
    ServiceAutoFires();   // BUILD M (A9.30)

    // The cycle sees every tick, so a paused interval (the loop, the legend) is skipped rather than counted when
    // it resumes; it can only advance while unpaused, i.e. on the scenario screen.
    if (wxscreens::Step(*cycle, millis(), feeds.stored) && mode == Mode::Scenario) BuildScenario();
    // 10 min without a touch: back to the ladder's top (A9.3)
    if (mode == Mode::Scenario) {
        const int idx = wxscreens::IdleReturn(zoomLadder, zoomIdx, millis(), lastTouchMs);
        if (idx != zoomIdx) ApplyZoom(idx, "idle");
    }
    if (mode == Mode::Loop) WaitForLoopFrame();
}

// A zoom step, a toggle or the idle return: the scenario recomposed at the new radius. At a ladder end the index
// does not move and the screen is recomposed and printed all the same, so every gesture leaves one screen line.
void WeatherscopeManager::ApplyZoom(int idx, const char* why)
{
    const int before = ZoomMiles();
    zoomIdx = idx;
    Out("[wx] zoom %s %d -> %d mi\n", why, before, ZoomMiles());
    if (mode == Mode::Scenario) BuildScenario();
}

// A tap opens the loop: 15 frames once, oldest first, each starting at its deadline (A8.20), at the current zoom
// (Q19); the cycle waits.
void WeatherscopeManager::StartLoop()
{
    wxscreens::Pause(*cycle, true);
    // The frame-invariant layer, built HERE at the tap and never inside a frame's draw (A9.23): the first frame's
    // deadline is set after it. Rebuilt on every tap, so no layer outlives the loop it was built for.
    wxcrop::Header h0, last;
    if (wxcrop::ParseHeader(history[0].data, history[0].size, &h0) == wxcrop::Status::Ok &&
        wxcrop::ParseHeader(history[nHistory - 1].data, history[nHistory - 1].size, &last) == wxcrop::Status::Ok) {
        *screenIn = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(h0, grid, 0, last.frameEpoch, ZoomMiles(), townOrder, screenIn);
        const int64_t t0 = esp_timer_get_time();
        const int px = wxscreens::BuildLayer(*layer, *screenIn, wxscreens::MakeLayerKey(*screenIn, cycle->index));
        Serial.printf("[wx] layer build_us=%lu pixels=%d valid=%d rebuilt=tap R=%d\n",
                      (unsigned long)(esp_timer_get_time() - t0), px, (int)layer->valid, ZoomMiles());
    }
    loopRebuilds = 0;
    mode = Mode::Loop;
    loopIndex = 0;
    loopFrameDue = false;
    loopT0Us = esp_timer_get_time() + kLoopPeriodUs;
    Serial.printf("[wx] screen=loop R=%d\n", ZoomMiles());
}

// Holds the loop task until the next frame's deadline, so the frame starts on time: sleeps in 1 ms ticks while
// there is room, then spins the last stretch. After the 15th frame, the 16th deadline ends the loop.
void WeatherscopeManager::WaitForLoopFrame()
{
    const int64_t deadline = loopT0Us + (int64_t)loopIndex * kLoopPeriodUs;
    while (deadline - esp_timer_get_time() > 2000) vTaskDelay(1);
    while (esp_timer_get_time() < deadline) {}
    if (loopIndex >= kLoopFrames) {
        FinishLoop();
        return;
    }
    loopFrameDue = true;
}

void WeatherscopeManager::FinishLoop()
{
    mode = Mode::Scenario;
    loopFrameDue = false;
    // printed only now: printing while it plays could stall or pace it (A8.19 item 4)
    Serial.printf("[wx] loop records=%d period_us=%ld R=%d layer_rebuilds=%d\n", kLoopFrames, (long)kLoopPeriodUs,
                  ZoomMiles(), loopRebuilds);
    for (int i = 0; i < kLoopFrames; ++i) {
        char hex[65];
        HexSha(records[i].sha, hex);
        Serial.printf("[wx] lrec %d epoch=%lu sha=%s start_us=%lld draw_us=%lu hash_us=%lu\n", i,
                      (unsigned long)records[i].epoch, hex, (long long)records[i].startUs,
                      (unsigned long)records[i].drawUs, (unsigned long)records[i].hashUs);
    }
    for (int i = 0; i < kLoopFrames; ++i) {
        const LoopRecord& r = records[i];
        uint32_t sum = 0;
        char line[220];
        int n = snprintf(line, sizeof line, "[wx] lprof %d decode=%lu", i, (unsigned long)r.decodeUs);
        for (int k = 0; k < wxscreens::Profile::kPhases && n > 0 && n < (int)sizeof line; ++k) {
            sum += r.phaseUs[k];
            n += snprintf(line + n, sizeof line - n, " %s=%lu", wxscreens::Profile::Name(k), (unsigned long)r.phaseUs[k]);
        }
        Serial.printf("%s sum=%lu draw_us=%lu hash_us=%lu\n", line, (unsigned long)sum, (unsigned long)r.drawUs,
                      (unsigned long)r.hashUs);
    }
    for (int i = 0; i < nHeldRuns; ++i) PrintPushRun(heldRuns[i]);
    if (droppedRuns) Serial.printf("[fb] push runs dropped while held: %d\n", droppedRuns);
    nHeldRuns = droppedRuns = 0;
    wxscreens::Pause(*cycle, false);
    Serial.printf("[wx] screen=scenario %s R=%d\n", kScenarioName[cycle->index], ZoomMiles());
}

void WeatherscopeManager::OpenLegend()
{
    wxscreens::Pause(*cycle, true);
    mode = Mode::Legend;
    Serial.println("[wx] screen=legend");
    // the bench's swatch and label lines (metadata, not a frame), for legend_check.py --fb
    Serial.printf("WX,legend,swatches=%d\n", wxlegend::kSwatchCount);
    int i = 0;
    for (const auto& sw : wxlegend::kSwatches)
        Serial.printf("WXL,%d,%s,%u,%d,%d,%d,%d\n", i++, wxlegend::KindName(sw.kind), (unsigned)sw.code, sw.x, sw.y,
                      sw.w, sw.h);
    int r = 0;
    for (const auto& row : wxlegend::kRows)
        Serial.printf("WXT,%d,%d,%d,%d,%d\n", r++, wxlegend::kLabelX, (int)row.y,
                      wxlegend::kCharW * (int)strlen(row.label), wxlegend::kCharH);
}

void WeatherscopeManager::CloseLegend()
{
    mode = Mode::Scenario;
    wxscreens::Pause(*cycle, false);
    Serial.printf("[wx] screen=scenario %s R=%d\n", kScenarioName[cycle->index], ZoomMiles());
}

void WeatherscopeManager::OnTap()
{
    if (mode == Mode::Scenario) StartLoop();
    else if (mode == Mode::Legend) CloseLegend();
}

void WeatherscopeManager::OnSwipeLeft()
{
    if (mode == Mode::Scenario) OpenLegend();
}

// Up (dir -1) zooms in, down (+1) zooms out, as on Blipscope (A9.3), through radarzoom::Step unchanged.
void WeatherscopeManager::OnSwipeVertical(int dir)
{
    if (mode != Mode::Scenario) return;
    radarzoom::Edge edge = radarzoom::Edge::None;
    const int idx = radarzoom::Step(zoomIdx, dir, zoomLadder, edge);
    ApplyZoom(idx, edge == radarzoom::Edge::Max ? "in (edge: Max)" : edge == radarzoom::Edge::Min ? "out (edge: Min)"
                   : dir < 0 ? "in" : "out");
}

// A fired long press: 60 or 30 -> 15, 15 -> 60 (longpress::ToggleTarget, unchanged).
void WeatherscopeManager::OnHoldFired()
{
    if (mode != Mode::Scenario) return;
    // BUILD M (A9.30): open a record; BuildScenario closes its work, the next push its gap
    fireT0 = esp_timer_get_time();
    outUs = 0;
    outShort = outN = 0;
    pushEndBeforeFire = lastPushEnd;
    const int target = longpress::ToggleTarget(zoomIdx, zoomLadder.Top());
    if (target >= 0) ApplyZoom(target, "hold");
    fireT0 = 0;          // no BuildScenario ran: nothing recorded
    fireWhy = "hold";    // the glass's, until a bench byte or an auto-fire says otherwise
}

// BUILD M (A9.30): a fire from the bench ('H') or the schedule, through the glass's own handler.
void WeatherscopeManager::FireMeasured(const char* why)
{
    fireWhy = why;
    OnHoldFired();
}

// BUILD M (A9.30): the stored-mode auto-fires (boot + 60/90/120 s, and 'A''s +20/+40/+60 s), one per tick, only on
// the scenario screen; a due fire waits there until the screen is.
void WeatherscopeManager::ServiceAutoFires()
{
    if (!feeds.stored || mode != Mode::Scenario) return;
    static bool bootScheduled = false;
    if (!bootScheduled) {
        bootScheduled = true;
        const uint32_t boot[3] = {60000, 90000, 120000};
        for (uint32_t t : boot)
            if (nAutoFires < 6) autoFireAtMs[nAutoFires++] = t;
    }
    const uint32_t now = millis();
    for (int i = 0; i < nAutoFires; ++i) {
        if ((int32_t)(now - autoFireAtMs[i]) < 0) continue;
        for (int j = i + 1; j < nAutoFires; ++j) autoFireAtMs[j - 1] = autoFireAtMs[j];
        --nAutoFires;
        FireMeasured("auto");
        return;
    }
}

// BUILD M (A9.30): the records, on 'R', then cleared.
void WeatherscopeManager::PrintFireRecords()
{
    const int n = nFires < kFireRecords ? nFires : kFireRecords;
    Serial.printf("[wx] fires n=%d of %d pending_auto=%d\n", n, nFires, nAutoFires);
    for (int k = 0; k < n; ++k) {
        const int i = nFires <= kFireRecords ? k : (nFires + k) % kFireRecords;
        const FireRecord& r = fires[i];
        Serial.printf("[wx] fire %d why=%s build_us=%lu print_us=%lu prints=%u short=%u gap_us=%lu base_us=%lu "
                      "stall_us=%ld\n", k, r.why, (unsigned long)r.buildUs, (unsigned long)r.printUs,
                      (unsigned)r.prints, (unsigned)r.shortWrites, (unsigned long)r.gapUs, (unsigned long)r.baseUs,
                      (long)r.gapUs - (long)r.baseUs);
    }
    nFires = 0;
}

// BUILD M (A9.30): BuildScenario's and ApplyZoom's prints come through here, so the time they spend in the USB
// driver (HWCDC's backpressure wait) is measured apart from the composition, and a short write is counted. The
// bytes are printf's own.
size_t WeatherscopeManager::Out(const char* fmt, ...)
{
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    const int64_t t0 = esp_timer_get_time();
    const size_t w = Serial.write((const uint8_t*)buf, (size_t)n);
    if (fireT0) {
        outUs += esp_timer_get_time() - t0;
        ++outN;
        if (w < (size_t)n) ++outShort;
    }
    return w;
}

// Under 40 px on both axes a tap; otherwise a swipe on the larger axis (the radar's classifier). Only a stroke the
// long press does not own reaches here.
void WeatherscopeManager::ClassifyStroke(int dx, int dy)
{
    constexpr int kSwipeMin = 40;
    const int adx = abs(dx), ady = abs(dy);
    if (adx < kSwipeMin && ady < kSwipeMin) OnTap();
    else if (adx >= ady) { if (dx < 0) OnSwipeLeft(); }
    else OnSwipeVertical(dy > 0 ? +1 : -1);
}

// A held stroke's rejoin grace ended without a rejoin: 40 px or more is a swipe from the lift's delta (rule 3).
void WeatherscopeManager::EndHoldGrace(const longpress::GraceEnd& e)
{
    if (e.swipe) ClassifyStroke(e.dx, e.dy);
}

void WeatherscopeManager::HandleTouch()
{
    int32_t tx = 0, ty = 0;
    const TouchPoll poll = ReadTouch(tft, http, tx, ty);
    if (poll == TouchPoll::Skipped) return;
    const uint32_t now = millis();
    const bool touched = poll == TouchPoll::Touched;
    if (touched) {
        lastTouchMs = now;
        if (!wasTouched) {
            const longpress::PressOutcome p = longpress::OnPress(longPress, now, tx, ty, mode == Mode::Scenario);
            if (p.prev.ended) EndHoldGrace(p.prev);
            if (p.is != longpress::PressIs::Rejoin) {
                touchStartX = tx;
                touchStartY = ty;
            }
        }
        touchLastX = tx;
        touchLastY = ty;
        if (longpress::OnHeld(longPress, now, tx, ty) == longpress::HeldIs::Fire) OnHoldFired();
    } else {
        const longpress::GraceEnd ge = longpress::OnIdle(longPress, now);
        if (ge.ended) EndHoldGrace(ge);
    }
    if (!touched && wasTouched) {
        const int dx = touchLastX - touchStartX, dy = touchLastY - touchStartY;
        if (longpress::OnRelease(longPress, now, dx, dy) == longpress::ReleaseIs::PassThrough) ClassifyStroke(dx, dy);
    }
    wasTouched = touched;
}

// The bench's gestures (Q21): 'T' a tap, 'S' a left swipe, 'U'/'D' an up/down swipe, 'H' a fired long press -- fed to
// the SAME handlers as the glass. Only in stored mode, and like stored mode they must be unreachable on a customer
// unit before any Weatherscope ships.
void WeatherscopeManager::HandleSerialGestures()
{
    while (Serial.available()) {
        const int ch = Serial.read();
        if (!feeds.stored) continue;
        const bool known = ch == 'T' || ch == 'S' || ch == 'U' || ch == 'D' || ch == 'H' || ch == 'A' || ch == 'R';
        Serial.printf("[wx] gesture %c\n", known ? (char)ch : '?');
        if (!known) continue;
        lastTouchMs = millis();
        if (ch == 'T') OnTap();
        else if (ch == 'S') OnSwipeLeft();
        else if (ch == 'U') OnSwipeVertical(-1);
        else if (ch == 'D') OnSwipeVertical(+1);
        else if (ch == 'A') {   // BUILD M (A9.30): three auto-fires at +20, +40 and +60 s
            const uint32_t now = millis();
            for (uint32_t d : {20000u, 40000u, 60000u})
                if (nAutoFires < 6) autoFireAtMs[nAutoFires++] = now + d;
            Serial.printf("[wx] autofire scheduled n=%d\n", nAutoFires);
        }
        else if (ch == 'R') PrintFireRecords();
        else FireMeasured("H");
    }
}

// ----------------------------------------------------------------------------- brightness

void WeatherscopeManager::UpdateBrightness()
{
    if (lastBrightnessCheck != 0 && millis() - lastBrightnessCheck < 20000) return;
    lastBrightnessCheck = millis();

    bool night = false;
    if (autoDim && hasLatLon) {
        const time_t utc = time(nullptr);
        if (utc > 1600000000)
            night = SunElevationDeg(deviceLat, deviceLon, utc) < -0.833f;
    }
    nightDim = night;

    uint8_t target = configuredBrightness;
    if (night) {
        target = configuredBrightness / 5;
        if (target < 10) target = 10;
    }
    if (target != currentBrightness) {
        currentBrightness = target;
        tft.setBrightness(target);
    }
}

// ----------------------------------------------------------------------------- drawing

void WeatherscopeManager::Draw(BandCanvas& backbuffer, bool /*firstPass*/)
{
    switch (mode) {
    case Mode::Splash: DrawSplash(backbuffer); break;
    case Mode::Status: DrawStatus(backbuffer); break;
    case Mode::Scenario: DrawScenario(backbuffer); break;
    case Mode::Loop: DrawLoopFrame(backbuffer); break;
    case Mode::Legend: DrawLegend(backbuffer); break;
    }
    // DIAG ONLY (A9.22): what the backbuffer now holds, MSRRCCCC -- mode, scene, radius, draw count.
    uint32_t m = 5, sc = 0, rr = 0;
    if (mode == Mode::Scenario) { m = 1; sc = (uint32_t)cycle->index; rr = (uint32_t)ZoomMiles(); }
    else if (mode == Mode::Loop) { m = 2; sc = (uint32_t)(loopIndex - 1) & 15; rr = (uint32_t)ZoomMiles(); }
    else if (mode == Mode::Legend) m = 3;
    else if (mode == Mode::Status) m = 4;
    framebuf::SetTag((m << 28) | ((sc & 15) << 24) | ((rr & 255) << 16) | (++drawCount & 0xFFFF));
}

void WeatherscopeManager::OnPushed(uint32_t tag, uint8_t phase, uint32_t pushUs)
{
    {   // BUILD M (A9.30): the gap from one push's end to the next push's start, from this hook's time less pushUs
        const int64_t end = esp_timer_get_time();
        const int64_t start = end - (int64_t)pushUs;
        if (openFire >= 0) {
            FireRecord& r = fires[openFire];
            r.gapUs = pushEndBeforeFire ? (uint32_t)(start - pushEndBeforeFire) : 0;
            const int n = nGaps < 16 ? nGaps : 16;
            uint32_t sorted[16];
            for (int i = 0; i < n; ++i) sorted[i] = gaps[i];
            std::sort(sorted, sorted + n);
            r.baseUs = n ? sorted[n / 2] : 0;
            openFire = -1;
            nGaps = 0;   // the next fire's baseline starts here
        } else if (lastPushEnd) {
            gaps[nGaps % 16] = (uint32_t)(start - lastPushEnd);
            ++nGaps;
        }
        lastPushEnd = end;
    }
    const uint16_t scene = (uint16_t)(tag >> 16), count = (uint16_t)(tag & 0xFFFF);
    if (pushRun.n && scene == pushRun.scene) {
        pushRun.last = count;
        ++pushRun.n;
        pushRun.phases |= (uint8_t)(1u << (phase & 7));
        if (pushUs < pushRun.pushMin) pushRun.pushMin = pushUs;
        if (pushUs > pushRun.pushMax) pushRun.pushMax = pushUs;
        return;
    }
    if (pushRun.n) {
        if (mode == Mode::Loop) {
            if (nHeldRuns < kHeldRuns) heldRuns[nHeldRuns++] = pushRun;
            else ++droppedRuns;
        } else {
            PrintPushRun(pushRun);
        }
    }
    pushRun = PushRun();
    pushRun.scene = scene;
    pushRun.first = pushRun.last = count;
    pushRun.n = 1;
    pushRun.phases = (uint8_t)(1u << (phase & 7));
    pushRun.pushMin = pushRun.pushMax = pushUs;
}

void WeatherscopeManager::PrintPushRun(const PushRun& r) const
{
    char ph[24];
    int n = 0;
    ph[0] = '\0';
    for (int p = 0; p < 8; ++p)
        if (r.phases & (1u << p)) n += snprintf(ph + n, sizeof ph - n, "%s%d", n ? "," : "", p);
    Serial.printf("[fb] push %04x draws %u-%u n=%lu phases=%s push_us=%lu-%lu\n", (unsigned)r.scene, (unsigned)r.first,
                  (unsigned)r.last, (unsigned long)r.n, ph, (unsigned long)r.pushMin, (unsigned long)r.pushMax);
}

void WeatherscopeManager::PrintCopyStats()
{
    if (copyN)
        Serial.printf("[wx] fbcopy n=%lu min=%lu max=%lu mean=%lu\n", (unsigned long)copyN, (unsigned long)copyMin,
                      (unsigned long)copyMax, (unsigned long)(copySum / copyN));
    copyN = copyMin = copyMax = 0;
    copySum = 0;
}

// The scenario's cached screen, a straight copy: both are 240 x 240 at 16 bpp, so the bytes are the same
// representation.
void WeatherscopeManager::DrawScenario(BandCanvas& c)
{
    LGFX_Sprite& s = c.sprite();
    if (FullScreen(s)) {
        const int64_t t0 = esp_timer_get_time();
        memcpy(s.getBuffer(), screenSprite->getBuffer(), (size_t)s.bufferLength());
        const uint32_t us = (uint32_t)(esp_timer_get_time() - t0);   // the full-frame PSRAM copy (A9.22's P4)
        if (copyN == 0 || us < copyMin) copyMin = us;
        if (us > copyMax) copyMax = us;
        copySum += us;
        ++copyN;
    } else {
        screenSprite->pushSprite(&s, 0, 0);
    }
    if (mode == Mode::Scenario) DrawHoldRing(c);   // on top, every frame; never in a cached screen (A9.21 item 5)
}

// THE HOLD RING, Blipscope's own (AircraftManager::DrawHoldRing, docs/v17-long-press-zoom.md "Ring visibility";
// Daniel's call for Weatherscope, A9.21 item 5): a progress band just inside the rim, where no finger is, filling from
// 12 o'clock while the hold builds, drawn complete once fired. When and how far are LongPressPolicy's, unchanged.
// Drawn over the copied screen each frame, so the frame after the release no longer has it.
void WeatherscopeManager::DrawHoldRing(BandCanvas& c)
{
    const bool fired = longPress.phase == longpress::Phase::Fired;
    const bool visible = fired || longpress::RingVisible(longPress);
    if (visible != ringShown) {
        ringShown = visible;
        Serial.printf("[wx] hold ring %s\n", visible ? "on" : "off");
    }
    if (!visible) return;
    const float k = SCREEN_SIZE / 240.0f;
    const int cc = SCREEN_SIZE_DIV_2;
    const int R1 = SCREEN_SIZE_DIV_2 - (int)(2 * k), R0 = R1 - (int)(7 * k);
    c.drawCircle(cc, cc, R1 + 1, lgfx::color888(0, 90, 0));   // the dim track
    const uint32_t p = fired ? 1000 : longpress::RingPermille(longPress, millis());
    if (p == 0) return;
    // LovyanGFX angles: 0 = 3 o'clock, clockwise. Start at 12 o'clock and never pass 360 in one call.
    const float a1 = 270.0f + 360.0f * (float)p / 1000.0f;
    const uint32_t green = lgfx::color888(0, 200, 0);
    if (a1 <= 360.0f) {
        c.fillArc(cc, cc, R0, R1, 270.0f, a1, green);
    } else {
        c.fillArc(cc, cc, R0, R1, 270.0f, 360.0f, green);
        c.fillArc(cc, cc, R0, R1, 0.0f, a1 - 360.0f, green);
    }
}

// One loop frame, at its deadline: decode, then the whole screen composed into the backbuffer at the current zoom,
// with the 60 mi map layer's codes hashed as they are drawn (B2) and the loop's band. The record is written, not
// printed.
void WeatherscopeManager::DrawLoopFrame(BandCanvas& c)
{
    LGFX_Sprite& s = c.sprite();
    if (!loopFrameDue) {   // between frames (should not happen: Update holds until a frame is due)
        DrawScenario(c);
        return;
    }
    const int64_t start = esp_timer_get_time();
    LoopRecord& rec = records[loopIndex];
    rec.startUs = start;
    wxcrop::Header h, last;
    const int64_t d0 = esp_timer_get_time();
    const bool decoded = FullScreen(s) && DecodeCrop(history[loopIndex], &h);
    rec.decodeUs = (uint32_t)(esp_timer_get_time() - d0);
    memset(rec.phaseUs, 0, sizeof rec.phaseUs);
    if (decoded &&
        wxcrop::ParseHeader(history[nHistory - 1].data, history[nHistory - 1].size, &last) == wxcrop::Status::Ok) {
        *screenIn = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(h, grid, loopIndex, last.frameEpoch, ZoomMiles(), townOrder, screenIn);
        *screenOut = wxscreens::ScreenOut();
        SpriteSink sink(s);
        RowHash rows;
        wxscreens::Profile prof;
        prof.clock = ProfClock;
        if (wxscreens::ComposeLayered(sink, *screenIn, *layer, cycle->index, screenOut, &rows, &prof)) ++loopRebuilds;
        for (int k = 0; k < wxscreens::Profile::kPhases; ++k) rec.phaseUs[k] = prof.Us(k);
        rows.Finish(rec.sha);
        rec.hashUs = rows.us;
        rec.epoch = h.frameEpoch;
    } else {
        memset(rec.sha, 0, sizeof rec.sha);
        rec.hashUs = 0;
        rec.epoch = 0;
    }
    rec.drawUs = (uint32_t)(esp_timer_get_time() - start);
    loopFrameDue = false;
    ++loopIndex;
}

// Screen 7, the legend: the bench's drawing, from WxLegend.h and the final palette. Dry has no swatch (ruled).
void WeatherscopeManager::DrawLegend(BandCanvas& c)
{
    LGFX_Sprite& s = c.sprite();
    s.fillSprite(lgfx::color888(0, 0, 0));
    s.setTextColor(lgfx::color888(255, 255, 255));
    s.setTextSize(1);
    for (const auto& row : wxlegend::kRows) s.drawString(row.label, wxlegend::kLabelX, row.y);
    for (const auto& sw : wxlegend::kSwatches) {
        if (sw.kind == wxlegend::Kind::NoData) {
            const uint16_t line = s.color565(wxpalette::kMissingHatch.r, wxpalette::kMissingHatch.g, wxpalette::kMissingHatch.b);
            const uint16_t bg = s.color565(wxpalette::kDry.r, wxpalette::kDry.g, wxpalette::kDry.b);
            for (int yy = sw.y; yy < sw.y + sw.h; ++yy)
                for (int xx = sw.x; xx < sw.x + sw.w; ++xx)
                    s.drawPixel(xx, yy, (xx + yy) % wxpalette::kHatchPeriod == 0 ? line : bg);
            continue;
        }
        wxpalette::Rgb col;
        switch (sw.kind) {
        case wxlegend::Kind::Rain: col = wxpalette::Rain(sw.code, kFrameDim); break;
        case wxlegend::Kind::Snow: col = wxpalette::Snow(sw.code); break;
        default: col = wxpalette::kNoCoverage; break;
        }
        s.fillRect(sw.x, sw.y, sw.w, sw.h, s.color565(col.r, col.g, col.b));
    }
}

void WeatherscopeManager::DrawSplash(BandCanvas& c)
{
    c.setTextSize(3);
    CenterText(c, "Weatherscope", SCREEN_SIZE_DIV_2 - 24, kFg);
    c.setTextSize(1);
    CenterText(c, "weather radar", SCREEN_SIZE_DIV_2 + 12, kDim);
}

// The face when there is nothing to cycle (not stored mode, or the frame path failed): who the device is, whether
// it is online, where its settings are, and the time it believes it is.
void WeatherscopeManager::DrawStatus(BandCanvas& c)
{
    c.setTextSize(2);
    CenterText(c, "Weatherscope", SCREEN_SIZE / 5, kFg);

    const bool online = WiFi.status() == WL_CONNECTED;
    CenterText(c, online ? "Wi-Fi connected" : "No Wi-Fi", SCREEN_SIZE * 34 / 100, online ? kAccent : kWarn);
    c.setTextSize(1);
    if (online) CenterText(c, String(WiFi.RSSI()) + " dBm", SCREEN_SIZE * 34 / 100 + 20, kDim);

    const time_t utc = time(nullptr);
    if (utc < 1600000000) {
        c.setTextSize(2);
        CenterText(c, "syncing time", SCREEN_SIZE_DIV_2 + 4, kDim);
    } else {
        struct tm t;
        gmtime_r(&utc, &t);
        char hms[12];
        snprintf(hms, sizeof(hms), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        c.setTextSize(3);
        CenterText(c, hms, SCREEN_SIZE_DIV_2 + 4, kFg);
        c.setTextSize(1);
        CenterText(c, "UTC", SCREEN_SIZE_DIV_2 + 32, kDim);
    }

    String url = "http://" + DeviceIdentity::Name() + ".local";
    url.toLowerCase();
    c.setTextSize(1);
    CenterText(c, url, SCREEN_SIZE * 76 / 100, kDim);
}

void WeatherscopeManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
