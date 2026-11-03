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

export function validateSsid(ssid) {
  return typeof ssid === 'string' && !ssid.includes('\0') &&
    new TextEncoder().encode(ssid).length >= 1 && new TextEncoder().encode(ssid).length <= 32;
}

/** 校验个人网络口令；WPA3 SAE 不使用 64 位原始 PSK。 */
export function validatePassword(password, security = 'auto') {
  if (security === 'wpa3-personal') return /^[\x20-\x7e]{1,63}$/.test(password);
  return /^[\x20-\x7e]{8,63}$/.test(password) || /^[0-9a-fA-F]{64}$/.test(password);
}

function ipv4(text) {
  if (!/^(0|[1-9]\d{0,2})(\.(0|[1-9]\d{0,2})){3}$/.test(text)) throw new Error('invalidIp');
  const parts = text.split('.').map(Number);
  if (parts.some(part => part > 255)) throw new Error('invalidIp');
  return parts.reduce((value, part) => ((value << 8) | part) >>> 0, 0);
}

export function ipv4Configuration(mode, address, netmask, gateway) {
  if (mode === 'dhcp') return { mode };
  if (mode !== 'static') throw new Error('invalidIp');
  const ip = ipv4(address), mask = ipv4(netmask), route = gateway ? ipv4(gateway) : 0;
  const suffix = (~mask) >>> 0;
  const host = value => (value >>> 24) !== 0 && (value >>> 24) !== 127 &&
    (value >>> 24) < 224 && ((value & suffix) >>> 0) !== 0 && ((value & suffix) >>> 0) !== suffix;
  if (!mask || suffix < 3 || (suffix & (suffix + 1)) !== 0 || !host(ip) ||
      (route && (!host(route) || route === ip || (route & mask) !== (ip & mask)))) throw new Error('invalidSubnet');
  return { mode, address, netmask, gateway: gateway || '0.0.0.0' };
}
