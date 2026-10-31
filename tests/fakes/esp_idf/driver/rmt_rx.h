#ifndef TEST_DRIVER_RMT_RX_H
#define TEST_DRIVER_RMT_RX_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct fake_rmt_channel *rmt_channel_handle_t;
typedef union {
    struct {
        uint32_t duration0: 15;
        uint32_t level0: 1;
        uint32_t duration1: 15;
        uint32_t level1: 1;
    };
    uint32_t val;
} rmt_symbol_word_t;
enum { RMT_CLK_SRC_APB = 1 };
typedef struct {
    int gpio_num, clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    int intr_priority;
    struct { uint32_t invert_in: 1, with_dma: 1, allow_pd: 1; } flags;
} rmt_rx_channel_config_t;
typedef struct {
    uint32_t signal_range_min_ns, signal_range_max_ns;
    struct { uint32_t en_partial_rx: 1; } flags;
} rmt_receive_config_t;
typedef struct {
    rmt_symbol_word_t *received_symbols;
    size_t num_symbols;
    struct { uint32_t is_last: 1; } flags;
} rmt_rx_done_event_data_t;
typedef bool (*rmt_rx_done_callback_t)(rmt_channel_handle_t,
    const rmt_rx_done_event_data_t *, void *);
typedef struct { rmt_rx_done_callback_t on_recv_done; } rmt_rx_event_callbacks_t;
esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *, rmt_channel_handle_t *);
esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t,
    const rmt_rx_event_callbacks_t *, void *);
esp_err_t rmt_enable(rmt_channel_handle_t);
esp_err_t rmt_receive(rmt_channel_handle_t, void *, size_t, const rmt_receive_config_t *);
esp_err_t rmt_disable(rmt_channel_handle_t);
esp_err_t rmt_del_channel(rmt_channel_handle_t);
#endif
