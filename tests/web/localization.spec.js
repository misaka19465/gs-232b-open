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
    await expect(page.locator('#networks .network-row')).toHaveCount(4);
    expect(requests.every(url => url.startsWith('http://127.0.0.1:8766/'))).toBe(true);
    expect(errors).toEqual([]);
  });
}

test.describe('English locale workflows', () => {
  test.use({ locale: 'en-US' });

  test('validation, advanced settings, trial feedback, and saving work in the English locale', async ({ page }) => {
    const state = await mockDevice(page, { ready: false });
    await page.goto('/provisioning.html');
    await page.locator('#networks .network-row').first().click();
    await input(page, 'password').fill('short');
    await page.locator('#join').click();
    await expect(page.locator('#dialog-error')).toBeVisible();
    await expect(page.locator('#dialog-error')).toHaveAttribute('data-text', 'invalidPassword');
    await input(page, 'password').fill('correct-password');
    await selectStatic(page);
    await expect(page.locator('#advanced')).toHaveAttribute('aria-expanded', 'true');
    await expect(page.locator('#static-fields')).toBeVisible();
    await input(page, 'ip-address').fill('192.168.1.0');
    await page.locator('#join').click();
    await expect(page.locator('#dialog-error')).toBeVisible();
    await expect(page.locator('#dialog-error')).toHaveAttribute('data-text', 'invalidSubnet');
    expect(state.commands).not.toContain('CFG BEGIN');
    await input(page, 'ip-address').fill('192.168.1.64');
    await page.locator('#join').click();
    await expect(page.locator('#trial-card')).toBeVisible();
    await expect(page.locator('#trial-status')).toHaveAttribute('data-text', 'trialConnecting');
    await expect(page.locator('#save')).toHaveJSProperty('disabled', true);
    state.ready = true;
    await expect(page.locator('#save')).toHaveJSProperty('disabled', false);
    await page.locator('#save').click();
    await expect(page.locator('#trial-card')).toBeHidden();
    expect(state.commands.filter(command => command.startsWith('CFG CONFIRM'))).toHaveLength(1);
  });

  test('empty and failed scans recover and manual entry validates in the English locale', async ({ page }) => {
    const state = await mockDevice(page, { aps: [] });
    await page.goto('/provisioning.html');
    await expect(page.locator('#scan-message')).toHaveAttribute('data-text', 'empty');
    await expect(page.locator('#networks .network-row')).toHaveCount(0);
    state.fail = 'PROV SCAN';
    await page.locator('#refresh').click();
    await expect(page.locator('#scan-message')).toHaveAttribute('data-text', 'scanFailed');
    state.fail = null;
    state.aps = [{ ssid: 'Only Network', rssi: -40, auth: 0 }];
    await page.locator('#refresh').click();
    await expect(page.locator('#networks .network-row')).toHaveCount(state.aps.length);
    await expect(page.locator('#scan-status')).toBeHidden();
    await page.locator('#other-network').click();
    await expect(input(page, 'manual-ssid')).toBeVisible();
    await expect(input(page, 'manual-key')).toBeVisible();
    await page.locator('#join').click();
    await expect(page.locator('#dialog-error')).toBeVisible();
    await expect(page.locator('#dialog-error')).toHaveAttribute('data-text', 'invalidSsid');
    expect(state.commands).not.toContain('CFG BEGIN');
  });
});

test('language changes preserve the current dialog, form values, and device work', async ({ page }) => {
  const state = await mockDevice(page);
  const ssid = 'My Network', password = 'correct-password', address = '192.168.1.64';
  await page.goto('/provisioning.html');
  await page.locator('#other-network').click();
  await input(page, 'manual-ssid').fill(ssid);
  const invalidKey = 'z'.repeat(64);
  await input(page, 'manual-key').fill(invalidKey);
  await page.locator('#join').click();
  await expect(page.locator('#dialog-error')).toBeVisible();
  await expect(page.locator('#dialog-error')).toHaveAttribute('data-text', 'invalidPassword');
  const commands = [...state.commands];
  await page.evaluate(browserLanguages, { languages: ['en-US', 'zh-CN'] });
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
  await expect(page.locator('#network-dialog')).toHaveAttribute('open', '');
  await expect(page.locator('#dialog-error')).toBeVisible();
  await expect(page.locator('#dialog-error')).toHaveAttribute('data-text', 'invalidPassword');
  await expect(input(page, 'manual-ssid')).toHaveValue(ssid);
  await expect(input(page, 'manual-key')).toHaveValue(invalidKey);
  expect(state.commands).toEqual(commands);
  await input(page, 'manual-key').fill(password);
  await page.locator('#join').click();
  await selectStatic(page);
  await input(page, 'ip-address').fill(address);
  await page.evaluate(browserLanguages, { languages: ['zh-CN'] });
  await expect(page.locator('html')).toHaveAttribute('lang', 'zh-CN');
  await expect(page.locator('#dialog-heading')).toHaveText(ssid);
  await expect(page.locator('#advanced')).toHaveAttribute('aria-expanded', 'true');
  await expect(page.locator('#ip-mode')).toHaveJSProperty('value', 'static');
  await expect(input(page, 'ip-address')).toHaveValue(address);
  await expect(input(page, 'password')).toHaveValue(password);
  await page.locator('#join').click();
  await expect(page.locator('#trial-card')).toBeVisible();
  await expect(page.locator('#save')).toHaveJSProperty('disabled', false);
  await page.evaluate(browserLanguages, { languages: ['en'] });
  await expect(page.locator('html')).toHaveAttribute('lang', 'en');
  await expect(page.locator('#trial-card')).toBeVisible();
  await expect(page.locator('#save')).toHaveJSProperty('disabled', false);
  expect(state.candidate.wifi).toMatchObject({ ssid, password, address });
  expect(state.commands.filter(command => command.startsWith('CFG APPLY'))).toHaveLength(1);
  await page.locator('#restore').click();
  await expect(page.locator('#trial-card')).toBeHidden();
  expect(state.commands).toContain('CFG ABORT {"trial_id":1}');
  await page.evaluate(browserLanguages, { languages: ['zh'] });
  await expect(page.locator('html')).toHaveAttribute('lang', 'zh-CN');
  await expect(page.locator('#trial-card')).toBeHidden();
  expect(state.commands.filter(command => command.startsWith('CFG APPLY'))).toHaveLength(1);
  expect(state.commands.filter(command => command.startsWith('CFG ABORT'))).toHaveLength(1);
});
