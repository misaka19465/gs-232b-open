#ifndef GS232B_COMMAND_CONTROL_H
#define GS232B_COMMAND_CONTROL_H

#include "gs232b/line_parser.h"
#include "gs232b/ports.h"
#include "gs232b/sequence_store.h"
#include "gs232b/calibration.h"
#include "gs232b/response_encoder.h"

#define GS232B_CONTROL_MAX_SESSIONS 7U
#define GS232B_CONTROL_REQUESTS_PER_SESSION 8U
#define GS232B_CONTROL_MAX_REQUESTS \
    (GS232B_CONTROL_MAX_SESSIONS * GS232B_CONTROL_REQUESTS_PER_SESSION)
#define GS232B_CONTROL_FEED_MAX_BYTES 256U
#define GS232B_CONTROL_IDLE_RELEASE_US UINT64_C(2000000)

typedef enum {
    GS232B_REQUEST_QUERY = 0,
    GS232B_REQUEST_WRITE,
    GS232B_REQUEST_OWNER_GET,
    GS232B_REQUEST_OWNER_TAKE,
    GS232B_REQUEST_OWNER_RELEASE,
    GS232B_REQUEST_STOP_AZ,
    GS232B_REQUEST_STOP_EL,
    GS232B_REQUEST_STOP_ALL
} gs232b_request_access_t;

typedef struct {
    gs232b_command_context_t context;
    gs232b_line_kind_t kind;
    gs232b_request_access_t access;
    gs232b_byte_span_t line;
    const gs232b_position_command_t *position; /* NOTE: M/W 使用数字视图，line 为空。 */
    const gs232b_position_plan_t *position_plan; /* NOTE: 仅执行已验证的数字命令时有效。 */
    const gs232b_response_t *response; /* NOTE: 仅 complete 期间有效的校准响应正文。 */
    uint64_t reply_id; /* NOTE: submitted 分配的会话内响应标识，0 表示未跟踪。 */
} gs232b_control_request_t;

typedef struct {
    bool axes_active[GS232B_AXIS_COUNT]; /* NOTE: 包含尚未输出的目标和换向等待。 */
    bool output_stopped; /* NOTE: 仅已知方向全停且 PWM=0 时为 true。 */
    gs232b_interaction_t interaction;
    bool faulted;
} gs232b_control_activity_t;

/**
 * @brief 借用的同步执行端口；回调必须有界、非阻塞，不得重入修改控制器的 API。
 * @note validate 只校验，不改变动作；execute 只处理已获准的完整普通行，
 *       必须按 request.context.session 隔离响应。QUERY 不得修改动作。
 *       cancel 无条件清除执行端暂存动作，不提交输出；stop
 *       撤销 mask 指定轴的已接纳目标/手动/换向动作并立即提交停止，其他轴
 *       已接纳的动作继续。tick 推进采样和运动安全检查，不得另读命令。
 *       snapshot 必须反映上述真实状态，interaction 由核心填入；运动故障由执行端锁定并全停。
 *       complete 仅向来源会话报告结果，line/response 视图仅在回调期间有效；
 *       成功且 response 非空时编码该正文，不追加普通确认；错误始终编码协议错误。
 *       M/W 完成通知只含 position 元数据，angles=NULL；有效机械角读取 active。
 *       可调用 control_snapshot 复制只读状态，OWNER GET/TAKE/RELEASE 在此编码响应。
 *       position_context 只复制最新坐标/轴能力，不采样或提交输出；M/W 全量校验
 *       由控制核心完成。execute 对数字命令按 position_plan 先停 stop_mask，
 *       再原子接纳 axis_mask 的首点；序列状态、T 和后续计时由控制核心维护。
 *       前置条件错误不得改变旧动作，输出故障按全停故障处理。
 *       sequence_target 原子接纳后续点的机械角计划，不缓存角度或改变计时；
 *       fault 锁定给定诊断、撤销两轴动作并尝试一次全停，保留首次故障。
 *       故障停机失败须保留输出未知，后续 tick 不自动重试；两回调不得重入。
 *       P36/P45/Z 由核心校验变更条件，execute 成功应用/持久化后核心清序列；
 *       重复模式和 P45 下的 Z 保留序列。N 从 control_snapshot.sequence 编码。
 *       裸 M/W 由控制核心在权限通过后清任务、全停并返回错误，不调用 execute。
 *       普通请求 count=1；同周期同来源同种停止合并，count 是应答数量。
 *       epoch 作废的普通请求静默丢弃。回调和 ctx 生命周期覆盖控制器。
 *       calibration.motion 必须是上述回调操作的同一运动核心，与控制器共用单调时钟；
 *       calibration.store 只在已知全停时保存零点，必须有界且失败保留旧记录。
 *       O/O2/F/F2 及发起会话的 GS 交互输入由核心处理，不调用 validate/execute。
 *       respond 是必需的异步校准响应端口，仅向完整会话键指定的有界 TX 队列复制
 *       编码帧；失败时核心关闭该会话、全停并撤权，适配器须同步清理实际连接。
 */
