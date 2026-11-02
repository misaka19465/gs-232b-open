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
