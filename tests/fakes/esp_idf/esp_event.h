#ifndef TEST_ESP_EVENT_H
#define TEST_ESP_EVENT_H

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef const char *esp_event_base_t;
typedef struct fake_event_handler *esp_event_handler_instance_t;
typedef void (*esp_event_handler_t)(void *, esp_event_base_t, int32_t, void *);
#define ESP_EVENT_DECLARE_BASE(name) extern esp_event_base_t const name
#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t const name = #name
#define ESP_EVENT_ANY_ID (-1)

esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    esp_event_handler_t callback, void *ctx, esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id,
    esp_event_handler_instance_t instance);
esp_err_t esp_event_post(esp_event_base_t base, int32_t id, const void *data,
    size_t size, TickType_t timeout);

#endif
