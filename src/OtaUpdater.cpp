#include "OtaUpdater.h"
#include "LGFX.h"
#include "Layout.h"
#include "OtaCerts.h"
#include "BootScreen.h"
#include "Board.h"
#include "variants/Variant.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>   // the HTTP_UE_* failure codes, kept as the fail-<n> tokens (A12.104)
#include <Update.h>
#include <Preferences.h>
#include <time.h>
#include <esp_ota_ops.h>
#include <WiFi.h>
#ifdef WX_OTA_BENCH
#include <mbedtls/sha256.h>
#endif

#include "HeapHealth.h"
#include "BootReportPolicy.h"
#include "OtaInstall.h"

namespace {

// NVS namespace for the one-shot OTA memory report (see TakeOtaMemReport).
// It has to outlive the update itself: the attempt is recorded by the OLD
// firmware moments before the download, and reported by the NEW firmware --
// a successful OTA reboots straight into the new image, so nothing after
// httpUpdate.update() ever runs to record the happy path in RAM.
constexpr const char* OTA_MEM_NS = "ota-mem";

// NVS namespace for the reboot-then-fetch handshake. SEPARATE from ota-mem on
// purpose: that one is a telemetry record cleared on read, this one is control
// state that must survive a reboot and a rate-limit window. Mixing them would
// let a dropped telemetry read clear a reboot cap.
constexpr const char* OTA_BOOT_NS = "ota-boot";

// One flag-triggered reboot per 24 h. Matches the daily timer's own cadence, so
// the cap costs nothing in the healthy case and bounds the damage in every
// unhealthy one.

// 2026 in epoch seconds. time(nullptr) returns a small number until NTP lands,
// and an unsynced clock must not be read as "the cap expired long ago" -- that
// is the reading that produces a reboot loop, so an unsynced clock REFUSES.
constexpr uint32_t CLOCK_SANE_EPOCH = 1735689600UL; // 2025-01-01T00:00:00Z

// Pre-arm the record BEFORE the download begins, as "incomplete". Whatever
// happens next leaves a truthful record: success reboots (the new firmware
// finalises it), failure rewrites it in this same boot, and a WDT/power-loss
// reboot mid-flash leaves "incomplete" standing -- which is precisely the
// verdict that case deserves and the only trace it would otherwise leave.
void NoteOtaAttempt(int fwTo, uint32_t preLargest)
{
    Preferences p;
    if (!p.begin(OTA_MEM_NS, false))
        return; // telemetry must never be a reason an update doesn't happen
    p.putInt("from", FW_VERSION);
    p.putInt("to", fwTo);
    p.putUInt("pre", preLargest);
    p.putUInt("post", 0); // 0 = not measured yet; filled at report time
    p.putString("res", "incomplete");
    // Why the boot that is attempting this happened. A watchdog-armed reboot is
    // ESP_RST_SW exactly like the daily update check, so the cause is appended
    // rather than inferred: "SW" and "SW_NETWD" are different facts about a
    // customer's home. Suffixed rather than sent as a new field on purpose --
    // this rides the existing 6-field report and needs no Worker arity change,
    // and "_" survives the Worker's [^\w.-] sanitiser and its 16-char cap.
    String rst = ResetReasonName();
    rst += rebootcause::Suffix(ConsumeDeferredRebootCause());   // _NETWD, _TOUCHWD (RebootCause.h)
    p.putString("rst", rst);
    p.end();
}

// Failure is recorded in the same boot, so post is the heap right after the
// attempt -- the interesting number when asking why an update didn't fit.
void NoteOtaResult(const String& res)
{
    Preferences p;
    if (!p.begin(OTA_MEM_NS, false))
        return;
    p.putUInt("post", ESP.getMaxAllocHeap());
    p.putString("res", res);
    p.end();
}

void NoteOtaFailed(int err)
{
    NoteOtaResult(String("fail-") + err);
}

// "releases/latest/download/<asset>" always resolves to the newest published
// (non-draft, non-prerelease) release's asset, redirecting to the CDN.
//
// All SKUs are built and released together from one commit, so a single shared
// version.txt (an integer) gates everyone. Each SKU then downloads ITS OWN binary,
// named by its variant slug -- one SKU must never flash another's image. CI
// publishes firmware-<slug>.bin per SKU (see RELEASING.md).
//
// Bench builds may pin OTA to a specific (pre-)release via -DOTA_RELEASE_BASE
// (e.g. .../releases/download/<tag>): pre-releases never resolve through
// /latest, so a pre-gate test release is invisible to the fleet while a bench
// unit pointed at its tag exercises the full production OTA path.
#ifndef OTA_RELEASE_BASE
#define OTA_RELEASE_BASE "https://github.com/Valar-Systems/valar-scopes/releases/latest/download"
#endif
const char* VERSION_URL = OTA_RELEASE_BASE "/version.txt";

// FEATURE_EAM and the radar app can share a board (and thus a variant::SLUG) while shipping
// as separate products, so they ride separate OTA channels: FW_OTA_PREFIX is empty for the
// radar build and "eam-" for the EAM build, keeping firmware-s3-146.bin and
// firmware-eam-s3-146.bin distinct. A device only ever fetches its own channel's binary.
#ifndef FW_OTA_PREFIX
#define FW_OTA_PREFIX ""
#endif

// WHAT THIS DEVICE ASKS FOR: its own board, and its own scope (OtaIdentity.h). The URL
// and the identity check are both built from this one request, so the name fetched
// and the name checked cannot disagree. A scope switch is a request for another scope
// on the same board; nothing here can ask for another board.
ota::Identity OwnRequest()
{
    return ota::Request(variant::SLUG, FW_OTA_PREFIX);
}

String FirmwareUrl(const ota::Identity& req)
{
    return String(OTA_RELEASE_BASE "/firmware-") + req.scope + req.board + ".bin";
}

// X.509 validation needs a roughly-correct clock (cert validity periods), but the
// boot-time update check runs right after configTime() starts NTP in the background.
// Wait briefly for the first sync; give up if it never lands (no internet -- the
// version fetch would fail anyway) and let the daily re-check try again.
bool WaitForClock()
{
    constexpr time_t BUILD_ERA = 1750000000; // mid-2025; NTP-synced time is always past this
    for (int i = 0; i < 40; ++i) {           // up to ~10 s, usually syncs in 1-2 s
        if (time(nullptr) > BUILD_ERA)
            return true;
        delay(250);
    }
    return false;
}

void drawStatus(LGFX& tft, LGFX_Sprite& fb, const String& msg)
{
    // Compose full-frame through the backbuffer (SPD2010 drops direct partial
    // writes) and flush for the RGB panel -- the same path every boot screen uses.
    DrawCenteredScreen(tft, fb, lgfx::color888(0, 0, 0), lgfx::color888(0, 255, 0), msg.c_str());
    board::DisplayFlush(tft);
}

// Draw the "Updating firmware..." title plus a progress bar at `pct` (0-100),
// composed full-frame so it shows on every panel (see drawStatus / BootScreen.h).
void drawProgress(LGFX& tft, LGFX_Sprite& fb, int pct)
{
#if defined(BLIPSCOPE_PANEL_SPD2010)
    auto& g = fb;   // compose into the sprite, push even-aligned below
#else
    auto& g = tft;  // direct to the panel (flushed after)
#endif
    g.fillScreen(lgfx::color888(0, 0, 0));
    g.setTextColor(lgfx::color888(0, 255, 0));
    g.drawCenterString("Updating firmware...", SCREEN_SIZE_DIV_2, SCREEN_SIZE_DIV_2 - 24);

    const int barW = SCREEN_SIZE - 100;         // 140 on a 240 screen
    const int barX = (SCREEN_SIZE - barW) / 2;   // centred on any panel
    const int barY = SCREEN_SIZE_DIV_2 + 20;
    g.drawRect(barX, barY, barW, 14, lgfx::color888(0, 120, 0));
    g.fillRect(barX + 2, barY + 2, ((barW - 4) * pct) / 100, 10, lgfx::color888(0, 255, 0));

#if defined(BLIPSCOPE_PANEL_SPD2010)
    fb.pushSprite(0, 0);
#endif
    board::DisplayFlush(tft); // RGB panels: make the paint visible (no-op on SPI SKUs)
}

// The bar's first and last draws each say so on serial (A2.1): absence cannot be seen
// on a panel, so "a refusal draws nothing" is checked by counting these lines.
void ScreenProgress(LGFX& tft, LGFX_Sprite& fb, int pct)
{
    drawProgress(tft, fb, pct);
    Serial.printf("[ota] screen: progress %d\n", pct);
}

// ota::Install's source over the download stream. An empty read is waited out by ota::ReadSome through waitEmpty()
// (docs/ota-head-patience.md): on this stream 0 means "nothing yet" as often as "the end".
struct StreamSource {
    Stream& s;
    size_t read(uint8_t* buf, size_t n)
    {
#ifdef OTA_BENCH_HEAD_STALL
        // Bench only (docs/ota-head-patience.md, B-STALL): every read comes back empty, so the head's bound is watched
        // ending in refused-truncated, with no reboot. No env sets this; check-no-bench-hooks.sh refuses its marker in
        // every shipping image.
        static bool said = false;
        if (!said) { said = true; Serial.println("[ota-bench] head stall: every read held empty"); }
        (void)buf; (void)n;
        return 0;
#else
        return s.readBytes(buf, n);
#endif
    }
    void waitEmpty() { delay(ota::kEmptyWaitMs); }
};

// ota::Install's flash: a thin adapter over Update (A1.2).
//
// writeRest is a loop over Update.write, NOT Update.writeStream: writeStream insists
// that the stream's next byte is the image magic 0xE9, which is true at the start of
// an image and false once the 352-byte head has been read. The loop keeps
// writeStream's patience exactly -- 300 empty reads 100 ms apart, then
// UPDATE_ERROR_STREAM -- so a stalled download still records fail-6.
//
// While one exists past begin(), ota::FlashWriteActive() says so, and the touch-wedge reboot
// waits (TouchWedgeCap.h; A12.119). Today the two cannot overlap -- this runs on the loop task,
// in setup() or the bench's OtaBenchRun -- so the flag asserts that rather than assuming it.
volatile bool flashWriting = false;

class UpdateFlash {
public:
    UpdateFlash(LGFX& tft, LGFX_Sprite& fb, const String& md5) : tft_(tft), fb_(fb), md5_(md5) {}
    ~UpdateFlash() { flashWriting = false; }   // every exit: done, refused, failed