typedef struct {
    void *ctx;
    gs232b_error_t (*validate)(void *ctx, const gs232b_control_request_t *request);
    gs232b_error_t (*execute)(void *ctx, const gs232b_control_request_t *request);
    void (*cancel)(void *ctx);
    gs232b_error_t (*stop)(void *ctx, uint8_t axis_mask);
    gs232b_error_t (*tick)(void *ctx);
    gs232b_error_t (*snapshot)(void *ctx, gs232b_control_activity_t *activity);
    gs232b_error_t (*position_context)(void *ctx, gs232b_position_context_t *context);
    gs232b_error_t (*sequence_target)(void *ctx, const gs232b_position_plan_t *plan);
    gs232b_error_t (*fault)(void *ctx, gs232b_fault_code_t code, gs232b_error_t error);
    void (*complete)(void *ctx, const gs232b_control_request_t *request,
                     gs232b_error_t result, uint32_t count);
    gs232b_calibration_ports_t calibration;
    gs232b_error_t (*respond)(void *ctx, gs232b_session_key_t session,
                               const gs232b_response_t *response);
    /** @brief 可选：为每个完整行/解析错误登记响应顺序；只可设置 reply_id，不得重入。 */
    void (*submitted)(void *ctx, gs232b_control_request_t *request);
    /** @brief 可选：通知未执行请求已失效；可保留只读响应，禁止执行任何写副作用。 */
    void (*discarded)(void *ctx, const gs232b_control_request_t *request);
} gs232b_control_executor_t;

typedef struct {
    gs232b_command_context_t context;
    uint32_t count;
} gs232b_control_stop_notice_t;

typedef struct gs232b_control gs232b_control_t;

/* NOTE: 以下布局仅供静态分配；调用方不得直接修改、复制活动对象或重用工作区。 */
typedef struct {
    gs232b_control_t *control;
    uint64_t generation;
    gs232b_session_key_t key;
    gs232b_transport_t transport;
    bool connected;
    gs232b_line_parser_t parser;
    gs232b_command_context_t partial_context;
    size_t pending_requests;
    gs232b_control_stop_notice_t stops[3];
} gs232b_control_session_t;

typedef struct {
    gs232b_command_context_t context;
    gs232b_line_kind_t kind;
    gs232b_request_access_t access;
    size_t offset;
    size_t size;
    bool is_position;
    gs232b_position_command_t position;
    uint64_t reply_id;
} gs232b_control_queued_t;

struct gs232b_control {
    gs232b_clock_t clock;
    gs232b_control_executor_t executor;
    gs232b_control_session_t sessions[GS232B_CONTROL_MAX_SESSIONS];
    gs232b_control_queued_t requests[GS232B_CONTROL_MAX_REQUESTS];
    uint8_t *request_bytes;
    size_t request_capacity;
    size_t request_bytes_used;
    size_t request_count;
    gs232b_sequence_store_t sequence;
    gs232b_sequence_snapshot_t sequence_state;
    gs232b_calibration_t calibration;
    gs232b_time_us_t sequence_started_at_us;
    gs232b_time_us_t sequence_checked_at_us;
    gs232b_command_context_t input_reservation;
    bool upload_queued;
    gs232b_session_key_t owner;
    gs232b_command_epoch_t epoch;
    gs232b_time_us_t idle_since_us;
    uint8_t stop_mask;
    bool full_stop_pending;
    bool idle_timer_running;
    bool epoch_exhausted;
    bool fault_seen;
    bool dispatching;
    bool initialized;
};

typedef struct {
    gs232b_session_key_t owner;
    gs232b_transport_t owner_transport; /* NOTE: 仅 owner 有效时有意义。 */
    gs232b_command_epoch_t epoch;
    gs232b_control_activity_t activity;
    gs232b_sequence_snapshot_t sequence; /* NOTE: N 使用已下发点序号，与轴到位无关。 */
    size_t pending_requests;
    size_t request_bytes_used;
    uint8_t stop_mask;
    bool full_stop_pending;
    bool idle_timer_running;
    gs232b_time_us_t idle_since_us;
    bool epoch_exhausted;
    gs232b_session_key_t input_reservation;
    bool upload_queued;
} gs232b_control_snapshot_t;

