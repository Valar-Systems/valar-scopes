#pragma once

/* ============================================================================
 * FreeRTOS, as much as EamFeedClient uses: two depth-1 queues and one task.
 *
 * THE POINT: the test drives the shipping EamFeedClient through its PUBLIC
 * surface -- Begin(), Configure(), Poll(), Latest() -- and the shipping worker
 * body. xTaskCreatePinnedToCore records the task instead of starting it; the test
 * then runs that task function itself, on demand, for exactly one request. A
 * request queue with nothing in it ends the run (hostrtos::WorkerIdle is thrown
 * out of RunWorker's for(;;)), which is how one worker iteration is bounded
 * without editing the loop. Implemented in test_eam_long_message.cpp.
 * ==========================================================================*/

#include <stddef.h>
#include <stdint.h>

typedef void* QueueHandle_t;
typedef void* TaskHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void*);

#define pdTRUE  1
#define pdFALSE 0
#define portMAX_DELAY ((TickType_t)0xffffffffUL)

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stack, void* arg,
                                   UBaseType_t prio, TaskHandle_t* handle, BaseType_t core);