    bool begin(size_t total)
    {
        flashWriting = true;
        if (!Update.begin(total, U_FLASH)) {
            err_ = Update.getError();
            return false;
        }
        if (md5_.length() && !Update.setMD5(md5_.c_str())) {
            err_ = HTTP_UE_SERVER_FAULTY_MD5;
            Update.abort();
            return false;
        }
        ScreenProgress(tft_, fb_, 0);   // the bar's first draw: after acceptance, never for a refusal
        return true;
    }

    size_t write(const uint8_t* buf, size_t n)
    {
        const size_t w = Update.write(const_cast<uint8_t*>(buf), n);
        if (w != n) err_ = Update.getError();
        return w;
    }

    template <class Source>
    size_t writeRest(Source& src, size_t n)
    {
        uint8_t buf[1024];
        size_t done = 0;
        while (done < n) {
            const size_t got = ota::ReadSome(src, buf, n - done < sizeof buf ? n - done : sizeof buf);
            if (got == 0) {   // ota::kEmptyReads empty reads in a row: the stream has stalled
                err_ = UPDATE_ERROR_STREAM;
                Update.abort();
                return done;
            }
            if (write(buf, got) != got) return done;
            done += got;
        }
        return done;
    }

    bool end()
    {
        ScreenProgress(tft_, fb_, 100);   // every byte is written: the bar's last draw, as httpUpdate drew it
        if (!Update.end()) {
            err_ = Update.getError();
            return false;
        }
        return true;
    }

