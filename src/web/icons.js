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

// NOTE: 内联 Material 图形，避免图标字体或 CDN 在设备热点下不可达。
const paths = {
  wifi: 'M1 9l2 2c4.97-4.97 13.03-4.97 18 0l2-2C16.93 2.93 7.08 2.93 1 9Zm8 8 3 3 3-3a4.246 4.246 0 0 0-6 0Zm-4-4 2 2c2.76-2.76 7.24-2.76 10 0l2-2c-3.86-3.86-10.13-3.86-14 0Z',
  lock: 'M18 8h-1V6a5 5 0 0 0-10 0v2H6a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V10a2 2 0 0 0-2-2ZM9 6a3 3 0 0 1 6 0v2H9Zm3 11a2 2 0 1 1 0-4 2 2 0 0 1 0 4Z',
  add: 'M11 5h2v6h6v2h-6v6h-2v-6H5v-2h6Z',
  chevron: 'm9 6-1.4 1.4 4.6 4.6-4.6 4.6L9 18l6-6Z',
  expand: 'm7.4 8.6 4.6 4.6 4.6-4.6L18 10l-6 6-6-6Z',
  refresh: 'M17.7 6.3A8 8 0 1 0 20 12h-2a6 6 0 1 1-1.8-4.2L13 11h8V3Z',
  close: 'M19 6.41 17.59 5 12 10.59 6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 12 13.41 17.59 19 19 17.59 13.41 12Z',
};

/** @returns {SVGElement} 无障碍树中隐藏的 Material 图标，可指定 Web Component 插槽。 */
export function icon(name, slot) {
  const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  if (slot) svg.setAttribute('slot', slot);
  const path = document.createElementNS(svg.namespaceURI, 'path');
  path.setAttribute('d', paths[name]);
  svg.append(path);
  return svg;
}
