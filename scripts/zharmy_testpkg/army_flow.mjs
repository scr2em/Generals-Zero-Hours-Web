// Army package variant of GeneralsMD/Code/Main/web/test/starter_flow.mjs (same steps and options), used to test the
// engine side of army packages (docs/ARMY_PACKAGES.md). Extra option:
//   (extra step W:PIXELS turns the mouse wheel at the pointer)
//   --userfile <path>=<local file>  (repeatable) written to the browser's /userdata/<path> before the game starts
//                          (used to preseed 'command and conquer generals zero hour data/skirmish.ini')
//   --viewport WxH  browser window size (default 1100x800); a small one makes the software renderer faster
//   (extra step K:Key/MS holds a key down for MS milliseconds)
//   --army <file.zharmy>   (repeatable) copied into the browser's file system as /game/armies/<name> after the starter
//                          content is in place, and passed to the game as `-army /game/armies/<name>`
// After the engine starts, the engine's /userdata/ArmyReport.json is printed. Step `R:` of the original is kept.
//
// Drives the real game (z_generals) with the free starter content through the launcher and the menus, and prints the
// engine's console log after every step. This is how the starter pack's menus, setup screens and map are exercised
// without a person at the mouse.
//
//   node starter_flow.mjs --site <build/GeneralsMD> [--pack <built starterpack dir>] [--steps "<steps>"] [--out <dir>]
//                         [--port 8941] [--arg -noshellmap] [--wait 60] [--log boot.log]
//
// --pack     copied to <site>/starterpack (skip it when the build target starter_pack already put it there)
// --steps    space separated steps; coordinates are the 800x600 design resolution of the menus, mapped onto the canvas
//              m:X,Y   move the mouse          c:X,Y   click (move, press, release)       d:X,Y   double click
//              w:SEC   wait                     k:Key   press a key (Playwright names: Escape, Enter, KeyA)
//              r:X1,Y1,X2,Y2  drag a selection box   R:X,Y  right click (move/attack order)
//              s:NAME  screenshot to <out>/NAME.png      l:PATTERN  wait until a log line matches the regex (30 s)
//              n       print the new log lines (done after every step anyway; this just marks a point)
// Use tools/wnd_pos.py <pack> <layout.wnd> to find the centre of a button.
//
// Needs Playwright (NODE_PATH=.../node_modules) and a Chromium: CHROMIUM_PATH, the Linux sandbox's, or Playwright's own.
import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

const require = createRequire(import.meta.url);
let playwright;
try { playwright = require('playwright'); }
catch { playwright = require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright')); }

const opt = { port: 8941, wait: 60, out: '.', steps: 'm:300,300 w:1 s:menu', args: [], army: [], userfile: [], viewport: [1100, 800] };
{
	const a = process.argv.slice(2);
	for (let i = 0; i < a.length; ++i) {
		switch (a[i]) {
			case '--site': opt.site = a[++i]; break;
			case '--pack': opt.pack = a[++i]; break;
			case '--steps': opt.steps = a[++i]; break;
			case '--out': opt.out = a[++i]; break;
			case '--port': opt.port = Number(a[++i]); break;
			case '--wait': opt.wait = Number(a[++i]); break;
			case '--arg': opt.args.push(a[++i]); break;
			case '--viewport': opt.viewport = a[++i].split('x').map(Number); break;
			case '--army': opt.army.push(a[++i]); break;
			case '--userfile': opt.userfile.push(a[++i]); break;
			case '--log': opt.log = a[++i]; break;
			case '--page': opt.page = a[++i]; break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site) { console.error('--site is required'); process.exit(2); }
	opt.page = opt.page || 'z_generals.html';
}
fs.mkdirSync(opt.out, { recursive: true });
if (opt.pack) fs.cpSync(opt.pack, path.join(opt.site, 'starterpack'), { recursive: true });

const here = path.dirname(new URL(import.meta.url).pathname);
const server = spawn('python3', [path.join(here, '..', '..', 'GeneralsMD', 'Code', 'Main', 'web', 'serve.py'), '--port', String(opt.port), '--dir', opt.site],
	{ env: { ...process.env, SERVE_QUIET: '1' }, stdio: 'inherit' });
process.on('exit', () => server.kill());
await new Promise((r) => setTimeout(r, 800));

const chromium = [process.env.CHROMIUM_PATH, '/opt/pw-browsers/chromium-1194/chrome-linux/chrome'].find((p) => p && fs.existsSync(p));
const browser = await playwright.chromium.launch({
	executablePath: chromium,
	args: ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
		'--enable-webgl', '--enable-features=SharedArrayBuffer'],
});
const context = await browser.newContext({ viewport: { width: opt.viewport[0], height: opt.viewport[1] } });
const page = await context.newPage();
const logs = [];
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2).padStart(7);
page.on('console', (m) => logs.push(stamp() + ' ' + m.type() + ': ' + m.text()));
page.on('pageerror', (e) => logs.push(stamp() + ' PAGEERROR ' + e.message));
page.on('worker', (w) => w.on('console', (m) => logs.push(stamp() + ' [worker] ' + m.text())));