    int error() const { return err_; }

private:
    LGFX& tft_;
    LGFX_Sprite& fb_;
    String md5_;
    int err_ = 0;
};

// THE DOWNLOAD, opened here rather than by httpUpdate.update(), so the identity is
// read from the stream BEFORE anything is written (A1.2). Every condition httpUpdate
// turned into an HTTP_UE_* code keeps that code (A12.104's mapping), and the request
// is shaped as httpUpdate shaped it -- HTTP/1.0 (no chunked transfer), its timeout,
// forced redirects (latest/download redirects to GitHub's CDN), its user agent --
// minus the x-ESP32-* headers, which carried the device's MAC addresses to a CDN
// that reads none of them.
struct Download {
    enum Kind { Failed, NoUpdate, Done } kind;
    int code;           // Failed: the fail-<n> code
    ota::Result result; // Done: what ota::Install decided
};

Download OpenAndInstall(WiFiClientSecure& client, const String& url, const ota::Identity& req,
                        LGFX& tft, LGFX_Sprite& fb)
{
    Download d;
    d.kind = Download::Failed;
    d.code = 0;   // httpUpdate's code when HTTPClient::begin refused the URL
    memset(&d.result, 0, sizeof d.result);

    HTTPClient http;
    if (!http.begin(client, url))
        return d;
    http.useHTTP10(true);
    http.setTimeout(8000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setUserAgent("ESP32-http-Update");
    http.addHeader("Cache-Control", "no-cache");
    const char* keys[] = {"x-MD5"};
    http.collectHeaders(keys, 1);

    const int code = http.GET();
    if (code <= 0) {
        d.code = code;
    } else if (code == HTTP_CODE_NO_CONTENT || code == HTTP_CODE_NOT_MODIFIED) {
        d.kind = Download::NoUpdate;
    } else if (code != HTTP_CODE_OK) {
        d.code = code == HTTP_CODE_NOT_FOUND ? HTTP_UE_SERVER_FILE_NOT_FOUND
               : code == HTTP_CODE_FORBIDDEN ? HTTP_UE_SERVER_FORBIDDEN
               : HTTP_UE_SERVER_WRONG_HTTP_CODE;
    } else {
        const int len = http.getSize();
        const int space = ESP.getFreeSketchSpace();
        if (len <= 0) {
            d.code = HTTP_UE_SERVER_NOT_REPORT_SIZE;
        } else if (!space) {
            d.code = HTTP_UE_NO_PARTITION;
        } else if (len > space) {
            d.code = HTTP_UE_TOO_LESS_SPACE;
        } else {
            StreamSource src{*http.getStreamPtr()};
            UpdateFlash flash(tft, fb, http.header("x-MD5"));
            d.result = ota::Install(src, flash, (size_t)len, req);
            d.kind = Download::Done;
        }
    }
    http.end();   // after a refusal this closes the connection 352 bytes in
    return d;
}

} // namespace

bool ota::FlashWriteActive() { return flashWriting; }

#ifdef WX_OTA_BENCH
// O9's bench source (docs/weatherscope-stage3.md A1.3, A12.104): ota::Install with the
// REAL adapter, fed an image's first bytes over USB-CDC instead of a download, so the
// board -- not a fake -- shows whether a refusal touches flash. Bench env only; its
// "[ota-bench]" marker is in check-no-bench-hooks.sh's list, so no shipping image can
// carry it unseen.
namespace {

// A byte budget over Serial, ACKNOWLEDGED (A12.107). Every chunk of at most 64 bytes --
// one USB full-speed packet -- is asked for with a "[ota-bench] more" line, and the host
// sends exactly one chunk per request, so no more than 64 bytes are ever in flight and the
// receive buffer cannot overrun. (The first run, unpaced, lost bytes on its 8 KB send while
// the Updater erased a sector, and its budget never drained.) After `left` bytes it reads 0
// at once and asks for nothing more, so a cut stream ends in writeRest's 300 empty reads
// rather than in Serial's timeout.
struct SerialBudget {
    size_t left;
    uint8_t chunk[64];
    size_t have = 0, pos = 0;
    size_t read(uint8_t* buf, size_t n)
    {
        if (pos == have) {
            if (left == 0) return 0;
            const size_t want = left < sizeof chunk ? left : sizeof chunk;
            Serial.println("[ota-bench] more");
            have = Serial.readBytes(chunk, want);
            pos = 0;
            left -= have;
            if (have == 0) return 0;
        }
        if (n > have - pos) n = have - pos;
        memcpy(buf, chunk + pos, n);
        pos += n;
        return n;
    }
    void waitEmpty() { delay(ota::kEmptyWaitMs); }   // ota::ReadSome's wait (docs/ota-head-patience.md)
};

// sha256 of the inactive slot's first 64 KB, 12 hex: whether anything was written.
void SlotSha12(const esp_partition_t* slot, char* out)
{
    mbedtls_sha256_context c;
    mbedtls_sha256_init(&c);
    mbedtls_sha256_starts(&c, 0);
    uint8_t buf[1024];
    for (size_t off = 0; off < 65536; off += sizeof buf) {
        esp_partition_read(slot, off, buf, sizeof buf);
        mbedtls_sha256_update(&c, buf, sizeof buf);
    }
    uint8_t d[32];
    mbedtls_sha256_finish(&c, d);
    mbedtls_sha256_free(&c);
    for (int i = 0; i < 6; ++i) snprintf(out + 2 * i, 3, "%02x", d[i]);
}

}  // namespace

void OtaBenchRun(LGFX& tft)
{
    // The bench env is the s3-128, whose GC9A01 the bar draws on directly (drawProgress): the sprite is never
    // created or touched, it only fills the adapter's signature.
    LGFX_Sprite fb(&tft);
    Serial.setTimeout(3000);
    const String line = Serial.readStringUntil('\n');
    unsigned long total = 0, n = 0;
    if (sscanf(line.c_str(), "OTA %lu %lu", &total, &n) != 2 || total == 0 || n > total) {
        Serial.println("[ota-bench] bad header");
        Serial.setTimeout(1000);
        return;
    }
    const esp_partition_t* slot = esp_ota_get_next_update_partition(nullptr);
    const esp_partition_t* bootBefore = esp_ota_get_boot_partition();
    char shaBefore[13], shaAfter[13];
    SlotSha12(slot, shaBefore);
    Serial.println("[ota-bench] go");
    SerialBudget src{(size_t)n};
    UpdateFlash flash(tft, fb, String());
    const ota::Result r = ota::Install(src, flash, (size_t)total, OwnRequest());
    uint8_t sink[64];
    while (src.left && src.read(sink, sizeof sink)) {}   // a refusal leaves the rest unread: drain it
    SlotSha12(slot, shaAfter);
    const esp_partition_t* bootAfter = esp_ota_get_boot_partition();
    char tok[32];
    ota::Token(r, tok, sizeof tok);
    Serial.printf("[ota-bench] result=%s boot=%s/%s slot=%s/%s\n", tok, bootBefore ? bootBefore->label : "?",
                  bootAfter ? bootAfter->label : "?", shaBefore, shaAfter);
    Serial.setTimeout(1000);
}
#endif

// ---------------------------------------------------------------------------
// REBOOT-THEN-FETCH. Declared in OtaUpdater.h; EXTERNAL LINKAGE ON PURPOSE --
// main.cpp calls both. They sit outside the anonymous namespace above, which
// is where the constants they use belong and where these must not.

bool ConsumeDeferredCheckFlag()
{
    Preferences p;
    if (!p.begin(OTA_BOOT_NS, false))
        return false;
    const bool pending = p.getBool("pending", false);
    // CLEARED BEFORE THE CHECK RUNS, not after. See the header: everything after
    // this line can crash without arming another reboot.
    if (pending)
        p.putBool("pending", false);
    p.end();
    return pending;
}

// THE CAUSE OF THE LAST DEFERRED REBOOT, read once at boot and cleared.
//
// Two callers now arm this path -- the daily update check and the reachability
// watchdog -- and they must share ONE 24 h cap, not have one each. Two guards on
// one rule is two rules, and the second is always the stale one (CLAUDE.md).
// Sharing the cap is also the safety property the watchdog needs: an unreachable
// network cannot reboot a board more often than an ordinary update check can.
//
// The cause is recorded rather than inferred, because after the reboot the two
// are indistinguishable -- both are ESP_RST_SW with the pending flag set.
uint8_t ConsumeDeferredRebootCause()
{
    // IDEMPOTENT WITHIN A BOOT, and that is not an optimisation. Two readers
    // want this value -- NetWatchdog::Begin() to say why the board restarted,
    // and NoteOtaAttempt() to stamp it into the telemetry -- and a one-shot
    // read would silently give the second one zero depending on which ran
    // first. The NVS row is still cleared exactly once.
    static uint8_t cached = 0;
    static bool    read   = false;
    if (read) return cached;
    read = true;
    Preferences p;
    if (!p.begin(OTA_BOOT_NS, false))
        return 0;
    cached = p.getUChar("cause", 0);
    if (cached != 0)
        p.putUChar("cause", 0);
    p.end();
    return cached;
}

// The map's F6 (docs/blipscope-map-client-steps.md, step A; Q-MA1): is this the new image's first boot after an update?
// The same test TakeOtaMemReport finalises with -- the pre-armed "incomplete" record naming the version now running --
// read BEFORE the first check-in clears that record (fire-once), which is why the map reads it at its first Initialise.
// READ-ONLY: the namespace is opened read-only, so this can never create, finalise or clear the record the check-in
// reports; and CACHED for the boot, so a later call (after the check-in has cleared it) still says what was true at boot.
bool BootFollowsUpdate()
{
    static bool cached = false;
    static bool read   = false;
    if (read) return cached;
    read = true;
    Preferences p;
    if (!p.begin(OTA_MEM_NS, true))
        return false;   // no record at all: not an update's first boot
    cached = p.getString("res", "") == "incomplete" && p.getInt("to", 0) == FW_VERSION;
    p.end();
    return cached;
}

void StampRebootCause(uint8_t cause)
{
    Preferences p;
    if (!p.begin(OTA_BOOT_NS, false)) {
        Serial.printf("[ota] reboot cause %u not stamped: NVS unavailable\n", (unsigned)cause);
        return;   // the reboot still happens; it is only reported as a plain SW
    }
    p.putUChar("cause", cause);
    p.end();
}

bool DeferUpdateCheckToReboot(uint32_t largestBlock, bool scheduled)
{
    return DeferRebootWithCause(REBOOT_CAUSE_OTA_CHECK, largestBlock, scheduled);
}

bool DeferRebootWithCause(uint8_t cause, uint32_t largestBlock, bool scheduled)
{
    const char* why = (cause == REBOOT_CAUSE_NET_WEDGE) ? "net" : "update check";
    const time_t nowT = time(nullptr);
    const uint32_t now = (nowT > 0) ? (uint32_t)nowT : 0;

    if (now < CLOCK_SANE_EPOCH) {
        Serial.printf("[ota] %s deferral refused: clock not synced "
                      "(largest=%u); the daily cap cannot be enforced without it\n",
                      why, (unsigned)largestBlock);
        return false;
    }

    Preferences p;
    if (!p.begin(OTA_BOOT_NS, false)) {
        Serial.printf("[ota] %s deferral refused: NVS unavailable\n", why);
        return false;
    }
    const uint32_t last = p.getUInt("lastReb", 0);
    if (!rebootcap::Allows(now, last, scheduled, REBOOT_MIN_INTERVAL_S)) {
        p.end();
        Serial.printf("[ota] %s deferral refused: last reboot %lus ago, "
                      "needs %lus (%s; largest=%u)\n",
                      why, (unsigned long)(now - last),
                      (unsigned long)rebootcap::MinGapS(scheduled, REBOOT_MIN_INTERVAL_S),
                      scheduled ? "scheduled" : "unscheduled", (unsigned)largestBlock);
        return false;
    }

    // Stamp the cap BEFORE the flag. If power is lost between the two writes the
    // device wakes with the cap set and the flag clear -- one missed update. The
    // other order wakes it with a flag and no cap, which is the reboot loop.
    p.putUInt("lastReb", now);
    p.putUChar("cause", cause);
    p.putBool("pending", true);
    p.end();

    Serial.printf("[ota] %s deferred to reboot (cause=%u largest=%u)\n",
                  why, (unsigned)cause, (unsigned)largestBlock);
    Serial.flush();
    delay(150); // let the line reach a serial capture before the reset
    ESP.restart();
    return true; // not reached
}

const char* ResetReasonName()
{
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_SW:       return "SW";        // ESP.restart() -- ours
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT:      return "WDT";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
        case ESP_RST_EXT:      return "EXT";
        case ESP_RST_SDIO:     return "SDIO";
        case ESP_RST_USB:      return "USB";       // esptool on a native-USB S3
        case ESP_RST_JTAG:     return "JTAG";
        case ESP_RST_EFUSE:    return "EFUSE";
        case ESP_RST_PWR_GLITCH: return "PWR_GLITCH";
        case ESP_RST_CPU_LOCKUP: return "CPU_LOCKUP";
        // ESP_RST_UNKNOWN and anything a future IDF adds. THE NUMBER IS PRINTED,
        // not swallowed: five real reasons -- USB, JTAG, EFUSE, PWR_GLITCH and
        // CPU_LOCKUP -- were being reported as a bare "UNKNOWN" by an earlier
        // version of this switch, which is how a diagnostic quietly becomes a
        // shrug. A reason this code does not know should still name itself.
        default: {
            static char buf[16];
            snprintf(buf, sizeof(buf), "UNKNOWN_%d", (int)esp_reset_reason());
            return buf;
        }
    }
}

