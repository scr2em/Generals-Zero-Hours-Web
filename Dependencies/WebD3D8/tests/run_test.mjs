// Runs web_d3d8_test in headless Chromium and saves a screenshot.
//
//   node run_test.mjs <build-dir> <screenshot.png> [test args, comma separated]
//
// e.g. node run_test.mjs build/em-d3d8/Dependencies/WebD3D8 /tmp/shot.png --no-s3tc
//
// Test arguments: --no-s3tc (decode DXT on the CPU), --implicit (no explicit
// swap control), --shaders (vs.1.1/ps.1.x scenes), --scene2 / --scene3 (texgen,
// projection, lights, stencil, formats...), --debug (GL error checks),
// --frames=N (long run; two screenshots are compared while it renders, which
// shows whether frames are presented from a thread that never yields),
// --yield (emscripten_sleep(0) per frame through the OnFramePresented hook).
//
// Needs the `playwright` npm package (global install is fine) and Chromium
// (PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers). WebGL2 is provided by SwiftShader.
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
import { startServer } from './serve.mjs';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const [buildDir, shot = 'web_d3d8_test.png', ...testArgs] = process.argv.slice(2);
if (!buildDir) { console.error('usage: node run_test.mjs <build-dir> [screenshot.png] [test args...]'); process.exit(2); }

const server = await startServer(path.resolve(buildDir));
const port = server.address().port;
const exe = fs.existsSync('/opt/pw-browsers/chromium/chrome') ? '/opt/pw-browsers/chromium/chrome' : undefined;
const browser = await playwright.chromium.launch({
  executablePath: exe,
  args: ['--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
         '--enable-webgl', '--no-sandbox', '--enable-features=SharedArrayBuffer'],
});
const page = await browser.newPage({ viewport: { width: 700, height: 760 } });
let done = false;
const lines = [];
page.on('console', (m) => { lines.push(m.text()); if (m.text().includes('TEST_DONE')) done = true; });
page.on('worker', (w) => w.on('console', (m) => lines.push('[worker] ' + m.text())));
page.on('pageerror', (e) => lines.push('PAGEERROR ' + e.message));
const query = testArgs.length ? '?args=' + encodeURIComponent(testArgs.join(',')) : '';
await page.goto(`http://127.0.0.1:${port}/web_d3d8_test.html${query}`);
const t0 = Date.now();
// With a running (long) test, grab two screenshots while it still renders to
// verify that frames are presented from a thread that never yields.
const mid = [];
if (testArgs.some((a) => a.startsWith('--frames='))) {
  for (let i = 0; i < 2; ++i) {
    await new Promise((r) => setTimeout(r, 2500));
    mid.push(await page.locator('#canvas').screenshot());
  }
  console.log(done ? 'MIDRUN: test already finished' :
    `MIDRUN: screenshots during rendering ${mid[0].equals(mid[1]) ? 'IDENTICAL (frozen?)' : 'differ (animating, presenting works)'}`);
}
while (!done && Date.now() - t0 < 90000) await new Promise((r) => setTimeout(r, 200));
await new Promise((r) => setTimeout(r, 500));
await page.locator('#canvas').screenshot({ path: shot });
console.log(lines.join('\n'));
console.log(done ? 'RESULT: finished' : 'RESULT: timeout');
await browser.close();
server.close();
process.exit(done ? 0 : 1);
