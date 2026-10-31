#ifndef GS232B_ESP_UART_H
#define GS232B_ESP_UART_H

#include "gs232b/gs_dispatcher.h"

#ifdef ESP_PLATFORM
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define GS232B_UART_RX_RING_BYTES (32U * 1024U)
#define GS232B_UART_TX_BYTES 4096U
#define GS232B_UART_RX_PACKETS 8U
#define GS232B_UART_EVENTS 32U
#define GS232B_UART_DEFAULT_BAUD 9600U

/* NOTE: 私有布局仅供静态分配；活动实例不可复制。RX 任务只搬运和登记故障，
 * dispatcher、TX FIFO 和全部生命周期入口由同一控制任务串行调用。 */
typedef struct {
    gs232b_command_context_t context;
    size_t size;
    uint8_t bytes[GS232B_CONTROL_FEED_MAX_BYTES];
} gs232b_uart_packet_t;

typedef struct {
    QueueHandle_t events;
    QueueHandle_t packets;
    SemaphoreHandle_t rx_lock;
    SemaphoreHandle_t stopped;
    TaskHandle_t rx_task;
    gs232b_dispatcher_t *dispatcher;
    gs232b_session_key_t session;
    gs232b_command_context_t rx_context;
    uint8_t tx[GS232B_UART_TX_BYTES];
    size_t tx_head;
    size_t tx_count;
    atomic_bool enabled;
    atomic_bool stopping;
    bool loss_pending;
    bool loss_reported;
    bool opening;
    bool driver_installed;
    bool opened;
} gs232b_esp_uart_t;

/**
 * @brief 独占 UART1，TX=GPIO6、RX=GPIO7，8N1、无 CTS/RTS，启动专用 RX 任务。
 * @param adapter 清零的静态实例，生命周期覆盖任务及借用端口。
 * @param baud 已确认的 1200/2400/4800/9600；0 使用工厂回退 9600。
 * @note RX driver ring 为 32768 字节，另有八个 256 字节盖章数据包；TX 为
 *       4096 字节 FIFO，驱动无 TX ring。没有自动波特率匹配或 USB2 拔线检测。
 *       失败时释放已取得资源，释放失败须先重试 close；全局只允许一个实例。
 */
gs232b_error_t gs232b_esp_uart_open(gs232b_esp_uart_t *adapter, uint32_t baud);
/** @brief 借用只发送/关闭端口；仅 attach 建立的完整会话键可发送，原子有界复制。 */
gs232b_session_io_t gs232b_esp_uart_io(gs232b_esp_uart_t *adapter);
/**
 * @brief 为已用上述 io 初始化的 dispatcher 建立 UART 会话并排队一次连接欢迎语。
 * @note 必须在协议就绪后调用；line_storage 借用至会话关闭。该 io 仅路由 UART，
 *       多传输应用须在自己的 io 路由中按完整会话键分派，不可把 USB stdin 接入。
 */
gs232b_error_t gs232b_esp_uart_attach(gs232b_esp_uart_t *adapter,
    gs232b_dispatcher_t *dispatcher, uint8_t *line_storage, size_t capacity);
/**
 * @brief 控制任务每轮 tick 前后调用：优先交付丢字节事件，最多 feed 256 字节、TX 128 字节。
 * @note 零等待；RX 正持锁时返回 WOULD_BLOCK。epoch 改变时丢弃上游未盖新章的
 *       输入；有残留则等待下一个 CR，禁止旧字节重新成为新动作。无输入不推断断线。
 *       协议拒绝码可原样返回，会话仍可用；硬件 TX 失败关闭会话并触发控制者停机。
 */
gs232b_error_t gs232b_esp_uart_service(gs232b_esp_uart_t *adapter);
/**
 * @brief 关闭会话、请求 RX 自行退出，再释放 UART/队列；可重试失败的释放。
 * @note 只能由控制任务调用；最多等待 RX 退出 100ms，不得在 ISR 中调用。
 */
gs232b_error_t gs232b_esp_uart_close(gs232b_esp_uart_t *adapter);
#endif
#endif