void LogOtaSlot(const char* when)
{
    const esp_partition_t* run  = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    if (run) esp_ota_get_state_partition(run, &st);

    // The state matters as much as the label. A freshly-flashed image sits in
    // PENDING_VERIFY until it is marked valid; if it is still PENDING after a power
    // cycle, the bootloader may roll back to the other slot -- which is exactly the
    // "does it STAY there" question the partition change was never proved against.
    const char* stateName =
        st == ESP_OTA_IMG_NEW            ? "NEW"            :
        st == ESP_OTA_IMG_PENDING_VERIFY ? "PENDING_VERIFY" :
        st == ESP_OTA_IMG_VALID          ? "VALID"          :
        st == ESP_OTA_IMG_INVALID        ? "INVALID"        :
        st == ESP_OTA_IMG_ABORTED        ? "ABORTED"        : "UNDEFINED";

    Serial.printf("[ota-slot] %s running=%s @0x%06x state=%s next=%s fw=%d\n",
                  when,
                  run  ? run->label  : "?", run ? (unsigned)run->address : 0u,
                  stateName,
                  next ? next->label : "?",
                  FW_VERSION);

    // What this image says it is, read back from its own identity record
    // (OtaIdentity.cpp). This read is also what keeps the record in the image: the
    // linker script does not KEEP its section, so an unread record would be dropped.
    uint8_t rec[ota::kRecordSize];
    memcpy(rec, &ota::kOwnRecord, sizeof rec);
    ota::Identity own;
    const ota::Parsed p = ota::ParseRecord(rec, own);
    if (p == ota::Parsed::Ok)
        Serial.printf("[ota-id] board=%s scope=%s (record v%u)\n", own.board, own.scope[0] ? own.scope : "-",
                      (unsigned)rec[4]);
    else
        Serial.printf("[ota-id] own record unreadable (%s)\n", p == ota::Parsed::NoRecord ? "no-record" : "bad-record");
}

