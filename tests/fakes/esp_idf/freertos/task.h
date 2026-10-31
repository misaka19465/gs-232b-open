#ifndef TEST_FREERTOS_TASK_H
#define TEST_FREERTOS_TASK_H
#include "freertos/FreeRTOS.h"
typedef void (*TaskFunction_t)(void *);
typedef struct fake_task *TaskHandle_t;
BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack_size,
    void *ctx, UBaseType_t priority, TaskHandle_t *task);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
#endif
