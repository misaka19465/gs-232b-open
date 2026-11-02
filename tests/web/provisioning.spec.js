import { test, expect } from '@playwright/test';
import { mockDevice, input, selectStatic } from './device-fixture.js';
import { validateSsid, validatePassword, ipv4Configuration } from '../../src/web/validation.js';

test('network list sorts, deduplicates, ends with Other, and refreshes offline assets', async ({ page }) => {
  const state = await mockDevice(page);
  const requests = [], errors = [];
  page.on('request', request => requests.push(request.url()));
  page.on('pageerror', error => errors.push(error.message));
  await page.goto('/provisioning.html');
  await expect(page.locator('#networks .network-row')).toHaveCount(4);
  await expect(page.locator('#networks .network-row').first()).toContainText('Radio House');
  await expect(page.locator('mdui-list-item').last()).toHaveAttribute('id', 'other-network');
  await expect(page.locator('mdui-list')).toHaveCount(1);
  await page.locator('#refresh').click();
  await expect.poll(() => state.scans).toBe(2);
  expect(requests.every(url => url.startsWith('http://127.0.0.1:8766/'))).toBe(true);
  expect(errors).toEqual([]);
});

test('an empty scan keeps Other available and clears the empty message after refresh', async ({ page }) => {
  const state = await mockDevice(page, { aps: [] });
  await page.goto('/provisioning.html');
  await expect(page.locator('#scan-message')).toContainText('未发现可用网络');
  await expect(page.locator('#other-network')).toBeVisible();
  await expect(page.locator('#networks .network-row')).toHaveCount(0);
  state.aps = [{ ssid: 'Available Again', rssi: -40, auth: 0 }];
  await page.locator('#refresh').click();
  await expect(page.locator('#networks .network-row')).toHaveCount(1);
  await expect(page.locator('#scan-status')).toBeHidden();
  await expect(page.locator('mdui-list-item').last()).toHaveAttribute('id', 'other-network');
});

test('large scan results are fetched in cursor pages from one scan snapshot', async ({ page }) => {
  const aps = Array.from({ length: 21 }, (_, index) => ({
    ssid: `Network ${index + 1}`, rssi: -30 - index, auth: index % 2 ? 3 : 0,
  }));
  const state = await mockDevice(page, { aps });
  await page.goto('/provisioning.html');
  await expect(page.locator('#networks .network-row')).toHaveCount(21);
  expect(state.scans).toBe(1);
  expect(state.commands.filter(command => command.startsWith('PROV SCAN'))).toEqual([
    'PROV SCAN',
    'PROV SCAN {"scan_id":1,"cursor":8}',
    'PROV SCAN {"scan_id":1,"cursor":16}',
  ]);
  await expect(page.locator('#networks .network-row').first()).toContainText('Network 1');
  await expect(page.locator('#networks .network-row').nth(20)).toContainText('Network 21');
});

test('a failed continuation keeps the previous complete list and allows refresh', async ({ page }) => {
  const state = await mockDevice(page);
  await page.goto('/provisioning.html');
  await expect(page.locator('#networks .network-row')).toHaveCount(4);
  state.aps = Array.from({ length: 17 }, (_, index) => ({ ssid: `New ${index}`, rssi: -30 - index, auth: 0 }));
  state.fail = 'PROV SCAN {';
  await page.locator('#refresh').click();
  await expect(page.locator('#scan-message')).toContainText('扫描失败');
  await expect(page.locator('#networks .network-row')).toHaveCount(4);
  await expect(page.locator('#networks')).toContainText('Radio House');
  state.fail = null;
  await page.locator('#refresh').click();
  await expect(page.locator('#networks .network-row')).toHaveCount(17);
  expect(state.scans).toBe(3);
});

