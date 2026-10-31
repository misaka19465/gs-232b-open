#ifndef TEST_FREERTOS_QUEUE_H
#define TEST_FREERTOS_QUEUE_H

#include "freertos/FreeRTOS.h"

typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticks_to_wait);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReset(QueueHandle_t queue);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);

#endif
