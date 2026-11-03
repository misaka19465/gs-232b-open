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

import { str } from '@lit/localize';

// NOTE: str 模板中的数字是源语言动态参数的索引，由 Lit Localize 代入实际值。
export default {
  title: 'Connect to Wi-Fi',
  introduction: 'Choose a network to connect to',
  networks: 'Available networks',
  networkCount: str`${0} networks`,
  networkCountOne: str`${0} network`,
  encrypted: 'Password required',
  open: 'Open network',
  other: 'Other network…',
  otherDescription: 'Add a network manually',
  refresh: 'Refresh',
  scanning: 'Scanning for nearby networks',
  empty: 'No networks found. Refresh or add a network manually.',
  offline: 'Connecting to the device. Stay connected to its Wi-Fi hotspot.',
  ready: 'Connected to the device. You can now set up Wi-Fi.',
  disconnected: 'Connection lost. Reconnecting; unconfirmed changes will be rolled back automatically.',
  scanFailed: 'Scan failed. Refresh to try again.',
  scanTruncated: 'Only the 64 strongest networks are shown. You can add other networks manually.',
  otherTitle: 'Add another network',
  otherHint: 'Enter the network name and key, then continue to connection settings.',
  ssid: 'Network name (SSID)',
  key: 'Network key',
  keyHint: 'Leave blank for an open network',
  password: 'Password',
  passwordHint: 'Enter the password for this Wi-Fi network',
  openHint: 'This network does not require a password',
  confirmOther: 'Continue',
  connect: 'Connect',
  connecting: 'Connecting',
  cancel: 'Cancel',
  advanced: 'Advanced settings…',
  collapseAdvanced: 'Hide advanced settings',
  ipMode: 'IP assignment',
  dhcp: 'Automatic (DHCP)',
  static: 'Static IP',
  ipAddress: 'IP address',
  netmask: 'Subnet mask',
  gateway: 'Gateway (optional)',
  gatewayHint: 'Only needed to communicate outside this subnet',
  invalidSsid: 'The network name must be 1–32 UTF-8 bytes and must not contain a null character.',
  invalidPassword: 'Enter 8–63 characters or a 64-digit hexadecimal key.',
  invalidIp: 'Enter a valid IPv4 address.',
  invalidSubnet: 'Check the subnet mask and gateway. Addresses must be valid host addresses in the same subnet.',
  trialTitle: 'Confirm this connection',
  trialConnecting: 'Waiting for Wi-Fi. Previous settings will be restored automatically unless you confirm.',
  trialReady: str`Connected to ${0} · ${1}`,
  remaining: str`Confirm within ${0} seconds or previous settings will be restored automatically.`,
  save: 'Confirm and save',
  restore: 'Restore previous settings',
  saved: 'Network settings saved',
  restored: 'Previous settings restored',
  expired: 'The connection trial has ended. Select a network again.',
  rollingBack: 'Restoring previous settings. Please wait.',
  failed: 'Setup failed. Check the network and settings, then try again.',
  notReady: 'The connection is not ready. Wait before confirming.',
  busy: 'Settings are being processed. Try again later.',
  requestFailed: 'The device could not complete the request. Try again.',
  timeout: 'The device timed out. Reconnecting.',
};
