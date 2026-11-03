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

import { str } from '@lit/localize';

export default {
  title: '连接到 Wi-Fi',
  introduction: '选择要连接的网络',
  networks: '可用网络',
  networkCount: ({ count }) => str`${count} 个网络`,
  networkCountOne: ({ count }) => str`${count} 个网络`,
  encrypted: '需要密码',
  open: '开放网络',
  other: '其他网络…',
  otherDescription: '手动添加网络',
  refresh: '刷新',
  scanning: '正在扫描附近网络',
  empty: '未发现可用网络，请刷新或手动添加',
  offline: '正在连接设备，请保持设备热点连接',
  ready: '已连接设备，可以开始配网',
  disconnected: '连接已断开，正在重连；未确认的配置将自动回滚',
  scanFailed: '扫描失败，请刷新后重试',
  scanTruncated: '附近网络较多，当前仅显示信号较强的 64 个，可手动添加其他网络',
  otherTitle: '添加其他网络',
  otherHint: '输入网络名称和密钥，然后继续设置连接方式',
  ssid: '网络名称（SSID）',
  key: '密钥',
  keyHint: '开放网络请留空',
  password: '密码',
  passwordHint: '请输入此 Wi-Fi 的密码',
  openHint: '此网络无需密码',
  confirmOther: '确定',
  connect: '连接',
  connecting: '正在连接',
  cancel: '取消',
  advanced: '高级设置…',
  collapseAdvanced: '收起高级设置',
  ipMode: 'IP 获取方式',
  dhcp: '自动获取（DHCP）',
  static: '静态 IP',
  ipAddress: 'IP 地址',
  netmask: '子网掩码',
  gateway: '网关（可选）',
  gatewayHint: '仅在需要跨网段通信时填写',
  invalidSsid: '网络名称须为 1–32 个字符，且不能包含空字符',
  invalidPassword: '请输入 8–63 位密码',
  invalidSaePassword: 'WPA3 密码须为 1–63 位',
  invalidIp: '请输入有效的 IPv4 地址',
  invalidSubnet: '请检查子网掩码和网关，地址须为同一网段内的有效主机地址',
  trialTitle: '确认本次连接',
  trialConnecting: '正在等待 Wi-Fi 连接；未确认将自动恢复原配置',
  trialReady: ({ ssid, address }) => str`已连接 ${ssid} · ${address}`,
  remaining: ({ seconds }) => str`请在 ${seconds} 秒内确认，否则将自动恢复原配置`,
  save: '确认保存',
  restore: '恢复原配置',
  saved: '网络配置已保存',
  restored: '已恢复原配置',
  expired: '试连接已结束，请重新选择网络',
  rollingBack: '正在恢复原配置，请稍候',
  failed: '配置失败，请检查网络和设置后重试',
  notReady: '尚未连接成功，请稍后再确认',
  busy: '已有配置正在处理，请稍后重试',
  requestFailed: '设备未完成本次请求，请重试',
  timeout: '设备响应超时，正在重新连接',
};
