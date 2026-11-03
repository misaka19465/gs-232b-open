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

import { test, expect } from '@playwright/test';
import { mockDevice, input, selectStatic } from './device-fixture.js';

function browserLanguages({ languages, language = languages[0] || 'en-US' }) {
  Object.defineProperty(navigator, 'languages', { configurable: true, get: () => languages });
  Object.defineProperty(navigator, 'language', { configurable: true, get: () => language });
  window.dispatchEvent(new Event('languagechange'));
}

for (const { languages, language, expected } of [
  { languages: ['zh-Hans-CN', 'en-US'], expected: 'zh-CN' },
  { languages: ['en-GB', 'zh-CN'], expected: 'en' },
  { languages: ['de-DE', 'zh-TW', 'en'], expected: 'zh-CN' },
  { languages: ['fr-FR'], expected: 'en' },
  { languages: [], language: 'zh-CN', expected: 'zh-CN' },
  { languages: [], language: '', expected: 'en' },
]) {
  test(`browser language preferences ${JSON.stringify(languages)} / ${language ?? ''} select ${expected}`, async ({ page }) => {
    await page.addInitScript(browserLanguages, { languages, language });
    await mockDevice(page);
    const requests = [], errors = [];
    page.on('request', request => requests.push(request.url()));
    page.on('pageerror', error => errors.push(error.message));
    await page.goto('/provisioning.html');
    await expect(page.locator('html')).toHaveAttribute('lang', expected);
    const title = expected === 'en' ? 'Connect to Wi-Fi' : '连接到 Wi-Fi';
    await expect(page.locator('h1')).toHaveText(title);
    await expect(page).toHaveTitle(`GS-232B · ${title}`);
    await expect(page.locator('#networks .network-row')).toHaveCount(4);
    await expect(page.locator('#network-count')).toHaveText(expected === 'en' ? '4 networks' : '4 个网络');
    expect(requests.every(url => url.startsWith('http://127.0.0.1:8766/'))).toBe(true);
    expect(errors).toEqual([]);
  });
}

test.describe('English provisioning', () => {
  test.use({ locale: 'en-US' });

  test('translates validation, advanced settings, trial feedback, and saving', async ({ page }) => {
    const state = await mockDevice(page, { ready: false });
    await page.goto('/provisioning.html');
    await page.locator('#networks .network-row').first().click();
    await expect(page.locator('#close-dialog')).toHaveAttribute('aria-label', 'Cancel');
    await expect(page.locator('#password')).toHaveAttribute('label', 'Password');
    await expect(page.locator('#dialog-description')).toHaveText('Enter the password for this Wi-Fi network');
    await input(page, 'password').fill('short');
    await page.locator('#join').click();
    await expect(page.locator('#dialog-error')).toHaveText('Enter 8–63 printable ASCII characters or a 64-digit hexadecimal key.');
    await input(page, 'password').fill('correct-password');
    await selectStatic(page);
    await expect(page.locator('#ip-mode')).toHaveAttribute('label', 'IP assignment');
    await expect(page.locator('#advanced')).toContainText('Hide advanced settings');
    await expect(page.locator('#gateway')).toHaveAttribute('helper', 'Only needed to communicate outside this subnet');
    await input(page, 'ip-address').fill('192.168.1.0');
    await page.locator('#join').click();
    await expect(page.locator('#dialog-error')).toContainText('valid host addresses in the same subnet');
    await input(page, 'ip-address').fill('192.168.1.64');
    await page.locator('#join').click();
    await expect(page.locator('#trial-status')).toContainText('Waiting for Wi-Fi.');
    await expect(page.locator('#trial-remaining')).toHaveText(/Confirm within \d+ seconds or previous settings will be restored automatically\./);
    state.ready = true;
    await expect(page.locator('#trial-status')).toHaveText('Connected to Radio House · 192.168.1.64');
    await page.locator('#save').click();
    await expect(page.locator('#status')).toHaveText('Network settings saved');
    expect(state.commands.filter(command => command.startsWith('CFG CONFIRM'))).toHaveLength(1);
  });

  test('translates empty and failed scans, manual entry, and singular network counts', async ({ page }) => {
    const state = await mockDevice(page, { aps: [] });
    await page.goto('/provisioning.html');
    await expect(page.locator('#network-count')).toHaveText('0 networks');
    await expect(page.locator('#scan-message')).toHaveText('No networks found. Refresh or add a network manually.');
    state.fail = 'PROV SCAN';
    await page.locator('#refresh').click();
    await expect(page.locator('#scan-message')).toHaveText('Scan failed. Refresh to try again.');
    state.fail = null;
    state.aps = [{ ssid: 'Only Network', rssi: -40, auth: 0 }];
    await page.locator('#refresh').click();
    await expect(page.locator('#network-count')).toHaveText('1 network');
    await expect(page.locator('#networks .network-row')).toHaveAttribute('description', 'Open network');
    await page.locator('#other-network').click();
    await expect(page.locator('#dialog-heading')).toHaveText('Add another network');
    await expect(page.locator('#manual-ssid')).toHaveAttribute('label', 'Network name (SSID)');
    await expect(page.locator('#manual-key')).toHaveAttribute('helper', 'Leave blank for an open network');
    await expect(page.locator('#join')).toHaveText('Continue');
    await page.locator('#join').click();
    await expect(page.locator('#dialog-error')).toContainText('1–32 UTF-8 bytes');
  });
});