const NOISE = /Error finding file|Got so far|Error opening directory|FramePacer|Pathfind cell|gameFrame call/;
let printed = 0;
function flush(label) {
	const fresh = logs.slice(printed).filter((l) => !NOISE.test(l));
	printed = logs.length;
	if (opt.log) fs.writeFileSync(opt.log, logs.join('\n') + '\n');
	if (label) console.log('--- ' + label + ' (' + fresh.length + ' lines) ---');
	for (const l of fresh) console.log(l.slice(0, 300));
}

const armyArgs = opt.army.flatMap((f) => ['-army', '/game/armies/' + path.basename(f).toLowerCase()]);
const query = ['picker=input', ...[...opt.args, ...armyArgs].map((a) => 'arg=' + encodeURIComponent(a))].join('&');
await page.goto(`http://127.0.0.1:${opt.port}/${opt.page}?${query}`);
await page.waitForFunction(() => document.getElementById('download-starter'), null, { timeout: 30000 });
await page.click('#download-starter');
await page.waitForFunction(() => /^(Ready|Download failed|Could not|This server)/.test(document.getElementById('state-starter').textContent), null, { timeout: 180000 });
console.log('starter content: ' + (await page.textContent('#state-starter')));
await page.waitForFunction(() => !document.getElementById('play').disabled, null, { timeout: 30000 });
for (const f of opt.army) {
	const b64 = fs.readFileSync(f).toString('base64');
	await page.evaluate(async ({ name, b64 }) => {
		const bin = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
		const root = await navigator.storage.getDirectory();
		const game = await root.getDirectoryHandle('game', { create: true });
		const dir = await game.getDirectoryHandle('armies', { create: true });
		const handle = await dir.getFileHandle(name, { create: true });
		const w = await handle.createWritable();
		await w.write(bin);
		await w.close();
	}, { name: path.basename(f).toLowerCase(), b64 });
	console.log('package in the browser: /game/armies/' + path.basename(f).toLowerCase());
}
for (const spec of opt.userfile) {
	const [rel, local] = spec.split('=');
	const b64 = fs.readFileSync(local).toString('base64');
	await page.evaluate(async ({ rel, b64 }) => {
		const bin = Uint8Array.from(atob(b64), (c) => c.charCodeAt(0));
		let dir = await (await navigator.storage.getDirectory()).getDirectoryHandle('userdata', { create: true });
		const parts = rel.split('/');
		for (const d of parts.slice(0, -1)) dir = await dir.getDirectoryHandle(d, { create: true });
		const w = await (await dir.getFileHandle(parts[parts.length - 1], { create: true })).createWritable();
		await w.write(bin);
		await w.close();
	}, { rel, b64 });
	console.log('user data file: /userdata/' + rel);
}
logs.push(stamp() + ' --- Play ---');
await page.click('#play');
try {
	await page.waitForFunction(() => window.__zhFrames > 30, null, { timeout: opt.wait * 1000 });
} catch { console.log('the engine did not start rendering frames within ' + opt.wait + ' s'); }
flush('engine start');

async function printReport(label) {
	const text = await page.evaluate(async () => {
		try {
			const root = await navigator.storage.getDirectory();
			const dir = await root.getDirectoryHandle('userdata');
			const file = await (await dir.getFileHandle('armyreport.json')).getFile();
			return await file.text();
		} catch (e) {
			const names = [];
			try { const root = await navigator.storage.getDirectory(); for await (const [n] of root.entries()) names.push(n);
				const ud = await root.getDirectoryHandle('userdata'); for await (const [n] of ud.entries()) names.push('userdata/' + n); } catch (e2) {}
			return 'no ArmyReport.json (' + e.message + '); OPFS: ' + names.join(' ');
		}
	});
	console.log('--- ' + label + ' ---');
	console.log(text);
}
await printReport('ArmyReport.json');

