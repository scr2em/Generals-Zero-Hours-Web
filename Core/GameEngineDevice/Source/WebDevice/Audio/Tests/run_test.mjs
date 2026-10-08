// Runs web_audio_test in headless Chromium.
//
//   node run_test.mjs <build-dir containing web_audio_test.html> [seconds-to-wait] [scenarios, comma separated]
//
// Needs the `playwright` npm package (global install is fine) and Chromium
// (PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers). Chromium is started with
// --autoplay-policy=no-user-gesture-required, like a page that got a user gesture (the
// launcher's Play click) would be allowed to play. Exit code 0 when the test printed
// RESULT: PASS and the page-side assertions below hold.
import { createRequire } from 'node:module';
import path from 'node:path';
import { startServer } from './serve.mjs';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const [buildDir, waitSeconds = '120', scenarios = ''] = process.argv.slice(2);
// "gesture" runs without the autoplay switch and clicks the page once the program waits for it; "activated" clicks
// first and starts the program afterwards (the launcher's Play button), which must give a running context at once.
const activatedMode = scenarios === 'activated';
const gestureMode = scenarios === 'gesture' || activatedMode;
if (!buildDir) { console.error('usage: node run_test.mjs <build-dir> [seconds]'); process.exit(2); }

const server = await startServer(path.resolve(buildDir));
const port = server.address().port;
const browser = await playwright.chromium.launch({
  args: ['--no-sandbox', '--enable-features=SharedArrayBuffer'].concat(gestureMode ? [] : ['--autoplay-policy=no-user-gesture-required']),
});
const page = await browser.newPage();
let done = false;
const lines = [];
let clicked = false;
const echo = (s) => {
  lines.push(s);
  if (process.env.VERBOSE) console.log(s);
  if (s.includes('TEST_DONE')) done = true;
  if (s.includes('WAITING_FOR_GESTURE') && !clicked) {
    clicked = true;
    // a trusted click, like the launcher's Play button
    setTimeout(() => page.mouse.click(60, 60).catch(() => {}), 800);
  }
};
page.on('console', (m) => echo(m.text()));
page.on('worker', (w) => w.on('console', (m) => echo('[worker] ' + m.text())));
page.on('pageerror', (e) => echo('PAGEERROR ' + e.message));
await page.goto(`http://127.0.0.1:${port}/web_audio_test.html` + (scenarios ? '?args=' + encodeURIComponent(activatedMode ? 'gesture' : scenarios) + (activatedMode ? '&wait=1' : '') : ''));
if (activatedMode) {
  await new Promise((r) => setTimeout(r, 1500));
  await page.mouse.click(60, 60);
  clicked = true;
}
const t0 = Date.now();
while (!done && Date.now() - t0 < Number(waitSeconds) * 1000) await new Promise((r) => setTimeout(r, 200));

let failed = !done;
const extra = [];
const expect = (name, ok, detail = '') => { extra.push(`page: ${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ' (' + detail + ')' : ''}`); if (!ok) failed = true; };
if (done) {
  // Assertions about the live Web Audio graph, evaluated on the main thread of the page.
  if (gestureMode) {
    expect(activatedMode ? 'the context was running at once after the click' : 'the context started suspended without a gesture', lines.some((l) => l === (activatedMode ? 'INITIAL_STATE 2' : 'INITIAL_STATE 1')));
    expect('the page was clicked', clicked);
  }
  const info = await page.evaluate(() => {
    const A = globalThis.zhWebAudio;
    if (!A || !A.ctx) return null;
    return {
      state: A.ctx.state,
      sampleRate: A.ctx.sampleRate,
      stats: A.stats,
      liveVoices: A.voices.filter(Boolean).length,
      tapFrames: globalThis.zhTest.totalFrames(),
      listener: [A.ctx.listener.positionX.value, A.ctx.listener.forwardY.value, A.ctx.listener.upZ.value],
      destinationChannels: A.ctx.destination.channelCount,
    };
  });
  expect('the AudioContext and zhWebAudio exist', !!info);
  if (info) {
    expect('AudioContext is running', info.state === 'running', info.state);
    expect('context sample rate follows the request', info.sampleRate === 44100, String(info.sampleRate));
    if (gestureMode) {
      expect('source nodes were started', info.stats.sourcesStarted > 0, String(info.stats.sourcesStarted));
    } else if (!scenarios) {	// the totals of a full run
      expect('AudioBuffers were created', info.stats.buffersCreated >= 10, String(info.stats.buffersCreated));
      expect('source nodes were started and ended', info.stats.sourcesStarted >= 60 && info.stats.sourcesEnded >= 30, `${info.stats.sourcesStarted}/${info.stats.sourcesEnded}`);
      expect('voices were created', info.stats.voicesCreated >= 20, String(info.stats.voicesCreated));
      expect('music was streamed in chunks', info.stats.streamChunks >= 20, String(info.stats.streamChunks));
    } else {
      expect('source nodes were started', info.stats.sourcesStarted > 0, String(info.stats.sourcesStarted));
    }
    expect('the command interpreter had no errors', info.stats.errors === 0, String(info.stats.errors));
    expect('the output was rendered and tapped', info.tapFrames > (gestureMode ? 4000 : 44100 * (scenarios ? 1 : 20)), String(info.tapFrames));
    expect('all voices were released', info.liveVoices <= 1, String(info.liveVoices));
  }
  await page.evaluate(() => { globalThis.zhTestAck = 1; });
  const t1 = Date.now();
  while (!lines.some((l) => l.startsWith('SHUTDOWN:')) && Date.now() - t1 < 10000) await new Promise((r) => setTimeout(r, 100));
  expect('shutdown released the context', lines.some((l) => l === 'SHUTDOWN: ok'));
}
if (!lines.some((l) => l.includes('RESULT: PASS'))) failed = true;
if (lines.some((l) => l.startsWith('PAGEERROR'))) failed = true;
if (!process.env.VERBOSE) console.log(lines.join('\n'));
console.log(extra.join('\n'));
console.log(failed ? (done ? 'RUN: FAILED' : 'RUN: TIMEOUT') : 'RUN: PASSED');
await browser.close();
server.close();
process.exit(failed ? 1 : 0);
