#include "uart_api.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct fake_queue {
    pthread_mutex_t lock;
    size_t length, item_size, head, count;
    unsigned char bytes[];
};
struct fake_semaphore { pthread_mutex_t lock; bool binary, ready; };
struct fake_task { TaskFunction_t entry; void *ctx; };
static struct fake_task *scheduled;

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    if (!fake_uart_allocation()) { return NULL; }
    struct fake_queue *queue = calloc(1, sizeof(*queue) + length * item_size);
    assert(queue != NULL && pthread_mutex_init(&queue->lock, NULL) == 0);
    queue->length = length; queue->item_size = item_size;
    ++fake_uart.live_queues;
    return queue;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    assert(ticks == 0 && pthread_mutex_lock(&queue->lock) == 0);
    bool fits = queue->count < queue->length;
    if (fits) {
        memcpy(queue->bytes + ((queue->head + queue->count) % queue->length) * queue->item_size,
            item, queue->item_size);
        ++queue->count;
    }
    assert(pthread_mutex_unlock(&queue->lock) == 0);
    return fits ? pdPASS : pdFAIL;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    assert(ticks == 0 && pthread_mutex_lock(&queue->lock) == 0);
    bool present = queue->count != 0;
    if (present) {
        memcpy(item, queue->bytes + queue->head * queue->item_size, queue->item_size);
        queue->head = (queue->head + 1) % queue->length;
        --queue->count;
    }
    assert(pthread_mutex_unlock(&queue->lock) == 0);
    return present ? pdPASS : pdFAIL;
}

BaseType_t xQueueReset(QueueHandle_t queue)
{
    assert(pthread_mutex_lock(&queue->lock) == 0);
    queue->head = queue->count = 0;
    assert(pthread_mutex_unlock(&queue->lock) == 0);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue)
{
    assert(pthread_mutex_lock(&queue->lock) == 0);
    UBaseType_t count = (UBaseType_t)queue->count;
    assert(pthread_mutex_unlock(&queue->lock) == 0);
    return count;
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t queue)
{
    return (UBaseType_t)queue->length - uxQueueMessagesWaiting(queue);
}

void vQueueDelete(QueueHandle_t queue)
{
    assert(pthread_mutex_destroy(&queue->lock) == 0);
    free(queue);
    --fake_uart.live_queues;
}

static SemaphoreHandle_t create_semaphore(bool binary)
{
    if (!fake_uart_allocation()) { return NULL; }
    SemaphoreHandle_t semaphore = calloc(1, sizeof(*semaphore));
    assert(semaphore != NULL && pthread_mutex_init(&semaphore->lock, NULL) == 0);
    semaphore->binary = binary;
    ++fake_uart.live_semaphores;
    return semaphore;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return create_semaphore(false); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return create_semaphore(true); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t semaphore, TickType_t ticks)
{
    if (!semaphore->binary) {
        int result = ticks == 0 ? pthread_mutex_trylock(&semaphore->lock) :
                                 pthread_mutex_lock(&semaphore->lock);
        return result == 0 ? pdPASS : pdFAIL;
    }
    fake_uart.ticks_waited += ticks;
    /* NOTE: 确定性任务调度替身只在退出等待时运行任务；正常 RX 由测试逐步推进。 */
    if (!semaphore->ready && scheduled != NULL && !fake_uart.defer_exit) {
        scheduled->entry(scheduled->ctx);
    }
    bool ready = semaphore->ready;
    semaphore->ready = false;
    return ready ? pdPASS : pdFAIL;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t semaphore)
{
    if (semaphore->binary) { semaphore->ready = true; }
    else { assert(pthread_mutex_unlock(&semaphore->lock) == 0); }
    return pdPASS;
}

void vSemaphoreDelete(SemaphoreHandle_t semaphore)
{
    assert(pthread_mutex_destroy(&semaphore->lock) == 0);
    free(semaphore);
    --fake_uart.live_semaphores;
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack_size,
    void *ctx, UBaseType_t priority, TaskHandle_t *task)
{
    assert(strcmp(name, "gs_uart_rx") == 0 && scheduled == NULL);
    if (fake_uart.fail_task) { return pdFAIL; }
    scheduled = calloc(1, sizeof(*scheduled));
    assert(scheduled != NULL);
    scheduled->entry = entry; scheduled->ctx = ctx;
    fake_uart.task_entry = entry; fake_uart.task_ctx = ctx;
    fake_uart.task_priority = priority; fake_uart.task_stack = stack_size;
    *task = scheduled;
    ++fake_uart.live_tasks;
    return pdPASS;
}

void vTaskDelay(TickType_t ticks) { assert(ticks == 1); }

void vTaskDelete(TaskHandle_t task)
{
    assert(task == NULL && scheduled != NULL);
    free(scheduled); scheduled = NULL;
    --fake_uart.live_tasks;
}
