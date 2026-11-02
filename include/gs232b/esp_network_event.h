#ifndef GS232B_ESP_NETWORK_EVENT_H
#define GS232B_ESP_NETWORK_EVENT_H

#ifdef ESP_PLATFORM
#include "esp_event.h"
#include "esp_netif.h"

#define GS232B_NETWORK_EVENT_CAPACITY 16U

/* NOTE: SDK 回调只复制值对象；不借用 SDK payload，不等待锁或调用 netif/控制 API。
 * generation 隔离重建前已排队的事件；队列溢出必须撤销地址和旧控制会话。 */
typedef struct {
    esp_event_base_t base;
    int32_t id;
    unsigned generation;
    uint64_t occurred_at_us;
    void *driver;
    ip_event_got_ip_t ip;
    uint16_t reason;
} gs232b_esp_network_event_t;
#endif
#endif
