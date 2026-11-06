// The radar's weather switch (include/WxLayerSwitch.h; docs/wx-device.md, "The layer on by default"). Rows are tagged
// WS1-WS4 so test/host/sabotage_wx_layer_switch.py can require exactly the sabotaged rows to go red.
//
//   test_wx_layer_switch.exe      exit 0 all pass, 1 a check failed
#include <cstdio>

#include "WxLayerSwitch.h"

static int failures = 0;
static void check(bool ok, const char* id, const char* what)
{
    if (!ok) ++failures;
    std::printf("  %s  %s %s\n", ok ? "ok  " : "FAIL", id, what);
}

int main()
{
    check(wxlayerswitch::On(""), "WS1", "unset (GetStoredString's \"\") is ON: every board that updates from v18");
    check(wxlayerswitch::On("true"), "WS2", "a saved \"true\" is on");
    check(!wxlayerswitch::On("false"), "WS3", "a saved \"false\" is OFF: an owner's choice stands");
    check(!wxlayerswitch::On("TRUE") && !wxlayerswitch::On("1") && !wxlayerswitch::On("on"), "WS4",
          "any other value is off, the sweep's rule");

    std::printf("%s\n", failures ? "WX LAYER SWITCH TESTS FAILED" : "all WX layer switch checks passed");
    return failures ? 1 : 0;
}
