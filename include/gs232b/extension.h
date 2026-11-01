#ifndef GS232B_EXTENSION_H
#define GS232B_EXTENSION_H

#include "gs232b/extension_json.h"

#define GS232B_EXTENSION_MAX_HANDLERS 32U
#define GS232B_EXTENSION_NAME_MAX 31U
#define GS232B_EXTENSION_COMMAND_BYTES (2U * GS232B_EXTENSION_NAME_MAX + 2U)
#define GS232B_EXTENSION_DATA_MAX_BYTES 2048U
#define GS232B_EXTENSION_RESPONSE_MAX_BYTES 2304U

typedef struct {
    char cmd[GS232B_EXTENSION_COMMAND_BYTES];
    gs232b_json_value_t arguments; /* NOTE: 省略 JSON 时为借用的空对象。 */
    bool has_json;
    gs232b_command_context_t context;
    gs232b_transport_t transport;
} gs232b_extension_request_t;

/**
 * @brief 无副作用检查字段值、权限及状态；接纳和执行前都会调用。
 * @note 语法、重复键和 schema 白名单先由框架检查。不得修改输出、所有权、存储或 ctx 状态。
 */
typedef gs232b_error_t (*gs232b_extension_check_fn)(
    void *ctx, const gs232b_extension_request_t *request);
/**
 * @brief 同步执行完整且已验证的请求，输出一个 JSON 对象作为 data；size=0 表示空对象。
 * @note data 不附加 NUL；不得自行发送响应或重入 dispatcher/control；不得保留请求视图。
 *       回调必须有界，校验失败必须在任何副作用之前返回。ctx 及 schema 由调用者持有。
 */
typedef gs232b_error_t (*gs232b_extension_handler_fn)(
    void *ctx, const gs232b_extension_request_t *request,
    uint8_t *data, size_t capacity, size_t *size);

typedef struct {
    const char *domain;
    const char *action;
    const gs232b_json_schema_t *schema; /* NOTE: NULL 拒绝所有参数字段。 */
    gs232b_extension_check_fn check;
    gs232b_extension_handler_fn handle;
    void *ctx;
} gs232b_extension_handler_t;

typedef struct {
    char cmd[GS232B_EXTENSION_COMMAND_BYTES];
    const gs232b_json_schema_t *schema;
    gs232b_extension_check_fn check;
    gs232b_extension_handler_fn handle;
    void *ctx;
} gs232b_extension_entry_t;

typedef struct {
    gs232b_extension_entry_t entries[GS232B_EXTENSION_MAX_HANDLERS];
    size_t count;
} gs232b_extension_registry_t;

typedef struct {
    char cmd[GS232B_EXTENSION_COMMAND_BYTES];
    gs232b_error_t result;
    gs232b_byte_span_t data; /* NOTE: 仅成功时借用，存储须覆盖响应发送。 */
} gs232b_extension_response_t;

/** @brief 初始化尚未使用的注册表。 */
void gs232b_extension_registry_init(gs232b_extension_registry_t *registry);
/** @brief 复制规范化的命令名，借用 ctx/schema；同名返回 CONFLICT，满表返回 RESOURCE_EXHAUSTED。 */
gs232b_error_t gs232b_extension_register(gs232b_extension_registry_t *registry,
                                         const gs232b_extension_handler_t *handler);
/** @brief 严格解析最多 2048 字节的 !DOMAIN ACTION [JSON对象]，不含 CR；命令规范化为 ASCII 大写。 */
gs232b_error_t gs232b_extension_parse(const uint8_t *line, size_t size,
                                      gs232b_extension_request_t *request);
/** @brief 查找 handler 并执行 schema 及 check 检查，不调用 handle。 */
gs232b_error_t gs232b_extension_validate(const gs232b_extension_registry_t *registry,
                                         const gs232b_extension_request_t *request);
/** @brief 再次检查后至多调用一次 handle；响应借用 data，返回实际业务结果。 */
gs232b_error_t gs232b_extension_dispatch(const gs232b_extension_registry_t *registry,
    const gs232b_extension_request_t *request, uint8_t *data, size_t capacity,
    gs232b_extension_response_t *response);
/** @brief 构造无正文错误响应；cmd 为规范命令名，未知/行级错误使用空名称。 */
void gs232b_extension_response_error(const char *cmd, gs232b_error_t error,
                                      gs232b_extension_response_t *response);
/** @brief 将内部结果映射为扩展契约允许的稳定错误名。 */
const char *gs232b_extension_error_name(gs232b_error_t error);
/** @brief 编码恰一行 !JSON\r\n；无 NUL，容量不足不修改 buffer，size 返回所需长度。 */
gs232b_error_t gs232b_extension_response_encode(const gs232b_extension_response_t *response,
    uint8_t *buffer, size_t capacity, size_t *size);

#endif
