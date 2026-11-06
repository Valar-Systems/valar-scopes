// The probe for -sDYNAMIC_EXECUTION=0 (docs/web-renderer.md, the follow-up, F4). Not shipped.
//
// The flag changes no byte of the web build, so the build cannot show that it took. This can: emscripten_run_script
// needs eval, so linked with build.sh's flags its loader must hold none, and linked again with only that one flag
// removed it must hold exactly one. check_site_rules.mjs grades both.
#include <emscripten.h>

int main()
{
    emscripten_run_script("0");
    return 0;
}
