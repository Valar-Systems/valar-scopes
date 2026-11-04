#pragma once
#include "Arduino.h"
#include <map>
#include <string>
#include <vector>

// Blipscope web demo: NVS as an in-memory map per namespace, empty at every page load. A logbook or a
// counter the radar writes lives until the tab closes, and nothing persists.
typedef enum { PT_I8, PT_U8, PT_I16, PT_U16, PT_I32, PT_U32, PT_I64, PT_U64, PT_STR, PT_BLOB, PT_INVALID } PreferenceType;

class Preferences
{
  public:
    // Every value is stored as bytes here, so the only type the demo can report for a key is "blob" or
    // "absent"; Logbook asks only whether a key is a blob.
    PreferenceType getType(const char* k) { return get(k) ? PT_BLOB : PT_INVALID; }
    bool begin(const char* ns, bool readOnly = false) { ns_ = ns ? ns : ""; ro_ = readOnly; return true; }
    void end() {}
    bool clear() { store()[ns_].clear(); return true; }
    bool remove(const char* k) { return store()[ns_].erase(k) > 0; }
    bool isKey(const char* k) { return store()[ns_].count(k) > 0; }
    size_t freeEntries() { return 500; }

    size_t putString(const char* k, const String& v) { return put(k, v.str(), v.length()); }
    size_t putString(const char* k, const char* v) { return put(k, v ? v : "", v ? std::strlen(v) : 0); }
    String getString(const char* k, const String& d = String()) { auto* v = get(k); return v ? String(*v) : d; }
    size_t getString(const char* k, char* buf, size_t n) { auto* v = get(k); if (!v || !buf || !n) return 0; size_t c = std::min(n - 1, v->size()); std::memcpy(buf, v->data(), c); buf[c] = 0; return c; }
    size_t putBool(const char* k, bool v) { return put(k, v ? "1" : "0", 1); }
    bool getBool(const char* k, bool d = false) { auto* v = get(k); return v ? *v == "1" : d; }
    size_t putInt(const char* k, int32_t v) { return put(k, std::to_string(v), 4); }
    int32_t getInt(const char* k, int32_t d = 0) { auto* v = get(k); return v ? (int32_t)std::strtol(v->c_str(), nullptr, 10) : d; }
    size_t putUInt(const char* k, uint32_t v) { return put(k, std::to_string(v), 4); }
    uint32_t getUInt(const char* k, uint32_t d = 0) { auto* v = get(k); return v ? (uint32_t)std::strtoul(v->c_str(), nullptr, 10) : d; }
    size_t putLong(const char* k, int32_t v) { return putInt(k, v); }
    int32_t getLong(const char* k, int32_t d = 0) { return getInt(k, d); }
    size_t putULong(const char* k, uint32_t v) { return putUInt(k, v); }
    uint32_t getULong(const char* k, uint32_t d = 0) { return getUInt(k, d); }
    size_t putLong64(const char* k, int64_t v) { return put(k, std::to_string(v), 8); }
    int64_t getLong64(const char* k, int64_t d = 0) { auto* v = get(k); return v ? (int64_t)std::strtoll(v->c_str(), nullptr, 10) : d; }
    size_t putULong64(const char* k, uint64_t v) { return put(k, std::to_string(v), 8); }
    uint64_t getULong64(const char* k, uint64_t d = 0) { auto* v = get(k); return v ? (uint64_t)std::strtoull(v->c_str(), nullptr, 10) : d; }
    size_t putUChar(const char* k, uint8_t v) { return put(k, std::to_string(v), 1); }
    uint8_t getUChar(const char* k, uint8_t d = 0) { return (uint8_t)getUInt(k, d); }
    size_t putUShort(const char* k, uint16_t v) { return put(k, std::to_string(v), 2); }
    uint16_t getUShort(const char* k, uint16_t d = 0) { return (uint16_t)getUInt(k, d); }
    size_t putFloat(const char* k, float v) { return put(k, std::to_string(v), 4); }
    float getFloat(const char* k, float d = 0) { auto* v = get(k); return v ? std::strtof(v->c_str(), nullptr) : d; }
    size_t putBytes(const char* k, const void* p, size_t n) { return (p && n) ? put(k, std::string((const char*)p, n), n) : 0; }
    size_t getBytes(const char* k, void* p, size_t n) { auto* v = get(k); if (!v) return 0; size_t c = std::min(n, v->size()); std::memcpy(p, v->data(), c); return c; }
    size_t getBytesLength(const char* k) { auto* v = get(k); return v ? v->size() : 0; }

  private:
    std::string ns_;
    bool ro_ = false;
    static std::map<std::string, std::map<std::string, std::string>>& store() { static std::map<std::string, std::map<std::string, std::string>> s; return s; }
    // Returns what Arduino's Preferences returns: the bytes written (a type's width; a blob's or string's length).
    size_t put(const char* k, const std::string& v, size_t ret) { if (ro_ || !k) return 0; store()[ns_][k] = v; return ret; }
    const std::string* get(const char* k) { auto& m = store()[ns_]; auto it = m.find(k ? k : ""); return it == m.end() ? nullptr : &it->second; }
};
