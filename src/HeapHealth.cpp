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

// ---- fix 1: throttle state ------------------------------------------------
//
// Plain volatiles rather than a mutex. A torn read here costs at most one extra
// trial or one skipped one, which is the same order as the thing being
// throttled -- a lock would cost more than the error it prevents.
volatile uint32_t gNextTrialMs = 0;
volatile bool     gLastVerdict = true;

// ---- fix 4: the reserved handshake block ----------------------------------
//
// Taken on the loop task, released on a network task, so the exchange has to be
// atomic: two tasks must never both believe they own the pointer. A portMUX
// critical section is used rather than std::atomic<void*> because the release
// path also has to free, and doing the test and the free as one indivisible step
// is what stops a double free.
portMUX_TYPE gBallastMux = portMUX_INITIALIZER_UNLOCKED;
void*        gBallast = nullptr;
uint32_t     gBallastReacquireFailures = 0;

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

// ---------------------------------------------------------------------------
// Fix 1 -- the throttled gate.
// ---------------------------------------------------------------------------
bool heaphealth::CanHandshakeThrottled()
{
    const uint32_t now = (uint32_t)millis();

    // Inside the backoff window: answer from the cached verdict and do not touch
    // the allocator. Only a NEGATIVE verdict is ever cached -- see below -- so
    // this branch can only return false, and the gate can never be more
    // permissive than the untrottled one.
    if (gNextTrialMs != 0 && (int32_t)(now - gNextTrialMs) < 0)
        return gLastVerdict;

    const bool ok = CanHandshake();
    gLastVerdict = ok;
    // Arm the backoff ONLY on a refusal. A success must not start a window: the
    // next caller through is about to consume the block this call just proved
    // was there, so the following request has to ask the allocator again rather
    // than be told "yes" on the strength of someone else's answer.
    gNextTrialMs = ok ? 0 : (now + RETRY_BACKOFF_MS);
    return ok;
}

// ---------------------------------------------------------------------------
// Fix 4 -- the reserved handshake block.
// ---------------------------------------------------------------------------
void heaphealth::ReserveHandshakeBallast()
{
    // Allocate OUTSIDE the critical section: heap_caps_malloc can take the heap's
    // own lock, and taking two locks in one order here and the other order
    // anywhere else is how this would deadlock at 3am on a customer's desk.
    taskENTER_CRITICAL(&gBallastMux);
    const bool alreadyHeld = gBallast != nullptr;
    taskEXIT_CRITICAL(&gBallastMux);
    if (alreadyHeld)
        return;

    void* p = heap_caps_malloc(TLS_HANDSHAKE_BYTES, kTlsCaps);
    if (p == nullptr) {
        ++gBallastReacquireFailures;
        return; // normal while a TLS session is live; a later attempt will get it
    }

    // Re-check under the lock: another task may have claimed it in the window
    // above, and two live pointers means one gets leaked.
    bool keep = false;
    taskENTER_CRITICAL(&gBallastMux);
    if (gBallast == nullptr) { gBallast = p; keep = true; }
    taskEXIT_CRITICAL(&gBallastMux);
    if (!keep)
        heap_caps_free(p);
}

bool heaphealth::BallastHeld()
{
    taskENTER_CRITICAL(&gBallastMux);
    const bool held = gBallast != nullptr;
    taskEXIT_CRITICAL(&gBallastMux);
    return held;
}

void heaphealth::ReleaseBallastForHandshake()
{
    // Claim the pointer under the lock and free it outside, for the same
    // lock-ordering reason as above. Whoever wins the exchange owns the free, so
    // a concurrent second caller sees nullptr and does nothing.
    taskENTER_CRITICAL(&gBallastMux);
    void* p = gBallast;
    gBallast = nullptr;
    taskEXIT_CRITICAL(&gBallastMux);
    if (p != nullptr)
        heap_caps_free(p);
}

uint32_t heaphealth::BallastReacquireFailures() { return gBallastReacquireFailures; }