test('the device memory limit is visible when more than 64 networks are found', async ({ page }) => {
  const aps = Array.from({ length: 72 }, (_, index) => ({ ssid: `AP ${index}`, rssi: -30 - index, auth: 3 }));
  const state = await mockDevice(page, { aps });
  await page.goto('/provisioning.html');
  await expect(page.locator('#networks .network-row')).toHaveCount(64);
  await expect(page.locator('#scan-message')).toContainText('64');
  await expect(page.locator('#scan-message')).toBeVisible();
  expect(state.scans).toBe(1);
  expect(state.commands.filter(command => command.startsWith('PROV SCAN'))).toHaveLength(8);
});

test('secured network expands advanced DHCP and static fields inside the same dialog', async ({ page }) => {
  const state = await mockDevice(page, { ready: false });
  await page.goto('/provisioning.html');
  await page.locator('#networks .network-row').first().click();
  await expect(input(page, 'password')).toBeVisible();
  await expect(page.locator('#advanced-panel')).toBeHidden();
  const panel = page.locator('#network-dialog').locator('div[part="panel"]');
  await panel.evaluate(element => Promise.all(element.getAnimations().map(animation => animation.finished)));
  const basic = await panel.boundingBox();
  await page.locator('#advanced').click();
  await expect(page.locator('#advanced-panel')).toBeVisible();
  await expect(page.locator('#static-fields')).toBeHidden();
  const dhcp = await panel.boundingBox();
  expect(dhcp.height).toBeGreaterThan(basic.height);
  await page.locator('#ip-mode').click();
  await page.locator('mdui-menu-item[value="static"]').click();
  await expect(page.locator('#static-fields')).toBeVisible();
  const fixed = await panel.boundingBox();
  expect(fixed.height).toBeGreaterThan(dhcp.height);
  await expect(page.locator('mdui-dialog[open]')).toHaveCount(1);
  await expect(page.locator('[id*=dns], [name*=dns]')).toHaveCount(0);
  await input(page, 'password').fill('correct-password');
  await input(page, 'ip-address').fill('192.168.1.64');
  await input(page, 'gateway').fill('192.168.1.1');
  await page.locator('#join').click();
  await expect(page.locator('#trial-card')).toBeVisible();
  expect(state.candidate).toEqual({ wifi: { enabled: true, ssid: 'Radio House', password: 'correct-password',
    mode: 'static', address: '192.168.1.64', netmask: '255.255.255.0', gateway: '192.168.1.1' } });
  await expect(page.locator('#save')).toHaveJSProperty('disabled', true);
  expect(state.commands.some(command => command.startsWith('CFG CONFIRM'))).toBe(false);
  state.ready = true;
  await expect(page.locator('#save')).toHaveJSProperty('disabled', false);
  await page.locator('#save').click();
  await expect(page.locator('#status')).toContainText('网络配置已保存');
  expect(state.commands.filter(command => command.startsWith('CFG CONFIRM'))).toHaveLength(1);
});

test('open network still opens a dialog, hides the password, and sends an empty key', async ({ page }) => {
  const state = await mockDevice(page);
  await page.goto('/provisioning.html');
  await page.locator('#networks .network-row').filter({ hasText: 'Guest Network' }).click();
  await expect(page.locator('#network-dialog')).toHaveAttribute('open', '');
  await expect(input(page, 'password')).toBeHidden();
  await expect(page.locator('#advanced')).toBeVisible();
  await page.locator('#join').click();
  await expect(page.locator('#trial-card')).toBeVisible();
  expect(state.candidate.wifi).toEqual({ enabled: true, ssid: 'Guest Network', password: '', mode: 'dhcp' });
});

