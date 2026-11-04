#pragma once
#include "FreeRTOS.h"

// One thread, cooperative tasks that never yield while holding a lock: every take succeeds.
inline SemaphoreHandle_t xSemaphoreCreateMutex() { return reinterpret_cast<SemaphoreHandle_t>(1); }
inline SemaphoreHandle_t xSemaphoreCreateBinary() { return reinterpret_cast<SemaphoreHandle_t>(1); }
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return reinterpret_cast<SemaphoreHandle_t>(1); }
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return pdTRUE; }
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, TickType_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { return pdTRUE; }
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
