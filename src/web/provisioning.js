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

import 'mdui/components/button.js';
import 'mdui/components/button-icon.js';
import 'mdui/components/list.js';
import 'mdui/components/list-item.js';
import 'mdui/components/dialog.js';
import 'mdui/components/text-field.js';
import 'mdui/components/select.js';
import 'mdui/components/menu-item.js';
import 'mdui/components/circular-progress.js';
import { t, setMessage, followBrowserLanguage } from './localization.js';
import { icon } from './icons.js';
import { ManagementChannel } from './management-channel.js';
import { fetchNetworks } from './network-scan.js';
import { securityForAuth, securityForPassword } from './wifi-security.js';
import { validateSsid, validatePassword, ipv4Configuration } from './validation.js';

const $ = id => document.getElementById(id);
for (const element of document.querySelectorAll('[data-icon]')) element.append(icon(element.dataset.icon));
followBrowserLanguage();

const dialog = $('network-dialog');
let selection = null, manual = false, working = false, scanning = false, polling = false;
let trial = null, trialDeadline = 0, trialNetwork = null, readyToSave = false;
let firstScan = true, changed = false;
const status = key => { setMessage($('status'), key); };
const error = key => {
  const element = $('dialog-error');
  setMessage(element, key);
  element.hidden = false;
  element.scrollIntoView({ block: 'nearest' });
};

function controls() {
  $('refresh').disabled = !channel.ready || working || scanning || !!trial;
  $('refresh').loading = scanning;
  $('join').disabled = !channel.ready || working || !!trial;
  $('join').loading = working;
  for (const row of $('networks').children) row.disabled = working || !!trial;
  $('save').disabled = working || !channel.ready || !readyToSave;
  $('restore').disabled = working || !channel.ready;
  $('advanced').disabled = working;
  $('close-dialog').disabled = working;
}

function resetDialog() {
  $('dialog-error').hidden = true;
  $('advanced-panel').hidden = true;
  $('static-fields').hidden = true;
  $('advanced').setAttribute('aria-expanded', 'false');
  setMessage($('advanced').querySelector('[data-text]'), 'advanced');
  $('ip-mode').value = 'dhcp';
  $('ip-address').value = '';
  $('netmask').value = '255.255.255.0';
  $('gateway').value = '';
}

function showNetwork(network, password = '') {
  if (working || trial) return;
  manual = false;
  selection = network;
  resetDialog();
  $('manual-fields').hidden = true;
  $('password-fields').hidden = !network.secure;
  $('password').value = network.secure ? password : '';
  delete $('dialog-heading').dataset.text;
  $('dialog-heading').textContent = network.ssid;
  setMessage($('dialog-description'), network.secure ? 'passwordHint' : 'openHint');
  $('advanced').hidden = false;
  setMessage($('join'), 'connect');
  dialog.open = true;
  controls();
}

$('other-network').addEventListener('click', () => {
  if (working || trial) return;
  manual = true;
  selection = null;
  resetDialog();
  $('manual-fields').hidden = false;
  $('password-fields').hidden = true;
  $('manual-ssid').value = '';
  $('manual-key').value = '';
  setMessage($('dialog-heading'), 'otherTitle');
  setMessage($('dialog-description'), 'otherHint');
  $('advanced').hidden = true;
  setMessage($('join'), 'confirmOther');
  dialog.open = true;
  controls();
});

$('close-dialog').addEventListener('click', () => { dialog.open = false; });
// NOTE: IP 下拉菜单也会冒泡 close/closed，仅对话框自身关闭时阻止操作或清理凭据。
dialog.addEventListener('close', event => { if (event.target === dialog && working) event.preventDefault(); });
dialog.addEventListener('closed', event => {
  if (event.target !== dialog) return;
  $('password').value = $('manual-key').value = '';
  selection = null;
});
$('advanced').addEventListener('click', () => {
  const expanded = $('advanced-panel').hidden;
  $('advanced-panel').hidden = !expanded;
  $('advanced').setAttribute('aria-expanded', String(expanded));
  setMessage($('advanced').querySelector('[data-text]'), expanded ? 'collapseAdvanced' : 'advanced');
});
$('ip-mode').addEventListener('change', () => { $('static-fields').hidden = $('ip-mode').value !== 'static'; });
$('network-form').addEventListener('submit', event => { event.preventDefault(); join(); });
$('network-form').addEventListener('keydown', event => {
  if (event.key === 'Enter' && event.composedPath()[0]?.tagName === 'INPUT') {
    event.preventDefault(); join();
  }
});
$('join').addEventListener('click', join);

async function scan() {
  if (!channel.ready || scanning || working || trial) return;
  scanning = true;
  controls();
  $('scan-status').hidden = false;
  $('scan-progress').hidden = false;
  setMessage($('scan-message'), 'scanning');
  try {
    const reply = await fetchNetworks(channel);
    const networks = new Map();
    for (const ap of reply.aps || []) {
      if (!validateSsid(ap.ssid)) continue;
      const security = securityForAuth(ap.auth);
      if (security === null) continue;
      const secure = ap.auth !== 0;
      const key = JSON.stringify([ap.ssid, security]);
      const previous = networks.get(key);
      if (!previous || ap.rssi > previous.rssi) networks.set(key, { ...ap, secure, security });
    }
    const list = [...networks.values()].sort((a, b) => b.rssi - a.rssi);
    const other = $('other-network');
    $('networks').replaceChildren(...list.map(network => {
      const row = document.createElement('mdui-list-item');
      row.className = 'network-row';
      row.rounded = true;
      row.textContent = network.ssid;
      row.dataset.description = network.secure ? 'encrypted' : 'open';
      row.description = t(row.dataset.description);
      const leading = document.createElement('span');
      leading.slot = 'icon'; leading.className = 'network-icon'; leading.append(icon('wifi'));
      const trailing = document.createElement('span');
      trailing.slot = 'end-icon'; trailing.className = 'trailing-icon';
      if (network.secure) { const lock = icon('lock'); lock.classList.add('lock'); trailing.append(lock); }
      trailing.append(icon('chevron'));
      row.append(leading, trailing);
      row.addEventListener('click', () => showNetwork(network));
      return row;
    }), other);
    setMessage($('network-count'), list.length === 1 ? 'networkCountOne' : 'networkCount', { count: list.length });
    $('scan-status').hidden = list.length > 0 && !reply.truncated;
    $('scan-progress').hidden = true;
    setMessage($('scan-message'), reply.truncated ? 'scanTruncated' : 'empty');
    firstScan = false;
  } catch {
    $('scan-progress').hidden = true;
    setMessage($('scan-message'), 'scanFailed');
  } finally { scanning = false; controls(); }
}
$('refresh').addEventListener('click', scan);

