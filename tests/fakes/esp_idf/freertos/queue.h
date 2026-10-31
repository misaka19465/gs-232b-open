#ifndef TEST_FREERTOS_QUEUE_H
#define TEST_FREERTOS_QUEUE_H

#include "freertos/FreeRTOS.h"

typedef struct fake_queue *QueueHandle_t;
QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size);
BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item);
BaseType_t xQueuePeek(QueueHandle_t queue, void *item, TickType_t ticks_to_wait);
void vQueueDelete(QueueHandle_t queue);

#endif
