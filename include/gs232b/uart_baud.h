#ifndef GS232B_UART_BAUD_H
#define GS232B_UART_BAUD_H

#include "gs232b/persistence.h"

#define GS232B_UART_DEFAULT_BAUD 9600U
#define GS232B_UART_BAUD_WINDOW_US UINT64_C(15000000)
#define GS232B_UART_BAUD_REQUIRED_CRS 3U

/* NOTE: 仅供静态分配；输入为真实捕获的连续电平宽度，单位为微秒。 */
typedef struct {
    uint32_t baud;
    uint32_t candidate;
    uint32_t previous;
    unsigned phase;
    unsigned complete_crs;
} gs232b_uart_baud_detector_t;

/** @brief 判断是否为协议支持的 1200/2400/4800/9600 bit/s。 */
bool gs232b_uart_baud_valid(uint32_t baud);
/** @brief 清除候选、完整字符计数及确认结果；有缺口/截断的捕获必须调用。 */
void gs232b_uart_baud_reset(gs232b_uart_baud_detector_t *detector);
/**
 * @brief 输入一个电平持续区间；三个同速完整 CR 后锁存 baud，误差容限为 ±8%。
 * @note 只接纳 8N1 的 0x0D 波形，停止高电平至少一个位宽；不推测缺失区间。
 *       同一捕获内区间必须连续，不能把丢失的边沿拼接成字符。
 */
void gs232b_uart_baud_pulse(gs232b_uart_baud_detector_t *detector,
                           bool high, uint32_t duration_us);
/** @brief 捕获边界丢弃半字符及其连续计数，保留此前以完整停止位结束的 CR 计数。 */
void gs232b_uart_baud_end_capture(gs232b_uart_baud_detector_t *detector);
/** @brief 加载 UART 独立版本化记录；错误时输出工厂速率及 revision=0。 */
gs232b_error_t gs232b_uart_baud_load(const gs232b_store_t *store,
                                   uint32_t *baud, uint64_t *revision);
/** @brief 原子保存已确认速率；沿用记录 CRC、revision 比较及双槽提交。 */
gs232b_error_t gs232b_uart_baud_commit(const gs232b_store_t *store,
    uint64_t expected_revision, uint32_t baud, uint64_t *new_revision);

#endif
