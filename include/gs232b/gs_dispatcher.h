#ifndef GS232B_GS_DISPATCHER_H
#define GS232B_GS_DISPATCHER_H

#include "gs232b/command_control.h"
#include "gs232b/persistence.h"
#include "gs232b/extension.h"
#include "gs232b/network_config.h"
#include "gs232b/provisioning.h"

#define GS232B_DISPATCHER_REPLIES_PER_SESSION 16U

/* NOTE: 以下布局仅供静态分配；响应占位只保存值对象，不复制长序列或帮助正文。 */
typedef struct {
    uint64_t id;
    uint8_t command;
    uint8_t state;
    gs232b_response_t response;
} gs232b_dispatcher_reply_t;

typedef struct {
    gs232b_session_key_t key;
    uint64_t next_id;
    size_t head;
    size_t count;
    gs232b_error_t failure;
    uint64_t extension_reply_id;
    bool management_authorized;
    uint8_t extension_data[GS232B_EXTENSION_DATA_MAX_BYTES];
    gs232b_dispatcher_reply_t replies[GS232B_DISPATCHER_REPLIES_PER_SESSION];
} gs232b_dispatcher_session_t;

typedef struct {
    gs232b_control_t control;
    gs232b_motion_t *motion;
    gs232b_store_t store;
    gs232b_session_io_t io;
    gs232b_azimuth_display_settings_t display;
    uint64_t display_revision;
    gs232b_config_transaction_t network_config;
    gs232b_net_status_port_t network_status;
    gs232b_net_apply_port_t network_apply;
    gs232b_provisioning_t provisioning;
    gs232b_dispatcher_session_t sessions[GS232B_CONTROL_MAX_SESSIONS];
    gs232b_session_key_t watchdog_session;
    gs232b_time_us_t watchdog_timeout_us;
    gs232b_time_us_t watchdog_renewed_us;
    uint8_t admin_verifier[32];
    uint64_t admin_revision;
    bool admin_enabled;
    bool network_reset_pending;
    bool entries_available[4];
    bool initialized;
    gs232b_extension_registry_t extensions;
} gs232b_dispatcher_t;

/**
 * @brief 将完整官方 GS 命令接入同一运动、序列、校准、显示存储及会话输出。
 * @param dispatcher 尚未活动的静态对象，不得重新初始化活动实例。
 * @param motion 已初始化且已知全停的唯一运动核心；使用其单调时钟。
 * @param store 借用的原子分域存储；加载显示设置，不存在时使用 P45/N Center。
 * @param io send 必须原子复制到指定完整会话键的有界 FIFO，close 关闭对应连接。
 * @param request_bytes 独占请求池，至少 GS232B_LINE_STORAGE_BYTES。
 * @param capacity 请求池字节数。
 * @note 所有入口和运动核心必须串行调用；回调不得重入。会话及输入必须经本门面，
 *       control 仅供只读快照。扩展经注册表验证及分派，独立输出单行 JSON。
 *       显示域加载失败会拒绝初始化；既有零点应由启动流程加载到 motion。
 */
gs232b_error_t gs232b_dispatcher_init(gs232b_dispatcher_t *dispatcher,
    gs232b_motion_t *motion, gs232b_store_t store, gs232b_session_io_t io,
    uint8_t *request_bytes, size_t capacity);

/** @brief 串行追加扩展处理器；复制命令名，借用 schema/ctx，重复注册返回 CONFLICT。 */
gs232b_error_t gs232b_dispatcher_register_extension(gs232b_dispatcher_t *dispatcher,
    const gs232b_extension_handler_t *handler);

/** @brief 打开仅接纳扩展命令的管理会话；保留真实 ETH/STA/AP 传输及完整会话键。 */
gs232b_error_t gs232b_dispatcher_management_session_open(gs232b_dispatcher_t *dispatcher,
    gs232b_transport_t transport, uint8_t *line_storage, size_t capacity,
    gs232b_session_key_t *session);

/** @brief 在打开任何会话前装载已确认的网络配置；不启动或重配任何网络接口。 */
gs232b_error_t gs232b_dispatcher_network_configure(gs232b_dispatcher_t *dispatcher,
    const gs232b_net_config_t *active, uint64_t revision, gs232b_net_status_port_t status);
/** @brief 设置完整配置应用端口；只允许在任何网络管理会话打开前调用。 */
gs232b_error_t gs232b_dispatcher_network_apply_configure(gs232b_dispatcher_t *dispatcher,
    gs232b_net_apply_port_t apply);

/**
 * @brief Attach the buttonless SoftAP port and its independent AP identity.
 * @note Call before network_configure when possible. The API is also valid after
 *       dispatcher init and before the first network record is loaded.
 */
gs232b_error_t gs232b_dispatcher_provisioning_configure(gs232b_dispatcher_t *dispatcher,
    gs232b_provisioning_ap_port_t ap_port, const uint8_t mac[6]);

/**
 * @brief 协议入口就绪后打开会话，只向新会话发送一次 Connect OK 加 CRLF。
 * @note UART 应在波特率匹配/回退完成后调用；工作区借用至关闭。
 */
gs232b_error_t gs232b_dispatcher_session_open(gs232b_dispatcher_t *dispatcher,
    gs232b_transport_t transport, uint8_t *line_storage, size_t capacity,
    gs232b_session_key_t *session);
/** @brief 同步关闭完整会话键、清响应队列；活动控制者关闭时全停并撤权。 */
gs232b_error_t gs232b_dispatcher_session_close(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session);
/** @brief 在输入进入上游 RX 队列前捕获 session 和 epoch，不得在出队时重盖章。 */
gs232b_error_t gs232b_dispatcher_capture(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session, gs232b_command_context_t *context);
/**
 * @brief 接纳最多 256 字节并按来源顺序发送已就绪响应，普通命令由 tick 执行。
 * @note 错误不能越过此前请求；TX 或响应占位耗尽时关闭来源会话，控制者全停。
 */
gs232b_error_t gs232b_dispatcher_feed(gs232b_dispatcher_t *dispatcher,
    gs232b_command_context_t context, const uint8_t *bytes, size_t size);
/** @brief 将传输丢字节送入通信故障清理，并按来源顺序刷出一次协议错误。 */
gs232b_error_t gs232b_dispatcher_input_lost(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session);
/** @brief 上游旧 epoch 输入屏障：静默丢弃来源半行和后缀至下一次 CR。 */
gs232b_error_t gs232b_dispatcher_resync_input(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session);
/** @brief 优先停止、推进运动/序列/校准和一条普通请求，按会话顺序发送响应。 */
gs232b_error_t gs232b_dispatcher_tick(gs232b_dispatcher_t *dispatcher);

/**
 * @brief 当前控制者自愿设置应用看门狗，0 关闭，其他值为 1000..600000 毫秒。
 * @note 默认关闭；也可经 !SESSION WATCHDOG/PING 调用。仅该控制者成功完成的
 *       完整命令或显式 watchdog_ping 续期；WS 控制帧及周期显示不续期。
 *       tick 在执行普通命令前检查期限，超时关闭该会话并执行所有者失联清理。
 */
gs232b_error_t gs232b_dispatcher_watchdog_set(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session, uint32_t timeout_ms);
/** @brief 应用心跳续期入口，仅当前控制者可调用；不得由 WS Ping/Pong 调用。 */
gs232b_error_t gs232b_dispatcher_watchdog_ping(gs232b_dispatcher_t *dispatcher,
    gs232b_session_key_t session);

#endif
