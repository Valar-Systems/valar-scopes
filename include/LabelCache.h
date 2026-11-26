#pragma once
// P6 (L1), the label-layout cache (docs/speed-p5p6.md section 2 and amendment 4g, on weatherscope/speed-p5p6).
//
// TODAY, per contact per frame, LabelLayout ran twice (the box in the first pass, the draw in the second). Each run
// formatted every enabled field to learn which were non-empty (Arduino String allocations), then formatted and
// measured the chosen ones; the draw formatted them a third time for drawString.
//
// THE CACHE keeps, per contact, the chosen fields' strings and widths until any value they were formatted from
// changes. The placement (ChordWidthPx, drop-never-truncate, the clamp, each row's y) is recomputed EVERY frame from
// the cached widths, because it depends on where the contact is. The box and the draw share the one entry.
//
// "Any value they were formatted from" is the KEY: every input of every info field, exactly (strings length-prefixed,
// floats by their bits, ints, and the enabled-field mask). LabelKey builds it beside the formatters
// (src/AircraftInfoFields.cpp), and test/host/test_p6_labels.cpp's P6-COVER row moves every input the real formatters
// read, one at a time, and requires the key to move whenever any field's text does. A contact whose inputs or labels
// do not fit the fixed buffers is never cached: it is laid out as today, every frame.
//
// The indicator row (section 2): the cache holds strings and widths, never pixels, so it cannot hold the screen
// indicator. Entry is plain data (asserted below), and nothing in it is drawn except through drawString.
//
// Header-only and free of Arduino and LovyanGFX: both layouts take an adapter F, so the host compiles exactly this.
// F provides: int Count(); const char* FieldKey(int); bool On(int); Str Format(int) (Str has isEmpty(), c_str(),
// length()); int Width(const Str&); int LineHeight(); int Chord(int rowY, int lineH); int Half();
// void MakeKey(KeyBuf&).
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include "LabelLines.h"

#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#endif

namespace labelcache {

constexpr int kLines = labellines::MAX_LINES;
constexpr int kMaxFields = 32;    // AircraftManager's LABEL_MAX_FIELDS: the enabled mask is one bit a field
constexpr int kKeyBytes = 192;    // one contact's inputs, length-prefixed
constexpr int kTextBytes = 40;    // one label line with its NUL (a 240 px chord holds 38 glyphs of the 6 px font)

// Every input a contact's info fields are formatted from. Equal keys format every field equal. An input past the
// buffer sets `overflow`, and an overflowed key equals nothing, so that contact is never cached.
struct KeyBuf {
    uint8_t b[kKeyBytes];
    uint16_t len = 0;
    bool overflow = false;

