export function validateSsid(ssid) {
  return typeof ssid === 'string' && !ssid.includes('\0') &&
    new TextEncoder().encode(ssid).length >= 1 && new TextEncoder().encode(ssid).length <= 32;
}

export function validatePassword(password) {
  return /^[\x20-\x7e]{8,63}$/.test(password) || /^[0-9a-fA-F]{64}$/.test(password);
}

function ipv4(text) {
  if (!/^(0|[1-9]\d{0,2})(\.(0|[1-9]\d{0,2})){3}$/.test(text)) throw new Error('invalidIp');
  const parts = text.split('.').map(Number);
  if (parts.some(part => part > 255)) throw new Error('invalidIp');
  return parts.reduce((value, part) => ((value << 8) | part) >>> 0, 0);
}

export function ipv4Configuration(mode, address, netmask, gateway) {
  if (mode === 'dhcp') return { mode };
  if (mode !== 'static') throw new Error('invalidIp');
  const ip = ipv4(address), mask = ipv4(netmask), route = gateway ? ipv4(gateway) : 0;
  const suffix = (~mask) >>> 0;
  const host = value => (value >>> 24) !== 0 && (value >>> 24) !== 127 &&
    (value >>> 24) < 224 && ((value & suffix) >>> 0) !== 0 && ((value & suffix) >>> 0) !== suffix;
  if (!mask || suffix < 3 || (suffix & (suffix + 1)) !== 0 || !host(ip) ||
      (route && (!host(route) || route === ip || (route & mask) !== (ip & mask)))) throw new Error('invalidSubnet');
  return { mode, address, netmask, gateway: gateway || '0.0.0.0' };
}