void MaybeUpdateFirmware(LGFX& tft, LGFX_Sprite& fb, HttpRequestManager& http)
{
    // 1. read the latest published version, through the SHARED client.
    //
    // This used to build its own WiFiClientSecure + HTTPClient, which stood a second TLS
    // context up alongside the feed client's live keep-alive session. That violates the
    // one-client invariant the rest of the system is built on, and it trades a working
    // radar for a question whose answer is almost always "no". GetSecure keeps the pinned
    // roots (a spoofed binary is persistent code execution, and a spoofed VERSION silently
    // pins the fleet on an old build) while costing no extra context -- see its header.
    if (!WaitForClock()) {
        Serial.println("[ota] clock not synced; skipping update check");
        return;
    }

    const HttpResult ver = http.GetSecure(VERSION_URL, OTA_ROOT_CAS);
    if (!ver.success) {
        Serial.printf("[ota] version check failed: HTTP %d %s\n",
                      ver.statusCode, ver.errorMessage.c_str());
        return;
    }

    const int latest = ver.response.toInt();

    const ota::Identity req = OwnRequest();
    Serial.printf("[ota] channel=%s%s current=%d latest=%d\n", req.scope, req.board, FW_VERSION, latest);
    if (latest <= FW_VERSION)
        return; // already up to date

    // 2. download + flash the new image, showing a progress bar on the screen.
    //
    // httpUpdate needs a Client of its own, so this IS a second TLS context -- unavoidable,
    // and acceptable because the radar is suspended behind a progress bar for the duration.
    // What is not acceptable is it being the second SIMULTANEOUS one, which is what happens
    // if a background feed poll is holding a keep-alive session when the download's
    // handshake asks for its own large contiguous block. So: take the request bus for the
    // whole download, then drop the shared session while nothing can re-open it.
    //
    // Blocking-with-timeout rather than a bare TryAcquireBus: an in-flight fetch is the
    // common case, not an error. If the bus never frees, skip -- the daily re-check will
    // try again, and a missed update beats a wedged one.
    bool bus = false;
    for (int i = 0; i < 100 && !bus; ++i) {
        bus = http.TryAcquireBus();
        if (!bus) delay(100);
    }
    if (!bus) {
        Serial.println("[ota] request bus busy for 10 s; deferring update to the next check");
        return;
    }
    http.ReleaseTlsLocked(); // free the feed session BEFORE the download asks for its own

    Serial.println("[ota] newer firmware available -- updating");
    // Memory evidence through the whole download+flash window (gate item: a
    // production-shaped OTA must complete at the steady-state heap operating
    // point, and the ledger wants numbers, not just pass/fail). Serial carries
    // the fine-grained ladder for a bench unit; NVS carries the summary home
    // from the field (see TakeOtaMemReport) -- the same number in both, so the
    // fleet's report can be read against a bench capture directly.
    const uint32_t preLargest = ESP.getMaxAllocHeap();
    Serial.printf("[ota-mem] pre-update free=%u largest=%u free8=%u tlsOk=%d\n",
                  ESP.getFreeHeap(), preLargest,
                  heaphealth::FreeInternal8Bit(), (int)heaphealth::CanHandshake());
    NoteOtaAttempt(latest, preLargest);
    // Nothing is drawn yet: the bar appears only once the image is accepted (A2.1), so a
    // refused head never flashes a 0% bar.

    WiFiClientSecure updClient;
    updClient.setCACert(OTA_ROOT_CAS);

    // Repaint only on a whole-percent change: each paint is a full-frame push +
    // panel flush, and the callback fires per flash sector.
    static int lastPct = -1;
    lastPct = -1;
    Update.onProgress([&tft, &fb](size_t current, size_t total) {
        const int pct = total > 0 ? (int)((current * 100ULL) / total) : 0;
        if (pct != lastPct) {
            lastPct = pct;
            drawProgress(tft, fb, pct);
            // Heap sampled every 5% across download + OTA-partition writes: the memory
            // evidence the gate's OTA-at-steady-state leg exists for. largest= is kept
            // ALONGSIDE free8= rather than replaced, so the plateau stays visible in the
            // same line that shows the number which actually tracks (see HeapHealth.h).
            if (pct % 5 == 0)
                Serial.printf("[ota-mem] pct=%d free=%u largest=%u free8=%u\n",
                              pct, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                              heaphealth::FreeInternal8Bit());

            // The trial allocation is deliberately SPARSE. It is not a free measurement:
            // it asks for 16,717 contiguous bytes at the moment the heap is tightest, so
            // sampling it every 5% would risk causing the failure it is here to observe.
            // Every 20% gives five points across the transfer, which is enough to see a
            // trend, at five transient allocations rather than twenty. A rejection here
            // does NOT count as a hard failure -- OnAllocFailed is scoped out via
            // heaphealth::InTrial() -- so the gate saying "no" stays distinguishable from
            // the allocator actually failing.
            if (pct % 20 == 0)
                Serial.printf("[ota-mem] pct=%d TRIAL tlsOk=%d rej=%lu\n",
                              pct, (int)heaphealth::CanHandshake(),
                              (unsigned long)heaphealth::TrialRejectionCount());

#ifdef OTA_FAULT_AT_PCT
            // Bench-only fault injection (never compiled into a shipping env). Drops the
            // link mid-transfer to exercise the failure path deliberately: the download must
            // fail, NoteOtaFailed must record it, ReleaseBus() must run, and the device
            // must come back to a working radar rather than wedging with the bus held.
            // A partial OTA that bricks a unit is worse than one that never starts.
            static bool faulted = false;
            if (!faulted && pct >= OTA_FAULT_AT_PCT) {
                faulted = true;
                Serial.printf("[ota-fault] INJECTING link loss at pct=%d\n", pct);
                WiFi.disconnect(false, false);
            }
#endif
        }
    });

    const String firmwareUrl = FirmwareUrl(req);
    Serial.printf("[ota] downloading %s\n", firmwareUrl.c_str());
    const Download d = OpenAndInstall(updClient, firmwareUrl, req, tft, fb);
    Update.onProgress(nullptr);   // the lambda captures this frame's tft/fb by reference

    if (d.kind == Download::Done && d.result.outcome == ota::Outcome::Installed) {
        // The pre-armed "incomplete" stands; the new image finalises it as "ok".
        Serial.println("[ota] update installed; restarting");
        Serial.flush();
        delay(150);
        ESP.restart();
    }

    if (d.kind == Download::Done && ota::IsRefusal(d.result.outcome)) {
        // A REFUSAL DRAWS NOTHING (A2.1, ruled A4.1): the customer cannot act on it,
        // and it repeats on every check until a correctly labelled image is published.
        // The record and this line carry it.
        char res[32], image[2 * ota::kFieldMax + 1];
        ota::Token(d.result, res, sizeof res);
        if (d.result.imageKnown)
            snprintf(image, sizeof image, "%s%s", d.result.image.scope, d.result.image.board);
        else
            snprintf(image, sizeof image, "-");
        Serial.printf("[ota] refused %s: requested=%s%s image=%s\n", res + strlen("refused-"), req.scope,
                      req.board, image);
        NoteOtaResult(res);
    } else if (d.kind == Download::NoUpdate) {
        // httpUpdate's HTTP_UPDATE_NO_UPDATES: nothing written, and the pre-armed
        // "incomplete" stands, as it always has (A12.104 keeps the quirk).
        Serial.println("[ota] the server answered 204/304: no update");
    } else {
        const int err = d.kind == Download::Done ? d.result.error : d.code;
        Serial.printf("[ota] update failed (%d)\n", err);
        NoteOtaFailed(err);
        drawStatus(tft, fb, "Update failed");
        Serial.println("[ota] screen: status Update failed");
        delay(2000);
    }
    // An installed image restarts above, so this runs on every other path -- the
    // success path never needs the bus again.
    http.ReleaseBus();
}

