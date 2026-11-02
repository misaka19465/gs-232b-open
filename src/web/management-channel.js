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

export class ManagementChannel {
  constructor(onReady, onClose) {
    this.onReady = onReady;
    this.onClose = onClose;
    this.queue = Promise.resolve();
    this.connect();
  }

  get ready() { return this.socket?.readyState === WebSocket.OPEN; }

  connect() {
    const socket = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws/manage`);
    this.socket = socket;
    socket.binaryType = 'arraybuffer';
    const decoder = new TextDecoder();
    let buffer = '';
    socket.onopen = () => this.onReady();
    socket.onmessage = ({ data }) => {
      buffer += typeof data === 'string' ? data : decoder.decode(data, { stream: true });
      if (buffer.length > 65536) { socket.close(); return; }
      let end;
      while ((end = buffer.indexOf('\n')) >= 0) {
        const line = buffer.slice(0, end).trim();
        buffer = buffer.slice(end + 1);
        if (!line.startsWith('!')) continue;
        let reply;
        try { reply = JSON.parse(line.slice(1)); }
        catch { continue; }
        const pending = this.pending;
        if (!pending) continue;
        this.pending = null;
        clearTimeout(pending.timer);
        if (reply.ok) pending.resolve(reply.data);
        else pending.reject(reply);
      }
    };
    socket.onclose = () => {
      if (this.pending) {
        clearTimeout(this.pending.timer);
        this.pending.reject(new Error('disconnected'));
        this.pending = null;
      }
      this.onClose();
      setTimeout(() => this.connect(), 1500);
    };
  }

  // NOTE: 状态轮询、扫描和写事务共用单请求队列，避免 FIFO 响应错配。
  command(command) {
    const result = this.queue.then(() => new Promise((resolve, reject) => {
      if (!this.ready) { reject(new Error('disconnected')); return; }
      const timer = setTimeout(() => {
        this.socket.close();
        reject(new Error('timeout'));
      }, 15000);
      this.pending = { resolve, reject, timer };
      this.socket.send(`!${command}\r`);
    }));
    this.queue = result.catch(() => {});
    return result;
  }
}
