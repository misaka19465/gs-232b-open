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

#ifndef GS232B_NETWORK_CONFIG_H
#define GS232B_NETWORK_CONFIG_H

#include "gs232b/ethernet.h"
#include "gs232b/ports.h"
#include "gs232b/wifi_sta.h"

#define GS232B_CONFIG_IDLE_TIMEOUT_US UINT64_C(60000000)
#define GS232B_CONFIG_TRIAL_MIN_SECONDS 30U
#define GS232B_CONFIG_TRIAL_MAX_SECONDS 600U

typedef struct {
    /* NOTE: DHCP 模式仍保留静态字段供后续编辑；实际 DHCP 驱动配置须使用零 IPv4。 */
    gs232b_ethernet_config_t eth;
    gs232b_wifi_sta_config_t wifi;
} gs232b_net_config_t;

typedef struct {
    bool eth;
    bool wifi;
} gs232b_net_changes_t;

/** @brief 可选的只读运行快照；回调须有界、不重入，不返回任何明文凭据。 */
typedef struct {
    void *ctx;
    gs232b_error_t (*snapshot)(void *ctx, gs232b_ethernet_snapshot_t *eth,
        gs232b_wifi_sta_snapshot_t *wifi);
} gs232b_net_status_port_t;

/** @brief 请求运行时切换完整网络配置；OK/WOULD_BLOCK 表示整个请求已被接纳。 */
typedef struct {
    void *ctx;
    gs232b_error_t (*apply)(void *ctx, const gs232b_net_config_t *config);
} gs232b_net_apply_port_t;

typedef enum {
    GS232B_CONFIG_TRIAL_NONE = 0,
    GS232B_CONFIG_TRIAL_ACTIVE,
    GS232B_CONFIG_TRIAL_ROLLBACK_PENDING
} gs232b_config_trial_state_t;

typedef struct {
    gs232b_config_trial_state_t state;
    gs232b_net_config_t config;
    uint32_t id;
    uint32_t timeout_s;
    uint64_t base_revision;
    gs232b_time_us_t started_us;
    gs232b_time_us_t deadline_us;
} gs232b_config_trial_t;

/* NOTE: 以下布局只供静态分配；所有操作须与 dispatcher 在同一控制任务串行执行。 */
typedef struct {
    gs232b_clock_t clock;
    gs232b_net_config_t active;
    gs232b_net_config_t candidate;
    uint64_t active_revision;
    uint64_t base_revision;
    gs232b_session_key_t session;
    gs232b_time_us_t renewed_us;
    uint32_t last_id;
    uint32_t candidate_id;
    uint32_t last_trial_id;
    uint32_t last_confirmed_trial_id;
    gs232b_config_trial_t trial;
} gs232b_config_transaction_t;

/** @brief 完整校验 ETH 静态网络及 WPA2 凭据；DHCP 忽略保留的静态字段。 */
bool gs232b_net_config_valid(const gs232b_net_config_t *config);
/** @brief 按实际字段比较接口变更，不比较结构体填充字节。 */
gs232b_net_changes_t gs232b_net_config_changes(const gs232b_net_config_t *active,
    const gs232b_net_config_t *candidate);
/** @brief 初始化非活动事务，复制已确认配置及 revision；NULL active 使用 DHCP/禁用 STA。 */
gs232b_error_t gs232b_config_transaction_init(gs232b_config_transaction_t *transaction,
    gs232b_clock_t clock, const gs232b_net_config_t *active, uint64_t revision);
/** @brief 判断候选是否尚在 60 秒期限内；时钟倒退也使候选过期，无副作用。 */
bool gs232b_config_transaction_live(const gs232b_config_transaction_t *transaction);
/** @brief 清除过期候选及凭据；不能续期，且不修改 active。 */
void gs232b_config_transaction_expire(gs232b_config_transaction_t *transaction);
/** @brief 关闭完整会话键对应的候选；复用同一槽位的新连接不继承事务。 */
void gs232b_config_transaction_close(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session);
/** @brief 复制 active 建立单一事务；并发 BEGIN 返回 CONFLICT，ID 耗尽拒绝回绕。 */
gs232b_error_t gs232b_config_transaction_begin(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session);
/** @brief 检查事务存在、完整会话键及 base revision；只读且不续期。 */
gs232b_error_t gs232b_config_transaction_access(const gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session);
/** @brief 原子替换当前候选并续期；调用者先完成字段校验，本操作不触发网络或存储。 */
gs232b_error_t gs232b_config_transaction_update(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session, const gs232b_net_config_t *candidate);
/** @brief 完整校验候选、输出接口差异并续期；校验失败不续期。 */
gs232b_error_t gs232b_config_transaction_check(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session, gs232b_net_changes_t *changes);
/** @brief 发起者显式放弃候选并清除凭据，不修改 active。 */
gs232b_error_t gs232b_config_transaction_abort(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session);

/** @brief 应用已校验候选，持久化唯一 trial ID，并启动有界试运行期限。 */
gs232b_error_t gs232b_config_trial_apply(gs232b_config_transaction_t *transaction,
    gs232b_session_key_t session, uint32_t candidate_id, uint32_t timeout_s,
    const gs232b_store_t *store, gs232b_net_apply_port_t apply, uint32_t *trial_id);
/** @brief 通过 trial ID 原子持久化配置；最近一次成功确认可安全重复调用。 */
gs232b_error_t gs232b_config_trial_confirm(gs232b_config_transaction_t *transaction,
    uint32_t trial_id, const gs232b_store_t *store, gs232b_net_apply_port_t apply);
/** @brief 放弃匹配的试运行并恢复完整 active；失败时保留可重试的回滚状态。 */
gs232b_error_t gs232b_config_trial_abort(gs232b_config_transaction_t *transaction,
    uint32_t trial_id, gs232b_net_apply_port_t apply);
/** @brief 到期或回滚待重试时恢复 active；失败留在 rollback_pending。 */
gs232b_error_t gs232b_config_trial_tick(gs232b_config_transaction_t *transaction,
    gs232b_net_apply_port_t apply);

#endif
