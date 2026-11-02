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

#ifndef GS232B_EXTENSION_JSON_H
#define GS232B_EXTENSION_JSON_H

#include "gs232b/line_parser.h"

#define GS232B_JSON_MAX_DEPTH 16U
#define GS232B_JSON_MAX_FIELDS 32U

typedef enum {
    GS232B_JSON_OBJECT, GS232B_JSON_ARRAY, GS232B_JSON_STRING,
    GS232B_JSON_NUMBER, GS232B_JSON_BOOL, GS232B_JSON_NULL
} gs232b_json_type_t;

typedef struct {
    gs232b_json_type_t type;
    gs232b_byte_span_t raw;
} gs232b_json_value_t;

typedef struct gs232b_json_schema gs232b_json_schema_t;
typedef struct {
    const char *name;
    gs232b_json_type_t type;
    bool required;
    size_t max_bytes; /* NOTE: 字符串解码后的字节上限；0 表示不另设上限。 */
    const gs232b_json_schema_t *object; /* NOTE: 对象字段的递归白名单；NULL 仅接受空对象。 */
} gs232b_json_field_t;

struct gs232b_json_schema {
    const gs232b_json_field_t *fields;
    size_t count;
};

/** @brief 严格校验完整 JSON、UTF-8、代理对和重复键；拒绝所有原始控制字节。 */
gs232b_error_t gs232b_json_parse(gs232b_byte_span_t raw, gs232b_json_value_t *value);
/** @brief 按白名单校验对象的未知、必需、类型及解码长度字段；NULL schema 仅接受空对象。 */
gs232b_error_t gs232b_json_validate_object(gs232b_json_value_t object,
                                           const gs232b_json_schema_t *schema);
/** @brief 校验 schema 描述符；所有描述符及字符串由注册者持有至 dispatcher 销毁。 */
bool gs232b_json_schema_valid(const gs232b_json_schema_t *schema);
/** @brief 获取对象字段；名称按解码后的 Unicode 精确比较，未找到返回 NOT_FOUND。 */
gs232b_error_t gs232b_json_get(gs232b_json_value_t object, const char *name,
                               gs232b_json_value_t *value);
/** @brief 解码字符串到 UTF-8 字节，不附加 NUL；容量不足不修改 buffer，size 返回所需长度。 */
gs232b_error_t gs232b_json_string_decode(gs232b_json_value_t value,
    uint8_t *buffer, size_t capacity, size_t *size);
/** @brief 编码 UTF-8 字节为带双引号的 JSON 字符串；支持 NUL、引号、反斜杠及控制字节。 */
gs232b_error_t gs232b_json_string_encode(gs232b_byte_span_t text,
    uint8_t *buffer, size_t capacity, size_t *size);
/** @brief 严格读取无符号十进制整数，不接受负数、小数、指数或溢出。 */
gs232b_error_t gs232b_json_uint32(gs232b_json_value_t value, uint32_t *number);
/** @brief 严格读取无符号十进制 uint64 整数，不接受负数、小数、指数或溢出。 */
gs232b_error_t gs232b_json_uint64(gs232b_json_value_t value, uint64_t *number);

#endif
