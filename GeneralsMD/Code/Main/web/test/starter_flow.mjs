// Drives the real game (z_generals) with the free starter content through the launcher and the menus, and prints the
// engine's console log after every step. This is how the starter pack's menus, setup screens and map are exercised
// without a person at the mouse.
//
//   node starter_flow.mjs --site <build/GeneralsMD> [--pack <built starterpack dir>] [--steps "<steps>"] [--out <dir>]
//                         [--port 8941] [--arg -noshellmap] [--wait 60] [--log boot.log] [--profile <dir>] [--size 1100x800]
//                         [--dpr 2] [--resolution auto|fit|fitsharp|1280x720 ...] [--expect-trap]
//
// --profile  keep the browser profile (OPFS: saves, options, the downloaded starter content) in this directory, so that
//            a later run starts with what an earlier one left
// --expect-trap  the run is supposed to end in a wasm trap or abort (-webcrashtest=trap): fail if it does not
// --dpr      the display's device pixel ratio (2 = Retina); --resolution picks the launcher's Resolution setting
// --pack     copied to <site>/starterpack (skip it when the build target starter_pack already put it there)
// --steps    space separated steps; coordinates are the 800x600 design resolution of the menus, mapped onto the canvas
//              m:X,Y   move the mouse          c:X,Y   click (move, press, release)       d:X,Y   double click
//              w:SEC   wait                     k:Key   press a key (Playwright names: Escape, Enter, KeyA)
//              r:X1,Y1,X2,Y2  drag a selection box   R:X,Y  right click (move/attack order)
//              s:NAME  screenshot to <out>/NAME.png      l:PATTERN  wait until a log line matches the regex (30 s)
//              n       print the new log lines (done after every step anyway; this just marks a point)
//   shell and platform steps:
//              q:SELECTOR  click an element of the page (launcher buttons: #play-again, #quit, #screenshot ...)
//              g       wait until the engine renders frames again (after a page reload that restarts the game: "Play again")
//              kd:Key / ku:Key   key down / up         wheel:DY   mouse wheel (pixels, + = down)
//              bd:right / bu:right   mouse button down / up (left, middle, right)
//              v:EXPR  evaluate a JavaScript expression in the page and print the result (~ stands for a space)
//              e:EXPR  like v:, but the step fails (exit code 1) unless the result is truthy
//              u:EXPR  wait (30 s) until the expression is truthy, else the step fails
//              t:ID=REGEX   wait (30 s) until the text of element #ID matches, else the step fails
//              reload  reload the page (same browser profile: OPFS, localStorage stay) and press Play again
//              size:W,H   resize the browser window (viewport)
//              fs      toggle the game's fullscreen button        hide / show   tab hidden / visible (visibilitychange)
//              x:SEC   just wait (like w:)    L:REGEX  fail unless a log line matches (no waiting)    N:REGEX  fail if one does
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

const opt = { port: 8941, wait: 60, out: '.', steps: 'm:300,300 w:1 s:menu', args: [] };
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
			case '--log': opt.log = a[++i]; break;
			case '--page': opt.page = a[++i]; break;
			case '--stack-on': opt.stackOn = a[++i]; break;
			case '--profile': opt.profile = a[++i]; break;
			case '--size': opt.size = a[++i].split('x').map(Number); break;
			case '--dpr': opt.dpr = Number(a[++i]); break;
			case '--resolution': opt.resolution = a[++i]; break;
			case '--expect-trap': opt.expectTrap = true; break;
			default: console.error('unknown option ' + a[i]); process.exit(2);
		}
	}
	if (!opt.site) { console.error('--site is required'); process.exit(2); }
	opt.page = opt.page || 'z_generals.html';
}
fs.mkdirSync(opt.out, { recursive: true });
if (opt.pack) fs.cpSync(opt.pack, path.join(opt.site, 'starterpack'), { recursive: true });

const here = path.dirname(new URL(import.meta.url).pathname);
const server = spawn('python3', [path.join(here, '..', 'serve.py'), '--port', String(opt.port), '--dir', opt.site],
	{ env: { ...process.env, SERVE_QUIET: '1' }, stdio: 'inherit' });
process.on('exit', () => server.kill());
await new Promise((r) => setTimeout(r, 800));

