#pragma once

#include <Arduino.h>
#include <vector>
#include <utility>
#include <Preferences.h>

// Persistent, bounded log of the EAMs and Skyking codewords this device has seen. Mirrors the
// radar Logbook's shape (NVS namespace + newline-delimited sets + dirty-flag debounced flush),
// but tracks EAM ids / codewords instead of aircraft types. It answers two questions the screens
// need across reboots: is this EAM/codeword NEW to this device, and how many codewords this month.
//
// EAM events are infrequent, so the debounce is short (a new entry is flushed within ~10 s) -- a
// reboot loses at most the last few seconds, not the log.
class EamLogbook {
public:
    void Begin(); // load from NVS (idempotent)

    // Record an EAM id (with its heard epoch). Returns true if it was NEW to this device.
    bool NoteEam(const String& id, long heardEpoch);
    // Record a codeword (with its seen epoch). Returns true if it was NEW to this device.
    bool NoteCodeword(const String& codeword, long seenEpoch);

    uint32_t EamCount() const { return eamCount; }          // unique EAMs ever logged
    size_t CodewordCount() const { return codewords.size(); }
    size_t CodewordsThisMonth(long nowEpoch) const;         // codewords whose seen-month == now's

    void MaybePersist(); // flush to NVS if dirty and the debounce has elapsed

private:
    Preferences prefs;
    bool loaded = false;
    bool dirty = false;
    unsigned long lastPersistMs = 0;

    std::vector<String> eamIds;                       // recent ids (bounded), for dedupe
    uint32_t eamCount = 0;                            // odometer (survives id eviction)
    std::vector<std::pair<String, long>> codewords;   // codeword + first-seen epoch (bounded)

    void Persist();
};
