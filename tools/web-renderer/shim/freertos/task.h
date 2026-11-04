#pragma once
#include "FreeRTOS.h"

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* arg,
                                   UBaseType_t priority, TaskHandle_t* handle, BaseType_t core);
inline BaseType_t xTaskCreate(TaskFunction_t fn, const char* name, uint32_t stackDepth, void* arg,
                              UBaseType_t priority, TaskHandle_t* handle)
{
    return xTaskCreatePinnedToCore(fn, name, stackDepth, arg, priority, handle, tskNO_AFFINITY);
}
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t t);
inline void taskYIELD() { vTaskDelay(0); }
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 4096; }
TaskHandle_t xTaskGetCurrentTaskHandle();
inline void vTaskSuspend(TaskHandle_t) {}
inline void vTaskResume(TaskHandle_t) {}
inline const char* pcTaskGetName(TaskHandle_t) { return "task"; }