async function point(x, y) {
	const box = await page.locator('#canvas').boundingBox();
	return { x: box.x + (x / 800) * box.width, y: box.y + (y / 600) * box.height };
}

for (const step of opt.steps.split(/\s+/).filter(Boolean)) {
	const [kind, arg = ''] = [step.split(':')[0], step.slice(step.indexOf(':') + 1)];
	const xy = () => arg.split(',').map(Number);
	switch (kind) {
		case 'm': { const p = await point(...xy()); await page.mouse.move(p.x, p.y, { steps: 4 }); break; }
		case 'c': { const p = await point(...xy()); await page.mouse.move(p.x, p.y, { steps: 4 }); await page.waitForTimeout(150);
			await page.mouse.down(); await page.waitForTimeout(80); await page.mouse.up(); break; }
		case 'r': { const [x1, y1, x2, y2] = xy(); const a = await point(x1, y1), b = await point(x2, y2);
			await page.mouse.move(a.x, a.y, { steps: 3 }); await page.mouse.down(); await page.mouse.move(b.x, b.y, { steps: 8 });
			await page.waitForTimeout(100); await page.mouse.up(); break; }
		case 'R': { const p = await point(...xy()); await page.mouse.move(p.x, p.y, { steps: 4 }); await page.waitForTimeout(150);
			await page.mouse.down({ button: 'right' }); await page.waitForTimeout(80); await page.mouse.up({ button: 'right' }); break; }
		case 'd': { const p = await point(...xy()); await page.mouse.move(p.x, p.y, { steps: 4 }); await page.mouse.dblclick(p.x, p.y); break; }
		case 'w': await page.waitForTimeout(Number(arg) * 1000); break;
		case 'k': await page.keyboard.press(arg); break;
		case 'K': { const [key, ms] = arg.split('/'); await page.keyboard.down(key); await page.waitForTimeout(Number(ms)); await page.keyboard.up(key); break; }   // hold a key for MS
		case 'W': await page.mouse.wheel(0, Number(arg)); break;   // mouse wheel (pixels, positive = down) at the pointer
		case 's': await page.screenshot({ path: path.join(opt.out, arg + '.png') }); break;
		case 'l': {
			const re = new RegExp(arg);
			const until = Date.now() + 30000;
			while (Date.now() < until && !logs.some((l) => re.test(l))) await page.waitForTimeout(250);
			if (!logs.some((l) => re.test(l))) console.log('(no log line matched /' + arg + '/ in 30 s)');
			break;
		}
		case 'U': {   // list the browser's persistent user data (OPFS /userdata), and print files whose path matches ARG
			const out = await page.evaluate(async (pat) => {
				const lines = [];
				const walk = async (dir, pre) => {
					for await (const [n, h] of dir.entries()) {
						if (h.kind === 'directory') await walk(h, pre + n + '/');
						else {
							const f = await h.getFile();
							lines.push(pre + n + ' (' + f.size + ')');
							if (pat && new RegExp(pat, 'i').test(pre + n)) lines.push('    ' + (await f.text()).slice(0, 4000).replace(/\n/g, '\n    '));
						}
					}
				};
				try { await walk(await (await navigator.storage.getDirectory()).getDirectoryHandle('userdata'), ''); } catch (e) { lines.push('error ' + e.message); }
				return lines.join('\n');
			}, arg === 'U:' ? '' : arg);
			console.log(out);
			break;
		}
		case 'n': break;
		default: console.log('unknown step ' + step);
	}
	flush(step);
}

const errors = await page.evaluate(() => ({ shown: !document.getElementById('errors').hidden, text: document.getElementById('errors-log').textContent })).catch(() => null);
if (errors && errors.shown) console.log('launcher error panel: ' + errors.text.slice(0, 600));
if (opt.log) fs.writeFileSync(opt.log, logs.join('\n') + '\n');
const trapped = logs.some((l) => /RuntimeError|unreachable|memory access out of bounds|Aborted\(|abort\(/.test(l));
console.log(trapped ? 'RESULT: wasm trap or abort' : 'RESULT: no trap');
await browser.close();
process.exit(trapped ? 1 : 0);
