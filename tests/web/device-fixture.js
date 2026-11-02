export async function mockDevice(page, options = {}) {
  const state = {
    commands: [], scans: 0, candidate: null, trial: null, connected: true,
    ready: true, runtimeSsid: null, fail: null, dropApply: false, scanId: 0,
    aps: [
      { ssid: 'Radio House', rssi: -43, auth: 3 },
      { ssid: 'Workshop', rssi: -57, auth: 3 },
      { ssid: 'Guest Network', rssi: -68, auth: 0 },
      { ssid: '家里的 Wi-Fi', rssi: -74, auth: 3 },
      { ssid: 'Radio House', rssi: -80, auth: 3 },
    ], ...options,
  };
  await page.routeWebSocket('**/ws/manage', ws => {
    state.socket = ws;
    ws.onMessage(message => {
      const command = String(message).replace(/^!/, '').trim();
      state.commands.push(command);
      if (state.fail && command.startsWith(state.fail)) {
        ws.send(`!${JSON.stringify({ ok: false, error: { code: 'INVALID_ARGUMENT' } })}\r\n`);
        return;
      }
      let data = {};
      if (command.startsWith('PROV SCAN')) {
        let cursor = 0, scanId = state.scanId;
        if (command === 'PROV SCAN') {
          state.scans++; scanId = ++state.scanId;
          state.scanSnapshot = state.aps.slice(0, 64).map(ap => ({ ...ap }));
          state.scanTruncated = state.aps.length > 64;
        } else ({ cursor, scan_id: scanId } = JSON.parse(command.slice(10)));
        if (scanId !== state.scanId) {
          ws.send(`!${JSON.stringify({ ok: false, error: 'CONFLICT' })}\r\n`);
          return;
        }
        const total = state.scanSnapshot.length, end = Math.min(total, cursor + 8);
        data = { scan_id: scanId, offset: cursor, total, next: end < total ? end : null,
          truncated: state.scanTruncated, aps: state.scanSnapshot.slice(cursor, end) };
      }
      if (command === 'CFG BEGIN') data = { candidate_id: 1 };
      if (command.startsWith('NET SET ')) state.candidate = JSON.parse(command.slice(8));
      if (command.startsWith('CFG APPLY ')) {
        state.trial = { state: 'active', trial_id: 1, config: state.candidate };
        state.deadline = Date.now() + 60000;
        data = { trial_id: 1 };
        if (state.dropApply) { state.dropApply = false; ws.close(); return; }
      }
      if (command === 'NET GET') {
        const wifi = state.trial?.config.wifi;
        data = {
          active: { wifi: { enabled: false, mode: 'dhcp', ssid: '' } },
          trial: state.trial ? { ...state.trial, remaining_us: Math.max(0, state.deadline - Date.now()) * 1000 } : null,
          runtime: { wifi: { state: state.ready ? 'ready' : 'connecting',
            ipv4_valid: state.ready, ssid: state.runtimeSsid ?? wifi?.ssid ?? '',
            address: wifi?.address || '192.168.1.50' } },
        };
      }
      if (command.startsWith('CFG CONFIRM ')) { state.trial = null; data = { confirmed: true }; }
      if (command.startsWith('CFG ABORT')) { state.candidate = null; state.trial = null; }
      ws.send(`!${JSON.stringify({ ok: true, data })}\r\n`);
    });
  });
  return state;
}

export const input = (page, id) => page.locator(`#${id}`).locator('input');

export async function selectStatic(page) {
  await page.locator('#advanced').click();
  await page.locator('#ip-mode').click();
  await page.locator('mdui-menu-item[value="static"]').click();
}
