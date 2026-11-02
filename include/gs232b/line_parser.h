/*
 * GS-232B Open - rotator controller firmware.
 * Copyright (C) 2026 BG5GEB <bg5geb@misakanet.team>
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef GS232B_LINE_PARSER_H
#define GS232B_LINE_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gs232b/position_parser.h"

#define GS232B_GS_LINE_MAX_BYTES 16384U
#define GS232B_EXTENSION_LINE_MAX_BYTES 2048U
#define GS232B_LINE_STORAGE_BYTES GS232B_EXTENSION_LINE_MAX_BYTES
#define GS232B_LINE_PARTIAL_TIMEOUT_US UINT64_C(2000000)

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
    GS232B_LINE_ERROR_TIMEOUT,
    GS232B_LINE_ERROR_UPLOAD_REJECTED
} gs232b_line_error_t;

typedef struct {
    gs232b_line_event_type_t type;
    gs232b_line_kind_t kind;
    const uint8_t *bytes;
    size_t size;
    gs232b_line_error_t error;
    gs232b_error_t rejection;
    const gs232b_position_command_t *position;
} gs232b_line_event_t;

typedef void (*gs232b_line_event_fn)(void *ctx, const gs232b_line_event_t *event);

/**
 * @brief M/W 输入预约端口；begin 只预约并返回 3800 个 uint16_t 的独占工作区。
 * @note 不得产生运动副作用。失败时解析器只检查语法并排空到 CR。
 *       abort 在错误、超时或 invalidate 时释放成功的预约，至多一次；
 *       成功提交后由事件接收方持有工作区直到执行/拒绝，负责释放预约。
 *       回调不得重入解析器；ctx 的生命周期覆盖解析器。
 */
typedef struct {
    void *ctx;
    gs232b_error_t (*begin)(void *ctx, uint16_t **angles);
    void (*abort)(void *ctx);
} gs232b_line_upload_t;

/** @brief 单条增量解析器状态。工作区由调用方持有。 */
typedef struct {
    uint8_t *storage;
    size_t storage_capacity;
    size_t length;
    size_t stored;
    size_t pending_spaces;
    gs232b_line_upload_t upload;
    gs232b_position_parser_t position;
    gs232b_error_t upload_error;
    gs232b_line_kind_t kind;
    gs232b_line_error_t discard_error;
    gs232b_time_us_t last_byte_at_us;
    bool active;
    bool kind_known;
    bool discarding;
    bool error_reported;
    bool swallow_lf;
    bool is_position;
    bool upload_held;
} gs232b_line_parser_t;

/**
 * @brief 初始化行解析器。
 * @param parser 解析器状态。
 * @param storage 调用方持有的行工作区，至少 GS232B_LINE_STORAGE_BYTES 字节。
 * @param storage_capacity 工作区容量。
 * @param upload 必须提供的增量参数预约/释放端口。
 * @note 并发接收半行的解析器不能共用同一工作区。
 * @return 参数有效时返回 GS232B_OK。
 */
gs232b_error_t gs232b_line_parser_init(gs232b_line_parser_t *parser,
                                       uint8_t *storage, size_t storage_capacity,
                                       gs232b_line_upload_t upload);

/**
 * @brief 输入一段字节；事件按字节顺序同步回调。
 * @param parser 已初始化的解析器状态。
 * @param bytes 输入字节；size 为零时可为 NULL。
 * @param size 输入字节数。
 * @param now_us 本次输入的单调时钟时间戳。
 * @param event_fn 必须提供的同步事件回调。
 * @param event_ctx 原样传给事件回调的上下文。
 * @note 普通行 bytes 为大写 GS 或原样扩展正文；M/W 的 bytes=NULL、size=0，
 *       position 为数字视图。视图仅在回调期间有效；成功接收方须保留预约
 *       或复制参数。解析器须串行调用且不得在回调中重入。
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
