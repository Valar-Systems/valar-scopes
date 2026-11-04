// Blipscope web demo: main.cpp's radar half, for the page. The same globals in the same order, the same
// boot calls for the radar (tft.init, a 16-bit full-screen backbuffer, appManager.Initialise), and a
// frame that is loop()'s draw cycle line for line: Update, clear, the sweep when the radar shows it, Draw,
// push. The page calls demo_frame() once per animation frame and then replays the pushed display list.
//
// Left out of the frame, because the demo has none of them: Wi-Fi and the config page, OTA and the quiet
// hour, the factory-reset path, MQTT, the leaderboard, the logbook flush hand-off.
#include <Arduino.h>
#include "LGFX.h"
#include "Layout.h"
#include "BandCanvas.h"
#include "DrawHelpers.h"
#include "AircraftManager.h"
#include "ConfigurationWebServer.h"
#include "HttpRequestManager.h"
#include "OpenSkyAuthTokenHandler.h"
#include <emscripten.h>

LGFX tft;
LGFX_Sprite backbuffer(&tft);
ConfigurationWebServer configServer;
HttpRequestManager http;
OpenSkyAuthTokenHandler authHandler(http);
AircraftManager appManager(configServer, authHandler, http, tft);

extern "C" EMSCRIPTEN_KEEPALIVE void demo_frame()
{
    const uint32_t frameStartUs = micros();
    appManager.Update();

    // main.cpp loop(), the radar edition's draw cycle (BAND_H == SCREEN_SIZE on every S3: one band).
    const bool drawScan = appManager.SweepEnabled() && appManager.IsRadarView() && !appManager.NightClockActive() &&
                          !appManager.SweepSuppressed();
    const float sweep = appManager.CurrentSweepAngle();
    for (int bandY = 0; bandY < SCREEN_SIZE; bandY += BAND_H) {
        BandCanvas canvas(backbuffer, bandY);
        const bool firstPass = (bandY == 0);
        canvas.fillScreen(lgfx::color888(0, 0, 0));
        if (drawScan)
            DrawRadarSweep(canvas, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2 - 1, SCREEN_SIZE_DIV_2, sweep);
        appManager.Draw(canvas, firstPass);
        backbuffer.pushSprite(0, bandY);
    }
    appManager.RecordFrameUs(micros() - frameStartUs);

    // The fetch and enrich tasks, between frames (web_rtos.cpp).
    rtos_run_ready_tasks();
}

int main()
{
    tft.init();
    backbuffer.setPsram(true);
    backbuffer.setColorDepth(16);
    backbuffer.createSprite(SCREEN_SIZE, BAND_H);
    configServer.Initialise();
    appManager.Initialise();
    rtos_run_ready_tasks();   // let the tasks reach their first wait
    return 0;
}
