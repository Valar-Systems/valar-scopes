// v19's build: WX_LAYER_SHIPS undefined (include/WxLayerSwitch.h; docs/wx-device.md, "v19 ships without weather"). No stored
// value can turn the layer on. Tagged WF1 so test/host/sabotage_wx_layer_switch.py can require exactly it to go red.
//
//   test_wx_layer_off.exe      exit 0 all pass, 1 a check failed
#include <cstdio>

#include "WxLayerSwitch.h"

int main()
{
    const bool off = !wxlayerswitch::kShips && !wxlayerswitch::On("") && !wxlayerswitch::On("true") && !wxlayerswitch::On("false");
    std::printf("  %s  WF1 the flag undefined: kShips false, and off for unset, \"true\" and \"false\"\n", off ? "ok  " : "FAIL");
    std::printf("%s\n", off ? "all WX layer off checks passed" : "WX LAYER OFF TESTS FAILED");
    return off ? 0 : 1;
}
