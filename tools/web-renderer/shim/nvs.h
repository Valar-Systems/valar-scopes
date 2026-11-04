#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_system.h"

// Blipscope web renderer: the raw NVS calls Logbook makes. Statistics answer a roomy partition; a raw
// write is accepted and discarded (Preferences is the demo's real store, in memory).
typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY = 0, NVS_READWRITE = 1 } nvs_open_mode_t;
typedef struct { size_t used_entries, free_entries, available_entries, total_entries, namespace_count; } nvs_stats_t;
inline esp_err_t nvs_get_stats(const char*, nvs_stats_t* s)
{
    if (s) { s->used_entries = 40; s->free_entries = 4000; s->available_entries = 4000; s->total_entries = 4040; s->namespace_count = 4; }
    return ESP_OK;
}
inline esp_err_t nvs_open(const char*, nvs_open_mode_t, nvs_handle_t* h) { if (h) *h = 1; return ESP_OK; }
inline esp_err_t nvs_set_str(nvs_handle_t, const char*, const char*) { return ESP_OK; }
inline esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }
inline void nvs_close(nvs_handle_t) {}
