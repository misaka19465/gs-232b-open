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

/**
 * @param {{command(command: string): Promise<object>}} channel 串行请求通道。
 * @returns {Promise<{aps: object[], truncated: boolean}>} 同一次扫描的完整分页结果。
 * @throws {Error} 扫描 ID、游标或总数不一致时拒绝混合或不完整结果。
 */
export async function fetchNetworks(channel) {
  const aps = [];
  const deadline = Date.now() + 15000;
  let scanId, total, truncated, cursor = 0;
  while (true) {
    let reply;
    try {
      reply = await channel.command(scanId === undefined ? 'PROV SCAN' :
        `PROV SCAN ${JSON.stringify({ scan_id: scanId, cursor })}`);
    } catch (error) {
      // NOTE: 板级扫描在网络工作任务执行；只轮询尚未取得 ID 的首块，保持分页快照一致。
      if (scanId !== undefined || !['WOULD_BLOCK', 'BUSY'].includes(error?.error) || Date.now() >= deadline)
        throw error;
      await new Promise(resolve => setTimeout(resolve, 100));
      continue;
    }
    if (!Number.isInteger(reply.scan_id) || reply.scan_id < 1 || reply.scan_id > 0xffffffff ||
        !Number.isInteger(reply.total) || reply.total < 0 || reply.total > 64 ||
        reply.offset !== cursor || !Array.isArray(reply.aps) || reply.aps.length > 8 ||
        typeof reply.truncated !== 'boolean') throw new Error('invalidScanPage');
    if (scanId === undefined) {
      scanId = reply.scan_id; total = reply.total; truncated = reply.truncated;
    } else if (reply.scan_id !== scanId || reply.total !== total || reply.truncated !== truncated) {
      throw new Error('changedScanSnapshot');
    }
    const end = cursor + reply.aps.length;
    if (end > total || (reply.next === null ? end !== total :
      !reply.aps.length || reply.next !== end || end >= total)) throw new Error('invalidScanCursor');
    aps.push(...reply.aps);
    if (reply.next === null) return { aps, truncated };
    cursor = reply.next;
  }
}
