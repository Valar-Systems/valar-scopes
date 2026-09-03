#include "OtaUpdater.h"
#include "LGFX.h"
#include "Layout.h"
#include "OtaCerts.h"
#include "BootScreen.h"
#include "Board.h"
#include "variants/Variant.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <time.h>
#include <esp_ota_ops.h>
#include <WiFi.h>

#include "HeapHealth.h"

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
constexpr uint32_t REBOOT_MIN_INTERVAL_S = 24UL * 60UL * 60UL;

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
    p.putString("rst", ResetReasonName()); // why the boot that is attempting this happened
    p.end();
}

// Failure is recorded in the same boot, so post is the heap right after the
// attempt -- the interesting number when asking why an update didn't fit.
void NoteOtaFailed(int err)
{
    Preferences p;
    if (!p.begin(OTA_MEM_NS, false))
        return;
    p.putUInt("post", ESP.getMaxAllocHeap());
    p.putString("res", String("fail-") + err);
    p.end();
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

String FirmwareUrl()
{
    return String(OTA_RELEASE_BASE "/firmware-") + FW_OTA_PREFIX + variant::SLUG + ".bin";
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

} // namespace

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

bool DeferUpdateCheckToReboot(uint32_t largestBlock)
{
    const time_t nowT = time(nullptr);
    const uint32_t now = (nowT > 0) ? (uint32_t)nowT : 0;

    if (now < CLOCK_SANE_EPOCH) {
        Serial.printf("[ota] update check deferral refused: clock not synced "
                      "(largest=%u); the daily cap cannot be enforced without it\n",
                      (unsigned)largestBlock);
        return false;
    }

    Preferences p;
    if (!p.begin(OTA_BOOT_NS, false)) {
        Serial.println("[ota] update check deferral refused: NVS unavailable");
        return false;
    }
    const uint32_t last = p.getUInt("lastReb", 0);
    if (last != 0 && now >= last && (now - last) < REBOOT_MIN_INTERVAL_S) {
        p.end();
        Serial.printf("[ota] update check deferral refused: last reboot %lus ago, "
                      "cap is %lus (largest=%u)\n",
                      (unsigned long)(now - last), (unsigned long)REBOOT_MIN_INTERVAL_S,
                      (unsigned)largestBlock);
        return false;
    }

    // Stamp the cap BEFORE the flag. If power is lost between the two writes the
    // device wakes with the cap set and the flag clear -- one missed update. The
    // other order wakes it with a flag and no cap, which is the reboot loop.
    p.putUInt("lastReb", now);
    p.putBool("pending", true);
    p.end();

    Serial.printf("[ota] update check deferred to reboot (largest=%u)\n",
                  (unsigned)largestBlock);
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
        default:               return "UNKNOWN";
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

    Serial.printf("[ota] channel=%s%s current=%d latest=%d\n", FW_OTA_PREFIX, variant::SLUG, FW_VERSION, latest);
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
    drawProgress(tft, fb, 0);

    WiFiClientSecure updClient;
    updClient.setCACert(OTA_ROOT_CAS);

    httpUpdate.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    httpUpdate.rebootOnUpdate(true);
    // Repaint only on a whole-percent change: each paint is a full-frame push +
    // panel flush, and the callback fires per network chunk.
    static int lastPct = -1;
    lastPct = -1;
    httpUpdate.onProgress([&tft, &fb](int current, int total) {
        const int pct = total > 0 ? (int)((current * 100L) / total) : 0;
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
            // link mid-transfer to exercise the failure path deliberately: httpUpdate must
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

    const String firmwareUrl = FirmwareUrl();
    Serial.printf("[ota] downloading %s\n", firmwareUrl.c_str());
    const t_httpUpdate_return ret = httpUpdate.update(updClient, firmwareUrl);
    if (ret == HTTP_UPDATE_FAILED) {
        Serial.printf("[ota] update failed (%d): %s\n",
                      httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
        NoteOtaFailed(httpUpdate.getLastError());
        drawStatus(tft, fb, "Update failed");
        delay(2000);
    }
    // HTTP_UPDATE_OK reboots automatically (rebootOnUpdate(true)), so this only runs on
    // the failure and no-update paths -- the success path never needs the bus again.
    http.ReleaseBus();
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
