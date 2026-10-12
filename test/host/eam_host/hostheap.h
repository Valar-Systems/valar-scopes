#pragma once

/* ============================================================================
 * A HEAP LEDGER FOR THE E6 LONG-MESSAGE TEST. Force-included (-include) into
 * every translation unit that test compiles: the shipping EamFeedClient.cpp and
 * EamModels.cpp, the REAL arduino-esp32 WString.cpp, and the test itself.
 *
 * WHY A RENAME AND NOT A HOOK. Three allocators are in play on the device and all
 * three end in malloc: ArduinoJson's DefaultAllocator (malloc/realloc/free),
 * WString (realloc/free), and std::vector (operator new -> malloc). Renaming the
 * C calls in these TUs, plus replacing the global operator new (hostheap.cpp),
 * puts every byte the shipping code asks for into one ledger -- on MinGW and on
 * Linux alike, with or without AddressSanitizer, and without touching the code
 * under test. The standard headers are pulled in FIRST, so none of them sees the
 * macros (libstdc++ names std::malloc in places).
 *
 * WHAT IT CANNOT SEE: an allocation made by a TU that does not include this file
 * (none of the code under test), and anything about the DEVICE's allocator. The
 * ledger records what was REQUESTED. DeviceBytes() converts a request into what
 * the ESP32-S3 heap would consume -- derived from the IDF heap source and the
 * Arduino sdkconfig, NOT measured; the bench step (docs/e6-long-message-check.md)
 * is what measures it.
 * ==========================================================================*/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
extern "C" {
#endif

void* hostheap_malloc(size_t n);
void* hostheap_calloc(size_t n, size_t size);
void* hostheap_realloc(void* p, size_t n);
void  hostheap_free(void* p);

#ifdef __cplusplus
}

namespace hostheap {

// ESP32-S3 Arduino (framework-arduinoespressif32-libs/esp32s3/qio_opi sdkconfig.h):
//   CONFIG_SPIRAM_USE_MALLOC 1, CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 4096
// and IDF heap_caps_malloc_default(): size <= 4096 -> internal, else SPIRAM first.
// So a request of 4096 B or less costs INTERNAL heap -- the scarce one, the one a
// TLS handshake needs 16,717 contiguous bytes of (HeapHealth.h).
constexpr size_t kAlwaysInternal = 4096;

// What one request consumes on the device, derived (not measured):
//   CONFIG_HEAP_POISONING_LIGHT 1 -> +8 B head (canary, size) + 4 B tail canary
//     (components/heap/multi_heap_poisoning.c POISON_OVERHEAD);
//   TLSF: the size is aligned up to 4 B, at least block_size_min (12 B), plus a
//     4 B block header (tlsf_block_functions.h block_header_overhead).
inline size_t DeviceBytes(size_t req)
{
    size_t n = (req + 12 + 3) & ~(size_t)3;
    if (n < 12) n = 12;
    return n + 4;
}

struct Totals {
    long reqBytes = 0;      // bytes requested, all live blocks
    long blocks = 0;        // live blocks
    long devInternal = 0;   // DeviceBytes() of live blocks of <= 4096 B
    long devPsram = 0;      // DeviceBytes() of live blocks of > 4096 B
    long biggest = 0;       // largest single request since start (not retired by free)
};

Totals Now();               // what is live
Totals Peak();              // the field-wise high-water mark since the last ResetPeak()
void ResetPeak();           // peak := now
long  Allocations();        // malloc/realloc/new calls since start (a churn count)

} // namespace hostheap
#endif

#define malloc  hostheap_malloc
#define calloc  hostheap_calloc
#define realloc hostheap_realloc
#define free    hostheap_free
