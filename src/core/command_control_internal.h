#ifndef GS232B_COMMAND_CONTROL_INTERNAL_H
#define GS232B_COMMAND_CONTROL_INTERNAL_H

#include "gs232b/command_control.h"

#define GS232B_CONTROL_AZ_MASK UINT8_C(1)
#define GS232B_CONTROL_EL_MASK UINT8_C(2)
#define GS232B_CONTROL_ALL_AXES UINT8_C(3)

/** @brief 检查已初始化的串行控制器。 */
bool gs232b_control_valid(const gs232b_control_t *control);
/** @brief 按完整键查找活动会话；不存在或 generation 不匹配时返回 NULL。 */
gs232b_control_session_t *gs232b_control_find(gs232b_control_t *control,
                                            gs232b_session_key_t session);
/** @brief 校验活动会话和当前 epoch；旧 epoch 返回 CONFLICT。 */
gs232b_error_t gs232b_control_check(gs232b_control_t *control,
                                    gs232b_command_context_t context);
/** @brief 判断无动作、待运行序列或交互且已知全停。 */
bool gs232b_control_idle(const gs232b_control_activity_t *activity,
                         gs232b_sequence_state_t sequence);
/** @brief 清普通队列、作废半行并递增 epoch；耗尽时锁定动作。 */
gs232b_error_t gs232b_control_invalidate(gs232b_control_t *control);
/** @brief 失效旧输入后全停或单轴停止；单轴保留 owner，全部停止撤权。 */
gs232b_error_t gs232b_control_stop_now(gs232b_control_t *control,
                                      uint8_t mask, bool revoke_owner);
/** @brief 新故障清理旧任务并撤权；执行端已经负责故障全停，不重试输出。 */
gs232b_error_t gs232b_control_observe_fault(gs232b_control_t *control);
/** @brief 完成通知；请求视图只在本次调用内有效。 */
void gs232b_control_complete(gs232b_control_t *control,
                              const gs232b_control_request_t *request,
                              gs232b_error_t error, uint32_t count);
/** @brief 解码完整行的权限类别；未知扩展默认拒绝。 */
gs232b_error_t gs232b_control_classify(gs232b_line_kind_t kind,
                                      gs232b_byte_span_t line,
                                      gs232b_request_access_t *access);
/** @brief 复制接纳后的完整普通行到有界共享池及来源会话队列。 */
gs232b_error_t gs232b_control_enqueue(gs232b_control_t *control,
                                     const gs232b_control_request_t *request);
/** @brief 在队列头上同步分发一条请求，返回处理结果；空队列返回 OK。 */
gs232b_error_t gs232b_control_dispatch_one(gs232b_control_t *control);
/** @brief 无输入时推进每会话半行超时，错误只回来源。 */
void gs232b_control_expire_lines(gs232b_control_t *control);
/** @brief 从 M/W 首字母预约唯一 staging；不取得控制权或改变输出。 */
gs232b_error_t gs232b_control_upload_begin(void *ctx, uint16_t **angles);
/** @brief 释放来源半行的输入预约；不改变旧 active 或其他会话的预约。 */
void gs232b_control_upload_abort(void *ctx);
/** @brief 检查写请求是否被其他会话的输入预约阻挡。 */
gs232b_error_t gs232b_control_check_reservation(
    const gs232b_control_t *control, gs232b_session_key_t session);
/** @brief 完整校验数字命令、当前活动状态和坐标，生成首点计划。 */
gs232b_error_t gs232b_control_validate_position(
    gs232b_control_t *control, const gs232b_position_command_t *command,
    gs232b_position_context_t *context, gs232b_position_plan_t *plan);
/** @brief 清有效序列和执行端任务，不提交输出。 */
void gs232b_control_cancel(gs232b_control_t *control);
/** @brief 清序列、序号和计时，不改变已下发的轴动作。 */
void gs232b_control_sequence_clear(gs232b_control_t *control);
/** @brief 首点成功且 active 提交后进入等待 T；单点则清序列状态。 */
void gs232b_control_sequence_loaded(gs232b_control_t *control);
/** @brief 从当前单调时刻启动；运行中的重复 T 不改变任何计时或目标。 */
gs232b_error_t gs232b_control_sequence_start(gs232b_control_t *control);
/** @brief 动作推进前检查时钟倒退及整间隔超期，异常时锁定故障全停。 */
gs232b_error_t gs232b_control_sequence_check(gs232b_control_t *control);
/** @brief 复查截止时间并至多下发一个点，成功后推进绝对截止时间。 */
gs232b_error_t gs232b_control_sequence_tick(gs232b_control_t *control);
/** @brief 校验显示变更前置条件，并标明成功执行后是否需要清除旧序列。 */
gs232b_error_t gs232b_control_sequence_display_change(gs232b_control_t *control,
    const gs232b_control_request_t *request, bool *changed);

#endif
