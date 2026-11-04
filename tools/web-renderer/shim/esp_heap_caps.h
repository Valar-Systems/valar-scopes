#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>

// Blipscope web renderer: every "capability" is the one heap. Sizes answer with fixed, plausible values;
// the demo makes no claim about the device's memory.
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_DEFAULT (1 << 12)
#define MALLOC_CAP_DMA (1 << 3)
typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char* function_name);
inline void* heap_caps_malloc(size_t n, uint32_t) { return std::malloc(n); }
inline void* heap_caps_calloc(size_t n, size_t s, uint32_t) { return std::calloc(n, s); }
inline void* heap_caps_realloc(void* p, size_t n, uint32_t) { return std::realloc(p, n); }
inline void heap_caps_free(void* p) { std::free(p); }
inline size_t heap_caps_get_free_size(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 7000000 : 180000; }
inline size_t heap_caps_get_largest_free_block(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 7000000 : 110000; }
inline size_t heap_caps_get_minimum_free_size(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 6900000 : 150000; }
inline size_t heap_caps_get_total_size(uint32_t caps) { return (caps & MALLOC_CAP_SPIRAM) ? 8388608 : 320000; }
inline int heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t) { return 0; }
inline bool heap_caps_check_integrity_all(bool) { return true; }
