// Host test for "open this message on the computer" (FEATURE_USB_OPEN).
//
// The device types into whatever the computer has focused, so the one property
// worth grading is WHAT it can type. It grades the shipping planner,
// src/eam/UsbOpenPlan.h, compiled as-is against the String shim: only the fixed
// archive prefix plus a validated [A-Za-z0-9-] message id, never over 200
// characters, and nothing at all when the config says so.
#include <cstdio>
#include <cstring>
#include "../../src/eam/UsbOpenPlan.h"

static int failures = 0;

static void expect(bool ok, const char* what)
{
    if (!ok) { printf("  FAIL  %s\n", what); failures++; }
    else printf("  ok    %s\n", what);
}

static bool eq(const String& got, const char* want) { return std::strcmp(got.c_str(), want) == 0; }

int main()
{
    using namespace usbopen;
    const char* root = "https://missileer.watch/";  // LOCKED (CLAUDE.md "Locked values")

    // The id is the only network-sourced text that can be typed, and only when clean.
    expect(ValidId("eam-f27e2d58"), "a feed id is valid");
    expect(ValidId("M20260922T160115Z"), "letters and digits are valid");
    expect(!ValidId(""), "empty is not an id");
    expect(!ValidId("eam f27"), "a space is refused");
    expect(!ValidId("eam-1\n"), "a newline (an Enter on the computer) is refused");
    expect(!ValidId("x;rm -rf ~"), "shell punctuation is refused");
    expect(!ValidId("abc/../def"), "a slash is refused");
    expect(!ValidId("\xc3\xa9t\xc3\xa9"), "non-ASCII is refused");
    expect(!ValidId(String(std::string(129, 'a'))), "an id over 128 chars is refused");
    expect(ValidId(String(std::string(128, 'a'))), "an id of exactly 128 chars is valid");

    // The URL: the canonical public site, whatever is shown -- never the Render archive
    // (the working tool), never an id (the site has no per-message anchor).
    expect(eq(UrlFor("eam-f27e2d58"), root), "a clean id opens the public site");
    expect(eq(UrlFor("bad id!"), root), "a dirty id opens the public site, not its text");
    expect(eq(UrlFor(""), root), "no id opens the public site");
    expect(std::strstr(UrlFor("eam-1").c_str(), "onrender") == nullptr, "the Render archive is never typed");
    expect(UrlFor(String(std::string(128, 'a'))).length() <= MAX_TYPED, "within the 200-char cap");

    // The plan: what a long press types, per setting.
    expect(eq(PlanUrl(Os::Windows, "eam-1", true), root), "Windows, message shown: the public site");
    expect(eq(PlanUrl(Os::Mac, "", true), root), "nothing shown, 'open the site': the public site");
    expect(PlanUrl(Os::Linux, "", false).length() == 0, "nothing shown, 'do nothing': types nothing");
    expect(PlanUrl(Os::Windows, "bad id!", false).length() == 0, "a dirty id with 'do nothing' types nothing");
    expect(PlanUrl(Os::Off, "eam-1", true).length() == 0, "OS set to Off: types nothing, even with a message");

    // The config value and the launcher chord.
    expect(OsFromConfig("") == Os::Windows, "absent setting = Windows (the default)");
    expect(OsFromConfig("junk") == Os::Windows, "an unknown setting = Windows");
    expect(OsFromConfig("mac") == Os::Mac && OsFromConfig("linux") == Os::Linux && OsFromConfig("off") == Os::Off,
           "mac / linux / off parse");
    const Chord w = ChordFor(Os::Windows), m = ChordFor(Os::Mac), l = ChordFor(Os::Linux);
    expect(w.mod == Mod::Gui && w.key == 'r' && !w.fKey, "Windows: Win+R");
    expect(m.mod == Mod::Gui && m.key == ' ' && !m.fKey, "macOS: Cmd+Space");
    expect(l.mod == Mod::Alt && l.fKey && l.fNum == 2, "Linux: Alt+F2");
    expect(WaitFor(Os::Windows) == 400 && WaitFor(Os::Mac) == 400 && WaitFor(Os::Linux) >= 400,
           "wait 400 ms for the launcher (longer on Linux)");

    printf("%s: %d failure(s)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
