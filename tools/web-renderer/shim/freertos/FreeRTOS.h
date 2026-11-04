#pragma once

// Blipscope web renderer: FreeRTOS as COOPERATIVE FIBERS (web/web_rtos.cpp). The page has one thread.
// A task created with xTaskCreate* becomes an Emscripten fiber; a call that would block (a queue
// receive on an empty queue, vTaskDelay) swaps back to the frame loop, which resumes every task
// whose wait is over once per frame. The firmware's task bodies run unmodified.
//
// What this cannot reproduce, stated: preemption and a second core. A task runs only between
// frames and only until it next blocks, so timing between the loop and the fetch task is not the
// device's; ordering within a task is.

#include <cstdint>
#include <cstddef>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void*);
typedef struct rtos_task* TaskHandle_t;
typedef struct rtos_queue* QueueHandle_t;
typedef struct rtos_queue* SemaphoreHandle_t;
typedef int portMUX_TYPE;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0
#define portMAX_DELAY ((TickType_t)0xffffffffUL)
#define portTICK_PERIOD_MS 1
#define configTICK_RATE_HZ 1000
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m) ((void)(m))
#define portEXIT_CRITICAL(m) ((void)(m))
#define portENTER_CRITICAL_ISR(m) ((void)(m))
#define portEXIT_CRITICAL_ISR(m) ((void)(m))
#define taskENTER_CRITICAL(m) ((void)(m))
#define taskEXIT_CRITICAL(m) ((void)(m))
#define tskNO_AFFINITY 0x7fffffff
#define configMAX_PRIORITIES 25

TickType_t xTaskGetTickCount();
inline BaseType_t xPortGetCoreID() { return 1; }

// The scheduler's half (web/web_rtos.cpp): run every task whose wait is over, until each blocks.
void rtos_run_ready_tasks();
