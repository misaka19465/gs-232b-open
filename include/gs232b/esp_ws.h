#ifndef GS232B_ESP_WS_H
#define GS232B_ESP_WS_H

#include "gs232b/gs_dispatcher.h"

#ifdef ESP_PLATFORM
#include <stdatomic.h>
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define GS232B_WS_URI "/ws/gs232b"
#define GS232B_WS_MANAGE_URI "/ws/manage"
#define GS232B_WS_CLIENTS 4U
#define GS232B_WS_MESSAGE_BYTES 16384U
#define GS232B_WS_TX_BYTES 4096U
#define GS232B_WS_CHUNK_BYTES GS232B_CONTROL_FEED_MAX_BYTES
#define GS232B_WS_RX_PACKETS (GS232B_WS_MESSAGE_BYTES / GS232B_WS_CHUNK_BYTES)
#define GS232B_WS_CONTROL_PACKETS 4U
#define GS232B_WS_INTERFACE_NAME_BYTES 6U
#define GS232B_WS_PING_US UINT64_C(5000000)
#define GS232B_WS_PONG_TIMEOUT_US UINT64_C(15000000)
#define GS232B_WS_SEND_TIMEOUT_US UINT64_C(2000000)

typedef struct {
    gs232b_command_context_t context;
    size_t size;
    uint8_t bytes[GS232B_WS_CHUNK_BYTES];
} gs232b_ws_packet_t;

typedef struct {
    httpd_ws_type_t type;
    size_t size;
    uint8_t bytes[125];
} gs232b_ws_control_t;

typedef struct {
    uint32_t address;
    uint64_t epoch;
    char if_name[GS232B_WS_INTERFACE_NAME_BYTES];
} gs232b_ws_interface_t;

struct gs232b_esp_ws;
typedef struct {
    struct gs232b_esp_ws *adapter;
    bool management;
} gs232b_ws_uri_context_t;
/* NOTE: 私有布局仅供静态分配。HTTPD 独占分片、心跳和 wire；控制任务独占
 * session。队列、接口快照和 rx_context 通过 lock 共享，状态标志使用原子操作。
 * CLOSED 经控制任务撤销会话后才回收，避免 fd/槽复用继承旧输入和所有权。 */
typedef struct {
    struct gs232b_esp_ws *adapter;
    int fd;
    gs232b_transport_t transport;
    gs232b_ws_interface_t interface;
    gs232b_session_key_t session;
    gs232b_command_context_t rx_context;
    SemaphoreHandle_t ready;
    gs232b_ws_packet_t rx[GS232B_WS_RX_PACKETS];
    size_t rx_head, rx_count;
    uint8_t tx[GS232B_WS_TX_BYTES];
    size_t tx_head, tx_count;
    uint8_t line[GS232B_LINE_STORAGE_BYTES];
    size_t message_size;
    bool fragmented;
    bool management;
    gs232b_ws_control_t controls[GS232B_WS_CONTROL_PACKETS];
    size_t control_head, control_count;
    uint8_t wire[GS232B_WS_CHUNK_BYTES + 10U];
    size_t wire_size, wire_sent;
    gs232b_time_us_t wire_started_us, last_ping_us, last_pong_us;
    uint32_t ping_serial;
    bool awaiting_pong, shutdown_sent;
    atomic_uint transport_seen_ms, send_started_ms;
    atomic_bool send_pending;
    atomic_bool occupied, pending, session_ready, close_requested, http_closed;
} gs232b_ws_client_t;

typedef struct gs232b_esp_ws {
    httpd_handle_t server;
    SemaphoreHandle_t lock;
    gs232b_dispatcher_t *dispatcher;
    gs232b_ws_client_t clients[GS232B_WS_CLIENTS];
    gs232b_ws_interface_t interfaces[3];
    gs232b_ws_uri_context_t uri_contexts[2];
    size_t cursor;
    gs232b_ws_client_t *opening;
    atomic_bool pump_pending;
    bool opened;
} gs232b_esp_ws_t;

/**
 * @brief 借用仅发送/关闭的会话端口；可在 open 前用于初始化 dispatcher。
 * @note 原子复制整个范围到每会话 4KiB FIFO；仅控制任务调用。多传输应用须
 *       按完整会话键路由 UART/WS，不能用 fd 替代 session_id + generation。
 */
gs232b_session_io_t gs232b_esp_ws_io(gs232b_esp_ws_t *adapter);
/**
 * @brief 在 TCP 80 启动共享 HTTPD，注册控制及管理 WebSocket，合计最多四个会话。
 * @param adapter 清零且生命周期覆盖 HTTPD 及借用端口的独占实例。
 * @param dispatcher 已用上述 io 或会话路由初始化的唯一 dispatcher。
 * @note 开启 WS、pre/post handshake，关闭阻塞 work queue。SDK 6.1 在 Upgrade
 *       后不调用普通 GET handler，连接欢迎语由 post hook 请求控制任务开会话。
 *       首次接纳连接前必须发布对应接口；HTTP 回调不调用 dispatcher/运动/存储。
 */
gs232b_error_t gs232b_esp_ws_open(gs232b_esp_ws_t *adapter, gs232b_dispatcher_t *dispatcher);
/**
 * @brief 发布 ETH/STA/SoftAP 本机 IPv4（主机序）、lwIP netif 名称及非零 session_epoch。
 * @param if_name 由 esp_netif_get_netif_impl_name 获取；address=0 时可为 NULL。
 * @note 由控制任务在 service 前调用；只使该接口旧连接失效。未发布的本机地址
 *       不接受连接；SoftAP 仅接受 /ws/manage 且须有活动配网窗口。管理会话只接纳
 *       扩展命令，ETH/STA 可从两条 URI 使用扩展；同 IP 的两个接口拒绝新连接。
 *       每个接受的 TCP socket 在 Upgrade 前以 SO_BINDTODEVICE 绑定此接口；绑定失败拒绝连接。
 */
gs232b_error_t gs232b_esp_ws_interface(gs232b_esp_ws_t *adapter,
    gs232b_transport_t transport, uint32_t address, uint64_t epoch, const char *if_name);
/**
 * @brief 控制任务每轮调用：先交付关闭，再轮转最多 feed 256 字节，调度一次 TX pump。
 * @note 不调用 tick；调用方持续调用 dispatcher_tick。所有控制任务入口串行，
 *       锁和 HTTPD work queue 均零等待。HTTPD 按 256 字节分块接收最多 16KiB，
 *       网络发送每轮每连接一次 MSG_DONTWAIT，支持短写，单帧最长 2 秒。
 *       5 秒 Ping/15 秒 Pong 超时仅作用于传输，不调用应用看门狗续期入口。
 */
gs232b_error_t gs232b_esp_ws_service(gs232b_esp_ws_t *adapter);
/**
 * @brief 同步关闭全部网络会话和 HTTPD 后释放资源，可重试 SDK 停服失败。
 * @note 仅停止阶段调用；httpd_stop 会等待 HTTPD 退出，不放入运动周期。
 */
gs232b_error_t gs232b_esp_ws_close(gs232b_esp_ws_t *adapter);
#endif
#endif
