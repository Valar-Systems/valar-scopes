#include "HeapHealth.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace {

/**
 * The caps mask the gate must ask with is the caps mask the ALLOCATOR uses --
 * 0x804 = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, read straight off the failing
 * allocation in the soak log. Asking with anything else is how the old gate got
 * into trouble in the first place, so this is not a value to tidy.
 *
 * (For the record: the probe showed INTERNAL and INTERNAL|8BIT are IDENTICAL at
 * every step on this chip, so the mask is not what was broken. It is pinned here
 * anyway, because "the two happen to agree today" is not a property to rely on
 * across a chip revision.)
 */
constexpr uint32_t kTlsCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;

volatile TaskHandle_t gTrialTask = nullptr;
volatile uint32_t     gRejections = 0;

} // namespace

bool heaphealth::CanAllocate(size_t bytes)
{
    // Publish before the attempt: heap_caps_malloc invokes the failure callback
    // synchronously on THIS task, so InTrial() is true exactly when it needs to be.
    gTrialTask = xTaskGetCurrentTaskHandle();
    void* p = heap_caps_malloc(bytes, kTlsCaps);
    gTrialTask = nullptr;

    if (p == nullptr) {
        ++gRejections;
        return false;
    }
    // Freed immediately and on purpose. The point is the answer, not the memory:
    // holding it would reserve against the very handshake the caller is about to
    // start, and the caller allocates its own.
    heap_caps_free(p);
    return true;
}

bool heaphealth::InTrial()
{
    const TaskHandle_t t = gTrialTask;
    return t != nullptr && t == xTaskGetCurrentTaskHandle();
}

uint32_t heaphealth::TrialRejectionCount() { return gRejections; }

uint32_t heaphealth::FreeInternal8Bit()
{
    return (uint32_t)heap_caps_get_free_size(kTlsCaps);
}
