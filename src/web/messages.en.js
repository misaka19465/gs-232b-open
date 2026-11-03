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

export default {
  title: 'Connect to Wi-Fi',
  introduction: 'Select a network to connect',
  networks: 'Available networks',
  networkCount: ({ count }) => str`${count} networks`,
  networkCountOne: ({ count }) => str`${count} network`,
  encrypted: 'Password required',
  open: 'Open network',
  other: 'Other network…',
  otherDescription: 'Manually add a network',
  refresh: 'Refresh',
  scanning: 'Scanning for nearby networks',
  empty: 'No available networks found. Refresh or add one manually',
  offline: 'Connecting to device. Keep the device hotspot connection active',
  ready: 'Device connected. Ready to start setup',
  disconnected: 'Connection lost. Reconnecting; unconfirmed settings will be automatically rolled back',
  scanFailed: 'Scan failed. Refresh and try again',
  scanTruncated: 'Many networks nearby. Showing only the 64 strongest signals; you can add other networks manually',
  otherTitle: 'Add other network',
  otherHint: 'Enter the network name and key, then continue to set up the connection',
  ssid: 'Network name (SSID)',
  key: 'Key',
  keyHint: 'Leave blank for open networks',
  password: 'Password',
  passwordHint: 'Enter the password for this Wi-Fi',
  openHint: 'This network does not require a password',
  confirmOther: 'OK',
  connect: 'Connect',
  connecting: 'Connecting',
  cancel: 'Cancel',
  advanced: 'Advanced settings…',
  collapseAdvanced: 'Collapse advanced settings',
  ipMode: 'IP assignment',
  dhcp: 'Automatic (DHCP)',
  static: 'Static IP',
  ipAddress: 'IP address',
  netmask: 'Subnet mask',
  gateway: 'Gateway (optional)',
  gatewayHint: 'Only required for cross-subnet communication',
  invalidSsid: 'Network name must be 1–32 characters and cannot contain null characters',
  invalidPassword: 'Enter a password of 8–63 characters',
  invalidSaePassword: 'WPA3 password must be 1–63 characters',
  invalidIp: 'Enter a valid IPv4 address',
  invalidSubnet: 'Check the subnet mask and gateway. The address must be a valid host address within the same subnet',
  trialTitle: 'Confirm this connection',
  trialConnecting: 'Waiting for Wi-Fi connection; the original settings will be restored automatically if not confirmed',
  trialReady: ({ ssid, address }) => str`Connected to ${ssid} · ${address}`,
  remaining: ({ seconds }) => str`Confirm within ${seconds} seconds, or the original settings will be restored automatically`,
  save: 'Confirm and save',
  restore: 'Restore original settings',
  saved: 'Network settings saved',
  restored: 'Original settings restored',
  expired: 'Trial connection ended. Please select a network again',
  rollingBack: 'Restoring original settings, please wait',
  failed: 'Configuration failed. Check the network and settings and try again',
  notReady: 'Not connected yet. Please confirm later',
  busy: 'Another configuration is in progress. Try again later',
  requestFailed: 'The device did not complete this request. Please try again',
  timeout: 'Device response timed out. Reconnecting',
};