// The boot reason, held until a check-in is ACKNOWLEDGED rather than merely sent.
//
// WHY THIS DIFFERS FROM ITS NEIGHBOURS, deliberately. UsageStore::Take commits
// its delta before the request leaves (`reported = total; Persist()`), and
// TakeOtaMemReport clears on read and says so: "a report lost to a failed
// request is not retried". Both are best-effort on purpose, and both are right,
// because a lost usage delta heals next hour and a lost OTA report is re-covered
// by the next update cycle.
//
// THIS ONE CANNOT BE BEST-EFFORT, AND THE REASON IS SAMPLING BIAS RATHER THAN
// TIDINESS. A boot reason happens once per boot and is gone forever if dropped
// -- and the boots we most want to see are the ones that FOLLOW A NETWORK
// PROBLEM, whose first check-in is therefore the one most likely to fail. A
// best-effort field here would systematically lose exactly the population it
// exists to measure, and would look like a healthy fleet while doing it. That is
// the same defect as the OTA-report coupling this path was built to escape, one
// layer further out.
//
// TWO FLAGS, NOT ONE. `pending` says there is still something to report;
// `inFlight` says a request is already carrying it. Without the second, a retry
// issued before the first result came back would put the reason on two requests
// and produce two rows for one boot -- turning a dropped sample into a
// double-counted one, which is worse.
namespace {
bootreport::State g_boot;      // the delivery rule; see include/BootReportPolicy.h
String g_bootValue;            // composed once; survives a failed attempt
} // namespace

