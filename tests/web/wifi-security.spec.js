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
import { mockDevice, input } from './device-fixture.js';
import { validatePassword } from '../../src/web/validation.js';

for (const [auth, security] of [
  [2, 'wpa-personal'], [3, 'wpa2-personal'], [4, 'auto'],
  [6, 'wpa3-personal'], [7, 'wpa2-personal'],
]) {
  test(`scan authentication ${auth} joins with ${security}`, async ({ page }) => {
    const state = await mockDevice(page, { aps: [{ ssid: 'Personal network', rssi: -40, auth }] });
    await page.goto('/provisioning.html');
    await page.locator('#networks .network-row').click();
    const password = auth === 6 ? 's' : 'example-password';
    await input(page, 'password').fill(password);
    await page.locator('#join').click();
    await expect(page.locator('#trial-card')).toBeVisible();
    expect(state.candidate.wifi.security).toBe(security);
    expect(state.candidate.wifi.password).toBe(password);
  });
}

test('scan excludes WEP, enterprise, OWE, and unknown authentication', async ({ page }) => {
  const aps = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 14, 15, 16, 17, 99].map(auth => ({
    ssid: `Auth ${auth}`, rssi: -40 - auth, auth,
  }));
  await mockDevice(page, { aps });
  await page.goto('/provisioning.html');
  const supported = aps.filter(ap => [0, 2, 3, 4, 6, 7].includes(ap.auth));
  const rows = page.locator('#networks .network-row');
  await expect(rows).toHaveCount(supported.length);
  for (const [index, ap] of supported.entries()) {
    await expect(rows.nth(index)).toHaveJSProperty('textContent', ap.ssid);
  }
});

for (const [password, security] of [['correct-password', 'auto'], ['short', 'wpa3-personal']]) {
  test(`manual networks handle ${security} without an authentication selector`, async ({ page }) => {
    const state = await mockDevice(page);
    await page.goto('/provisioning.html');
    await expect(page.locator('#refresh')).toBeEnabled();
    await page.locator('#other-network').click();
    await expect(page.locator('#manual-fields mdui-select')).toHaveCount(0);
    await input(page, 'manual-ssid').fill('Hidden Personal');
    await input(page, 'manual-key').fill('initial-password');
    await page.locator('#join').click();
    await expect(input(page, 'password')).toHaveValue('initial-password');
    await input(page, 'password').fill(password);
    await page.locator('#join').click();
    await expect(page.locator('#trial-card')).toBeVisible();
    expect(state.candidate.wifi.security).toBe(security);
  });
}

test('SAE and PSK validators reject control characters and raw PSKs in SAE mode', () => {
  expect(validatePassword('s', 'wpa3-personal')).toBe(true);
  expect(validatePassword('s', 'wpa2-personal')).toBe(false);
  expect(validatePassword('a'.repeat(64), 'wpa3-personal')).toBe(false);
  expect(validatePassword('a'.repeat(64), 'wpa-personal')).toBe(true);
  for (const security of ['auto', 'wpa-personal', 'wpa2-personal', 'wpa3-personal']) {
    for (const password of ['', 'valid-key\n', 'valid\0key', 'valid-key\x7f']) {
      expect(validatePassword(password, security)).toBe(false);
    }
  }
});
