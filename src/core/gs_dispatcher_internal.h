#ifndef GS232B_GS_DISPATCHER_INTERNAL_H
#define GS232B_GS_DISPATCHER_INTERNAL_H

#include "gs232b/gs_dispatcher.h"

typedef enum {
    GS_COMMAND_UNKNOWN = 0, GS_COMMAND_EMPTY,
    GS_COMMAND_R, GS_COMMAND_L, GS_COMMAND_A, GS_COMMAND_U, GS_COMMAND_D,
    GS_COMMAND_E, GS_COMMAND_S, GS_COMMAND_X1, GS_COMMAND_X2, GS_COMMAND_X3,
    GS_COMMAND_X4, GS_COMMAND_C, GS_COMMAND_B, GS_COMMAND_C2, GS_COMMAND_N,
    GS_COMMAND_H, GS_COMMAND_H2, GS_COMMAND_H3, GS_COMMAND_P36, GS_COMMAND_P45,
    GS_COMMAND_Z, GS_COMMAND_T
} gs_command_t;

enum { GS_REPLY_WAITING = 0, GS_REPLY_READY, GS_REPLY_QUERY, GS_REPLY_SKIP };

/** @brief 严格识别普通 GS 命令；M/W 数字和校准交互由控制核心处理。 */
gs_command_t gs232b_gs_command(const gs232b_control_request_t *request);
/** @brief 只校验普通命令语法及能力，不产生运动、存储或输出副作用。 */
gs232b_error_t gs232b_gs_validate(void *ctx, const gs232b_control_request_t *request);
/** @brief 执行已通过控制权检查的运动/速度/模式命令，查询保持只读。 */
gs232b_error_t gs232b_gs_execute(void *ctx, const gs232b_control_request_t *request);
/** @brief 从实际状态构造查询正文；失效反馈不使用旧值或伪造零。 */
gs232b_error_t gs232b_gs_query(gs232b_dispatcher_t *dispatcher,
    gs_command_t command, gs232b_response_t *response);
/** @brief 为完整行或解析错误创建来源会话内的响应占位。 */
void gs232b_gs_submitted(void *ctx, gs232b_control_request_t *request);
/** @brief 完成请求响应；合并的停止只标记来源会话的相应停止占位。 */
void gs232b_gs_complete(void *ctx, const gs232b_control_request_t *request,
    gs232b_error_t result, uint32_t count);
/** @brief 失效写请求静默跳过；查询保留正文和相对响应顺序。 */
void gs232b_gs_discarded(void *ctx, const gs232b_control_request_t *request);
/** @brief 将异步校准正文追加到发起会话的有界响应队列。 */
gs232b_error_t gs232b_gs_respond(void *ctx, gs232b_session_key_t session,
    const gs232b_response_t *response);
/** @brief 在控制核心回调返回后发送就绪前缀，并同步关闭输出失败会话。 */
gs232b_error_t gs232b_gs_flush(gs232b_dispatcher_t *dispatcher);

#endif
