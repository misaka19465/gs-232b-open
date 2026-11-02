import { build, transform } from 'esbuild';
import { readFile, writeFile } from 'node:fs/promises';
import { gzipSync } from 'node:zlib';

// NOTE: 产出单一离线 HTML，继续使用现有固件和宿主测试的 EMBED_FILES 路径。
const directory = new URL('../src/web/', import.meta.url);
const bundle = await build({
  entryPoints: [new URL('provisioning.js', directory).pathname],
  bundle: true,
  minify: true,
  write: false,
  format: 'iife',
  target: ['es2020'],
  // NOTE: 显式转义字符串中的换行与空白，内联产物不依赖模板字面量的行末空白。
  supported: { 'template-literal': false },
  legalComments: 'inline',
});
const css = await transform(
  await readFile(new URL('../node_modules/mdui/mdui.css', import.meta.url), 'utf8') +
    await readFile(new URL('provisioning.css', directory), 'utf8'),
  { loader: 'css', minify: true },
);
const template = await readFile(new URL('provisioning.template.html', directory), 'utf8');
const html = template.replace('/* WEB_STYLES */', css.code)
  .replace('/* WEB_SCRIPT */', () => bundle.outputFiles[0].text.replaceAll('</script', '<\\/script'));
await writeFile(new URL('provisioning.html', directory), html);
const compressed = gzipSync(html, { level: 9 });
await writeFile(new URL('provisioning.html.gz', directory), compressed);
console.log(`Offline provisioning HTML: ${Buffer.byteLength(html)} bytes (${compressed.length} bytes in firmware)`);
