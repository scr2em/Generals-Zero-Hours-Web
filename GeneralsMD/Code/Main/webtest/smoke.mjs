// Boots the WebAssembly game in headless Chromium against a data set and reports how far it got.
//
//   node smoke.mjs --site <build/GeneralsMD> --data <dir with ZeroHour/ and Generals/> [options]
//
// Options:
//   --page <html>         page to load (default z_generals.html)
//   --shot <file.png>     screenshot at the end (default smoke.png)
//   --log <file.txt>      write the whole console log here
//   --wait <seconds>      maximum time to wait after pressing Play (default 60)
//   --until <regex>       stop waiting as soon as a log line matches (e.g. 'frame 300')
//   --arg <game arg>      pass a command line argument to the game (repeatable), e.g. --arg -noshellmap
//   --input               after the game runs, send mouse and key input and report the input log
//   --quit                after the game runs, ask it to quit (Module._WebPlatform_RequestClose) and report how it ended
//   --reload-after <s>    reload the page after this long and start the game again (OPFS persistence)
//   --stack               at the end, print the call stacks of the engine's worker threads (diagnoses hangs)
//   --headful-gl          use the default GL instead of SwiftShader
//
// The data set is imported through the launcher exactly like a user would (picker=input mode).
// Needs the `playwright` npm package (NODE_PATH=/opt/node22/lib/node_modules) and Chromium
// (PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers). Exit code: 0 when the game started and either kept
// running or ended with a message in the launcher's error panel, 1 on a wasm trap/abort.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const opt = { page: "z_generals.html", shot: 'smoke.png', wait: 60, port: 8931, args: [] };
{
	const a = process.argv.slice(2);
	for (let i = 0; i < a.length; ++i) {
		switch (a[i]) {
			case '--site': opt.site = a[++i]; break;
			case '--page': opt.page = a[++i]; break;
			case '--data': opt.data = a[++i]; break;
			case '--shot': opt.shot = a[++i]; break;
			case '--log': opt.log = a[++i]; break;
			case '--wait': opt.wait = Number(a[++i]); break;
			case '--until': opt.until = new RegExp(a[++i]); break;
			case '--arg': opt.args.push(a[++i]); break;
			case '--input': opt.input = true; break;
			case '--quit': opt.quit = true; break;
			case '--reload-after': opt.reloadAfter = Number(a[++i]); break;
			case '--stack': opt.stack = true; break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site || !opt.data) { console.error('--site and --data are required'); process.exit(2); }
}

// An own server on a free port (other test runs and agents may use any fixed port).
const server = spawn('python3', [path.join(opt.site, 'serve.py'), '--port', '0', '--dir', opt.site],
	{ env: { ...process.env, SERVE_QUIET: '1', PYTHONUNBUFFERED: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
process.on('exit', () => server.kill());
process.on('uncaughtException', (e) => { console.error(e); server.kill(); process.exit(3); });
opt.port = await new Promise((resolve, reject) => {
	let text = '';
	server.stdout.on('data', (d) => { text += d; const m = /127\.0\.0\.1:(\d+)/.exec(text); if (m) resolve(Number(m[1])); });
	server.on('exit', () => reject(new Error('serve.py did not start')));
});

const exe = ['/opt/pw-browsers/chromium-1194/chrome-linux/chrome', '/opt/pw-browsers/chromium/chrome'].find((p) => fs.existsSync(p));
const browser = await playwright.chromium.launch({
	executablePath: exe,
	args: ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
		'--enable-webgl', '--enable-features=SharedArrayBuffer', ...(opt.stack ? ['--remote-debugging-port=9333'] : [])],
});
const context = await browser.newContext({ viewport: { width: 1100, height: 800 } });
const page = await context.newPage();
const logs = [];
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2).padStart(7);
page.on('console', (m) => logs.push(stamp() + ' ' + m.type() + ': ' + m.text()));
page.on('pageerror', (e) => logs.push(stamp() + ' PAGEERROR ' + e.message + (e.stack ? '\n' + e.stack : '')));
page.on('worker', (w) => w.on('console', (m) => logs.push(stamp() + ' [worker] ' + m.text())));

const query = ['picker=input', ...opt.args.map((a) => 'arg=' + encodeURIComponent(a))].join('&');

async function importData() {
	for (const [btn, id, dir] of [['#pick-game', 'state-game', 'ZeroHour'], ['#pick-generals', 'state-generals', 'Generals']]) {
		const [ch] = await Promise.all([page.waitForEvent('filechooser'), page.click(btn)]);
		await ch.setFiles(path.join(opt.data, dir));
		await page.waitForFunction((i) => /^(Ready|That does not|Import failed|Not enough)/.test(document.getElementById(i).textContent), id, { timeout: 120000 });
		console.log(id, '=', await page.textContent('#' + id));
	}
}

let playClickedAt = 0;
async function play() {
	await page.waitForFunction(() => !document.getElementById('play').disabled, null, { timeout: 30000 });
	logs.push(stamp() + ' --- Play ---');
	playClickedAt = Date.now();
	await page.click('#play');
}

async function waitForEnd(seconds) {
	const deadline = Date.now() + seconds * 1000;
	while (Date.now() < deadline) {
		if (opt.until && logs.some((l) => opt.until.test(l))) return 'matched';
		const errorsShown = await page.evaluate(() => !document.getElementById('errors').hidden).catch(() => false);
		if (errorsShown) { await page.waitForTimeout(1500); return 'error-panel'; }
		await page.waitForTimeout(250);
	}
	return 'timeout';
}

await page.goto(`http://127.0.0.1:${opt.port}/${opt.page}?${query}`);
await importData();
await play();
const tPlay = Date.now();
const how = await waitForEnd(opt.wait);
console.log(`stopped waiting after ${((Date.now() - tPlay) / 1000).toFixed(1)}s: ${how}`);

// The frame loop (WebMain.cpp) publishes its frame counter on the page about twice a second.
{
	const frames = async () => page.evaluate(() => window.__zhFrames || 0).catch(() => -1);
	const f1 = await frames();
	await page.waitForTimeout(3000);
	const f2 = await frames();
	console.log(`frames: ${f1} -> ${f2} in 3 s (${((f2 - f1) / 3).toFixed(1)} fps)`);
	const info = await page.evaluate(() => ({ first: window.__zhFirstFrameAt || 0, heap: window.__zhHeapBytes || 0, frameMs: window.__zhFrameMs || 0 })).catch(() => ({ first: 0, heap: 0 }));
	if (info.first) console.log(`startup: first frame ${((info.first - playClickedAt) / 1000).toFixed(1)} s after Play (includes module download from localhost, compile and engine init)`);
	if (info.frameMs) console.log(`engine time per frame: ${info.frameMs.toFixed(1)} ms`);
	if (info.heap) console.log(`wasm heap: ${(info.heap / 1048576).toFixed(0)} MB`);
}

if (opt.input && how !== 'error-panel') {
	await page.mouse.move(300, 300);
	await page.mouse.move(400, 350, { steps: 5 });
	await page.mouse.down(); await page.mouse.up();
	await page.keyboard.press('KeyA');
	await page.waitForTimeout(1500);
	const inputLines = logs.filter((l) => /input: /.test(l));
	console.log(`input reached the engine: ${inputLines.length} events` + (opt.args.includes('-webinputlog') ? '' : ' (start with --arg -webinputlog to count them)'));
	inputLines.slice(0, 12).forEach((l) => console.log('   ' + l.trim()));
}

if (opt.quit) {
	const f0 = await page.evaluate(() => window.__zhFrames || 0);
	await page.evaluate(() => Module._WebPlatform_RequestClose());
	const t = Date.now();
	let ended = false;
	while (Date.now() - t < 15000 && !ended) {
		ended = logs.some((l) => /Exited with code/.test(l)) || await page.evaluate(() => /ended/.test(document.getElementById('loading').textContent)).catch(() => false);
		if (!ended) await page.waitForTimeout(200);
	}
	const f1 = await page.evaluate(() => window.__zhFrames || 0);
	await page.waitForTimeout(1500);
	const f2 = await page.evaluate(() => window.__zhFrames || 0);
	console.log(`quit: ${ended ? 'the game ended' : 'THE GAME DID NOT END'} after ${((Date.now() - t) / 1000).toFixed(1)} s; frames ${f0} -> ${f1} -> ${f2}`);
	console.log('loading text:', await page.textContent('#loading'));
}

if (opt.reloadAfter) {
	await page.waitForTimeout(opt.reloadAfter * 1000);
	logs.push(stamp() + ' --- reload ---');
	await page.goto(`http://127.0.0.1:${opt.port}/${opt.page}?${query}`);
	await page.waitForFunction(() => /Ready/.test(document.getElementById('state-game').textContent), null, { timeout: 30000 });
	console.log('after reload: state-game =', await page.textContent('#state-game'));
	await play();
	console.log('second run:', await waitForEnd(opt.wait));
}

if (opt.stack) {
	const { dumpWorkerStacks } = await import('./cdpstack.mjs');
	console.log(await dumpWorkerStacks(9333));
}

const panel = await page.evaluate(() => ({
	shown: !document.getElementById('errors').hidden,
	title: document.getElementById('errors-title').textContent,
	text: document.getElementById('errors-log').textContent,
	loading: document.getElementById('loading').textContent,
	loadingHidden: document.getElementById('loading').hidden,
})).catch(() => null);
await page.screenshot({ path: opt.shot });
if (opt.log) fs.writeFileSync(opt.log, logs.join('\n') + '\n');

console.log(`--- console: ${logs.length} lines ---`);
const clip = (l) => l.slice(0, 400);
if (logs.length <= 120) logs.forEach((l) => console.log(clip(l)));
else {
	logs.slice(0, 60).forEach((l) => console.log(clip(l)));
	console.log(`... ${logs.length - 100} lines omitted${opt.log ? ' (see ' + opt.log + ')' : ''} ...`);
	logs.slice(-40).forEach((l) => console.log(clip(l)));
}
console.log('--- launcher error panel ---');
console.log(panel ? JSON.stringify(panel, null, 1) : '(page gone)');
const trapped = logs.some((l) => /RuntimeError|unreachable|memory access out of bounds|Aborted\(|abort\(/.test(l));
console.log(trapped ? 'RESULT: wasm trap or abort' : 'RESULT: no trap');
await browser.close();
process.exit(trapped ? 1 : 0);
