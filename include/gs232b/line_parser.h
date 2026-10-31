#ifndef GS232B_LINE_PARSER_H
#define GS232B_LINE_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gs232b/types.h"

#define GS232B_GS_LINE_MAX_BYTES 16384U
#define GS232B_EXTENSION_LINE_MAX_BYTES 2048U
#define GS232B_LINE_PARTIAL_TIMEOUT_US UINT64_C(2000000)
#define GS232B_M_SEQUENCE_MAX_POINTS 3800U
#define GS232B_W_SEQUENCE_MAX_PAIRS 1900U

typedef enum {
    GS232B_LINE_GS = 0,
    GS232B_LINE_EXTENSION
} gs232b_line_kind_t;

typedef enum {
    GS232B_LINE_EVENT_COMMIT = 0,
    GS232B_LINE_EVENT_ERROR
} gs232b_line_event_type_t;

typedef enum {
    GS232B_LINE_ERROR_NONE = 0,
    GS232B_LINE_ERROR_INVALID_BYTE,
    GS232B_LINE_ERROR_INVALID_SYNTAX,
    GS232B_LINE_ERROR_TOO_LONG,
    GS232B_LINE_ERROR_TIMEOUT
} gs232b_line_error_t;

typedef struct {
    gs232b_line_event_type_t type;
    gs232b_line_kind_t kind;
    const uint8_t *bytes;
    size_t size;
    gs232b_line_error_t error;
} gs232b_line_event_t;

typedef void (*gs232b_line_event_fn)(void *ctx, const gs232b_line_event_t *event);

/** @brief 单条增量解析器状态。工作区由调用方持有。 */
typedef struct {
    uint8_t *storage;
    size_t storage_capacity;
    size_t length;
    gs232b_line_kind_t kind;
    gs232b_line_error_t discard_error;
    gs232b_time_us_t last_byte_at_us;
    bool active;
    bool kind_known;
    bool discarding;
    bool error_reported;
    bool swallow_lf;
} gs232b_line_parser_t;

/**
 * @brief 初始化行解析器。
 * @param parser 解析器状态。
 * @param storage 调用方持有的行工作区，至少 GS232B_GS_LINE_MAX_BYTES 字节。
 * @param storage_capacity 工作区容量。
 * @note 并发接收半行的解析器不能共用同一工作区。
 * @return 参数有效时返回 GS232B_OK。
 */
gs232b_error_t gs232b_line_parser_init(gs232b_line_parser_t *parser,
                                       uint8_t *storage, size_t storage_capacity);

/**
 * @brief 输入一段字节；事件按字节顺序同步回调。
 * @param parser 已初始化的解析器状态。
 * @param bytes 输入字节；size 为零时可为 NULL。
 * @param size 输入字节数。
 * @param now_us 本次输入的单调时钟时间戳。
 * @param event_fn 必须提供的同步事件回调。
 * @param event_ctx 原样传给事件回调的上下文。
 * @note 提交行的视图只在回调期间有效；GS 字节已转为 ASCII 大写。解析器须串行调用且不得在回调中重入。
 * @return 参数有效时返回 GS232B_OK。
 */
gs232b_error_t gs232b_line_parser_feed(gs232b_line_parser_t *parser,
                                       const uint8_t *bytes, size_t size,
                                       gs232b_time_us_t now_us,
                                       gs232b_line_event_fn event_fn, void *event_ctx);

/**
 * @brief 定期调用，即使没有新字节也可使半行超时。
 * @param parser 已初始化的解析器状态。
 * @param now_us 当前单调时钟时间戳。
 * @param event_fn 必须提供的同步事件回调。
 * @param event_ctx 原样传给事件回调的上下文。
 * @return 参数有效时返回 GS232B_OK。
 */
gs232b_error_t gs232b_line_parser_tick(gs232b_line_parser_t *parser,
                                       gs232b_time_us_t now_us,
                                       gs232b_line_event_fn event_fn, void *event_ctx);

/**
 * @brief 静默作废活动半行，持续丢弃到其终止 CR；空闲解析器保持空闲。
 * @note 用于 epoch/交互切换；不得在事件回调中调用。后续超时也不重复报错。
 */
void gs232b_line_parser_invalidate(gs232b_line_parser_t *parser);

typedef struct {
    const uint8_t *bytes;
    size_t size;
} gs232b_byte_span_t;

/**
 * @brief 去除首尾 ASCII 空格；Tab 等字节不作为空白。
 * @param span 原始字节范围。
 * @return 指向去空格后内容的范围。
 */
gs232b_byte_span_t gs232b_byte_span_trim_spaces(gs232b_byte_span_t span);
/**
 * @brief 以 ASCII 大小写不敏感方式比较字节串和 NUL 结尾文本。
 * @param span 原始字节范围。
 * @param text NUL 结尾文本。
 * @return 内容相同时为 true。
 */
bool gs232b_byte_span_equal_ascii_ci(gs232b_byte_span_t span, const char *text);
/**
 * @brief 按一个或多个 ASCII 空格取下一个非空字段。
 * @param remaining 输入及剩余范围；成功后更新为未消费部分。
 * @param token 输出字段范围。
 * @return 找到字段时为 true。
 */
bool gs232b_byte_span_next_space_token(gs232b_byte_span_t *remaining,
                                       gs232b_byte_span_t *token);
/**
 * @brief 严格解析三位十进制数，不接受符号或周边空白。
 * @param span 数字字节范围。
 * @param value 输出 0～999 的值。
 * @return 恰含三位十进制数字时为 true。
 */
bool gs232b_byte_span_parse_decimal3(gs232b_byte_span_t span, uint16_t *value);

#endif
