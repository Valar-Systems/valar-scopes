#pragma once
// Blipscope web renderer: one address space. Every buffer counts as PSRAM, which is where each S3 SKU puts the
// large ones (Follow's track, the photo sprite); answering "internal" made Follow degrade itself.
inline bool esp_ptr_external_ram(const void*) { return true; }
inline bool esp_ptr_internal(const void*) { return false; }
