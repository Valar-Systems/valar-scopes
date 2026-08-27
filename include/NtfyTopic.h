#pragma once

// The ntfy topic is GENERATED, not typed.  Spec: docs/follow-mode-consolidated.md §14.1.
//
// =============================================================================
// WHY THIS EXISTS
//
// Follow changed what an ntfy topic *is*. A topic carrying "military flyover" is
// a hobby feed. A topic carrying **"N4523K is airborne"** is a named person's
// movements, published to a service where anyone who guesses the topic can read
// them.
//
// And a user-typed topic will be short and guessable, because that is what
// people type. `blipscope`, `planes`, `dads-flights`. The config page already
// warns that anyone who knows the topic can read the alerts — but a warning
// beside a blank box is advice, and this is the one place where the safe thing
// can simply be the default.
//
// So the field ships PRE-FILLED with `blip-<10 chars base32>` — 50 bits, which
// nobody guesses and nobody but the owner ever types. The owner can still
// overwrite it; some people legitimately want one shared topic across devices.
//
// =============================================================================
// esp_random(), NOT ANYTHING SEEDED FROM millis()
//
// §14.1 names this specifically and it is the whole point of the file. A fleet
// of devices boots through a very similar sequence, so a `millis()`-seeded
// generator would produce a correlated — and therefore searchable — set of
// topics across every unit ever shipped. That is not a weaker version of this
// feature; it is the failure this feature exists to prevent, arrived at through
// a function whose name sounds random.
//
// esp_random() is the hardware RNG. It requires the RF subsystem to be up for
// full entropy, which is why Ensure() is called after Wi-Fi rather than in the
// first line of setup().
//
// =============================================================================
// PERSISTED AT GENERATION, NOT AT FIRST SAVE
//
// So the page shows a stable value across reloads. A topic that regenerated on
// every page load would be a field the customer could never successfully
// subscribe a phone to.
//
// AND ONLY WHEN THE KEY WAS NEVER WRITTEN. `isKey()`, not "the string is empty":
// a device whose owner saved the form with the box cleared has the key present
// and empty, and that is a DECISION. Overwriting it would turn "I do not want
// alerts" into a live topic. Same rule as every other default in this codebase —
// see CLAUDE.md, "a default only reaches keys that were never saved".
//
// SHARED, NOT RADAR-ONLY. Seven products write this one key, and two policies
// for one key is how the two drift apart. The reasoning above is strongest for
// Follow and is not wrong anywhere else.

#include <Arduino.h>
#include <Preferences.h>

#include <esp_random.h>

namespace ntfytopic {

// Crockford-style base32: no I, L, O or U. The customer has to read this off a
// screen and type it into a phone, and 1/I and 0/O are the two pairs that make
// that go wrong. 32 symbols, so it is still exactly 5 bits a character.
inline const char* Alphabet() { return "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; }

/// A fresh topic. 10 characters x 5 bits = 50 bits.
inline String Generate()
{
    String out = "blip-";
    for (int i = 0; i < 10; ++i)
        out += Alphabet()[esp_random() % 32u];
    return out;
}

/// Generate and persist, ONCE, only if the key has never been written.
/// Returns true if it wrote one.
inline bool Ensure()
{
    Preferences p;
    if (!p.begin("config", /*readOnly=*/false))
        return false;
    if (p.isKey("ntfy-topic")) { p.end(); return false; }
    const String topic = Generate();
    p.putString("ntfy-topic", topic);
    p.end();
    // The VALUE is deliberately not logged. It is a bearer secret -- anyone who
    // knows it can read the owner's alerts -- and serial output is exactly the
    // place this project has leaked one before (see the Wi-Fi password
    // incident, and §17's list).
    Serial.println("[ntfy] generated a private topic for this device (see the config page)");
    return true;
}

} // namespace ntfytopic
