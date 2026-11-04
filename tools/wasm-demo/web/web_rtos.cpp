// Blipscope web demo: FreeRTOS tasks as cooperative Emscripten fibers (see shim/freertos/FreeRTOS.h).
//
// The firmware's RunFetchTask / RunEnrichTask loops run UNMODIFIED: each is a fiber with its own C and
// asyncify stacks. A blocking call inside a task (xQueueReceive on an empty queue, vTaskDelay) swaps back
// to the frame loop; once per frame rtos_run_ready_tasks() resumes every task whose wait is over, and
// each runs until it blocks again. Calls from the frame loop itself never block (a wait of 0 is honoured
// as written; a longer wait from the loop returns at once -- the loop never asks for one).
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <emscripten.h>
#include <emscripten/fiber.h>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

struct rtos_queue {
    UBaseType_t length, itemSize;
    std::deque<std::vector<uint8_t>> items;
};

struct rtos_task {
    std::string name;
    TaskFunction_t fn;
    void* arg;
    emscripten_fiber_t fiber;
    void* cStack;
    void* aStack;
    bool started = false, finished = false;
    rtos_queue* waitQueue = nullptr;     // blocked receiving from this queue
    double wakeAtMs = 0;                 // or sleeping until this time
};

static emscripten_fiber_t g_main;
static bool g_mainInit = false;
static char g_mainAsyncStack[64 * 1024];
static std::vector<rtos_task*> g_tasks;
static rtos_task* g_current = nullptr;     // null = the frame loop

static constexpr size_t TASK_C_STACK = 512 * 1024;
static constexpr size_t TASK_ASYNC_STACK = 128 * 1024;

TickType_t xTaskGetTickCount() { return (TickType_t)emscripten_get_now(); }

static void taskEntry(void* p)
{
    rtos_task* t = static_cast<rtos_task*>(p);
    t->fn(t->arg);
    t->finished = true;                       // a task returning is a bug on FreeRTOS too; just park it
    for (;;) emscripten_fiber_swap(&t->fiber, &g_main);
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t, void* arg, UBaseType_t,
                                   TaskHandle_t* handle, BaseType_t)
{
    if (!g_mainInit) {
        emscripten_fiber_init_from_current_context(&g_main, g_mainAsyncStack, sizeof g_mainAsyncStack);
        g_mainInit = true;
    }
    rtos_task* t = new rtos_task();
    t->name = name ? name : "task";
    t->fn = fn;
    t->arg = arg;
    t->cStack = std::malloc(TASK_C_STACK);
    t->aStack = std::malloc(TASK_ASYNC_STACK);
    emscripten_fiber_init(&t->fiber, taskEntry, t, t->cStack, TASK_C_STACK, t->aStack, TASK_ASYNC_STACK);
    g_tasks.push_back(t);
    if (handle) *handle = t;
    return pdPASS;
}

static void yieldToLoop()
{
    rtos_task* t = g_current;
    if (!t) return;                           // the frame loop never yields to itself
    emscripten_fiber_swap(&t->fiber, &g_main);
}

void vTaskDelay(TickType_t ticks)
{
    if (!g_current) return;
    g_current->wakeAtMs = emscripten_get_now() + ticks;
    yieldToLoop();
}
void vTaskDelete(TaskHandle_t t)
{
    if (!t) t = g_current;
    if (t) t->finished = true;
    if (t && t == g_current) yieldToLoop();
}
TaskHandle_t xTaskGetCurrentTaskHandle() { return g_current; }

void rtos_run_ready_tasks()
{
    // Run each ready task until it blocks; a task that unblocks another (a send) lets the second run in
    // the same pass. Bounded so a task that never blocks cannot hang the page.
    for (int pass = 0; pass < 8; pass++) {
        bool ran = false;
        for (rtos_task* t : g_tasks) {
            if (t->finished) continue;
            const double now = emscripten_get_now();
            const bool ready = !t->started || (t->waitQueue ? !t->waitQueue->items.empty() : now >= t->wakeAtMs);
            if (!ready) continue;
            t->started = true;
            g_current = t;
            emscripten_fiber_swap(&g_main, &t->fiber);
            g_current = nullptr;
            ran = true;
        }
        if (!ran) break;
    }
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize)
{
    rtos_queue* q = new rtos_queue();
    q->length = length;
    q->itemSize = itemSize;
    return q;
}
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t)
{
    if (!q || q->items.size() >= q->length) return pdFALSE;   // a full queue: never blocks in the demo
    const uint8_t* p = static_cast<const uint8_t*>(item);
    q->items.emplace_back(p, p + q->itemSize);
    return pdTRUE;
}
BaseType_t xQueueOverwrite(QueueHandle_t q, const void* item)
{
    if (!q) return pdFALSE;
    q->items.clear();
    return xQueueSend(q, item, 0);
}
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait)
{
    if (!q) return pdFALSE;
    const double deadline = emscripten_get_now() + (wait == portMAX_DELAY ? 1e18 : (double)wait);
    while (q->items.empty()) {
        if (wait == 0 || !g_current) return pdFALSE;
        g_current->waitQueue = q;
        g_current->wakeAtMs = deadline;
        yieldToLoop();
        g_current->waitQueue = nullptr;
        if (q->items.empty() && emscripten_get_now() >= deadline) return pdFALSE;
    }
    std::memcpy(item, q->items.front().data(), q->itemSize);
    q->items.pop_front();
    return pdTRUE;
}
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t)
{
    if (!q || q->items.empty()) return pdFALSE;
    std::memcpy(item, q->items.front().data(), q->itemSize);
    return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) { return q ? (UBaseType_t)q->items.size() : 0; }
void vQueueDelete(QueueHandle_t q) { delete q; }
BaseType_t xQueueReset(QueueHandle_t q) { if (q) q->items.clear(); return pdPASS; }