test('Other collects SSID and key then returns to the unified password dialog', async ({ page }) => {
  await mockDevice(page);
  await page.goto('/provisioning.html');
  await expect(page.locator('#refresh')).toHaveJSProperty('disabled', false);
  await page.locator('#other-network').click();
  await expect(input(page, 'manual-ssid')).toBeVisible();
  await expect(input(page, 'manual-key')).toBeVisible();
  await expect(page.locator('#advanced')).toBeHidden();
  await input(page, 'manual-ssid').fill('中'.repeat(11));
  await page.locator('#join').click();
  await expect(page.locator('#dialog-error')).toContainText('1–32');
  await input(page, 'manual-ssid').fill('Hidden Station');
  await input(page, 'manual-key').fill('hidden-secret');
  await page.locator('#join').click();
  await expect(page.locator('#dialog-heading')).toHaveText('Hidden Station');
  await expect(input(page, 'password')).toHaveValue('hidden-secret');
  await expect(input(page, 'manual-ssid')).toBeHidden();
  await expect(page.locator('#advanced')).toBeVisible();
  await expect(page.locator('mdui-dialog[open]')).toHaveCount(1);
});

test('Other with an empty key opens the password-free connection dialog', async ({ page }) => {
  const state = await mockDevice(page);
  await page.goto('/provisioning.html');
  await expect(page.locator('#refresh')).toHaveJSProperty('disabled', false);
  await page.locator('#other-network').click();
  await input(page, 'manual-ssid').fill('Hidden Open Network');
  await page.locator('#join').click();
  await expect(page.locator('#dialog-heading')).toHaveText('Hidden Open Network');
  await expect(input(page, 'password')).toBeHidden();
  await page.locator('#join').click();
  await expect(page.locator('#trial-card')).toBeVisible();
  expect(state.candidate.wifi.password).toBe('');
});

test('invalid password and subnet are rejected before starting a candidate', async ({ page }) => {
  const state = await mockDevice(page);
  await page.goto('/provisioning.html');
  await page.locator('#networks .network-row').first().click();
  await input(page, 'password').fill('short');
  await page.locator('#join').click();
  await expect(page.locator('#dialog-error')).toContainText('8–63');
  await input(page, 'password').fill('correct-password');
  await selectStatic(page);
  await input(page, 'ip-address').fill('192.168.1.0');
  await page.locator('#join').click();
  await expect(page.locator('#dialog-error')).toContainText('子网');
  expect(state.commands).not.toContain('CFG BEGIN');
});

test('device validation failure aborts the candidate and permits retry', async ({ page }) => {
  const state = await mockDevice(page, { fail: 'CFG CHECK' });
  await page.goto('/provisioning.html');
  await page.locator('#networks .network-row').first().click();
  await input(page, 'password').fill('correct-password');
  await page.locator('#join').click();
  await expect(page.locator('#dialog-error')).toBeVisible();
  expect(state.commands).toContain('CFG ABORT');
  expect(state.commands.some(command => command.startsWith('CFG APPLY'))).toBe(false);
  state.fail = null;
  await page.locator('#join').click();
  await expect(page.locator('#trial-card')).toBeVisible();
});

test('lost APPLY reply recovers the trial after reconnect without applying or confirming again', async ({ page }) => {
  const state = await mockDevice(page, { dropApply: true });
  await page.goto('/provisioning.html');
  await page.locator('#networks .network-row').first().click();
  await input(page, 'password').fill('correct-password');
  await page.locator('#join').click();
  await expect(page.locator('#trial-card')).toBeVisible({ timeout: 10000 });
  expect(state.commands.filter(command => command.startsWith('CFG APPLY'))).toHaveLength(1);
  expect(state.commands.some(command => command.startsWith('CFG CONFIRM'))).toBe(false);
});

test('an older network reaching ready cannot confirm a different trial network', async ({ page }) => {
  const state = await mockDevice(page, { runtimeSsid: 'Previous Network' });
  await page.goto('/provisioning.html');
  await page.locator('#networks .network-row').first().click();
  await input(page, 'password').fill('correct-password');
  await page.locator('#join').click();
  await expect(page.locator('#trial-card')).toBeVisible();
  await expect(page.locator('#save')).toHaveJSProperty('disabled', true);
  expect(state.commands.some(command => command.startsWith('CFG CONFIRM'))).toBe(false);
});