    void Bytes(const void* p, size_t n)
    {
        if (overflow || (size_t)len + n > (size_t)kKeyBytes) { overflow = true; return; }
        if (n) std::memcpy(b + len, p, n);
        len = (uint16_t)(len + n);
    }
    void Str(const char* s)   // length first, so "ab" + "" and "a" + "b" differ
    {
        const size_t n = s ? std::strlen(s) : 0;
        if (n > 0xFFFFu) { overflow = true; return; }
        const uint16_t n16 = (uint16_t)n;
        Bytes(&n16, sizeof n16);
        Bytes(s, n);
    }
    void Num(float v) { Bytes(&v, sizeof v); }      // by its bits: a change that does not move the text costs one refill
    void Num(int32_t v) { Bytes(&v, sizeof v); }
    bool operator==(const KeyBuf& o) const
    {
        return !overflow && !o.overflow && len == o.len && std::memcmp(b, o.b, len) == 0;
    }
};

// One contact's cached labels: the chosen fields (labellines::Select's order), their exact strings and widths.
struct Entry {
    bool valid = false;
    uint32_t enabled = 0;   // the enabled-field mask it was made under
    KeyBuf key;
    int8_t n = 0;
    uint8_t idx[kLines] = {};
    int16_t width[kLines] = {};
    char text[kLines][kTextBytes] = {};
};
static_assert(std::is_trivially_copyable<Entry>::value, "the label cache is plain data: strings and widths, no pixels");

struct Stats {
    long hits = 0, refills = 0, uncached = 0;
};

// What may draw, decided ONCE for both layouts (LayoutToday and Refresh): ENABLED **AND** NON-EMPTY -- a field that
// formats to "" for this contact must not spend one of the three lines -- and then the callsign fallback
// (labellines::FallbackField: only when the type is ticked and unknown). The cache key holds the callsign and the type
// whatever is ticked (LabelKey), so a cached entry can never carry a fallback that no longer applies.
template <class F>
inline void Decide(const F& f, int count, const char** keys, bool* willDraw)
{
    bool on[kMaxFields];
    for (int i = 0; i < count; ++i) {
        keys[i] = f.FieldKey(i);
        on[i] = f.On(i);
        willDraw[i] = on[i] && !f.Format(i).isEmpty();
    }
    const int fb = labellines::FallbackField(keys, on, willDraw, count);
    if (fb >= 0 && !f.Format(fb).isEmpty()) willDraw[fb] = true;
}

// TODAY's layout, unchanged (AircraftManager::LabelLayout before P6): the bench's OLD path, and the cache's fallback.
template <class F>
inline int LayoutToday(const F& f, int x, int y, int* idxOut, int* xOut, int* yOut, int* wOut)
{
    const int lineHeight = f.LineHeight();
    const int count = f.Count() < kMaxFields ? f.Count() : kMaxFields;

    const char* keys[kMaxFields];
    bool willDraw[kMaxFields];
    Decide(f, count, keys, willDraw);

    int chosen[kLines];
    const int n = labellines::Select(keys, willDraw, count, chosen);

    // DROP, NEVER TRUNCATE: a row that does not fit the disc's chord at its height is left out.
    int placed = 0;
    for (int k = 0; k < n; ++k) {
        const auto text = f.Format(chosen[k]);
        const int rowY = y + 5 + lineHeight * k;
        const int avail = f.Chord(rowY, lineHeight);
        if (avail <= 0) continue;                       // row is off the glass entirely
        const int w = f.Width(text);
        if (w > avail) continue;                        // cannot fit at this height
        const int xMin = f.Half() - avail / 2;
        const int xMax = f.Half() + avail / 2;
        int lx = x + 5;
        if (lx < xMin) lx = xMin;
        if (lx + w > xMax) lx = xMax - w;
        idxOut[placed] = chosen[k];
        xOut[placed]   = lx;
        yOut[placed]   = rowY;
        wOut[placed]   = w;
        ++placed;
    }
    return placed;
}

// Brings e up to this frame's inputs. True: e holds this frame's chosen strings and widths (a hit, or just refilled).
// False: this contact cannot be cached (an input past the key buffer, or a chosen line past kTextBytes), and the
// caller lays it out with LayoutToday, exactly as before.
template <class F>
inline bool Refresh(const F& f, Entry& e, Stats* st = nullptr)
{
    const int count = f.Count() < kMaxFields ? f.Count() : kMaxFields;
    uint32_t mask = 0;
    for (int i = 0; i < count; ++i)
        if (f.On(i)) mask |= 1u << i;
    KeyBuf k;
    f.MakeKey(k);
    if (e.valid && e.enabled == mask && e.key == k) {
        if (st) ++st->hits;
        return true;
    }
    e.valid = false;
    if (k.overflow) {
        if (st) ++st->uncached;
        return false;
    }
    const char* keys[kMaxFields];
    bool willDraw[kMaxFields];
    Decide(f, count, keys, willDraw);   // `mask` above is f.On(i), bit by bit, read in this same call
    int chosen[kLines];
    const int n = labellines::Select(keys, willDraw, count, chosen);
    for (int j = 0; j < n; ++j) {
        const auto text = f.Format(chosen[j]);
        const size_t len = text.length();
        if (len + 1 > (size_t)kTextBytes) {
            if (st) ++st->uncached;
            return false;
        }
        std::memcpy(e.text[j], text.c_str(), len + 1);
        e.idx[j] = (uint8_t)chosen[j];
        e.width[j] = (int16_t)f.Width(text);
    }
    e.n = (int8_t)n;
    e.enabled = mask;
    e.key = k;
    e.valid = true;
    if (st) ++st->refills;
    return true;
}

// The placement from a valid entry: LayoutToday's loop, on the cached widths, at THIS frame's (x, y). slotOut[k] is
// the entry's line for output row k (e.text[slotOut[k]]).
template <class F>
inline int PlaceCached(const F& f, const Entry& e, int x, int y, int* idxOut, int* xOut, int* yOut, int* wOut,
                       int* slotOut)
{
    const int lineHeight = f.LineHeight();
    int placed = 0;
    for (int k = 0; k < e.n; ++k) {
        const int rowY = y + 5 + lineHeight * k;
        const int avail = f.Chord(rowY, lineHeight);
        if (avail <= 0) continue;
        const int w = e.width[k];
        if (w > avail) continue;
        const int xMin = f.Half() - avail / 2;
        const int xMax = f.Half() + avail / 2;
        int lx = x + 5;
        if (lx < xMin) lx = xMin;
        if (lx + w > xMax) lx = xMax - w;
        idxOut[placed]  = e.idx[k];
        xOut[placed]    = lx;
        yOut[placed]    = rowY;
        wOut[placed]    = w;
        slotOut[placed] = k;
        ++placed;
    }
    return placed;
}

// A contact's cache, owned by the contact (TrackedAircraft::labelCache) and held in PSRAM on the device, so each
// contact pays one pointer of internal heap. A COPY STARTS EMPTY: the cache only accelerates, an empty entry refills,
// and an entry that came from elsewhere can hit only when every input is equal, when every field formats equal.
class Slot {
  public:
    Slot() = default;
    Slot(const Slot&) {}
    Slot& operator=(const Slot&) { return *this; }
    Slot(Slot&& o) noexcept : e_(o.e_) { o.e_ = nullptr; }
    Slot& operator=(Slot&& o) noexcept
    {
        if (this != &o) {
            Free();
            e_ = o.e_;
            o.e_ = nullptr;
        }
        return *this;
    }
    ~Slot() { Free(); }

    // The entry, made on first use; nullptr when there is no memory for it (the contact is then laid out as today).
    Entry* Get()
    {
        if (!e_) {
#if defined(ESP_PLATFORM)
            void* p = heap_caps_malloc(sizeof(Entry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
            void* p = ::operator new(sizeof(Entry), std::nothrow);
#endif
            if (p) e_ = new (p) Entry();
        }
        return e_;
    }

  private:
    void Free()
    {
        if (!e_) return;
        e_->~Entry();
#if defined(ESP_PLATFORM)
        heap_caps_free(e_);
#else
        ::operator delete(e_);
#endif
        e_ = nullptr;
    }
    Entry* e_ = nullptr;
};

}  // namespace labelcache
