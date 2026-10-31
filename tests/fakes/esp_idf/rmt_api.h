#ifndef TEST_RMT_API_H
#define TEST_RMT_API_H
#include "driver/rmt_rx.h"
typedef struct {
    rmt_rx_channel_config_t config;
    rmt_receive_config_t receive_config;
    rmt_rx_done_callback_t callback;
    void *ctx;
    rmt_symbol_word_t *buffer;
    size_t buffer_size;
    bool allocated, enabled, receiving, fail_disable, fail_delete, fail_receive;
    bool wrong_buffer;
    bool rx_high;
    int init_calls, fail_init_at;
    unsigned receives, completions;
} fake_rmt_t;
extern fake_rmt_t fake_rmt;
void fake_rmt_reset(void);
void fake_rmt_complete(const rmt_symbol_word_t *symbols, size_t count, bool last);
#endif
