#include "WeatherscopeManager.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <new>
#include <time.h>

#include <stdlib.h>   // before WxLogic.h, which calls strtod without including it (A8.15)

#include "DeviceIdentity.h"
#include "Layout.h"
#include "SolarDim.h"
#include "TouchPoll.h"
#include "WxCrop.h"
#include "WxLegend.h"
#include "WxLogic.h"
#include "WxPalette.h"
#include "WxRender.h"
#include "WxScenarios.h"
#include "WxScreens.h"
#include "WxTowns.h"

namespace {

constexpr unsigned long SPLASH_MS = 4000;  // the splash, once per boot
constexpr int64_t kLoopPeriodUs = 70000;   // 14.29 fps (A8.20)
constexpr int kFrameDim = wxpalette::kDim55; // rain code 1 at 55 %, as the bench (final palette)

const uint32_t kFg     = lgfx::color888(230, 237, 234);
const uint32_t kDim    = lgfx::color888(130, 150, 160);
const uint32_t kAccent = lgfx::color888(90, 200, 255);
const uint32_t kWarn   = lgfx::color888(255, 176, 40);

const char* const kScenarioName[4] = {"dry", "approaching", "herenow", "warning"};

uint32_t InkColour(wxscreens::Ink ink)
{
    switch (ink) {
    case wxscreens::Ink::White: return lgfx::color888(255, 255, 255);
    case wxscreens::Ink::Dim: return lgfx::color888(130, 150, 160);
    case wxscreens::Ink::Orange: return lgfx::color888(255, 165, 0);
    default: return lgfx::color888(200, 210, 215);
    }
}

const char* ScreenName(wxlogic::Screen s)
{
    switch (s) {
    case wxlogic::Screen::Warning: return "Warning";
    case wxlogic::Screen::PrecipNow: return "PrecipNow";
    case wxlogic::Screen::PrecipApproaching: return "PrecipApproaching";
    default: return "Dry";
    }
}
const char* PrecipName(wxlogic::Precip p)
{
    return p == wxlogic::Precip::Rain ? "Rain" : p == wxlogic::Precip::Snow ? "Snow" : "None";
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

// The map layer: every pixel's code (WxRender's CodeAt), painted in the final palette, rain only (the wire carries
// no type yet), with the 57,600 codes hashed row by row as they are drawn -- the code buffer the map was drawn
// FROM, without a buffer for it (A8.20). Off the disc is painted dry (black), as the bench.
template <typename Sprite>
void RenderMap(Sprite& s, const wxcrop::Header& h, const uint8_t* grid, const wxrender::View& v, uint8_t shaOut[32],
               uint32_t* hashUs)
{
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    uint32_t hashTime = 0;
    uint8_t row[wxrender::kSize];
    s.startWrite();
    for (int y = 0; y < wxrender::kSize; ++y) {
        for (int x = 0; x < wxrender::kSize; ++x) row[x] = wxrender::CodeAt(h, grid, v, x, y);
        const int64_t t = esp_timer_get_time();
        mbedtls_sha256_update(&sha, row, sizeof row);
        hashTime += (uint32_t)(esp_timer_get_time() - t);
        int x0 = 0;
        uint8_t run = row[0];
        for (int x = 1; x <= wxrender::kSize; ++x) {
            const uint8_t c = x < wxrender::kSize ? row[x] : 0xFE;
            if (c == run) continue;
            if (run == 15) {   // missing is hatched (ruled): a pattern, pixel by pixel
                for (int xx = x0; xx < x; ++xx) {
                    const wxpalette::Rgb m = wxpalette::MissingAt(xx, y);
                    s.writePixel(xx, y, s.color565(m.r, m.g, m.b));
                }
            } else {
                const wxpalette::Rgb p = wxpalette::Code(run == wxrender::kOffDisc ? 0 : (run & 15), false, kFrameDim);
                s.writeFastHLine(x0, y, x - x0, s.color565(p.r, p.g, p.b));
            }
            x0 = x;
            run = c;
        }
    }
    s.endWrite();
    const int64_t t = esp_timer_get_time();
    mbedtls_sha256_finish(&sha, shaOut);
    mbedtls_sha256_free(&sha);
    hashTime += (uint32_t)(esp_timer_get_time() - t);
    if (hashUs) *hashUs = hashTime;
}

// One overlay item, drawn opaque inside its own rectangle and nowhere else (A8.20).
void DrawItem(LGFX_Sprite& s, const wxscreens::Item& it)
{
    const uint32_t black = lgfx::color888(0, 0, 0);
    switch (it.kind) {
    case wxscreens::ItemKind::Text:
        s.fillRect(it.r.x, it.r.y, it.r.w, it.r.h, black);
        s.setTextSize(it.size);
        s.setTextColor(InkColour(it.ink));
        s.drawString(it.text, it.r.x, it.r.y);
        break;
    case wxscreens::ItemKind::Marker:
        s.fillRect(it.r.x, it.r.y, it.r.w, it.r.h, black);
        s.drawCircle(wxrender::kSize / 2, wxrender::kSize / 2, 4, InkColour(it.ink));
        s.fillRect(wxrender::kSize / 2 - 1, wxrender::kSize / 2 - 1, 2, 2, InkColour(it.ink));
        break;
    case wxscreens::ItemKind::Edge:
        s.fillRect(it.r.x, it.r.y, it.r.w, it.r.h, InkColour(it.ink));
        break;
    case wxscreens::ItemKind::Town:
        s.fillRect(it.r.x, it.r.y, it.r.w, it.r.h, black);
        s.fillRect(it.dotX - 1, it.dotY - 1, 3, 3, lgfx::color888(255, 255, 255));
        s.setTextSize(1);
        s.setTextColor(InkColour(it.ink));
        s.drawString(it.text, it.dotX + 4, it.dotY - 4);
        break;
    }
}

}  // namespace

// ----------------------------------------------------------------------------- set-up

void WeatherscopeManager::Initialise()
{
    // Called again after every config save (main.cpp): the splash and the buffers belong to boot only.
    if (!splashStarted) {
        splashStarted = true;
        splashStartMs = millis();
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

// The frame path, once: the cached map sprite, the grid, the layout and its label scratch, the scenario, the
// view, the cycle and the loop's records. Its PSRAM cost is printed as one number (A8.20's sizes row).
void WeatherscopeManager::MakeBuffers()
{
    const size_t psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    nHistory = feeds.radar.History(history, kLoopFrames);
    size_t cells = 0;
    for (size_t i = 0; i < nHistory; ++i) {
        wxcrop::Header h;
        if (wxcrop::ParseHeader(history[i].data, history[i].size, &h) == wxcrop::Status::Ok && h.Cells() > cells)
            cells = h.Cells();
    }
    mapSprite = new LGFX_Sprite(&tft);
    mapSprite->setPsram(true);
    mapSprite->setColorDepth(16);
    const bool spriteOk = mapSprite->createSprite(wxrender::kSize, wxrender::kSize) != nullptr;
    grid = cells ? (uint8_t*)heap_caps_malloc(cells, MALLOC_CAP_SPIRAM) : nullptr;
    gridCells = grid ? cells : 0;
    layout = (wxscreens::Layout*)heap_caps_malloc(sizeof(wxscreens::Layout), MALLOC_CAP_SPIRAM);
    if (layout) new (layout) wxscreens::Layout();
    labelScratch = (wxlogic::LabelBox*)heap_caps_malloc(sizeof(wxlogic::LabelBox) * wxtowns::kCount, MALLOC_CAP_SPIRAM);
    labelScratchTown = (int*)heap_caps_malloc(sizeof(int) * wxtowns::kCount, MALLOC_CAP_SPIRAM);
    records = (LoopRecord*)heap_caps_malloc(sizeof(LoopRecord) * kLoopFrames, MALLOC_CAP_SPIRAM);
    scenario = new wxscen::Scenario();
    view = new wxrender::View();
    cycle = new wxscreens::Cycle();
    const size_t psramAfter = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    psramUsed = psramBefore - psramAfter;
    buffersReady = spriteOk && grid && layout && labelScratch && labelScratchTown && records;
    Serial.printf("[wx] psram_used=%u sprite=%d grid=%u layout=%u history=%u ready=%d\n",
                  (unsigned)(psramBefore - psramAfter), (int)spriteOk, (unsigned)gridCells,
                  (unsigned)sizeof(wxscreens::Layout), (unsigned)nHistory, (int)buffersReady);
}

bool WeatherscopeManager::DecodeFrame(int index, uint32_t* epoch)
{
    if (index < 0 || (size_t)index >= nHistory || !grid) return false;
    wxcrop::Header h;
    if (wxcrop::DecodeInto(history[index].data, history[index].size, grid, gridCells, &h) != wxcrop::Status::Ok)
        return false;
    if (epoch) *epoch = h.frameEpoch;
    return true;
}

// A8.10's rule, on the device: approaching = the LATEST frame whose nearest echo is within 15 mi of the crop's
// centre; dry = the LATEST beyond 15 mi, or with none. Printed, and graded against scenarios.ref.json (B1).
void WeatherscopeManager::PickFrames()
{
    if (!buffersReady) return;
    for (size_t i = 0; i < nHistory; ++i) {
        wxcrop::Header h;
        if (wxcrop::DecodeInto(history[i].data, history[i].size, grid, gridCells, &h) != wxcrop::Status::Ok) continue;
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

// One of the four scenarios (wxscen::Kind order), built from its frame; its map is drawn ONCE into the cached
// sprite, and the layout worked out. Everything B1 grades is printed here.
void WeatherscopeManager::BuildScenario(int kindIndex)
{
    const wxscen::Kind kind = (wxscen::Kind)kindIndex;
    const int frame = kind == wxscen::Kind::Dry ? pickDry : pickApproaching;
    uint32_t epoch = 0;
    if (!DecodeFrame(frame, &epoch)) {
        Serial.printf("[wx] scn=%s FAILED: frame %d does not decode\n", kScenarioName[kindIndex], frame);
        return;
    }
    wxcrop::Header h;
    wxcrop::ParseHeader(history[frame].data, history[frame].size, &h);
    wxrender::BuildView(h, view);
    wxscen::Build(kind, h, grid, scenario);
    const wxlogic::Choice c = scenario->Choose();
    wxscreens::Texts t;
    wxscreens::BuildTexts(*scenario, c, &t);
    wxscreens::LatLon box[2];
    wxscreens::WarningBox(*scenario, box);
    wxscreens::BuildLayout(*layout, c.screen, t, *view, c.screen == wxlogic::Screen::Warning ? box : nullptr,
                           wxtowns::kTowns, wxtowns::kCount, labelScratch, labelScratchTown);
    uint8_t sha[32];
    RenderMap(*mapSprite, h, grid, *view, sha, nullptr);

    char hh[7], hex[65];
    Hhmmss(epoch, hh);
    HexSha(sha, hex);
    // the boot-time pick and PSRAM figure again, so a trace that opens after boot still has them
    Serial.printf("[wx] pick approaching=%s dry=%s psram_used=%u\n", pickApproachingName, pickDryName,
                  (unsigned)psramUsed);
    Serial.printf("[wx] scn=%s frame=%s screen=%s type=%s warning=%d\n", kScenarioName[kindIndex], hh,
                  ScreenName(c.screen), PrecipName(c.type), c.warning);
    Serial.printf("[wx] text clock=\"%s\" headline=\"%s\" title=\"%s|%s\" until=\"%s\" lineA=\"%s\" storm=\"%s\" "
                  "travel=\"%s\"\n",
                  t.clock, t.headline, t.title1 ? t.title1 : "", t.title2 ? t.title2 : "", t.until, t.lineA, t.storm,
                  t.travel);
    Serial.printf("[wx] codes %s\n", hex);
    Serial.printf("[wx] rects n=%d fixed=%d\n", layout->n, layout->nFixed);
    for (int i = 0; i < layout->n; ++i) {
        const wxscreens::Item& it = layout->items[i];
        const char* kindName = it.kind == wxscreens::ItemKind::Text ? "text" : it.kind == wxscreens::ItemKind::Marker
                             ? "marker" : it.kind == wxscreens::ItemKind::Edge ? "edge" : "town";
        Serial.printf("[wx] rect %s %d %d,%d,%d,%d %s \"%s\"\n", kScenarioName[kindIndex], i, it.r.x, it.r.y, it.r.w,
                      it.r.h, kindName, it.text);
    }
    Serial.printf("[wx] screen=scenario %s\n", kScenarioName[kindIndex]);
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
    Serial.printf("[wx] feeds: crop %u B %s %ux%u frame=%lu | history %u frames %u B | obs valid=%d t=%ld present=%u | "
                  "alerts=%u%s%s\n",
                  (unsigned)crop.size, wxcrop::StatusName(s), (unsigned)h.rows, (unsigned)h.cols,
                  (unsigned long)h.frameEpoch, (unsigned)nh, (unsigned)histBytes, (int)o.valid, (long)o.timeUtc,
                  (unsigned)o.presentCount, (unsigned)a.count, a.count ? " first=" : "", a.count ? a.items[0].event : "");
}

// ----------------------------------------------------------------------------- the loop task

void WeatherscopeManager::Update()
{
    UpdateBrightness();

    if (mode == Mode::Splash) {
        if (millis() - splashStartMs < SPLASH_MS) return;
        if (feeds.stored && buffersReady && pickApproaching >= 0 && pickDry >= 0) {
            mode = Mode::Scenario;
            wxscreens::Step(*cycle, millis(), feeds.stored);   // starts the hold clock
            BuildScenario(cycle->index);
        } else {
            mode = Mode::Status;
        }
        return;
    }

    HandleTouch();
    HandleSerialGestures();

    // The cycle sees every tick, so a paused interval (the loop, the legend) is skipped rather than counted when
    // it resumes; it can only advance while unpaused, i.e. on the scenario screen.
    if (wxscreens::Step(*cycle, millis(), feeds.stored) && mode == Mode::Scenario) BuildScenario(cycle->index);
    if (mode == Mode::Loop) WaitForLoopFrame();
}

// A tap opens the loop: 15 frames once, oldest first, each starting at its deadline (A8.20); the cycle waits.
void WeatherscopeManager::StartLoop()
{
    wxscreens::Pause(*cycle, true);
    mode = Mode::Loop;
    loopIndex = 0;
    loopFrameDue = false;
    loopT0Us = esp_timer_get_time() + kLoopPeriodUs;
    Serial.println("[wx] screen=loop");
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
    Serial.printf("[wx] loop records=%d period_us=%ld\n", kLoopFrames, (long)kLoopPeriodUs);
    for (int i = 0; i < kLoopFrames; ++i) {
        char hex[65];
        HexSha(records[i].sha, hex);
        Serial.printf("[wx] lrec %d epoch=%lu sha=%s start_us=%lld draw_us=%lu hash_us=%lu\n", i,
                      (unsigned long)records[i].epoch, hex, (long long)records[i].startUs,
                      (unsigned long)records[i].drawUs, (unsigned long)records[i].hashUs);
    }
    wxscreens::Pause(*cycle, false);
    Serial.printf("[wx] screen=scenario %s\n", kScenarioName[cycle->index]);
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
    Serial.printf("[wx] screen=scenario %s\n", kScenarioName[cycle->index]);
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

void WeatherscopeManager::HandleTouch()
{
    int32_t tx = 0, ty = 0;
    const TouchPoll poll = ReadTouch(tft, http, tx, ty);
    if (poll == TouchPoll::Skipped) return;
    if (poll == TouchPoll::Touched) {
        if (!wasTouched) { wasTouched = true; touchStartX = tx; touchStartY = ty; }
        touchLastX = tx;
        touchLastY = ty;
        return;
    }
    if (!wasTouched) return;
    wasTouched = false;
    const int dx = touchLastX - touchStartX, dy = touchLastY - touchStartY;
    if (abs(dx) < 40 && abs(dy) < 40) { OnTap(); return; }
    if (abs(dx) >= abs(dy) && dx < 0) OnSwipeLeft();
}

// The bench's gestures: a 'T' byte is a tap and an 'S' a left swipe, fed to the SAME handlers as the glass. Only in
// stored mode, and like stored mode it must be unreachable on a customer unit before any Weatherscope ships.
void WeatherscopeManager::HandleSerialGestures()
{
    while (Serial.available()) {
        const int ch = Serial.read();
        if (!feeds.stored) continue;
        Serial.printf("[wx] gesture %c\n", ch == 'T' || ch == 'S' ? (char)ch : '?');
        if (ch == 'T') OnTap();
        else if (ch == 'S') OnSwipeLeft();
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
}

// The scenario's cached map, then every overlay in layout order. The map is a straight copy of the sprite drawn
// once in BuildScenario: both are 240 x 240 at 16 bpp, so the bytes are the same representation.
void WeatherscopeManager::DrawScenario(BandCanvas& c)
{
    LGFX_Sprite& s = c.sprite();
    if (s.width() == wxrender::kSize && s.height() == wxrender::kSize && s.getColorDepth() == mapSprite->getColorDepth())
        memcpy(s.getBuffer(), mapSprite->getBuffer(), (size_t)s.bufferLength());
    else
        mapSprite->pushSprite(&s, 0, 0);
    for (int i = 0; i < layout->n; ++i) DrawItem(s, layout->items[i]);
}

// One loop frame, at its deadline: decode, map (hashed as drawn), the frame's clock and age, the progress bar, and
// the scenario's town labels where they clear the loop's own overlays. The record is written, not printed.
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
    uint32_t epoch = 0;
    wxcrop::Header h;
    if (DecodeFrame(loopIndex, &epoch) &&
        wxcrop::ParseHeader(history[loopIndex].data, history[loopIndex].size, &h) == wxcrop::Status::Ok) {
        RenderMap(s, h, grid, *view, rec.sha, &rec.hashUs);
    } else {
        memset(rec.sha, 0, sizeof rec.sha);
        rec.hashUs = 0;
    }
    rec.epoch = epoch;

    wxcrop::Header last;
    wxcrop::ParseHeader(history[nHistory - 1].data, history[nHistory - 1].size, &last);
    char clock[16], ago[32];
    wxscreens::ClockText(epoch, wxscen::kOffsetMin, clock, sizeof clock);
    const long mins = ((long)last.frameEpoch - (long)epoch) / 60;
    if (mins <= 0) snprintf(ago, sizeof ago, "NOW");
    else snprintf(ago, sizeof ago, "%ld MIN AGO", mins);
    // The loop's own overlays (not graded by pixel; B2 grades the records): "N", the frame's clock and age, and the
    // progress bar's box. Four items on the stack -- no allocation while the loop plays.
    wxscreens::Item fixed[4];
    const char* texts[3] = {"N", clock, ago};
    const int sizes[3] = {1, 2, 1}, ys[3] = {4, 168, 186};
    const wxscreens::Ink inks[3] = {wxscreens::Ink::Dim, wxscreens::Ink::White, wxscreens::Ink::Dim};
    for (int i = 0; i < 3; ++i) {
        fixed[i].r = wxscreens::TextBox(strlen(texts[i]), sizes[i], ys[i]);
        fixed[i].kind = wxscreens::ItemKind::Text;
        fixed[i].size = (uint8_t)sizes[i];
        fixed[i].ink = inks[i];
        fixed[i].dotX = fixed[i].dotY = 0;
        snprintf(fixed[i].text, sizeof fixed[i].text, "%s", texts[i]);
        DrawItem(s, fixed[i]);
    }
    fixed[3].r = wxscreens::Rect{60, 203, 124, 8};   // the progress bar: a track and a knob
    s.fillRect(fixed[3].r.x, fixed[3].r.y, fixed[3].r.w, fixed[3].r.h, lgfx::color888(0, 0, 0));
    s.fillRect(60, 206, 124, 2, InkColour(wxscreens::Ink::Dim));
    s.fillRect(60 + loopIndex * 120 / (kLoopFrames - 1), 203, 4, 8, InkColour(wxscreens::Ink::White));
    for (int i = layout->nFixed; i < layout->n; ++i)
        if (wxscreens::ClearOf(layout->items[i].r, fixed, 4)) DrawItem(s, layout->items[i]);
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
