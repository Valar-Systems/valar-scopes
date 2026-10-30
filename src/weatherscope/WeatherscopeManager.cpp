#include "WeatherscopeManager.h"

#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <new>
#include <stdarg.h>
#include <time.h>

#include <stdlib.h>   // before WxLogic.h, which calls strtod without including it (A8.15)

#include "DeviceIdentity.h"
#include "FrameBuffer.h"
#include "DiagText.h"      // /diag/wx (A9.42)
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
#include "WxTheme.h"
#include "WxTowns.h"

namespace {

constexpr unsigned long SPLASH_MS = 4000;  // the splash, once per boot
constexpr int64_t kLoopPeriodUs = 70000;   // 14.29 fps (A8.20)

// Every colour the manager draws is a role in WxTheme.h (3.5t, A12.47), read through Th(): the theme on the screen,
// which wx-theme and Auto's day/night phase choose (t5, A12.66). Their names, as the lines print them:
const char* WhichName(wxtheme::Which w) { return w == wxtheme::Which::Light ? "light" : "dark"; }
const char* SettingName(wxtheme::Setting s)
{
    return s == wxtheme::Setting::Light ? "light" : s == wxtheme::Setting::Dark ? "dark" : "auto";
}
const char* ReadName(wxtheme::Read r)
{
    return r == wxtheme::Read::Known ? "known" : r == wxtheme::Read::Unset ? "unset" : "unknown";
}
const char* PhaseName(daynight::Phase p)
{
    return p == daynight::Phase::Day ? "Day" : p == daynight::Phase::Night ? "Night" : "Unknown";
}
uint32_t C888(wxraster::Rgb c) { return lgfx::color888(c.r, c.g, c.b); }

// The stored cycle (A12.53): Dallas's six in wxscen::Kind's order (Q15), then 3.5b's Bend two (A10.4) -- Bend dry, the
// SYNTHETIC all-dry crop (Q33), and Bend rain, the loop's last frame -- each drawn with Bend's package.
constexpr int kCycleScenes = wxscreens::kScenarios + 2;
constexpr int kBendDry = wxscreens::kScenarios, kBendRain = wxscreens::kScenarios + 1;
const char* const kScenarioName[kCycleScenes] = {"dry", "approaching", "herenow", "warning", "stale", "clear",
                                                 "bdry", "brain"};

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

// FNV-1a over a buffer: the basemap image's identity in its line (A12.53), equal to the host's for the same view.
uint32_t Fnv(const uint8_t* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
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
    // 3.5t t5 (A12.66): wx-theme, read exactly (TP). An unknown value reads as Auto and is NAMED in the line below --
    // its first 24 bytes (free text, so a prefix is a true prefix) and its length -- never coerced.
    const String ts = configServer.GetStoredString("wx-theme");
    const wxtheme::Parsed tp = wxtheme::ParseSetting(ts.c_str());
    themeSetting = tp.setting;
    themeRead = tp.read;
    char named[100] = "";
    if (themeRead == wxtheme::Read::Unknown) {
        char head[25], tok[76];
        snprintf(head, sizeof head, "%s", ts.c_str());
        wxscenes::Token(head, tok, sizeof tok);
        snprintf(named, sizeof named, " stored=%s len=%u", tok, (unsigned)ts.length());
    }

    currentBrightness = configuredBrightness;
    tft.setBrightness(currentBrightness);
    lastBrightnessCheck = 0;

    // Flags only: the location itself is never logged.
    Serial.printf("[wx] init; location=%s autodim=%d brightness=%u stored=%d theme=%s read=%s%s\n",
                  hasLatLon ? "set" : "unset", (int)autoDim, (unsigned)configuredBrightness, (int)feeds.stored,
                  SettingName(themeSetting), ReadName(themeRead), named);
}

// A PSRAM object, constructed in place: nullptr when the allocation fails.
template <typename T>
static T* NewInPsram()
{
    void* p = heap_caps_malloc(sizeof(T), MALLOC_CAP_SPIRAM);
    return p ? new (p) T() : nullptr;
}

// 3.5b's basemap, held beside the layer (A10.2, A12.53): the validated package; the view's places, peaks and their
// ranking; the image's key and what its build drew; the vertex scratch; the image. One PSRAM block (9,776 B on the
// board); the image (57,600 B) and the scratch (maxVertices x 32 B) are blocks of their own.
struct WeatherscopeManager::Basemap {
    bmpk::Package pk;
    wxscenes::PackagePlaces places;
    wxscenes::PackagePeaks peaks;
    int order[wxscenes::kMaxPlaces];
    wxscreens::BaseKey key;
    bool keyValid = false;
    wxbase::BuildInfo info;
    wxbase::Scratch scratch;
    uint8_t* image = nullptr;
    uint32_t psramFree = 0, allocImage = 0, allocState = 0, allocScratch = 0;
};

// The frame path, once: the cached screen sprite, the grid, the scenario, the screen's input and output, the view,
// the town ranking, the cycle and the loop's records. Its PSRAM cost is printed as one number (A9.5's sizes).
void WeatherscopeManager::MakeBuffers()
{
    const size_t psramBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    nHistory = feeds.radar.History(history, kLoopFrames);
    nBendHistory = feeds.bendRadar ? feeds.bendRadar->History(bendHistory, kLoopFrames) : 0;
    // The grid holds the largest crop either place decodes (A12.53: Bend's 44,640 cells against Dallas's 38,520).
    size_t cells = 0;
    for (size_t i = 0; i <= nHistory + 1 + nBendHistory; ++i) {
        const wx::CropBytes& c = i < nHistory ? history[i]
                               : i == nHistory ? feeds.clear
                               : i <= nHistory + nBendHistory ? bendHistory[i - nHistory - 1]
                               : feeds.bendClear;
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

    // 3.5b's group (A12.53), allocated after psram_used so A9.23's line still grades its own: the state, the image and
    // the vertex scratch, each block's allocated size printed beside the free delta (A12.37 item 3). The package is
    // validated in place, from flash, before the scratch is sized from it; its terrain stays in flash.
    const size_t baseBefore = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    bmap = NewInPsram<Basemap>();
    int status = -1;   // no package bytes
    bool blocksOk = false;
    if (bmap) {
        bmap->image = (uint8_t*)heap_caps_malloc((size_t)wxbase::kSize * wxbase::kSize, MALLOC_CAP_SPIRAM);
        if (!feeds.bendPackage.Empty())
            status = (int)bmpk::Validate(feeds.bendPackage.data, feeds.bendPackage.size, &bmap->pk);
        const uint32_t cap = status == (int)bmpk::Status::kOk ? bmap->pk.maxVertices : 0;
        uint8_t* block = cap ? (uint8_t*)heap_caps_malloc((size_t)cap * 32, MALLOC_CAP_SPIRAM) : nullptr;
        if (block) {   // the doubles first (x, y, crossings), then qx and qy: every member on its own alignment
            bmap->scratch.x = (double*)block;
            bmap->scratch.y = bmap->scratch.x + cap;
            bmap->scratch.cross = bmap->scratch.y + cap;
            bmap->scratch.qx = (int32_t*)(bmap->scratch.cross + cap);
            bmap->scratch.qy = bmap->scratch.qx + cap;
            bmap->scratch.cap = cap;
        }
        bmap->psramFree = (uint32_t)(baseBefore - heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        bmap->allocState = (uint32_t)heap_caps_get_allocated_size(bmap);
        bmap->allocImage = bmap->image ? (uint32_t)heap_caps_get_allocated_size(bmap->image) : 0;
        bmap->allocScratch = block ? (uint32_t)heap_caps_get_allocated_size(block) : 0;
        blocksOk = bmap->image && (block || cap == 0);
    }
    pkgOk = status == (int)bmpk::Status::kOk && blocksOk;
    const bool bendReady = nBendHistory == (size_t)kLoopFrames && !feeds.bendClear.Empty();
    if (cycle) cycle->count = bendReady ? kCycleScenes : wxscreens::kScenarios;
    size_t bendBytes = 0;
    for (size_t i = 0; i < nBendHistory; ++i) bendBytes += bendHistory[i].size;
    // Sizes, a status and the package's CRC: never its origin.
    Serial.printf("[wx] bend history=%u frames=%u clear=%u package=%u ok=%d status=%d crc=%08lx sections=%d "
                  "max_vertices=%u\n",
                  (unsigned)nBendHistory, (unsigned)bendBytes, (unsigned)feeds.bendClear.size,
                  (unsigned)feeds.bendPackage.size, (int)pkgOk, status, (unsigned long)(bmap ? bmap->pk.crc32 : 0),
                  bmap ? bmap->pk.nSections : 0, (unsigned)(bmap ? bmap->pk.maxVertices : 0));
    char line[120];
    if (PsramBaseLine(line, sizeof line) > 0) Serial.print(line);
}

// [wx] psram_base: 3.5b's group's free delta and each block's allocated size (A12.53). ok = every block the package
// needs was allocated: the state, the image, and the scratch unless the package has no vertex to hold.
int WeatherscopeManager::PsramBaseLine(char* out, size_t cap) const
{
    if (!bmap) return snprintf(out, cap, "[wx] psram_base=0 image=0 state=0 scratch=0 ok=0\n");
    const bool ok = bmap->image && (bmap->scratch.cap || bmap->pk.maxVertices == 0);
    return snprintf(out, cap, "[wx] psram_base=%lu image=%lu state=%lu scratch=%lu ok=%d\n",
                    (unsigned long)bmap->psramFree, (unsigned long)bmap->allocImage, (unsigned long)bmap->allocState,
                    (unsigned long)bmap->allocScratch, (int)ok);
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
// THE INSTRUMENT (A9.34): each phase timed, every print through Out(), the record kept for 'R'. The hash and the
// k/prof/pick lines are bench-only, so they run only in stored mode (today BuildScenario runs only there anyway).
void WeatherscopeManager::BuildScenario()
{
    const int64_t t0 = esp_timer_get_time();
    composedWhich = which;   // t5: set first, so a build that fails is not retried every Update
    timingOut = true;
    outUs = 0;
    outN = outShort = 0;
    const bool bench = feeds.stored;
    const int kindIndex = cycle->index;
    const bool bend = OnBend();
    // Bend's two are Kind::Dry builds: Bend dry on its synthetic clear crop, Bend rain on the loop's last frame, whose
    // nearest echo lies beyond 15 mi (A12.53, Q54).
    const wxscen::Kind kind = bend ? wxscen::Kind::Dry : (wxscen::Kind)kindIndex;
    const wx::CropBytes& crop = kindIndex == kBendDry ? feeds.bendClear
                              : kindIndex == kBendRain ? bendHistory[nBendHistory - 1]
                              : kind == wxscen::Kind::Clear ? feeds.clear
                              : history[kind == wxscen::Kind::Dry ? pickDry : pickApproaching];
    wxcrop::Header h;
    const bool decoded = DecodeCrop(crop, &h);
    const int64_t tDecoded = esp_timer_get_time();
    const bool built = decoded && wxscen::Build(kind, h, grid, scenario);
    if (built && kindIndex == kBendDry) scenario->synthetic = true;   // Q33: a synthetic crop, labelled so
    const int64_t tBuilt = esp_timer_get_time();
    if (!built) {
        Out("[wx] scn=%s FAILED: its crop does not decode or build\n", kScenarioName[kindIndex]);
        timingOut = false;
        return;
    }
    PrintCopyStats();   // the hold that ended (A9.22's P4); through Out(), so its time is print time
    const int R = ZoomMiles();
    const int64_t tFrom0 = esp_timer_get_time();
    *screenIn = wxscreens::ScreenIn();
    wxscenes::FromScenario(*scenario, h, grid, R, townOrder, screenIn, bend ? wxscen::kBendOffsetMin : wxscen::kOffsetMin);
    screenIn->which = which;
    if (bend) {
        EnsureBase(h, "scenario");
        ApplyPackage();
    }
    *screenOut = wxscreens::ScreenOut();
    const int64_t tFrom1 = esp_timer_get_time();
    SpriteSink sink(*screenSprite);
    wxscreens::Profile prof;
    prof.clock = ProfClock;
    wxscreens::Compose(sink, *screenIn, *view, screenOut, nullptr, &prof);
    const int64_t tComposed = esp_timer_get_time();
    const uint32_t composeUs = (uint32_t)(tComposed - tFrom1);

    uint8_t sha[32];
    uint32_t shaUs = 0;
    if (bench) {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, (const uint8_t*)screenSprite->getBuffer(), (size_t)wxrender::kSize * wxrender::kSize * 2);
        mbedtls_sha256_finish(&ctx, sha);
        mbedtls_sha256_free(&ctx);
        shaUs = (uint32_t)(esp_timer_get_time() - tComposed);
    }

    char hh[7], hex[65], row[24], tok[160], tok2[160];
    Hhmmss(h.frameEpoch, hh);
    if (bend)   // k35b.ref.txt's names: B every layer with terrain off, C with it on (A10.6)
        snprintf(row, sizeof row, "%s-%d-%c", kScenarioName[kindIndex], R, screenIn->terrain ? 'C' : 'B');
    else
        snprintf(row, sizeof row, "%s-%d", kScenarioName[kindIndex], R);
    if (bench) {
        HexSha(sha, hex);
        // the boot-time pick and PSRAM figures again, so a trace that opens after boot still has them
        Out("[wx] pick approaching=%s dry=%s psram_used=%u\n", pickApproachingName, pickDryName, (unsigned)psramUsed);
        char line[120];
        if (PsramBaseLine(line, sizeof line) > 0) Out("%s", line);
    }
    Out("[wx] scn=%s R=%d frame=%s screen=%s synthetic=%d compose_us=%lu\n", kScenarioName[kindIndex], R, hh,
        ScreenName(scenario->Choose().screen), (int)scenario->synthetic, (unsigned long)composeUs);
    if (bench) {
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
    }
    Out("[wx] screen=scenario %s R=%d\n", kScenarioName[kindIndex], R);

    BuildRecord& r = builds[nBuilds % kBuildRecords];
    r = BuildRecord();
    r.scene = (uint8_t)kindIndex;
    r.fromMi = (uint16_t)(buildFromMi ? buildFromMi : R);
    r.toMi = (uint16_t)R;
    snprintf(r.why, sizeof r.why, "%s", buildWhy);
    r.decodeUs = (uint32_t)(tDecoded - t0);
    r.scenUs = (uint32_t)(tBuilt - tDecoded);
    r.fromUs = (uint32_t)(tFrom1 - tFrom0);
    r.composeUs = composeUs;
    r.shaUs = shaUs;
    r.printUs = (uint32_t)outUs;
    r.totalUs = (uint32_t)(esp_timer_get_time() - t0);
    r.prints = outN;
    r.shortWrites = outShort;
    ++nBuilds;
    timingOut = false;
    buildWhy = "cycle";
    buildFromMi = 0;
    curFrame.flags |= kRebuilt;
    curFrame.scene = (uint8_t)kindIndex;
    curFrame.radiusMi = (uint16_t)R;
}

// THE INSTRUMENT (A9.34): a print, timed (formatting included) while a BuildScenario is being timed; a short write is
// counted. The bytes are printf's own. A9.42 (item 2): every write is also recorded with the USB path it met --
// isPlugged() and isConnected() (which write() itself calls first, so sampling it perturbs nothing a write would not)
// and the ring's free space, all taken BEFORE the timed write.
size_t WeatherscopeManager::Out(const char* fmt, ...)
{
    const int64_t t0 = esp_timer_get_time();
    char buf[320];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t w = 0;
    if (n > 0) {
        if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
        WriteRecord& r = writeLog[writeSeq % kWriteLog];
        r = WriteRecord();
#if ARDUINO_USB_MODE
        r.freeBefore = (uint16_t)Serial.availableForWrite();
        r.plugged = HWCDC::isPlugged() ? 1 : 0;
        r.connected = Serial.isConnected() ? 1 : 0;
#else
        r.plugged = r.connected = 2;
#endif
        const int64_t w0 = esp_timer_get_time();
        w = Serial.write((const uint8_t*)buf, (size_t)n);
        const int64_t w1 = esp_timer_get_time();
        r.tUs = (uint32_t)w0;
        r.elapsedUs = (uint32_t)(w1 - w0);
        r.asked = (uint16_t)n;
        r.written = (uint16_t)w;
        ++writeSeq;
    }
    if (timingOut) {
        outUs += esp_timer_get_time() - t0;
        ++outN;
        if (n > 0 && w < (size_t)n) ++outShort;
    }
    return w;
}

bool WeatherscopeManager::OnBend() const { return cycle && cycle->index >= wxscreens::kScenarios; }

// The basemap for a Bend screen at the current zoom (A10.2's key, A12.53): rebuilt -- the image, the places, their
// ranking and the peaks, by the K rows' own path -- only when BaseWhy names a reason, and printed either way. Never
// called inside a loop frame. A place list the screen would refuse (over kMaxPlaces) fails the build, as Build refuses a
// partial basemap: no partial package is ever drawn.
void WeatherscopeManager::EnsureBase(const wxcrop::Header& h, const char* at)
{
    if (!pkgOk) return;   // no package: 3.5a's path (A9.1 D)
    Basemap& b = *bmap;
    const int R = ZoomMiles();
    const wxscreens::BaseKey k = wxscreens::MakeBaseKey(h, b.pk.crc32, b.pk.size, R, wxbase::kMaskAll, terrain);
    const char* why = wxscreens::BaseWhy(b.key, b.keyValid, k);
    uint32_t us = 0;
    if (why) {
        const int64_t t0 = esp_timer_get_time();
        wxrender::View v;
        wxrender::BuildViewKm(h, &v, R * 1.609344);   // the view Compose builds for this radius
        const bool placesOk = b.places.Load(b.pk, v);
        wxscreens::RankOrder(b.places.Pops(), b.places.Count(), b.order);
        b.peaks.Load(b.pk, v);
        b.info = wxbase::BuildInfo();
        const bool drawn = wxbase::Build(b.pk, v, R, wxbase::kMaskAll, terrain, b.scratch, b.image, &b.info);
        b.key = k;
        b.keyValid = drawn && placesOk;
        us = (uint32_t)(esp_timer_get_time() - t0);
    }
    Out("[wx] base build_us=%lu rebuilt=%s at=%s R=%d terrain=%d level=%d rings=%d rivers=%d roads=%d places=%d "
        "peaks=%d fnv=%08lx\n",
        (unsigned long)us, why ? why : "no", at, R, (int)b.key.terrain, (int)b.info.terrain, b.info.rings,
        b.info.rivers, b.info.roads, b.places.Count(), b.peaks.Count(),
        (unsigned long)Fnv(b.image, (size_t)wxbase::kSize * wxbase::kSize));
}

// The package's part of the screen (FromPackage), with the image's own terrain: an image and the other terrain state
// are never paired. Nothing when there is no valid image (3.5a's path).
void WeatherscopeManager::ApplyPackage()
{
    if (!pkgOk || !bmap->keyValid) return;
    Basemap& b = *bmap;
    wxscenes::FromPackage(b.pk, b.image, wxbase::kMaskAll, b.key.terrain != 0, &b.places, b.order, &b.peaks, screenIn);
}

// THE INSTRUMENT (A9.34, A9.42): the build records, the last 64 writes and the frame log, as lines, to a sink.
void WeatherscopeManager::RenderRecords(RecordSink& out)
{
    char line[200];
    int k;
    const int nb = nBuilds < kBuildRecords ? nBuilds : kBuildRecords;
    k = snprintf(line, sizeof line, "[wx] builds n=%d of %d\n", nb, nBuilds);
    out.Line(line, (size_t)k);
    for (int i = 0; i < nb; ++i) {
        const BuildRecord& r = builds[nBuilds <= kBuildRecords ? i : (nBuilds + i) % kBuildRecords];
        k = snprintf(line, sizeof line, "[wx] build %d why=%s scene=%s mi=%u->%u decode=%lu scen=%lu from=%lu "
                     "compose=%lu sha=%lu print=%lu total=%lu prints=%u short=%u frame=%lu\n", i, r.why,
                     kScenarioName[r.scene], (unsigned)r.fromMi, (unsigned)r.toMi, (unsigned long)r.decodeUs,
                     (unsigned long)r.scenUs, (unsigned long)r.fromUs, (unsigned long)r.composeUs,
                     (unsigned long)r.shaUs, (unsigned long)r.printUs, (unsigned long)r.totalUs, (unsigned)r.prints,
                     (unsigned)r.shortWrites, (unsigned long)r.frameUs);
        out.Line(line, (size_t)(k < (int)sizeof line ? k : (int)sizeof line - 1));
    }
    const uint32_t nw = writeSeq < (uint32_t)kWriteLog ? writeSeq : (uint32_t)kWriteLog;
    k = snprintf(line, sizeof line, "[wx] writes n=%lu of %lu\n", (unsigned long)nw, (unsigned long)writeSeq);
    out.Line(line, (size_t)k);
    for (uint32_t i = 0; i < nw; ++i) {
        const uint32_t seq = writeSeq - nw + i;
        const WriteRecord& r = writeLog[seq % kWriteLog];
        k = snprintf(line, sizeof line, "[wx] write %lu t=%lu asked=%u wrote=%u free=%u plugged=%u connected=%u us=%lu\n",
                     (unsigned long)seq, (unsigned long)r.tUs, (unsigned)r.asked, (unsigned)r.written,
                     (unsigned)r.freeBefore, (unsigned)r.plugged, (unsigned)r.connected, (unsigned long)r.elapsedUs);
        out.Line(line, (size_t)k);
    }
    const uint32_t n = frameSeq < (uint32_t)kFrameLog ? frameSeq : (uint32_t)kFrameLog;
    k = snprintf(line, sizeof line, "[wx] frames n=%lu of %lu\n", (unsigned long)n, (unsigned long)frameSeq);
    out.Line(line, (size_t)k);
    uint32_t prev = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint32_t seq = frameSeq - n + i;
        const FrameRecord& f = frameLog[seq % kFrameLog];
        k = snprintf(line, sizeof line, "[wx] frame %lu start=%lu dt=%lu ring=%u flags=%c%c%c scene=%s mi=%u\n",
                     (unsigned long)seq, (unsigned long)f.startUs, (unsigned long)(i ? f.startUs - prev : 0),
                     (unsigned)f.permille, (f.flags & kHeld) ? 'H' : '-', (f.flags & kFired) ? 'F' : '-',
                     (f.flags & kRebuilt) ? 'R' : '-', kScenarioName[f.scene], (unsigned)f.radiusMi);
        out.Line(line, (size_t)k);
        prev = f.startUs;
    }
}

namespace {
// 'R' (A9.42): each line waits, at most 50 ms, for the ring to have room, so the bench's own dump arrives whole at any
// TX timeout. Bench-only, on a command; never in the draw path.
class PacedSerial : public WeatherscopeManager::RecordSink {
public:
    void Line(const char* s, size_t n) override
    {
        const uint32_t t0 = millis();
        while (Serial.availableForWrite() < (int)n && millis() - t0 < 50) vTaskDelay(1);
        Serial.write((const uint8_t*)s, n);
    }
};
class TextBuffer : public WeatherscopeManager::RecordSink {
public:
    TextBuffer(char* buf, size_t cap) : buf_(buf), cap_(cap) {}
    void Line(const char* s, size_t n) override
    {
        if (len_ + n > cap_) n = cap_ - len_;
        memcpy(buf_ + len_, s, n);
        len_ += n;
    }
    size_t Len() const { return len_; }

private:
    char* buf_;
    size_t cap_, len_ = 0;
};
}  // namespace

// On 'R': the records, paced, then cleared.
void WeatherscopeManager::PrintRecords()
{
    PacedSerial out;
    RenderRecords(out);
    nBuilds = 0;
    frameSeq = 0;
    writeSeq = 0;
}

// /diag/wx (A9.42): a request from the route is answered here, on the loop task, with the same text as 'R'. Nothing is
// cleared, so a read over Wi-Fi does not disturb what the bench reads later.
void WeatherscopeManager::ServiceDiagText()
{
    const uint32_t n = diagtext::Pending();
    if (!n) return;
    if (!diagBuf) diagBuf = (char*)heap_caps_malloc(diagtext::kCapacity, MALLOC_CAP_SPIRAM);
    if (!diagBuf) return;
    TextBuffer out(diagBuf, diagtext::kCapacity);
    RenderRecords(out);
    diagtext::Publish(n, diagBuf, out.Len());
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
    MaybeSwitchTheme();

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

    // A fired hold's zoom, one frame after the fire: that frame showed the complete ring first (A9.34).
    if (pendingZoom >= 0) {
        const int idx = pendingZoom;
        pendingZoom = -1;
        if (mode == Mode::Scenario) ApplyZoom(idx, "hold");
    }
    HandleTouch();
    HandleSerialGestures();
    MaybeSwitchTheme();   // after 'O'
    ServiceDiagText();   // A9.42: /diag/wx, answered on this task

    // The cycle sees every tick, so a paused interval (the loop, the legend) is skipped rather than counted when
    // it resumes; it can only advance while unpaused, i.e. on the scenario screen.
    if (wxscreens::Step(*cycle, millis(), feeds.stored) && mode == Mode::Scenario) BuildScenario();
    // t5 (A12.66): the cached scenario in another theme than the screen's -- a switch here, or one that waited over the
    // legend or a loop -- is recomposed now, build reason "theme".
    if (mode == Mode::Scenario && composedWhich != which) {
        buildWhy = "theme";
        buildFromMi = ZoomMiles();
        BuildScenario();
    }
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
    Serial.printf("[wx] zoom %s %d -> %d mi\n", why, before, ZoomMiles());
    buildWhy = why;
    buildFromMi = before;
    if (mode == Mode::Scenario) BuildScenario();
}

// A tap opens the loop: 15 frames once, oldest first, each starting at its deadline (A8.20), at the current zoom
// (Q19); the cycle waits.
void WeatherscopeManager::StartLoop()
{
    wxscreens::Pause(*cycle, true);
    // The frame-invariant layer, built HERE at the tap and never inside a frame's draw (A9.23): the first frame's
    // deadline is set after it. Rebuilt on every tap, so no layer outlives the loop it was built for. On a Bend scenario
    // the loop is Bend's (A10.4), over the basemap the scenario showed: reused here, never rebuilt inside a frame.
    const bool bend = OnBend();
    const wx::CropBytes* frames = bend ? bendHistory : history;
    const size_t n = bend ? nBendHistory : nHistory;
    wxcrop::Header h0, last;
    if (wxcrop::ParseHeader(frames[0].data, frames[0].size, &h0) == wxcrop::Status::Ok &&
        wxcrop::ParseHeader(frames[n - 1].data, frames[n - 1].size, &last) == wxcrop::Status::Ok) {
        *screenIn = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(h0, grid, 0, last.frameEpoch, ZoomMiles(), townOrder, screenIn,
                                bend ? wxscen::kBendOffsetMin : wxscen::kOffsetMin);
        screenIn->which = which;   // the layer is built in the shown theme; no switch lands inside the loop (t5)
        if (bend) {
            EnsureBase(h0, "tap");
            ApplyPackage();
        }
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

// A fired long press: 60 or 30 -> 15, 15 -> 60 (longpress::ToggleTarget, unchanged). NOT NOW (A9.34): the fire's own
// frame draws the ring complete at the old radius; the rebuild runs at the start of the next Update.
void WeatherscopeManager::OnHoldFired()
{
    if (mode != Mode::Scenario) return;
    const int target = longpress::ToggleTarget(zoomIdx, zoomLadder.Top());
    if (target >= 0) pendingZoom = target;
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
    TouchPoll poll = ReadTouch(tft, http, tx, ty);
    if (benchPress) {   // 'P' (stored mode only, A9.34): a synthetic finger at the centre, through this same path
        poll = TouchPoll::Touched;
        tx = SCREEN_SIZE_DIV_2;
        ty = SCREEN_SIZE_DIV_2;
    }
    if (poll == TouchPoll::Skipped) return;
    const uint32_t now = millis();
    const bool touched = poll == TouchPoll::Touched;
    if (touched) {
        curFrame.flags |= kHeld;
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
        const bool known = ch == 'T' || ch == 'S' || ch == 'U' || ch == 'D' || ch == 'H' || ch == 'P' || ch == 'L' ||
                           ch == 'R' || ch == 'G' || ch == 'O';
        Serial.printf("[wx] gesture %c\n", known ? (char)ch : '?');
        if (!known) continue;
        if (ch == 'P') { benchPress = true; continue; }   // A9.34: the press is HandleTouch's to classify
        if (ch == 'L') { benchPress = false; continue; }
        if (ch == 'R') { PrintRecords(); continue; }
        if (ch == 'G') { OnTerrainByte(); continue; }     // A12.53: a setting, not a touch (no idle-return reset)
        if (ch == 'O') { OnAutoOffsetByte(); continue; }  // t5 (A12.66): the clock Auto reads, +15 min; not a touch
        lastTouchMs = millis();
        if (ch == 'T') OnTap();
        else if (ch == 'S') OnSwipeLeft();
        else if (ch == 'U') OnSwipeVertical(-1);
        else if (ch == 'D') OnSwipeVertical(+1);
        else OnHoldFired();
    }
}

// The bench byte G (Q29, A12.53): terrain on or off, on the scenario screen only, as a zoom is. A Bend scenario
// recomposes at once; a Dallas one has no package for terrain to change, so it waits for the next Bend screen.
void WeatherscopeManager::OnTerrainByte()
{
    if (mode != Mode::Scenario) {
        Serial.println("[wx] terrain ignored");
        return;
    }
    terrain = !terrain;
    Serial.printf("[wx] terrain %s\n", terrain ? "on" : "off");
    if (OnBend()) {
        buildWhy = "terrain";
        buildFromMi = ZoomMiles();
        BuildScenario();
    }
}

// ----------------------------------------------------------------------------- brightness

void WeatherscopeManager::UpdateBrightness()
{
    if (lastBrightnessCheck != 0 && millis() - lastBrightnessCheck < 20000) return;
    lastBrightnessCheck = millis();
    EvaluateDayNight();
}

// Auto's evaluation (3.5t t5, A12.66), on the 20 s tick and on 'O': the day/night phase with its hysteresis
// (daynight::Evaluate, t1) on the clock 'O' may have moved, then the dim from that same phase (TD). Two named changes
// from before t5, as TD rules them: the dim follows the band (on at -1.833 deg, off at +0.167 deg), and a lost clock
// keeps the last phase rather than un-dimming. The brightness rule is unchanged. The theme follows in MaybeSwitchTheme.
void WeatherscopeManager::EvaluateDayNight()
{
    const time_t utc = time(nullptr);
    phase = daynight::Evaluate(phase, utc > 1600000000, hasLatLon, deviceLat, deviceLon, utc + (time_t)autoOffsetMin * 60);
    const bool dim = wxtheme::NightDim(autoDim, phase);
    uint8_t target = configuredBrightness;
    if (dim) {
        target = configuredBrightness / 5;
        if (target < 10) target = 10;
    }
    if (dim != nightDim) Serial.printf("[wx] dim=%d brightness=%u\n", (int)dim, (unsigned)target);
    nightDim = dim;
    if (target != currentBrightness) {
        currentBrightness = target;
        tft.setBrightness(target);
    }
}

// The theme the setting and the phase choose (wxtheme::Select). Never inside a loop: there it waits, and applies at
// the first Update after the loop ends, so the loop's layer is never rebuilt by a switch (BSW-L). The cached scenario
// follows in Update (composedWhich); the splash, status and legend are drawn from Th() every frame.
void WeatherscopeManager::MaybeSwitchTheme()
{
    const wxtheme::Which want = wxtheme::Select(themeSetting, phase);
    if (want == which || mode == Mode::Loop) return;
    Serial.printf("[wx] theme=%s was=%s setting=%s phase=%s\n", WhichName(want), WhichName(which),
                  SettingName(themeSetting), PhaseName(phase));
    which = want;
}

// The bench byte O (Q48, A12.66): +15 min on the clock Auto reads, wrapping at 24 h; one evaluation at once; no
// elevation is printed. The switch check follows in Update.
void WeatherscopeManager::OnAutoOffsetByte()
{
    autoOffsetMin = (int16_t)((autoOffsetMin + 15) % 1440);
    EvaluateDayNight();
    Serial.printf("[wx] auto offset=+%d min phase=%s\n", (int)autoOffsetMin, PhaseName(phase));
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
    {   // the frame log (A9.34): this push's start, and what its frame drew
        FrameRecord f = curFrame;
        f.startUs = (uint32_t)(esp_timer_get_time() - (int64_t)pushUs);
        // A9.42: the frame a build ran in, push to push, onto that build's record
        if ((f.flags & kRebuilt) && nBuilds > 0 && lastPushStartUs)
            builds[(nBuilds - 1) % kBuildRecords].frameUs = f.startUs - lastPushStartUs;
        lastPushStartUs = f.startUs;
        if (!(f.flags & kRebuilt)) {
            f.scene = (uint8_t)(cycle ? cycle->index : 0);
            f.radiusMi = (uint16_t)ZoomMiles();
        }
        frameLog[frameSeq % kFrameLog] = f;
        ++frameSeq;
        curFrame = FrameRecord();
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
        Out("[wx] fbcopy n=%lu min=%lu max=%lu mean=%lu\n", (unsigned long)copyN, (unsigned long)copyMin,
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
    if (fired) curFrame.flags |= kFired;
    if (visible != ringShown) {
        ringShown = visible;
        Serial.printf("[wx] hold ring %s\n", visible ? "on" : "off");
    }
    if (!visible) return;
    const float k = SCREEN_SIZE / 240.0f;
    const int cc = SCREEN_SIZE_DIV_2;
    const int R1 = SCREEN_SIZE_DIV_2 - (int)(2 * k), R0 = R1 - (int)(7 * k);
    c.drawCircle(cc, cc, R1 + 1, C888(Th().holdTrack));   // the track
    const uint32_t p = fired ? 1000 : longpress::RingPermille(longPress, millis());
    curFrame.permille = (uint16_t)p;
    if (p == 0) return;
    // LovyanGFX angles: 0 = 3 o'clock, clockwise. Start at 12 o'clock and never pass 360 in one call.
    const float a1 = 270.0f + 360.0f * (float)p / 1000.0f;
    const uint32_t green = C888(Th().holdArc);
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
    const bool bend = OnBend();
    const wx::CropBytes* frames = bend ? bendHistory : history;
    const size_t n = bend ? nBendHistory : nHistory;
    wxcrop::Header h, last;
    const int64_t d0 = esp_timer_get_time();
    const bool decoded = FullScreen(s) && DecodeCrop(frames[loopIndex], &h);
    rec.decodeUs = (uint32_t)(esp_timer_get_time() - d0);
    memset(rec.phaseUs, 0, sizeof rec.phaseUs);
    if (decoded && wxcrop::ParseHeader(frames[n - 1].data, frames[n - 1].size, &last) == wxcrop::Status::Ok) {
        *screenIn = wxscreens::ScreenIn();
        wxscenes::FromLoopFrame(h, grid, loopIndex, last.frameEpoch, ZoomMiles(), townOrder, screenIn,
                                bend ? wxscen::kBendOffsetMin : wxscen::kOffsetMin);
        screenIn->which = which;
        if (bend) ApplyPackage();   // the image built at the scenario, reused at the tap: never rebuilt in a frame
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
    const wxtheme::Theme& th = Th();
    s.fillSprite(C888(th.bg));
    s.setTextColor(C888(th.white));
    s.setTextSize(1);
    for (const auto& row : wxlegend::kRows) s.drawString(row.label, wxlegend::kLabelX, row.y);
    for (const auto& sw : wxlegend::kSwatches) {
        if (sw.kind == wxlegend::Kind::NoData) {
            const uint16_t line = s.color565(th.hatchLine.r, th.hatchLine.g, th.hatchLine.b);
            const uint16_t bg = s.color565(th.hatchGround.r, th.hatchGround.g, th.hatchGround.b);
            for (int yy = sw.y; yy < sw.y + sw.h; ++yy)
                for (int xx = sw.x; xx < sw.x + sw.w; ++xx)
                    s.drawPixel(xx, yy, (xx + yy) % wxpalette::kHatchPeriod == 0 ? line : bg);
            continue;
        }
        wxraster::Rgb col;
        switch (sw.kind) {
        case wxlegend::Kind::Rain: col = th.rain[sw.code - 1]; break;   // code 1 teal (A10.16)
        case wxlegend::Kind::Snow: col = th.snow[sw.code - 1]; break;
        default: col = th.noCoverage; break;
        }
        s.fillRect(sw.x, sw.y, sw.w, sw.h, s.color565(col.r, col.g, col.b));
    }
}

void WeatherscopeManager::DrawSplash(BandCanvas& c)
{
    c.sprite().fillSprite(C888(Th().bg));   // t5: the theme's own ground over main.cpp's black clear (A12.43 §1)
    c.setTextSize(3);
    CenterText(c, "Weatherscope", SCREEN_SIZE_DIV_2 - 24, C888(Th().uiFg));
    c.setTextSize(1);
    CenterText(c, "weather radar", SCREEN_SIZE_DIV_2 + 12, C888(Th().uiDim));
}

// The face when there is nothing to cycle (not stored mode, or the frame path failed): who the device is, whether
// it is online, where its settings are, and the time it believes it is.
void WeatherscopeManager::DrawStatus(BandCanvas& c)
{
    const wxtheme::Theme& th = Th();
    c.sprite().fillSprite(C888(th.bg));   // t5: the theme's own ground over main.cpp's black clear (A12.43 §1)
    c.setTextSize(2);
    CenterText(c, "Weatherscope", SCREEN_SIZE / 5, C888(th.uiFg));

    const bool online = WiFi.status() == WL_CONNECTED;
    CenterText(c, online ? "Wi-Fi connected" : "No Wi-Fi", SCREEN_SIZE * 34 / 100, C888(online ? th.uiAccent : th.uiWarn));
    c.setTextSize(1);
    if (online) CenterText(c, String(WiFi.RSSI()) + " dBm", SCREEN_SIZE * 34 / 100 + 20, C888(th.uiDim));

    const time_t utc = time(nullptr);
    if (utc < 1600000000) {
        c.setTextSize(2);
        CenterText(c, "syncing time", SCREEN_SIZE_DIV_2 + 4, C888(th.uiDim));
    } else {
        struct tm t;
        gmtime_r(&utc, &t);
        char hms[12];
        snprintf(hms, sizeof(hms), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
        c.setTextSize(3);
        CenterText(c, hms, SCREEN_SIZE_DIV_2 + 4, C888(th.uiFg));
        c.setTextSize(1);
        CenterText(c, "UTC", SCREEN_SIZE_DIV_2 + 32, C888(th.uiDim));
    }

    String url = "http://" + DeviceIdentity::Name() + ".local";
    url.toLowerCase();
    c.setTextSize(1);
    CenterText(c, url, SCREEN_SIZE * 76 / 100, C888(th.uiDim));
}

void WeatherscopeManager::CenterText(BandCanvas& c, const String& s, int y, uint32_t color)
{
    c.setTextColor(color);
    c.drawString(s, SCREEN_SIZE_DIV_2 - c.textWidth(s) / 2, y);
}
