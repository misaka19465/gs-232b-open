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

#ifndef GS232B_FAKES_H
#define GS232B_FAKES_H

#include "gs232b/ports.h"

/* NOTE: 仅供测试使用，不分配动态内存且单线程运行。每个实例独占自身状态；
 * 不使用全局变量、真实计时器、休眠、ESP-IDF 头文件或外设。 */
#define GS232B_FAKE_OUTPUT_TRACE_CAPACITY 256
#define GS232B_FAKE_STORE_RECORD_CAPACITY 2048
#define GS232B_FAKE_SESSION_CAPACITY 7
#define GS232B_FAKE_SESSION_BUFFER_CAPACITY 4096

typedef struct {
    gs232b_error_t error;
    size_t remaining;
} gs232b_fake_failure_t;

/* NOTE: 接下来的 count 次有效调用失败，之后恢复正常行为。无效调用和容量
 * 检查不会消耗注入的失败次数。 */
gs232b_error_t gs232b_fake_fail_next(gs232b_fake_failure_t *failure,
                                    gs232b_error_t error, size_t count);

typedef struct {
    gs232b_time_us_t now_us;
} gs232b_fake_clock_t;

void gs232b_fake_clock_init(gs232b_fake_clock_t *fake, gs232b_time_us_t start_us);
gs232b_clock_t gs232b_fake_clock_port(gs232b_fake_clock_t *fake);
/* NOTE: 溢出时拒绝推进，且不使时间倒退。 */
gs232b_error_t gs232b_fake_clock_advance(gs232b_fake_clock_t *fake,
                                        gs232b_time_us_t delta_us);

typedef struct {
    gs232b_clock_t clock;
    gs232b_sample_t samples[GS232B_AXIS_COUNT];
    gs232b_fake_failure_t failures[GS232B_AXIS_COUNT];
    size_t read_count[GS232B_AXIS_COUNT];
} gs232b_fake_sampler_t;

gs232b_error_t gs232b_fake_sampler_init(gs232b_fake_sampler_t *fake,
                                       gs232b_clock_t clock);
gs232b_sampler_t gs232b_fake_sampler_port(gs232b_fake_sampler_t *fake);
/* NOTE: 在当前虚拟时间锁存一次采样；读取不会刷新采样。 */
gs232b_error_t gs232b_fake_sampler_set(gs232b_fake_sampler_t *fake,
                                      gs232b_axis_id_t axis, uint16_t raw,
                                      uint32_t adc_uv);

typedef struct {
    gs232b_time_us_t at_us;
    gs232b_output_frame_t frame;
    gs232b_error_t result;
} gs232b_fake_output_event_t;

typedef struct {
    gs232b_clock_t clock;
    gs232b_output_snapshot_t snapshot;
    gs232b_fake_failure_t failure;
    gs232b_fake_output_event_t trace[GS232B_FAKE_OUTPUT_TRACE_CAPACITY];
    size_t trace_count;
} gs232b_fake_output_t;

gs232b_error_t gs232b_fake_output_init(gs232b_fake_output_t *fake,
                                      gs232b_clock_t clock);
gs232b_output_t gs232b_fake_output_port(gs232b_fake_output_t *fake);
/* NOTE: trace 按调用顺序记录成功和注入失败的尝试。trace 满时以
 * RESOURCE_EXHAUSTED 拒绝后续提交，且不会覆盖历史；请在每个观测窗口之间
 * 显式清空。 */
void gs232b_fake_output_clear_trace(gs232b_fake_output_t *fake);

typedef enum {
    GS232B_FAKE_STORE_LOAD = 0,
    GS232B_FAKE_STORE_SAVE,
    GS232B_FAKE_STORE_ERASE,
    /* NOTE: 模拟候选槽已写入但提交标记未切换；旧活动记录保持可读。 */
    GS232B_FAKE_STORE_INTERRUPT,
    GS232B_FAKE_STORE_OPERATION_COUNT
} gs232b_fake_store_operation_t;

typedef struct {
    uint8_t bytes[GS232B_FAKE_STORE_RECORD_CAPACITY];
    size_t size;
    bool present;
    uint8_t previous_bytes[GS232B_FAKE_STORE_RECORD_CAPACITY];
    size_t previous_size;
    bool previous_present;
} gs232b_fake_store_record_t;

typedef struct {
    gs232b_fake_store_record_t records[GS232B_STORE_DOMAIN_COUNT];
    gs232b_fake_failure_t failures[GS232B_STORE_DOMAIN_COUNT]
                                    [GS232B_FAKE_STORE_OPERATION_COUNT];
} gs232b_fake_store_t;

void gs232b_fake_store_init(gs232b_fake_store_t *fake);
gs232b_store_t gs232b_fake_store_port(gs232b_fake_store_t *fake);

typedef enum {
    GS232B_FAKE_SESSION_RECEIVE = 0,
    GS232B_FAKE_SESSION_SEND,
    GS232B_FAKE_SESSION_OPERATION_COUNT
} gs232b_fake_session_operation_t;

typedef struct {
    uint8_t bytes[GS232B_FAKE_SESSION_BUFFER_CAPACITY];
    size_t size;
} gs232b_fake_byte_queue_t;

typedef struct {
    gs232b_session_snapshot_t snapshot;
    gs232b_fake_byte_queue_t rx;
    gs232b_fake_byte_queue_t tx;
    gs232b_fake_failure_t failures[GS232B_FAKE_SESSION_OPERATION_COUNT];
} gs232b_fake_session_slot_t;

typedef struct {
    gs232b_clock_t clock;
    gs232b_fake_session_slot_t slots[GS232B_FAKE_SESSION_CAPACITY];
} gs232b_fake_sessions_t;

gs232b_error_t gs232b_fake_sessions_init(gs232b_fake_sessions_t *fake,
                                        gs232b_clock_t clock);
gs232b_session_io_t gs232b_fake_sessions_port(gs232b_fake_sessions_t *fake);
gs232b_error_t gs232b_fake_sessions_open(gs232b_fake_sessions_t *fake,
                                        gs232b_transport_t transport,
                                        gs232b_session_key_t *session);
/* NOTE: 注入入站字节或取出已接收的出站字节，并保留所有 CR/LF、NUL 和分片。
 * 队列已满时返回 WOULD_BLOCK，且不执行部分写入；空队列读取返回 WOULD_BLOCK
 * 且大小为 0。 */
gs232b_error_t gs232b_fake_sessions_feed(gs232b_fake_sessions_t *fake,
                                        gs232b_session_key_t session,
                                        const uint8_t *bytes, size_t size);
gs232b_error_t gs232b_fake_sessions_take_tx(gs232b_fake_sessions_t *fake,
                                           gs232b_session_key_t session,
                                           uint8_t *buffer, size_t capacity,
                                           size_t *size);
gs232b_error_t gs232b_fake_sessions_fail_next(gs232b_fake_sessions_t *fake,
                                             gs232b_session_key_t session,
                                             gs232b_fake_session_operation_t operation,
                                             gs232b_error_t error, size_t count);
/* NOTE: 会话槽关闭后、再次复用前仍允许读取其快照。 */
gs232b_error_t gs232b_fake_sessions_snapshot(gs232b_fake_sessions_t *fake,
                                            gs232b_session_key_t session,
                                            gs232b_session_snapshot_t *snapshot);

#endif