String TakeBootReasonReport()
{
    if (!bootreport::Take(g_boot)) return String();
    if (g_bootValue.isEmpty()) {
        g_bootValue = ResetReasonName();
        // Composed ONCE and cached, which is load-bearing: this read CLEARS the
        // NVS cause, so a retry that recomposed would get a bare "SW" and
        // silently drop the _NETWD that is the whole point of the field.
        g_bootValue += rebootcause::Suffix(ConsumeDeferredRebootCause());   // one map (RebootCause.h)
    }
    return g_bootValue;
}

void AckBootReasonReport(bool delivered)
{
    bootreport::Ack(g_boot, delivered);
}

String TakeOtaMemReport()
{
    Preferences p;
    if (!p.begin(OTA_MEM_NS, false))
        return "";
    if (!p.isKey("res")) {
        p.end();
        return ""; // no OTA attempted since the last report: send nothing
    }

    const int      from = p.getInt("from", 0);
    const int      to   = p.getInt("to", 0);
    const uint32_t pre  = p.getUInt("pre", 0);
    uint32_t       post = p.getUInt("post", 0);
    String         res  = p.getString("res", "");
    String         rst  = p.getString("rst", "UNKNOWN");

    // Finalise the happy path. The old firmware could not: a successful update
    // reboots inside httpUpdate.update(), so the pre-armed "incomplete" record
    // is the last thing it wrote. If that record names the version now running,
    // the update plainly worked -- and anything else that rebooted us (watchdog,
    // power loss) leaves "incomplete" standing, correctly.
    if (res == "incomplete" && to == FW_VERSION)
        res = "ok";
    if (post == 0)
        post = ESP.getMaxAllocHeap(); // ok/incomplete: this check-in is the "after"

    p.clear(); // fire-once: cleared whether or not the request that carries it lands
    p.end();

    // SIX FIELDS SINCE 2026-09-03. The Worker accepts 5 or 6 (proxy/src/metrics.ts):
    // a device on older firmware keeps sending 5 and keeps being recorded, and the
    // parser must be deployed BEFORE any device sends 6 or every row is dropped
    // silently -- the arity-drift shape this repo keeps meeting.
    return String(from) + "," + String(to) + "," + String(pre) + "," + String(post)
           + "," + res + "," + rst;
}