/**
 * @brief 初始化静态控制核心，要求执行端无活动动作、序列或交互且已知全停。
 * @param request_bytes 独占的普通请求字节池，至少 GS232B_LINE_STORAGE_BYTES。
 * @param request_capacity 固定字节池容量；M/W 仅排队数字描述符，不占正文池。
 * @note 所有 API、执行端和传输入口必须由同一任务/外部锁串行调用。
 *       不得重新初始化活动对象；重启实例须同时丢弃全部外部会话和输入。
 */
gs232b_error_t gs232b_control_init(gs232b_control_t *control, gs232b_clock_t clock,
                                  gs232b_control_executor_t executor,
                                  uint8_t *request_bytes, size_t request_capacity);

/**
 * @brief 打开会话，分配非零会话键；复用槽位时 generation 单调增加，不回绕。
 * @param line_storage 会话独占工作区，至少 GS232B_LINE_STORAGE_BYTES；借用至关闭。
 * @note 传输适配器必须保存返回的完整键，不得仅保存 session_id。
 */
gs232b_error_t gs232b_control_session_open(gs232b_control_t *control,
                                          gs232b_transport_t transport,
                                          uint8_t *line_storage, size_t capacity,
                                          gs232b_session_key_t *session);
/** @brief 关闭完整会话键；控制者关闭时同步全停、清任务、撤权并作废旧输入。 */
gs232b_error_t gs232b_control_session_close(gs232b_control_t *control,
                                           gs232b_session_key_t session);

/**
 * @brief 在字节进入任何上游缓冲/队列前捕获会话和 epoch。
 * @note 外部已排队的每段输入必须保留此上下文，不能在出队时重新盖章。
 */
gs232b_error_t gs232b_control_capture(gs232b_control_t *control,
                                     gs232b_session_key_t session,
                                     gs232b_command_context_t *context);
/**
 * @brief 接纳至多 256 字节，首字节上下文一直保留到 CR；不执行普通命令。
 * @note 有效参数时总是消费整段，包括报错或队列满；返回段内首个错误。
 *       旧 epoch 的字节也消费并丢弃到 CR；旧 generation 不接纳任何字节。
 *       调用方须在块间让出调度，并持续调用 tick；只有独立完整 A/E/S 走停止槽。
 */
gs232b_error_t gs232b_control_feed(gs232b_control_t *control,
                                  gs232b_command_context_t context,
                                  const uint8_t *bytes, size_t size);

/**
 * @brief 显式申请，不抢占现有控制者；相同控制者重复申请可刷新空闲起点。
 * @note 要求活动上下文及无待处理停止；初次取权要求执行端空闲且已知全停。
 */
gs232b_error_t gs232b_control_take(gs232b_control_t *control,
                                  gs232b_command_context_t context);
/**
 * @brief 控制者在已知全停、无待运行序列和交互时释放，递增 epoch 并清旧输入。
 * @note 活动中返回 BUSY；可先请求 S 完成全停和撤权。
 */
gs232b_error_t gs232b_control_release(gs232b_control_t *control,
                                     gs232b_command_context_t context);
/**
 * @brief 将 A/E/S 放入独立停止槽；任何活动会话均可请求，不占普通队列。
 * @note stop 只能是三种 STOP access；实际输出在下次 tick 最先处理。
 *       UINT32_MAX 个同种未应答停止后报告资源耗尽，已锁存的停止仍保留。
 */
gs232b_error_t gs232b_control_request_stop(gs232b_control_t *control,
                                          gs232b_command_context_t context,
                                          gs232b_request_access_t stop);
/**
 * @brief 先处理停止，再推进执行端和半行超时，最多执行一条普通请求并检查空闲释放。
 * @note A/E 也作废旧队列/半行，但保留当前控制者与另一轴已接纳动作；A+E 不等于 S。
 *       故障首次出现时清任务并撤权，不重复尝试执行端已经失败的停机提交。
 *       epoch 耗尽后永久拒绝写动作/取权；查询及停止仍可用，绝不回绕。
 */
gs232b_error_t gs232b_control_tick(gs232b_control_t *control);
/** @brief 复制所有权、活动和队列值快照；不推进运动或接纳命令。 */
gs232b_error_t gs232b_control_snapshot(const gs232b_control_t *control,
                                      gs232b_control_snapshot_t *snapshot);

#endif
