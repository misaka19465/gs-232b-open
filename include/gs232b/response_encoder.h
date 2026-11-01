#ifndef GS232B_RESPONSE_ENCODER_H
#define GS232B_RESPONSE_ENCODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gs232b/types.h"
#include "gs232b/extension.h"

typedef enum {
    GS232B_RESPONSE_SUCCESS = 0,
    GS232B_RESPONSE_ERROR,
    GS232B_RESPONSE_AZIMUTH,
    GS232B_RESPONSE_ELEVATION,
    GS232B_RESPONSE_BOTH_ANGLES,
    GS232B_RESPONSE_SEQUENCE,
    GS232B_RESPONSE_HELP_AZIMUTH,
    GS232B_RESPONSE_HELP_ELEVATION,
    GS232B_RESPONSE_HELP_DISPLAY,
    GS232B_RESPONSE_ZERO_AZIMUTH_PROMPT,
    GS232B_RESPONSE_ZERO_ELEVATION_PROMPT,
    GS232B_RESPONSE_CALIBRATION_COMPLETE,
    GS232B_RESPONSE_CALIBRATION_AZIMUTH,
    GS232B_RESPONSE_CALIBRATION_BOTH,
    GS232B_RESPONSE_EXTENSION
} gs232b_response_kind_t;

typedef struct {
    gs232b_response_kind_t kind;
    union {
        struct {
            uint16_t azimuth_degrees;
            uint16_t elevation_degrees;
        } angles;
        struct {
            uint16_t current_point;
            uint16_t point_count;
        } sequence;
        struct {
            bool mode_450;
            bool south_center;
        } display;
        gs232b_extension_response_t extension;
    } data;
} gs232b_response_t;

/**
 * @brief 按 GS-232B Open 输出契约编码一帧响应。
 * @param response 带响应类型及其所需数据的值。
 * @param buffer 输出缓冲区；NULL 且 capacity 为零时只查询所需长度。
 * @param capacity 输出缓冲区容量。
 * @param size 输出编码长度；容量不足时仍返回所需长度。
 * @return 成功时返回 GS232B_OK；参数无效或缓冲区不足时返回对应错误。
 * @note 编码不包含 NUL 终止符；容量不足时不修改 buffer。
 */
gs232b_error_t gs232b_response_encode(const gs232b_response_t *response,
                                      uint8_t *buffer, size_t capacity,
                                      size_t *size);

#endif
