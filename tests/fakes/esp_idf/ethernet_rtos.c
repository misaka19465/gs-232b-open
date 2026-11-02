#include "ethernet_api.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct fake_queue {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    size_t size, length, head, count;
    unsigned char bytes[];
};
struct fake_semaphore {
    pthread_mutex_t lock;
    pthread_cond_t ready;
    bool available;
};
struct fake_task { TaskFunction_t entry; void *ctx; };

static bool wait_ready(pthread_cond_t *cond, pthread_mutex_t *lock, TickType_t ticks)
{
    if (ticks == 0) { return false; }
    if (ticks == portMAX_DELAY) { return pthread_cond_wait(cond, lock) == 0; }
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += ticks / 1000;
    deadline.tv_nsec += (long)(ticks % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
    return pthread_cond_timedwait(cond, lock, &deadline) == 0;
}

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    assert(length != 0);
    if (!fake_eth_allocation()) { return NULL; }
    QueueHandle_t queue = calloc(1, sizeof(*queue) + length * item_size);
    assert(queue != NULL && pthread_mutex_init(&queue->lock, NULL) == 0 &&
           pthread_cond_init(&queue->ready, NULL) == 0);
    queue->size = item_size;
    queue->length = length;
    fake_eth_resource(1, 0, 0);
    return queue;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks)
{
    assert(pthread_mutex_lock(&queue->lock) == 0);
    while (queue->count == queue->length) {
        if (!wait_ready(&queue->ready, &queue->lock, ticks)) {
            assert(pthread_mutex_unlock(&queue->lock) == 0);
            return pdFAIL;
        }
    }
    memcpy(queue->bytes + ((queue->head + queue->count) % queue->length) * queue->size, item, queue->size);
    ++queue->count;
    assert(pthread_cond_broadcast(&queue->ready) == 0);
    assert(pthread_mutex_unlock(&queue->lock) == 0);
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t ticks)
{
    assert(pthread_mutex_lock(&queue->lock) == 0);
    while (queue->count == 0) {
        if (!wait_ready(&queue->ready, &queue->lock, ticks)) {
            assert(pthread_mutex_unlock(&queue->lock) == 0);
            return pdFAIL;
        }
    }
    memcpy(item, queue->bytes + queue->head * queue->size, queue->size);
    queue->head = (queue->head + 1U) % queue->length;
    --queue->count;
    assert(pthread_cond_broadcast(&queue->ready) == 0);
    assert(pthread_mutex_unlock(&queue->lock) == 0);
    return pdPASS;
}

BaseType_t xQueueReset(QueueHandle_t queue)
{
    assert(pthread_mutex_lock(&queue->lock) == 0);
    queue->head = queue->count = 0;
    assert(pthread_cond_broadcast(&queue->ready) == 0);
    assert(pthread_mutex_unlock(&queue->lock) == 0);
    return pdPASS;
}

BaseType_t xQueueOverwrite(QueueHandle_t queue, const void *item)
{
    assert(queue->length == 1 && pthread_mutex_lock(&queue->lock) == 0);
    memcpy(queue->bytes, item, queue->size);
    queue->head = 0;
    queue->count = 1;
    assert(pthread_cond_broadcast(&queue->ready) == 0);
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

void vQueueDelete(QueueHandle_t queue)
{
    assert(pthread_mutex_destroy(&queue->lock) == 0);
    assert(pthread_cond_destroy(&queue->ready) == 0);
    free(queue);
    fake_eth_resource(-1, 0, 0);
}

static SemaphoreHandle_t semaphore(bool available)
{
    if (!fake_eth_allocation()) { return NULL; }
    SemaphoreHandle_t sem = calloc(1, sizeof(*sem));
    assert(sem != NULL && pthread_mutex_init(&sem->lock, NULL) == 0 &&
           pthread_cond_init(&sem->ready, NULL) == 0);
    sem->available = available;
    fake_eth_resource(0, 1, 0);
    return sem;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return semaphore(true); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return semaphore(false); }

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks)
{
    assert(pthread_mutex_lock(&sem->lock) == 0);
    while (!sem->available) {
        if (!wait_ready(&sem->ready, &sem->lock, ticks)) {
            assert(pthread_mutex_unlock(&sem->lock) == 0);
            return pdFAIL;
        }
    }
    sem->available = false;
    assert(pthread_mutex_unlock(&sem->lock) == 0);
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    assert(pthread_mutex_lock(&sem->lock) == 0);
    sem->available = true;
    assert(pthread_cond_broadcast(&sem->ready) == 0);
    assert(pthread_mutex_unlock(&sem->lock) == 0);
    return pdPASS;
}

void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    assert(pthread_mutex_destroy(&sem->lock) == 0);
    assert(pthread_cond_destroy(&sem->ready) == 0);
    free(sem);
    fake_eth_resource(0, -1, 0);
}

static void *task_main(void *ctx)
{
    struct fake_task *task = ctx;
    task->entry(task->ctx);
    free(task);
    fake_eth_resource(0, 0, -1);
    return NULL;
}

BaseType_t xTaskCreate(TaskFunction_t entry, const char *name, uint32_t stack_size,
    void *ctx, UBaseType_t priority, TaskHandle_t *task)
{
    assert(stack_size >= 4096 &&
        ((strcmp(name, "gs_store") == 0 && priority == 2) ||
         ((strcmp(name, "gs_eth") == 0 || strcmp(name, "gs_sta") == 0) && priority == 3)));
    if (fake_eth_status().fail_task) { return pdFAIL; }
    *task = malloc(sizeof(**task));
    assert(*task != NULL);
    **task = (struct fake_task){entry, ctx};
    fake_eth_resource(0, 0, 1);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, task_main, *task) == 0);
    assert(pthread_detach(thread) == 0);
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task) { assert(task == NULL); }
