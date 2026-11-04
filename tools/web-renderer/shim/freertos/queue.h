#pragma once
#include "FreeRTOS.h"

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t itemSize);
BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t wait);
inline BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t wait) { return xQueueSend(q, item, wait); }
BaseType_t xQueueOverwrite(QueueHandle_t q, const void* item);
BaseType_t xQueueReceive(QueueHandle_t q, void* item, TickType_t wait);
BaseType_t xQueuePeek(QueueHandle_t q, void* item, TickType_t wait);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q);
void vQueueDelete(QueueHandle_t q);
BaseType_t xQueueReset(QueueHandle_t q);
