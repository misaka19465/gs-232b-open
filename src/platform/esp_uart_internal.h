#ifndef GS232B_ESP_UART_INTERNAL_H
#define GS232B_ESP_UART_INTERNAL_H

#include "gs232b/esp_uart.h"
#include "driver/uart.h"
#include "sdkconfig.h"

#if !CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || !CONFIG_ESP_CONSOLE_SECONDARY_NONE || \
    (defined(CONFIG_ESP_CONSOLE_UART) && CONFIG_ESP_CONSOLE_UART)
#error "Protocol UART requires the native USB Serial/JTAG console only"
#endif

/** @brief 映射 IDF 错误，保留资源/参数诊断。 */
gs232b_error_t gs232b_uart_error(esp_err_t error);
/** @brief 持 RX 锁时合并接收故障并丢弃不再可信的数据；清理失败持续保留故障。 */
void gs232b_uart_loss_locked(gs232b_esp_uart_t *adapter);
/** @brief 持 RX 锁时检查完整有界事件队列，先故障后数据。 */
void gs232b_uart_events_locked(gs232b_esp_uart_t *adapter);
/** @brief 专用 RX 任务的一次有界搬运；测试替身可确定性调用，不能与该任务并发。 */
void gs232b_uart_rx_step(gs232b_esp_uart_t *adapter);
/** @brief 专用 RX 任务入口，只接收字节，不解析协议或驱动运动。 */
void gs232b_uart_rx_task(void *ctx);

#endif
