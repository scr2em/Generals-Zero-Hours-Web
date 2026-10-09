// Runs a build in headless Chromium against a folder fed through <input webkitdirectory> (the stand-in for
// showDirectoryPicker) and reports the console log.
//
//   node run_direct.mjs --site <build/GeneralsMD> --folder <dir with ZeroHour/ and Generals/>
//        [--script web_direct_test.js] [--arg X]... [--until REGEX] [--wait S] [--shot out.png] [--log out.txt]
//        [--mode opfs]   copy the folder into OPFS first and boot the old way (for comparison)
//        [--frames N]    wait until the game has run N frames (instead of --until)
//        [--steps "c:186,240 w:3 s:menu"]   mouse/keyboard steps once the game runs (see webtest/../starter_flow.mjs)
//        [--rss]   report the browser's memory (sum of RSS of its processes) before and after
//
// Needs: NODE_PATH=/opt/node22/lib/node_modules, PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers
import { createRequire } from 'node:module';
import { spawn, execSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const here = path.dirname(fileURLToPath(import.meta.url));
const opt = { script: 'web_direct_test.js', args: [], wait: 60, shot: null, log: null, query: [] };
{
	const a = process.argv.slice(2);
	for (let i = 0; i < a.length; ++i) {
		switch (a[i]) {
			case '--site': opt.site = a[++i]; break;
			case '--folder': opt.folder = a[++i]; break;
			case '--script': opt.script = a[++i]; break;
			case '--arg': opt.args.push(a[++i]); break;
			case '--query': opt.query.push(a[++i]); break;
			case '--until': opt.until = new RegExp(a[++i]); break;
			case '--wait': opt.wait = Number(a[++i]); break;
			case '--shot': opt.shot = a[++i]; break;
			case '--log': opt.log = a[++i]; break;
			case '--rss': opt.rss = true; break;
			case '--opfs': opt.opfs = true; break;
			case '--mode': opt.query.push('mode=' + a[++i]); opt.mode = a[i]; break;
			case '--frames': opt.frames = Number(a[++i]); break;
			case '--touch': opt.touch = a[++i]; break;
			case '--steps': opt.steps = a[++i]; break;
			case '--out': opt.out = a[++i]; break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site || (!opt.folder && !opt.opfs)) { console.error('--site and --folder are required'); process.exit(2); }
}

// A staging copy of the site (symlinks) with the test page next to the build.
const stage = fs.mkdtempSync(path.join(os.tmpdir(), 'zhdirect-site-'));
for (const f of fs.readdirSync(opt.site)) fs.symlinkSync(path.resolve(opt.site, f), path.join(stage, f));
fs.copyFileSync(path.join(here, 'direct_boot.html'), path.join(stage, 'direct_boot.html'));
for (const f of ['direct-source.js']) {
	const src = path.join(here, '..', f);
	try { fs.unlinkSync(path.join(stage, f)); } catch {}
	fs.symlinkSync(src, path.join(stage, f));
}
process.on('exit', () => fs.rmSync(stage, { recursive: true, force: true }));

const server = spawn('python3', [path.join(opt.site, 'serve.py'), '--port', '0', '--dir', stage],
	{ env: { ...process.env, SERVE_QUIET: '1', PYTHONUNBUFFERED: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
process.on('exit', () => server.kill());
const port = await new Promise((resolve, reject) => {
	let text = '';
	server.stdout.on('data', (d) => { text += d; const m = /127\.0\.0\.1:(\d+)/.exec(text); if (m) resolve(Number(m[1])); });
	server.on('exit', () => reject(new Error('serve.py did not start')));
});

const exe = ['/opt/pw-browsers/chromium-1194/chrome-linux/chrome', '/opt/pw-browsers/chromium/chrome'].find((p) => fs.existsSync(p));
const browser = await playwright.chromium.launch({
	executablePath: exe,
	args: ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
		'--enable-webgl', '--enable-features=SharedArrayBuffer'],
});
const context = await browser.newContext({ viewport: { width: 1000, height: 800 } });
const page = await context.newPage();
const logs = [];
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2).padStart(7);
page.on('console', (m) => logs.push(stamp() + ' ' + m.type() + ': ' + m.text()));
page.on('pageerror', (e) => logs.push(stamp() + ' PAGEERROR ' + e.message));
page.on('worker', (w) => w.on('console', (m) => logs.push(stamp() + ' [worker] ' + m.text())));

// Resident set of the browser: this process' descendants (other browsers on the machine do not count).
function rssMB() {
	const rows = execSync('ps -eo pid=,ppid=,rss=').toString().trim().split('\n').map((l) => l.trim().split(/\s+/).map(Number));
	const children = new Map();
	for (const [pid, ppid, rss] of rows) { if (!children.has(ppid)) children.set(ppid, []); children.get(ppid).push([pid, rss]); }
	let sum = 0;
	const walk = (pid) => { for (const [child, rss] of children.get(pid) || []) { sum += rss; walk(child); } };
	walk(process.pid);
	return Math.round(sum / 1024);
}
let rssPeak = 0;

const query = ['script=' + opt.script, ...opt.args.map((a) => 'arg=' + encodeURIComponent(a)), ...opt.query];
if (opt.opfs) query.push('nofolder=1');
await page.goto(`http://127.0.0.1:${port}/direct_boot.html?${query.join('&')}`);
await page.waitForFunction(() => window.__ready);
const rss0 = rssMB();
const tStart = Date.now();
if (opt.mode === 'handle') {
	// Visit 1: remember the folder; visit 2 (a reload): reopen it from the saved handle.
	await page.setInputFiles('#folder', opt.folder);
	await page.waitForFunction(() => window.__saved, null, { timeout: 120000 });
	console.log('visit 1: folder saved (IndexedDB handle)');
	await page.goto(`http://127.0.0.1:${port}/direct_boot.html?${query.filter((q) => !q.startsWith('mode=')).join('&')}&mode=saved`);
	await page.waitForFunction(() => window.__access, null, { timeout: 10000 });
	console.log('visit 2: saved folder permission = ' + await page.evaluate(() => window.__access));
	await page.click('#saved');
} else if (!opt.opfs) {
	await page.setInputFiles('#folder', opt.folder);
}

const deadline = Date.now() + opt.wait * 1000;
let how = 'timeout';
while (Date.now() < deadline) {
	if (opt.until && logs.some((l) => opt.until.test(l))) { how = 'matched'; break; }
	if (opt.touch && !opt.touched && logs.some((l) => /waiting for change/.test(l))) { fs.appendFileSync(opt.touch, 'x'); opt.touched = true; }
	if (opt.frames && (await page.evaluate(() => window.__zhFrames || 0).catch(() => 0)) >= opt.frames) { how = 'frames'; break; }
	const state = await page.evaluate(() => ({ err: window.__error, exit: window.__exitCode })).catch(() => ({}));
	if (state.err) { how = 'error: ' + state.err; break; }
	await page.waitForTimeout(200);
	rssPeak = Math.max(rssPeak, rssMB());
}
async function point(x, y) {
	const box = await page.locator('#canvas').boundingBox();
	return { x: box.x + (x / 800) * box.width, y: box.y + (y / 600) * box.height };
}
for (const step of (opt.steps || '').split(/\s+/).filter(Boolean)) {
	const [kind, arg = ''] = [step.split(':')[0], step.slice(step.indexOf(':') + 1)];
	const xy = () => arg.split(',').map(Number);
	switch (kind) {
		case 'm': { const p = await point(...xy()); await page.mouse.move(p.x, p.y, { steps: 4 }); break; }
		case 'c': { const p = await point(...xy()); await page.mouse.move(p.x, p.y, { steps: 4 }); await page.waitForTimeout(150);
			await page.mouse.down(); await page.waitForTimeout(80); await page.mouse.up(); break; }
		case 'w': await page.waitForTimeout(Number(arg) * 1000); break;
		case 'k': await page.keyboard.press(arg); break;
		case 's': await page.screenshot({ path: path.join(opt.out || '.', arg + '.png') }); break;
		case 'l': { const re = new RegExp(arg); const until = Date.now() + 30000;
			while (Date.now() < until && !logs.some((l) => re.test(l))) await page.waitForTimeout(250); break; }
		default: console.log('unknown step ' + step);
	}
}
const rss1 = rssMB();
const info = await page.evaluate(() => ({ copy: window.__copyMs, open: window.__openMs, first: window.__zhFirstFrameAt, bootAt: window.__bootAt, frames: window.__zhFrames, heap: window.__zhHeapBytes })).catch(() => ({}));
console.log(`finished: ${how} after ${((Date.now() - tStart) / 1000).toFixed(1)} s`);
if (info.copy) console.log(`copied into OPFS in ${Math.round(info.copy)} ms`);
console.log(`folder opened in ${info.open ? Math.round(info.open) : '?'} ms; first game frame ${info.first && info.bootAt ? ((info.first - info.bootAt) / 1000).toFixed(1) + ' s after boot' : 'n/a'}; frames ${info.frames || 0}`);
if (opt.rss) console.log(`browser RSS: ${rss0} MB before, peak ${rssPeak} MB, ${rss1} MB at the end`);
if (opt.shot) await page.screenshot({ path: opt.shot });
if (opt.log) fs.writeFileSync(opt.log, logs.join('\n') + '\n');
const interesting = logs.filter((l) => /DIRECT|zhdirect|WebPlatform|Fatal|Required|rror/.test(l));
(opt.log ? interesting : logs).slice(0, 300).forEach((l) => console.log(l.slice(0, 300)));
await browser.close();
process.exit(0);
