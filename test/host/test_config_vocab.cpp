// The form's declared vocabulary. See include/ConfigVocabulary.h.
#include "../../include/ConfigVocabulary.h"
#include <cstdio>
static int checks=0, failures=0;
static void ck(bool ok,const char* w){++checks; if(!ok){++failures;std::printf("  FAIL  %s\n",w);} }
using cfgvocab::Declares;
int main(){
    std::printf("== config form vocabulary ==\n");

    std::printf("  ---- a declared name may be written false\n");
    ck(Declares("scanline,fade,triangle","fade"),       "middle entry");
    ck(Declares("scanline,fade,triangle","scanline"),   "first entry");
    ck(Declares("scanline,fade,triangle","triangle"),   "last entry");
    ck(Declares("fade","fade"),                          "sole entry");

    // ---- THE CASE THE WHOLE FIX EXISTS FOR --------------------------------
    // A toggle the page never rendered. Writing false here is the silent wipe.
    std::printf("  ---- an UNdeclared name is never authorised\n");
    ck(!Declares("scanline,fade","triangle"), "a toggle this form never rendered");
    ck(!Declares("", "fade"),                 "empty vocabulary authorises NOTHING");
    ck(!Declares(nullptr, "fade"),            "absent vocabulary authorises NOTHING");

    // ---- DELIMITER-AWARE, NOT strstr --------------------------------------
    // The config page really has both of these shapes, and a substring match
    // would authorise writing false to a setting the form never mentioned --
    // which is the exact bug being fixed, reintroduced by the fix.
    std::printf("  ---- substring collisions do not authorise\n");
    ck(!Declares("info-type2","info-type"),  "info-type must not match inside info-type2");
    ck(!Declares("altcolor","alt"),          "alt must not match inside altcolor");
    ck(!Declares("info-type","info-type2"),  "and not the other way round");
    ck( Declares("info-type,info-type2","info-type"),  "CONTROL: both present, exact match works");
    ck( Declares("info-type,info-type2","info-type2"), "CONTROL: ... for either one");

    std::printf("  ---- whitespace a browser might introduce\n");
    ck(Declares("scanline, fade, triangle","fade"), "space after comma");
    ck(Declares(",,fade,,","fade"),                 "empty entries are skipped");

    // CONTROL: without this, everything above passes against a Declares() that
    // always returns false -- which would be safe but would never write false
    // for anything, silently disabling the whole-form behaviour.
    ck(Declares("a1,fade,z9","fade") && !Declares("a1,z9","fade"),
       "CONTROL: it can say YES and NO on the same vocabulary");

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
