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

import { msg, str } from '@lit/localize';

export default {
  title: msg('连接到 Wi-Fi'),
  introduction: msg('选择要连接的网络'),
  networks: msg('可用网络'),
  networkCount: ({ count }) => msg(str`${count} 个网络`),
  encrypted: msg('需要密码'),
  open: msg('开放网络'),
  other: msg('其他网络…'),
  otherDescription: msg('手动添加网络'),
  refresh: msg('刷新'),
  scanning: msg('正在扫描附近网络'),
  empty: msg('未发现可用网络，请刷新或手动添加'),
  offline: msg('正在连接设备，请保持设备热点连接'),
  ready: msg('已连接设备，可以开始配网'),
  disconnected: msg('连接已断开，正在重连；未确认的配置将自动回滚'),
  scanFailed: msg('扫描失败，请刷新后重试'),
  scanTruncated: msg('附近网络较多，当前仅显示信号较强的 64 个，可手动添加其他网络'),
  otherTitle: msg('添加其他网络'),
  otherHint: msg('输入网络名称和密钥，然后继续设置连接方式'),
  ssid: msg('网络名称（SSID）'),
  key: msg('密钥'),
  keyHint: msg('开放网络请留空'),
  password: msg('密码'),
  passwordHint: msg('请输入此 Wi-Fi 的密码'),
  openHint: msg('此网络无需密码'),
  confirmOther: msg('确定'),
  connect: msg('连接'),
  connecting: msg('正在连接'),
  cancel: msg('取消'),
  advanced: msg('高级设置…'),
  collapseAdvanced: msg('收起高级设置'),
  ipMode: msg('IP 获取方式'),
  dhcp: msg('自动获取（DHCP）'),
  static: msg('静态 IP'),
  ipAddress: msg('IP 地址'),
  netmask: msg('子网掩码'),
  gateway: msg('网关（可选）'),
  gatewayHint: msg('仅在需要跨网段通信时填写'),
  invalidSsid: msg('网络名称须为 1–32 个 UTF-8 字节，且不能包含空字符'),
  invalidPassword: msg('请输入 8–63 位可打印 ASCII 字符密码，或 64 位十六进制密钥'),
  invalidIp: msg('请输入有效的 IPv4 地址'),
  invalidSubnet: msg('请检查子网掩码和网关，地址须为同一网段内的有效主机地址'),
  trialTitle: msg('确认本次连接'),
  trialConnecting: msg('正在等待 Wi-Fi 连接；未确认将自动恢复原配置'),
  trialReady: ({ ssid, address }) => msg(str`已连接 ${ssid} · ${address}`),
  remaining: ({ seconds }) => msg(str`请在 ${seconds} 秒内确认，否则将自动恢复原配置`),
  save: msg('确认保存'),
  restore: msg('恢复原配置'),
  saved: msg('网络配置已保存'),
  restored: msg('已恢复原配置'),
  expired: msg('试连接已结束，请重新选择网络'),
  rollingBack: msg('正在恢复原配置，请稍候'),
  failed: msg('配置失败，请检查网络和设置后重试'),
  notReady: msg('尚未连接成功，请稍后再确认'),
  busy: msg('已有配置正在处理，请稍后重试'),
  requestFailed: msg('设备未完成本次请求，请重试'),
  timeout: msg('设备响应超时，正在重新连接'),
};
