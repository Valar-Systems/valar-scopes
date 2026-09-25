#pragma once
// "Open this message on the computer" -- the PURE half of the USB keyboard feature
// (FEATURE_USB_OPEN, Missileer on the 1.28" Kit S3). It decides WHAT gets typed;
// UsbOpen.cpp decides nothing and only replays the plan through USBHIDKeyboard.
//
// SAFETY IS A PROPERTY OF THIS FILE, which is why it is a header the host tests
// compile as-is (test/host/test_usb_open.cpp). The keyboard types into whatever
// the computer has focused, so the only characters that can ever leave the
// device are:
//   - the fixed, compile-time URL below, and nothing else, and
//   - at most MAX_TYPED characters in total.
// Nothing from the network is typed: not the base URL the config page can
// change, not message text, not a callsign, and (since 2026-09-24) not even the
// message id -- the public site has no per-message anchor to point it at.
//
// Keystrokes assume a US keyboard layout on the computer (':' and '/' move on
// AZERTY/QWERTZ); the URL is ASCII only.
#include <Arduino.h>

namespace usbopen {

// THE CANONICAL PUBLIC SITE -- a LOCKED value, same status as the palette (CLAUDE.md
// "Locked values"). missileerwatch.com 301s here. It was the Render archive page
// (valar-eam-feed.onrender.com/missileer/archive) until 2026-09-24; that page is the
// working tool, not what a person's long press should open (Fable, 2026-09-24).
// Deliberately NOT the runtime `eam-base-url`: that setting is text from a web
// form, and nothing typed into a computer may come from it.
static const char* const SITE_URL = "https://missileer.watch/";
static const unsigned MAX_ID_LEN = 128;
static const unsigned MAX_TYPED = 200;
// The launcher needs a moment to open before it takes keystrokes.
static const unsigned LAUNCHER_WAIT_MS = 400;
static const unsigned LAUNCHER_WAIT_LINUX_MS = 600;

enum class Os : uint8_t { Windows, Mac, Linux, Off };

// `eam-usb-os` from the config page; an absent or unknown value is Windows (the default).
inline Os OsFromConfig(const String& v)
{
    if (v == "mac" || v == "macos") return Os::Mac;
    if (v == "linux") return Os::Linux;
    if (v == "off") return Os::Off;
    return Os::Windows;
}

inline bool ValidId(const String& id)
{
    const unsigned n = id.length();
    if (n == 0 || n > MAX_ID_LEN) return false;
    for (unsigned i = 0; i < n; ++i) {
        const char c = id[i];
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
        if (!ok) return false;
    }
    return true;
}

// The URL to type: the public site. The shown message's id no longer rides along
// (missileer.watch has no per-message anchor); `id` is kept so a deep link can come
// back the day the site has one, without changing the callers.
inline String UrlFor(const String& id)
{
    (void)id;
    return String(SITE_URL);
}

// The launcher chord for each OS: a modifier held while one key is tapped.
enum class Mod : uint8_t { Gui, Alt };
struct Chord { Mod mod; char key; bool fKey; uint8_t fNum; };

inline Chord ChordFor(Os os)
{
    switch (os) {
        case Os::Mac:   return { Mod::Gui, ' ', false, 0 };   // Cmd+Space: Spotlight
        case Os::Linux: return { Mod::Alt, 0, true, 2 };      // Alt+F2: run dialog
        default:        return { Mod::Gui, 'r', false, 0 };   // Win+R: Run
    }
}

inline unsigned WaitFor(Os os) { return os == Os::Linux ? LAUNCHER_WAIT_LINUX_MS : LAUNCHER_WAIT_MS; }

// What to do for a long press. `shownId` is the id of the message on screen, or ""
// when none is; `emptyOpensArchive` is the config page's "when nothing is shown"
// choice (false = do nothing). Returns an empty string when nothing is typed.
inline String PlanUrl(Os os, const String& shownId, bool emptyOpensArchive)
{
    if (os == Os::Off) return String();
    if (!ValidId(shownId) && !emptyOpensArchive) return String();
    return UrlFor(shownId);
}

} // namespace usbopen
