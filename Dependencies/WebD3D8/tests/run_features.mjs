// Runs web_d3d8_features in headless Chromium (SwiftShader) for every mode of the matrix and fails when a
// check fails.
//
//   node run_features.mjs <build-dir> [--mode <name>[,<name>...]] [--show-pass] [--shots <dir>]
//
// <build-dir> is the directory that holds web_d3d8_features.html (build/em-d3d8/Dependencies/WebD3D8).
// Modes (default: all):
//   basic         border, flat shading, ps.1.x texture instructions, D3DX filters, state caching, the game's shader sources
//   basic-ext     the same with Chrome's draft WebGL extensions (WEBGL_draw_instanced_base_vertex_base_instance)
//   basic-nopv    the same with WEBGL_provoking_vertex switched off (triangle reordering fallback)
//   basic-nos3tc  the same with DXT decoded on the CPU
//   contextloss   WEBGL_lose_context + the game's Reset path
//   msaa          4x multisampled back buffer
//   msaa-off      multisampling requested but not available: the device falls back cleanly
//
// Needs the `playwright` npm package (NODE_PATH) and Chromium (PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers).
import { createRequire } from 'node:module';
import path from 'node:path';
import fs from 'node:fs';
import { startServer } from './serve.mjs';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const MODES = {
  'basic': ['--mode=basic'],
  'basic-ext': ['--mode=basic', '--ext'],   // browser flag below: draft WebGL extensions on (base vertex draws)
  'basic-nopv': ['--mode=basic', '--noprovoking'],
  'basic-nos3tc': ['--mode=basic', '--no-s3tc'],
  'contextloss': ['--mode=contextloss'],
  'msaa': ['--mode=msaa'],
  'msaa-off': ['--mode=msaa', '--nomsaa'],
};

const argv = process.argv.slice(2);
const buildDir = argv.find((a) => !a.startsWith('--'));
let wanted = Object.keys(MODES);
let showPass = false;
let shots = null;
for (let i = 0; i < argv.length; ++i) {
  if (argv[i] === '--mode') wanted = argv[++i].split(',');
  if (argv[i] === '--show-pass') showPass = true;
  if (argv[i] === '--shots') shots = argv[++i];
}
if (!buildDir) { console.error('usage: node run_features.mjs <build-dir> [--mode a,b] [--show-pass] [--shots dir]'); process.exit(2); }
if (shots) fs.mkdirSync(shots, { recursive: true });

const server = await startServer(path.resolve(buildDir));
const port = server.address().port;
const exe = fs.existsSync('/opt/pw-browsers/chromium/chrome') ? '/opt/pw-browsers/chromium/chrome' : undefined;
const browser = await playwright.chromium.launch({
  executablePath: exe,
  args: ['--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
         '--enable-webgl', '--no-sandbox', '--enable-features=SharedArrayBuffer'],
});

let failedModes = 0;
const baseArgs = ['--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
                  '--enable-webgl', '--no-sandbox', '--enable-features=SharedArrayBuffer'];
let extBrowser = null;
for (const name of wanted) {
  if (!MODES[name]) { console.error('unknown mode ' + name); failedModes++; continue; }
  let b = browser;
  if (name.endsWith('-ext')) {
    extBrowser ||= await playwright.chromium.launch({ executablePath: exe, args: [...baseArgs, '--enable-webgl-draft-extensions'] });
    b = extBrowser;
  }
  const page = await b.newPage({ viewport: { width: 700, height: 760 } });
  const lines = [];
  let done = false;
  page.on('console', (m) => { lines.push(m.text()); if (m.text().includes('TEST_DONE')) done = true; });
  page.on('worker', (w) => w.on('console', (m) => lines.push('[worker] ' + m.text())));
  page.on('pageerror', (e) => lines.push('PAGEERROR ' + e.message));
  await page.goto(`http://127.0.0.1:${port}/web_d3d8_features.html?args=${encodeURIComponent(MODES[name].join(','))}`);
  const t0 = Date.now();
  while (!done && Date.now() - t0 < 120000) await new Promise((r) => setTimeout(r, 200));
  await new Promise((r) => setTimeout(r, 300));
  if (shots) await page.locator('#canvas').screenshot({ path: path.join(shots, name + '.png') }).catch(() => {});
  const checks = lines.filter((l) => /^CHECK /.test(l));
  const fails = checks.filter((l) => / FAIL/.test(l));
  const passes = checks.length - fails.length;
  const other = lines.filter((l) => !/^CHECK /.test(l) && !/WebD3D8 report/.test(l));
  console.log(`== ${name}: ${passes} passed, ${fails.length} failed${done ? '' : ' (TIMEOUT)'}`);
  if (showPass) for (const l of checks.filter((c) => / PASS/.test(c))) console.log('   ' + l);
  for (const l of fails) console.log('   ' + l);
  const noteworthy = other.filter((l) => /PAGEERROR|rror|FAILED|unsupported|shader|assemble|missing|abort|Aborted/i.test(l));
  for (const l of noteworthy.slice(0, 25)) console.log('   | ' + l.slice(0, 300));
  if (process.env.FEATURES_VERBOSE) for (const l of other) console.log('   . ' + l.slice(0, 300));
  if (!done || fails.length || !checks.length) failedModes++;
  await page.close();
}
await browser.close();
if (extBrowser) await extBrowser.close();
server.close();
console.log(failedModes ? `RESULT: ${failedModes} mode(s) failed` : 'RESULT: all modes passed');
process.exit(failedModes ? 1 : 0);
