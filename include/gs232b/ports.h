#ifndef GS232B_PORTS_H
#define GS232B_PORTS_H

#include <stddef.h>
#include <stdint.h>

#include "gs232b/types.h"

/* NOTE: 端口借用 ctx，所有者的生命周期必须覆盖端口的每个副本。适配器负责
 * 串行化访问；回调必须快速返回、工作量有界，且不得保留调用方缓冲区。存储
 * 操作在运动循环之外执行。 */
typedef struct {
    void *ctx;
    gs232b_time_us_t (*now_us)(void *ctx);
} gs232b_clock_t;

typedef struct {
    void *ctx;
    gs232b_error_t (*read)(void *ctx, gs232b_axis_id_t axis, gs232b_sample_t *sample);
} gs232b_sampler_t;

/* NOTE: 成功时提供有效采样；失败时清除整个采样（包括 valid）。acquired_at_us
 * 表示采样获取时间，而不是读取缓存采样的时间。 */
gs232b_error_t gs232b_sampler_read(const gs232b_sampler_t *sampler,
                                  gs232b_axis_id_t axis, gs232b_sample_t *sample);

typedef struct {
    void *ctx;
    /* NOTE: 通过唯一的输出管理器接纳完整请求。OK 可能仍在换向等待，
     * 实际提交状态由管理器快照提供；运动层负责故障/停止策略。驱动返回错误
     * 时物理状态可能未知，包括 PWM/SPI 只完成部分更新的情况。 */
    gs232b_error_t (*apply)(void *ctx, const gs232b_output_frame_t *frame);
} gs232b_output_t;

gs232b_error_t gs232b_output_apply(const gs232b_output_t *output,
                                  const gs232b_output_frame_t *frame);

typedef enum {
    GS232B_STORE_NETWORK = 0,
    GS232B_STORE_DISPLAY,
    GS232B_STORE_ZERO_AZ,
    GS232B_STORE_ZERO_EL,
    GS232B_STORE_UART_BAUD,
    GS232B_STORE_CONFIG_META,
    GS232B_STORE_ADMIN,
    GS232B_STORE_DOMAIN_COUNT
} gs232b_store_domain_t;

/* NOTE: 记录是按逻辑域原子替换的不透明序列化数据。配置层负责编码模式、
 * 修订号和校验和；不得直接持久化原生 C 结构体。成功表示数据已持久化；
 * 保存或擦除失败时保留旧记录。掉电后必须保留完整的旧记录或新记录。 */
typedef struct {
    void *ctx;
    /* NOTE: BUFFER_TOO_SMALL 返回所需容量且不修改缓冲区；其他失败返回大小 0。
     * buffer 为 NULL 且容量为 0 时表示仅探测所需容量。 */
    gs232b_error_t (*load)(void *ctx, gs232b_store_domain_t domain,
                           uint8_t *buffer, size_t capacity, size_t *size);
    gs232b_error_t (*save)(void *ctx, gs232b_store_domain_t domain,
                           const uint8_t *record, size_t size);
    gs232b_error_t (*erase)(void *ctx, gs232b_store_domain_t domain);
    /* NOTE: 可选的上一完整槽读取。当前槽损坏时由版本化记录层调用；没有
     * 双槽能力的旧适配器可留空。放在旧字段之后以保持位置初始化兼容。 */
    gs232b_error_t (*load_previous)(void *ctx, gs232b_store_domain_t domain,
                                    uint8_t *buffer, size_t capacity, size_t *size);
} gs232b_store_t;

gs232b_error_t gs232b_store_load(const gs232b_store_t *store,
                                gs232b_store_domain_t domain,
                                uint8_t *buffer, size_t capacity, size_t *size);
gs232b_error_t gs232b_store_load_previous(const gs232b_store_t *store,
                                         gs232b_store_domain_t domain,
                                         uint8_t *buffer, size_t capacity,
                                         size_t *size);
gs232b_error_t gs232b_store_save(const gs232b_store_t *store,
                                gs232b_store_domain_t domain,
                                const uint8_t *record, size_t size);
gs232b_error_t gs232b_store_erase(const gs232b_store_t *store,
                                 gs232b_store_domain_t domain);

/* NOTE: 字节流不做文本转换、自动补 CR、解析或广播。每次操作都针对活动连接
 * 校验完整会话键。接收返回非空前缀或 WOULD_BLOCK，绝不等待输入。发送将完整
 * 字节范围复制到有界的每会话 FIFO；失败时不接收任何字节。成功表示已入队，
 * 不表示已经发送。关闭连接会丢弃其待发送字节。生命周期、所有者丢失、停止
 * 优先级、命令接纳和 epoch 变更由上层处理；传输回调不得直接驱动运动。 */
typedef struct {
    void *ctx;
    gs232b_error_t (*receive)(void *ctx, gs232b_session_key_t session,
                              uint8_t *buffer, size_t capacity, size_t *size);
    gs232b_error_t (*send)(void *ctx, gs232b_session_key_t session,
                           const uint8_t *bytes, size_t size);
    gs232b_error_t (*close)(void *ctx, gs232b_session_key_t session);
} gs232b_session_io_t;

gs232b_error_t gs232b_session_receive(const gs232b_session_io_t *io,
                                     gs232b_session_key_t session,
                                     uint8_t *buffer, size_t capacity, size_t *size);
gs232b_error_t gs232b_session_send(const gs232b_session_io_t *io,
                                  gs232b_session_key_t session,
                                  const uint8_t *bytes, size_t size);
gs232b_error_t gs232b_session_close(const gs232b_session_io_t *io,
                                   gs232b_session_key_t session);

#endif