const chromium = [process.env.CHROMIUM_PATH, '/opt/pw-browsers/chromium-1194/chrome-linux/chrome'].find((p) => p && fs.existsSync(p));
const launchArgs = ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
	'--enable-webgl', '--enable-features=SharedArrayBuffer'];
const viewport = { width: (opt.size || [1100, 800])[0], height: (opt.size || [1100, 800])[1] };
const contextOptions = { viewport, deviceScaleFactor: opt.dpr || 1 };
let browser, context;
if (opt.profile) {
	context = await playwright.chromium.launchPersistentContext(path.resolve(opt.profile), { executablePath: chromium, args: launchArgs, ...contextOptions });
	browser = { close: () => context.close() };
} else {
	browser = await playwright.chromium.launch({ executablePath: chromium, args: launchArgs });
	context = await browser.newContext(contextOptions);
}
const page = context.pages()[0] || await context.newPage();
const logs = [];
// --stack-on RE: print a JavaScript stack (with the wasm function names of a debug build) when a console line matches
if (opt.stackOn) await context.addInitScript((re) => {
	const rx = new RegExp(re);
	for (const k of ['log', 'warn', 'error']) {
		const orig = console[k].bind(console);
		console[k] = (...args) => { orig(...args); if (rx.test(args.join(' '))) orig('STACK ' + new Error().stack); };
	}
}, opt.stackOn);
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(2).padStart(7);
page.on('console', (m) => logs.push(stamp() + ' ' + m.type() + ': ' + m.text()));
page.on('dialog', (d) => d.accept().catch(() => {}));	// "leave this page?" while a game runs
page.on('pageerror', (e) => logs.push(stamp() + ' PAGEERROR ' + (e.stack || e.message)));
let workerCount = 0;
page.on('worker', (w) => { workerCount++; w.on('console', (m) => logs.push(stamp() + ' [worker] ' + m.text())); });

const NOISE = /Error finding file|Got so far|Error opening directory|FramePacer|Pathfind cell|gameFrame call/;
let printed = 0;
function flush(label) {
	const fresh = logs.slice(printed).filter((l) => !NOISE.test(l));
	printed = logs.length;
	if (label) console.log('--- ' + label + ' (' + fresh.length + ' lines) ---');
	for (const l of fresh) console.log(l.slice(0, 300));
}

const query = ['picker=input', ...opt.args.map((a) => 'arg=' + encodeURIComponent(a))].join('&');
const url = `http://127.0.0.1:${opt.port}/${opt.page}?${query}`;
await page.goto(url);
await page.waitForFunction(() => document.getElementById('download-starter'), null, { timeout: 30000 });
await page.click('#download-starter');
await page.waitForFunction(() => /^(Ready|Download failed|Could not|This server)/.test(document.getElementById('state-starter').textContent), null, { timeout: 180000 });
console.log('starter content: ' + (await page.textContent('#state-starter')));

// Presses Play on the launcher and waits for the engine to render frames.
async function startGame() {
	await page.waitForFunction(() => !document.getElementById('play').disabled, null, { timeout: 30000 });
	if (opt.resolution) await page.selectOption('#resolution', opt.resolution);
	logs.push(stamp() + ' --- Play ---');
	await page.click('#play');
	try {
		await page.waitForFunction(() => window.__zhFrames > 30, null, { timeout: opt.wait * 1000 });
	} catch { console.log('the engine did not start rendering frames within ' + opt.wait + ' s'); }
}
await startGame();
flush('engine start');
let failures = 0;
function fail(what) { failures++; console.log('FAIL: ' + what); }

async function point(x, y) {
	const box = await page.locator('#canvas').boundingBox();
	return { x: box.x + (x / 800) * box.width, y: box.y + (y / 600) * box.height };
}