test('a refreshed page recovers the actual trial.config.wifi protocol shape', async ({ page }) => {
  const state = await mockDevice(page, {
    trial: { state: 'active', trial_id: 8,
      config: { wifi: { enabled: true, ssid: 'Recovered Network', mode: 'dhcp' } } },
    deadline: Date.now() + 60000,
  });
  await page.goto('/provisioning.html');
  await expect(page.locator('#trial-status')).toContainText('Recovered Network');
  await expect(page.locator('#save')).toHaveJSProperty('disabled', false);
  await page.locator('#save').click();
  await expect(page.locator('#status')).toContainText('网络配置已保存');
  expect(state.commands).toContain('CFG CONFIRM {"trial_id":8}');
  expect(state.commands.some(command => command.startsWith('CFG APPLY'))).toBe(false);
});

test('SSID is rendered as text, never markup', async ({ page }) => {
  await mockDevice(page, { aps: [{ ssid: '<img src=x onerror=alert(1)>', rssi: -40, auth: 0 }] });
  await page.goto('/provisioning.html');
  await expect(page.locator('#networks .network-row')).toContainText('<img src=x onerror=alert(1)>');
  await expect(page.locator('#networks img')).toHaveCount(0);
  await page.locator('#networks .network-row').click();
  await expect(page.locator('#dialog-heading')).toHaveText('<img src=x onerror=alert(1)>');
});

test('validators enforce byte lengths, credential formats, and local IPv4 rules', () => {
  expect(validateSsid('中'.repeat(10) + 'ab')).toBe(true);
  expect(validateSsid('中'.repeat(11))).toBe(false);
  expect(validateSsid('bad\0name')).toBe(false);
  expect(validatePassword('abcdef12')).toBe(true);
  expect(validatePassword('a'.repeat(64))).toBe(true);
  expect(validatePassword('z'.repeat(64))).toBe(false);
  expect(ipv4Configuration('dhcp', 'invalid', '', '')).toEqual({ mode: 'dhcp' });
  expect(ipv4Configuration('static', '192.168.1.50', '255.255.255.0', '').gateway).toBe('0.0.0.0');
  for (const address of ['127.0.0.1', '224.0.0.1', '192.168.1.0', '192.168.1.255', '192.168.001.2', '256.1.1.1']) {
    expect(() => ipv4Configuration('static', address, '255.255.255.0', '')).toThrow();
  }
  expect(() => ipv4Configuration('static', '192.168.1.50', '255.0.255.0', '')).toThrow();
  expect(() => ipv4Configuration('static', '192.168.1.50', '255.255.255.0', '192.168.2.1')).toThrow();
});

for (const viewport of [{ width: 360, height: 800 }, { width: 430, height: 932 }, { width: 1280, height: 900 }]) {
  test(`Material You visual states at ${viewport.width}px`, async ({ page }) => {
    await page.setViewportSize(viewport);
    await mockDevice(page);
    const errors = [];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto('/provisioning.html');
    await expect(page.locator('#networks .network-row')).toHaveCount(4);
    const screenshot = async name => {
      expect(await page.evaluate(() => document.documentElement.scrollWidth)).toBe(viewport.width);
      await page.screenshot({ path: test.info().outputPath(`${name}.png`), animations: 'disabled' });
    };
    await screenshot('list');
    await page.locator('#networks .network-row').first().click();
    await expect(input(page, 'password')).toBeVisible();
    await screenshot('password');
    await page.locator('#advanced').click();
    await screenshot('advanced-dhcp');
    await page.locator('#ip-mode').click();
    await page.locator('mdui-menu-item[value="static"]').click();
    await expect(input(page, 'ip-address')).toBeVisible();
    await screenshot('advanced-static');
    await page.locator('#close-dialog').click();
    await expect(page.locator('#network-dialog')).toBeHidden();
    await page.locator('#other-network').click();
    await screenshot('other');
    await page.locator('#close-dialog').click();
    await expect(page.locator('#network-dialog')).toBeHidden();
    await page.locator('#networks .network-row').filter({ hasText: 'Guest Network' }).click();
    await screenshot('open-network');
    expect(errors).toEqual([]);
  });
}
