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

/** 将 ESP-IDF 6.1 扫描认证值转换为支持的个人认证策略；其余返回 null。 */
export function securityForAuth(auth) {
  switch (auth) {
    case 0: return 'open';
    case 2: return 'wpa-personal';
    case 3:
    case 7: return 'wpa2-personal';
    case 4: return 'auto';
    case 6: return 'wpa3-personal';
    default: return null;
  }
}

/**
 * 根据手动输入的口令选择连接策略；短于 8 字节的口令只适用于 SAE。
 * @param {string} password 用户输入的口令，调用方另行校验字符与长度。
 * @returns {string} 开放、自动个人认证或 WPA3 连接策略。
 */
export function securityForPassword(password) {
  if (!password) return 'open';
  return password.length < 8 ? 'wpa3-personal' : 'auto';
}