for (const step of opt.steps.split(/\s+/).filter(Boolean)) {
	let [kind, arg = ''] = [step.split(':')[0], step.slice(step.indexOf(':') + 1)];
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
		case 's': await page.screenshot({ path: path.join(opt.out, arg + '.png') }); break;
		case 'l': {
			const re = new RegExp(arg.replace(/~/g, ' '));
			const until = Date.now() + 30000;
			while (Date.now() < until && !logs.some((l) => re.test(l))) await page.waitForTimeout(250);
			if (!logs.some((l) => re.test(l))) console.log('(no log line matched /' + arg + '/ in 30 s)');
			break;
		}
		case 'n': break;
		case 'q': await page.click(arg, { force: true }); break;
		case 'g': try { await page.waitForFunction(() => window.__zhFrames > 30, null, { timeout: opt.wait * 1000 }); } catch { fail('the engine did not render frames within ' + opt.wait + ' s'); } break;
		case 'kd': await page.keyboard.down(arg); break;
		case 'ku': await page.keyboard.up(arg); break;
		case 'wheel': await page.mouse.wheel(0, Number(arg)); break;
		case 'bd': await page.mouse.down({ button: arg || 'left' }); break;
		case 'bu': await page.mouse.up({ button: arg || 'left' }); break;
		case 'v': case 'e': {
			let result;
			arg = arg.replace(/~/g, ' ');	// steps are split at spaces: ~ stands for a space in expressions
			try { result = await page.evaluate(`(async () => (${arg}))()`); } catch (err) { result = 'EXCEPTION ' + err.message; }
			console.log((kind === 'e' ? 'expect ' : 'value ') + arg + ' => ' + JSON.stringify(result));
			if (kind === 'e' && !result) fail('expression is not truthy: ' + arg);
			break;
		}
		case 'u': {
			arg = arg.replace(/~/g, ' ');
			try {
				await page.waitForFunction(arg, null, { timeout: 30000, polling: 250 });
				console.log('until ' + arg + ' => true');
			} catch { fail('never truthy within 30 s: ' + arg); }
			break;
		}
		case 't': {
			const eq = arg.indexOf('=');
			const id = arg.slice(0, eq), re = new RegExp(arg.slice(eq + 1));
			try {
				await page.waitForFunction(([i, r]) => new RegExp(r).test((document.getElementById(i) || {}).textContent || ''), [id, re.source], { timeout: 30000 });
				console.log('#' + id + ': ' + (await page.textContent('#' + id)).slice(0, 200));
			} catch { fail('#' + id + ' never matched /' + re.source + '/: ' + ((await page.textContent('#' + id).catch(() => '')) || '').slice(0, 200)); }
			break;
		}
		case 'reload': {
			await page.reload();
			await page.waitForFunction(() => document.getElementById('play'), null, { timeout: 30000 });
			await startGame();
			break;
		}
		case 'size': { const [w, h] = xy(); await page.setViewportSize({ width: w, height: h }); break; }
		case 'fs': await page.click('#fullscreen', { force: true }).catch(() => page.evaluate(() => document.getElementById('fullscreen').click())); break;
		case 'hide': case 'show': {
			await page.evaluate((hidden) => {
				Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => (hidden ? 'hidden' : 'visible') });
				Object.defineProperty(document, 'hidden', { configurable: true, get: () => hidden });
				document.dispatchEvent(new Event('visibilitychange'));
			}, kind === 'hide');
			break;
		}
		case 'x': await page.waitForTimeout(Number(arg) * 1000); break;
		case 'L': arg = arg.replace(/~/g, ' '); if (!logs.some((l) => new RegExp(arg).test(l))) fail('no log line matches /' + arg + '/'); break;
		case 'N': { arg = arg.replace(/~/g, ' '); const bad = logs.find((l) => new RegExp(arg).test(l)); if (bad) fail('unexpected log line: ' + bad.slice(0, 200)); break; }
		default: console.log('unknown step ' + step);
	}
	flush(step);
}

const errors = await page.evaluate(() => ({ shown: !document.getElementById('errors').hidden, text: document.getElementById('errors-log').textContent })).catch(() => null);
if (errors && errors.shown) console.log('launcher error panel: ' + errors.text.slice(0, 600));
if (opt.log) fs.writeFileSync(opt.log, logs.join('\n') + '\n');
const trapped = logs.some((l) => /RuntimeError|unreachable|memory access out of bounds|Aborted\(|abort\(/.test(l));
console.log('workers created: ' + workerCount);
console.log(trapped ? 'RESULT: wasm trap or abort' : 'RESULT: no trap');
if (opt.expectTrap && !trapped) { failures++; console.log('FAIL: the engine was expected to trap'); }
if (failures) console.log('RESULT: ' + failures + ' failed expectation(s)');
await browser.close();
process.exit((trapped && !opt.expectTrap) || failures ? 1 : 0);