async function join() {
  if (working || trial || !channel.ready) return;
  $('dialog-error').hidden = true;
  if (manual) {
    const ssid = $('manual-ssid').value, key = $('manual-key').value;
    const security = securityForPassword(key);
    if (!validateSsid(ssid)) { error('invalidSsid'); return; }
    if (key && !validatePassword(key, security)) {
      error(security === 'wpa3-personal' ? 'invalidSaePassword' : 'invalidPassword'); return;
    }
    showNetwork({ ssid, secure: !!key, security, automatic: true }, key);
    return;
  }
  if (!selection) return;
  const password = selection.secure ? $('password').value : '';
  const security = selection.automatic ? securityForPassword(password) : selection.security;
  if (selection.secure && !validatePassword(password, security)) {
    error(security === 'wpa3-personal' ? 'invalidSaePassword' : 'invalidPassword'); return;
  }
  let ip;
  try { ip = ipv4Configuration($('ip-mode').value, $('ip-address').value, $('netmask').value, $('gateway').value); }
  catch (failure) { error(failure.message); return; }
  const wifi = { enabled: true, ssid: selection.ssid, password, security, ...ip };
  working = true; controls();
  let candidate = false;
  try {
    const begun = await channel.command('CFG BEGIN');
    candidate = true;
    await channel.command(`NET SET ${JSON.stringify({ wifi })}`);
    await channel.command('CFG CHECK');
    const applied = await channel.command(`CFG APPLY ${JSON.stringify({ candidate_id: begun.candidate_id, timeout_s: 60 })}`);
    candidate = false;
    changed = true;
    trial = { state: 'active', trial_id: applied.trial_id, remaining_us: 60000000 };
    trialNetwork = selection;
    trialDeadline = Date.now() + 60000;
    readyToSave = false;
    working = false;
    dialog.open = false;
    $('trial-card').hidden = false;
    setMessage($('trial-status'), 'trialConnecting');
    status('trialConnecting');
    await refreshState();
  } catch {
    if (candidate && channel.ready) {
      try { await channel.command('CFG ABORT'); }
      catch { status('requestFailed'); }
    }
    error('failed');
    status(channel.ready ? 'failed' : 'disconnected');
  } finally { working = false; controls(); }
}

async function refreshState() {
  if (!channel.ready || polling || working) return;
  polling = true;
  try {
    const network = await channel.command('NET GET');
    const previousTrial = trial;
    trial = network.trial;
    readyToSave = false;
    if (trial) {
      trialNetwork = trial.config?.wifi || trialNetwork;
      trialDeadline = Date.now() + Math.ceil(trial.remaining_us / 1000);
      const wifi = network.runtime?.wifi;
      readyToSave = trial.state === 'active' && wifi?.state === 'ready' && wifi.ipv4_valid &&
        !!trialNetwork?.ssid && wifi.ssid === trialNetwork.ssid;
      setMessage($('trial-status'), trial.state !== 'active' ? 'rollingBack' : readyToSave
        ? 'trialReady' : 'trialConnecting', { ssid: trialNetwork?.ssid, address: wifi?.address });
      $('trial-card').hidden = false;
    } else {
      $('trial-card').hidden = true;
      if (previousTrial && changed) { changed = false; status('expired'); }
    }
  } catch { status(channel.ready ? 'requestFailed' : 'disconnected'); }
  finally { polling = false; controls(); }
}

$('save').addEventListener('click', async () => {
  if (working) return;
  await refreshState();
  if (!readyToSave || !trial || working) { status('notReady'); return; }
  const id = trial.trial_id;
  working = true; controls();
  try {
    await channel.command(`CFG CONFIRM ${JSON.stringify({ trial_id: id })}`);
    trial = null; changed = false; readyToSave = false;
    $('trial-card').hidden = true; status('saved');
  } catch { status('failed'); }
  finally { working = false; controls(); }
});
$('restore').addEventListener('click', async () => {
  if (working || !trial) return;
  working = true; controls();
  try {
    await channel.command(`CFG ABORT ${JSON.stringify({ trial_id: trial.trial_id })}`);
    trial = null; changed = false; readyToSave = false;
    $('trial-card').hidden = true; status('restored');
  } catch { status('failed'); }
  finally { working = false; controls(); }
});

const channel = new ManagementChannel(async () => {
  status('ready'); controls();
  await refreshState();
  if (firstScan || !trial) await scan();
}, () => { readyToSave = false; status('disconnected'); controls(); });
controls();
setInterval(() => {
  if (trial) {
    const seconds = Math.max(0, Math.ceil((trialDeadline - Date.now()) / 1000));
    setMessage($('trial-remaining'), 'remaining', { seconds });
    if (seconds === 0) { readyToSave = false; controls(); }
    refreshState();
  }
}, 1000);
