"""Pre-build patch: WiFiManager's customer-facing copy talks about "ESP".

THE PORTAL IS THE FIRST SCREEN A CUSTOMER EVER SEES, and after submitting a
password it tells them:

    Saving Credentials
    Trying to connect ESP to network.
    If it fails reconnect to AP to try again

Three problems in three lines. "ESP" is the chip family -- nobody who bought a
desk radar knows or should know that word. "AP" is jargon for the thing they
just joined. And "if it fails" is the only mention that failure is possible,
buried at the end of a message the phone has often already navigated away from
by the time it matters.

That last one is not cosmetic: a customer entered a wrong password, read exactly
this, and concluded the product was broken. Two days of hardware investigation
followed.

WHY A PATCH RATHER THAN A SETTER. WiFiManager exposes setCustomHeadElement and
setCustomMenuHTML but no hook for these PROGMEM strings -- they are compiled in.
Patching the dependency in .pio/ is the established pattern here; see
scripts/patch_async_buff.py, which exists for the same reason against
ESPAsyncWebServer.

Runs on every build and is idempotent, so it survives a fresh `pio` library
install -- .pio/ is gitignored and not part of the repo. If the library is ever
updated and a string no longer matches, this REPORTS that rather than silently
doing nothing: a copy fix that quietly stops applying is worse than none,
because nobody looks at the portal again once it has been signed off.
"""
import os

Import("env")  # noqa: F821  (provided by PlatformIO's SCons environment)

HEADER = os.path.join(
    env.subst("$PROJECT_LIBDEPS_DIR"), env["PIOENV"],  # noqa: F821
    "WiFiManager", "wm_strings_en.h",
)

# (what to find, what to replace it with, why)
PATCHES = [
    (
        "Saving Credentials<br/>Trying to connect ESP to network."
        "<br />If it fails reconnect to AP to try again",
        "Saving&hellip;<br/>Connecting your Blipscope to the network."
        "<br/><br/><b>Watch the device screen.</b> It will tell you in a few "
        "seconds if the password was wrong &mdash; then just rejoin this hotspot "
        "and try again.",
        "names the product, not the chip; points at the screen that now reports "
        "the reason within seconds",
    ),
    (
        '<dt>ESP-SDK/IDF</dt>',
        '<dt>SDK</dt>',
        "an info-page row; the acronym helps nobody who reads this page",
    ),
    (
        'const char S_titlewifi[]          PROGMEM = "Config ESP";',
        'const char S_titlewifi[]          PROGMEM = "Set up Blipscope";',
        "the browser tab title during setup",
    ),
]


def apply_patches():
    if not os.path.isfile(HEADER):
        print("patch_wm_copy: %s not found; skipping" % HEADER)
        return

    with open(HEADER, "r", encoding="utf-8", errors="replace") as fh:
        text = fh.read()

    applied, already, missing = 0, 0, []
    for find, repl, _why in PATCHES:
        if repl in text:
            already += 1
        elif find in text:
            text = text.replace(find, repl, 1)
            applied += 1
        else:
            missing.append(find[:48])

    if applied:
        with open(HEADER, "w", encoding="utf-8", newline="") as fh:
            fh.write(text)

    print("patch_wm_copy: %d applied, %d already patched" % (applied, already))
    # LOUD, not silent. A string that no longer matches means the library moved
    # and this fix has stopped reaching the customer -- which is exactly the
    # class of failure nobody notices, because the portal is not re-read.
    for m in missing:
        print("patch_wm_copy: WARNING -- pattern NOT FOUND, copy fix not applied: %r" % m)


apply_patches()
