#include "TlsAllocator.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#ifdef BLIPSCOPE_TLS_PSRAM
#include <mbedtls/platform.h>
#endif

namespace {

volatile uint32_t gPsram = 0;
volatile uint32_t gInternal = 0;
volatile uint32_t gFallback = 0;

#ifdef BLIPSCOPE_TLS_PSRAM

constexpr uint32_t kInternalCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
constexpr uint32_t kPsramCaps    = MALLOC_CAP_SPIRAM   | MALLOC_CAP_8BIT;

/**
 * mbedTLS calls this for every allocation. It must behave exactly like calloc,
 * including zeroing -- heap_caps_calloc does, which is why it is used rather
 * than malloc plus a memset we could get wrong.
 */
void* TlsCalloc(size_t n, size_t size)
{
    // Overflow check before the multiply is used for a size. mbedTLS is not
    // hostile input here, but a calloc that silently under-allocates is the
    // worst possible bug to introduce while fixing a memory bug.
    if (n != 0 && size > (SIZE_MAX / n))
        return nullptr;

    const size_t bytes = n * size;

    if (bytes >= tlsalloc::TLS_PSRAM_THRESHOLD) {
        void* p = heap_caps_calloc(n, size, kPsramCaps);
        if (p != nullptr) {
            ++gPsram;
            return p;
        }
        // PSRAM refused. Fall back to internal rather than failing the
        // handshake: internal is where this allocation lived before the fix, so
        // the fallback is exactly the old behaviour and can be no worse. Counted
        // separately -- a non-zero fallback count means PSRAM is not doing the
        // job and the [health] line should not read as a success.
        ++gFallback;
        void* q = heap_caps_calloc(n, size, kInternalCaps);
        if (q != nullptr)
            ++gInternal;
        return q;
    }

    void* p = heap_caps_calloc(n, size, kInternalCaps);
    if (p != nullptr)
        ++gInternal;
    return p;
}

/// One free for both regions: heap_caps_free resolves the region from the
/// pointer, so there is no bookkeeping to get out of step.
void TlsFree(void* p)
{
    if (p != nullptr)
        heap_caps_free(p);
}

#endif // BLIPSCOPE_TLS_PSRAM

} // namespace

void tlsalloc::Install()
{
#ifdef BLIPSCOPE_TLS_PSRAM
    const int rc = mbedtls_platform_set_calloc_free(TlsCalloc, TlsFree);
    Serial.printf("[tls-alloc] PSRAM routing INSTALLED rc=%d threshold=%u B "
                  "(psram_free=%u internal_largest=%u)\n",
                  rc, (unsigned)TLS_PSRAM_THRESHOLD,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
#else
    Serial.println("[tls-alloc] PSRAM routing NOT built in -- mbedTLS allocates "
                   "from the internal heap (see issue #245)");
#endif
}

uint32_t tlsalloc::PsramAllocs()    { return gPsram; }
uint32_t tlsalloc::InternalAllocs() { return gInternal; }
uint32_t tlsalloc::PsramFallbacks() { return gFallback; }
