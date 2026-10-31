#ifndef GS232B_POSITION_PARSER_H
#define GS232B_POSITION_PARSER_H

#include <stddef.h>

#include "gs232b/types.h"

#define GS232B_M_SEQUENCE_MAX_POINTS 3800U
#define GS232B_W_SEQUENCE_MAX_PAIRS 1900U
#define GS232B_POSITION_MAX_ANGLES GS232B_M_SEQUENCE_MAX_POINTS

typedef enum {
    GS232B_POSITION_BARE = 0,
    GS232B_POSITION_SINGLE,
    GS232B_POSITION_SEQUENCE
} gs232b_position_format_t;

/** @brief 已完成的 M/W 参数视图；angles 为协议角度，W 按 AZ/EL 交错。 */
typedef struct {
    char command;
    gs232b_position_format_t format;
    uint16_t interval_s;
    uint16_t point_count;
    const uint16_t *angles;
} gs232b_position_command_t;

/* NOTE: 私有增量状态；header 暂存间隔或单点方位，不占用角度容量。 */
typedef struct {
    uint16_t *angles;
    size_t fields;
    uint16_t header;
    uint16_t value;
    uint8_t digits;
    char command;
    bool leading_space;
} gs232b_position_parser_t;

/**
 * @brief 开始解析 M/W 后的字节，不保存原始行。
 * @param angles 至少 GS232B_POSITION_MAX_ANGLES 个 uint16_t；NULL 时只校验语法。
 * @note NULL 用于输入预约被拒后的排空，不可提交其参数视图。
 */
void gs232b_position_parser_init(gs232b_position_parser_t *parser, char command,
                                 uint16_t *angles);
/** @brief 消费一个参数字节；只接受三位数字及 ASCII 空格，不接收 CR。 */
gs232b_error_t gs232b_position_parser_push(gs232b_position_parser_t *parser,
                                           uint8_t byte);
/** @brief 在 CR 处校验数量/配对并输出参数；不检查角度范围或执行动作。 */
gs232b_error_t gs232b_position_parser_finish(gs232b_position_parser_t *parser,
                                             gs232b_position_command_t *command);

#endif
