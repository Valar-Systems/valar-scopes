// v19's build: WX_LAYER_SHIPS undefined (include/WxLayerSwitch.h; docs/wx-device.md, "v19 ships without weather"). No stored
// value can turn the layer on. Rows are tagged WF1-WF3 so test/host/sabotage_wx_layer_switch.py can require exactly the
// sabotaged rows to go red.
//
//   test_wx_layer_off.exe      exit 0 all pass, 1 a check failed
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
    check(!wxlayerswitch::kShips, "WF1", "the flag undefined: kShips is false");
    check(!wxlayerswitch::On(""), "WF2", "unset is OFF: a v18 board updating to v19 gets no weather");
    check(!wxlayerswitch::On("true"), "WF3", "a stored \"true\" is OFF: a key cannot turn weather on in this build");

    std::printf("%s\n", failures ? "WX LAYER OFF TESTS FAILED" : "all WX layer off checks passed");
    return failures ? 1 : 0;
}
