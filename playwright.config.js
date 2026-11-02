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

import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: './tests/web',
  fullyParallel: true,
  workers: 2,
  use: { baseURL: 'http://127.0.0.1:8766', viewport: { width: 430, height: 932 } },
  webServer: {
    command: 'python3 -m http.server 8766 --bind 127.0.0.1 --directory src/web',
    url: 'http://127.0.0.1:8766/provisioning.html',
    reuseExistingServer: false,
  },
});
