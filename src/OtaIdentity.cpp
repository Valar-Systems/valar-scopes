// This image's OTA identity record (include/OtaIdentity.h; docs/weatherscope-stage3.md A12.104).
//
// It sits in ESP-IDF's custom app descriptor section, which every esp32s3 linker
// script places right after esp_app_desc_t -- so it lands at file offset 0x120 in
// every image, where CI and other devices read it without scanning.
//
// THAT SECTION IS NOT KEPT BY THE LINKER SCRIPT (no KEEP in sections.ld), so with
// --gc-sections an unreferenced record would silently vanish and every image would
// read as pre-check. LogOtaSlot() reads it back at boot ("[ota-id] ..."): that read
// is the reference that keeps it, and CI's image check is what proves it is there.

#include "OtaIdentity.h"
#include "variants/Variant.h"

#ifndef FW_OTA_PREFIX
#define FW_OTA_PREFIX ""
#endif

namespace {

constexpr size_t Len(const char* s)
{
    size_t n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

constexpr bool AllSlug(const char* s)
{
    for (size_t i = 0; s[i] != '\0'; ++i)
        if (!ota::SlugChar(s[i])) return false;
    return true;
}

constexpr ota::Record Make(const char* board, const char* scope)
{
    ota::Record r{};
    for (size_t i = 0; i < 4; ++i) r.magic[i] = ota::kMagic[i];
    r.version = ota::kRecordVersion;
    r.length = (uint8_t)ota::kRecordSize;
    r.boardLen = (uint8_t)Len(board);
    r.scopeLen = (uint8_t)Len(scope);
    for (size_t i = 0; board[i] != '\0'; ++i) r.board[i] = board[i];
    for (size_t i = 0; scope[i] != '\0'; ++i) r.scope[i] = scope[i];
    return r;
}

// A name the record cannot carry fails the BUILD, never the parse on a device.
static_assert(Len(variant::SLUG) >= 1 && Len(variant::SLUG) <= ota::kFieldMax, "variant::SLUG must be 1-24 bytes");
static_assert(Len(FW_OTA_PREFIX) <= ota::kFieldMax, "FW_OTA_PREFIX must be at most 24 bytes");
static_assert(AllSlug(variant::SLUG) && AllSlug(FW_OTA_PREFIX), "board and scope must be [a-z0-9-]");

}  // namespace

namespace ota {
extern const Record kOwnRecord __attribute__((section(".rodata_custom_desc"), used)) = Make(variant::SLUG, FW_OTA_PREFIX);
}  // namespace ota
