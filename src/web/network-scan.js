/**
 * @param {{command(command: string): Promise<object>}} channel 串行请求通道。
 * @returns {Promise<{aps: object[], truncated: boolean}>} 同一次扫描的完整分页结果。
 * @throws {Error} 扫描 ID、游标或总数不一致时拒绝混合或不完整结果。
 */
export async function fetchNetworks(channel) {
  const aps = [];
  let scanId, total, truncated, cursor = 0;
  while (true) {
    const reply = await channel.command(scanId === undefined ? 'PROV SCAN' :
      `PROV SCAN ${JSON.stringify({ scan_id: scanId, cursor })}`);
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
