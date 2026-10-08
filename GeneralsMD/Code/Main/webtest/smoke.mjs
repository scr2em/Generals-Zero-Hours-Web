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
//   --reload-after <s>    reload the page after this long and start the game again (OPFS persistence)
//   --stack               at the end, print the call stacks of the engine's worker threads (diagnoses hangs)
//   --port <n>            server port (default 8931)
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
			case '--reload-after': opt.reloadAfter = Number(a[++i]); break;
			case '--stack': opt.stack = true; break;
			case '--port': opt.port = Number(a[++i]); break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site || !opt.data) { console.error('--site and --data are required'); process.exit(2); }
}

const server = spawn('python3', [path.join(opt.site, 'serve.py'), '--port', String(opt.port), '--dir', opt.site],
	{ env: { ...process.env, SERVE_QUIET: '1' }, stdio: 'inherit' });
process.on('exit', () => server.kill());
await new Promise((r) => setTimeout(r, 800));

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

async function play() {
	await page.waitForFunction(() => !document.getElementById('play').disabled, null, { timeout: 30000 });
	logs.push(stamp() + ' --- Play ---');
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

if (opt.input && how !== 'error-panel') {
	await page.mouse.move(300, 300);
	await page.mouse.move(400, 350, { steps: 5 });
	await page.mouse.down(); await page.mouse.up();
	await page.keyboard.press('KeyA');
	await page.waitForTimeout(1500);
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

// The frame loop (WebMain.cpp) publishes its frame counter on the page about twice a second.
{
	const frames = async () => page.evaluate(() => window.__zhFrames || 0).catch(() => -1);
	const f1 = await frames();
	await page.waitForTimeout(3000);
	const f2 = await frames();
	console.log(`frames: ${f1} -> ${f2} in 3 s (${((f2 - f1) / 3).toFixed(1)} fps)`);
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