test('language changes update the current dialog and trial without resetting form values or device work', async ({ page }) => {
  const state = await mockDevice(page);
  await page.goto('/provisioning.html');
  await page.locator('#other-network').click();
  await input(page, 'manual-ssid').fill('My Network');
  await input(page, 'manual-key').fill('short');
  await page.locator('#join').click();
  await expect(page.locator('#dialog-error')).toContainText('8–63');
  const commands = [...state.commands];
  await page.evaluate(browserLanguages, { languages: ['en-US', 'zh-CN'] });
  await expect(page.locator('.network-row').filter({ hasText: 'Radio House' })).toHaveAttribute('description', 'Password required');
  await expect(page.locator('#dialog-heading')).toHaveText('Add another network');
  await expect(page.locator('#dialog-error')).toContainText('printable ASCII');
  await expect(page.locator('#join')).toHaveText('Continue');
  await expect(input(page, 'manual-ssid')).toHaveValue('My Network');
  await expect(input(page, 'manual-key')).toHaveValue('short');
  expect(state.commands).toEqual(commands);
  await input(page, 'manual-key').fill('correct-password');
  await page.locator('#join').click();
  await selectStatic(page);
  await input(page, 'ip-address').fill('192.168.1.64');
  await page.evaluate(browserLanguages, { languages: ['zh-CN'] });
  await expect(page.locator('.network-row').filter({ hasText: 'Radio House' })).toHaveAttribute('description', '需要密码');
  await expect(page.locator('#dialog-heading')).toHaveText('My Network');
  await expect(page.locator('#join')).toHaveText('连接');
  await expect(page.locator('#advanced')).toContainText('收起高级设置');
  await expect(page.locator('#ip-mode')).toHaveJSProperty('value', 'static');
  await expect(page.locator('#ip-mode')).toHaveAttribute('label', 'IP 获取方式');
  await expect(input(page, 'ip-address')).toHaveValue('192.168.1.64');
  await expect(input(page, 'password')).toHaveValue('correct-password');
  await page.locator('#join').click();
  await expect(page.locator('#trial-status')).toHaveText('已连接 My Network · 192.168.1.64');
  await expect(page.locator('#trial-remaining')).toContainText('秒内确认');
  await page.evaluate(browserLanguages, { languages: ['en'] });
  await expect(page.locator('#trial-status')).toHaveText('Connected to My Network · 192.168.1.64');
  await expect(page.locator('#trial-remaining')).toContainText('seconds');
  await expect(page.locator('#restore')).toHaveText('Restore previous settings');
  expect(state.commands.filter(command => command.startsWith('CFG APPLY'))).toHaveLength(1);
  await page.locator('#restore').click();
  await expect(page.locator('#status')).toHaveText('Previous settings restored');
  await page.evaluate(browserLanguages, { languages: ['zh'] });
  await expect(page.locator('#status')).toHaveText('已恢复原配置');
});
