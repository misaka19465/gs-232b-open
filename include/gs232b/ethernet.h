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

#ifndef GS232B_ETHERNET_H
#define GS232B_ETHERNET_H

#include "gs232b/types.h"

#define GS232B_ETHERNET_DHCP_TIMEOUT_US UINT64_C(30000000)

typedef enum {
    GS232B_ETHERNET_DHCP = 0,
    GS232B_ETHERNET_STATIC
} gs232b_ethernet_mode_t;

/* NOTE: IPv4 均为主机序数值，例如 192.168.1.2 == 0xc0a80102；零 DNS 表示未配置。 */
typedef struct {
    uint32_t address;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t dns[2];
} gs232b_ethernet_ipv4_t;

typedef struct {
    gs232b_ethernet_mode_t mode;
    gs232b_ethernet_ipv4_t ipv4;
} gs232b_ethernet_config_t;

typedef enum {
    GS232B_ETHERNET_LINK_DOWN = 0,
    GS232B_ETHERNET_LINK_UP_NO_IP,
    GS232B_ETHERNET_DHCP_WAIT,
    GS232B_ETHERNET_READY,
    GS232B_ETHERNET_ERROR
} gs232b_ethernet_state_t;

typedef struct {
    gs232b_ethernet_config_t configured;
    gs232b_ethernet_ipv4_t current;
    gs232b_ethernet_state_t state;
    gs232b_error_t last_error;
    uint8_t mac[6];
    bool link_up;
    bool ipv4_valid;
    bool dhcp_running;
    /* NOTE: 本接口失效/地址变化时递增；控制任务须先撤销旧 ETH 会话及所有者，
     * 再接纳新输入。即使两次快照间已重连，也不能继续旧会话或旧动作。 */
    uint64_t session_epoch;
} gs232b_ethernet_snapshot_t;

/** @brief 严格解析四段十进制 IPv4；失败时清零输出，不接受缩写或前导零。 */
gs232b_error_t gs232b_ethernet_parse_ipv4(const char *text, uint32_t *address);

/**
 * @brief 校验静态单播主机、连续 /1../30 掩码、同网段网关及可选 DNS。
 * @note DHCP 配置的 ipv4 字段必须全部为零；不把运行租约写回配置。
 */
bool gs232b_ethernet_config_valid(const gs232b_ethernet_config_t *config);

#endif
