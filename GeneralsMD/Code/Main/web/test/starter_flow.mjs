// Drives the real game (z_generals) with the free starter content through the launcher and the menus, and prints the
// engine's console log after every step. This is how the starter pack's menus, setup screens and map are exercised
// without a person at the mouse.
//
//   --steps-file FILE  read the steps from a file ('#' starts a comment); --repl FILE  keep the session open after the steps and run the
//                      lines appended to FILE ('quit' ends it), for steering a match by hand
//   node starter_flow.mjs --site <build/GeneralsMD> [--pack <built starterpack dir>] [--steps "<steps>"] [--out <dir>]
//                         [--options Key=Value,Key=Value] [--port 8941] [--arg -noshellmap] [--wait 60] [--log boot.log] [--profile <dir>] [--size 1100x800]
//                         [--dpr 2] [--resolution auto|fit|fitsharp|1280x720 ...] [--expect-trap]
//
// --chromium-flags "<flags>"  extra Chromium command line flags, space separated (default: $ZH_CHROMIUM_FLAGS); with
//            --arg -dxwebgl2-backend=webgpu, the WebGPU ones: "--enable-unsafe-webgpu --enable-features=Vulkan
//            --use-vulkan=swiftshader --use-webgpu-adapter=swiftshader --use-angle=swiftshader"
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
//              F:N     wait for N more logic frames (the log shows a frame every 100)   f:N     wait (up to 40 min) until the game has reached logic frame N     W:RE   wait (up to 40 min) until a log line matches
//              Wt:SEC:RE  like W:, but the step fails after SEC seconds
//              K:N:REGEX  fail unless at least N log lines match
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
			case '--steps-file': opt.steps = fs.readFileSync(a[++i], 'utf8').replace(/#[^\n]*/g, ' '); break;
			case '--repl': opt.repl = a[++i]; break;
			case '--watchdog': opt.watchdog = Number(a[++i]); break;
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
			case '--options': opt.options = a[++i]; break;
			case '--expect-trap': opt.expectTrap = true; break;
			case '--chromium-flags': opt.chromiumFlags = a[++i]; break;
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
// Extra flags are added; several --enable-features are merged into one (Chromium would keep only the last).
const launchArgs = (() => {
	const all = ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
		'--enable-webgl', '--enable-features=SharedArrayBuffer',
		...(opt.chromiumFlags ?? process.env.ZH_CHROMIUM_FLAGS ?? '').split(/\s+/).filter(Boolean)];
	const features = all.filter((f) => f.startsWith('--enable-features=')).map((f) => f.slice(18));
	return [...all.filter((f) => !f.startsWith('--enable-features=')), '--enable-features=' + features.join(',')];
})();
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
// with --log the lines are appended to the file as they arrive, so a run that is cut short still leaves its log
const logPush = logs.push.bind(logs);
logs.push = (...lines) => { if (opt.log) fs.appendFileSync(opt.log, lines.join('\n') + '\n'); return logPush(...lines); };
// --stack-on RE: print a JavaScript stack (with the wasm function names of a debug build) when a console line matches
if (opt.stackOn) await context.addInitScript((re) => {
	const rx = new RegExp(re);
	for (const k of ['log', 'warn', 'error']) {
		const orig = console[k].bind(console);
		console[k] = (...args) => { orig(...args); if (rx.test(args.join(' '))) orig('STACK ' + new Error().stack); };
	}
}, opt.stackOn);
const t0 = Date.now();
if (opt.log) fs.writeFileSync(opt.log, '');
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

// --options: write Options.ini lines (the game reads them when it starts) before the first Play
async function writeOptions() {
	if (!opt.options) return;
	const text = opt.options.split(',').map((kv) => kv.replace('=', ' = ')).join('\r\n') + '\r\n';
	await page.evaluate(async (t) => {
		let dir = await navigator.storage.getDirectory();
		dir = await dir.getDirectoryHandle('userdata', { create: true });
		dir = await dir.getDirectoryHandle('command and conquer generals zero hour data', { create: true });
		const h = await dir.getFileHandle('options.ini', { create: true });
		const w = await h.createWritable();
		await w.write(t);
		await w.close();
	}, text);
	console.log('options.ini: ' + opt.options);
}
await writeOptions();

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

// a match that has ended (the score screen is up) never reaches later frames: the waits for frames stop then
function matchEnded() { return logs.some((l) => /Shell:push\(Menus\/ScoreScreen/.test(l)); }

function lastFrame() {
	for (let i = logs.length - 1; i >= 0 && i > logs.length - 4000; --i) {
		const m = /(?:Appended CRC on frame|ASSIST frame) (\d+)/.exec(logs[i]);
		if (m) return Number(m[1]);
	}
	return 0;
}

// --watchdog SEC: when the game has not logged a new logic frame for SEC seconds (it logs one every 100 frames), print
// the call stacks of the engine's threads once (the P step): that is a hang.
if (opt.watchdog) {
	let seen = -1, since = Date.now(), reported = false;
	setInterval(() => {
		const f = lastFrame();
		if (f !== seen) { seen = f; since = Date.now(); return; }
		if (!reported && f > 0 && Date.now() - since > opt.watchdog * 1000 && !matchEnded()) {
			reported = true;
			console.log('WATCHDOG: no new frame after ' + f + ' for ' + opt.watchdog + ' s');
			runStep('P').catch((e) => console.log('watchdog: ' + e));
		}
	}, 5000);
}

async function point(x, y) {
	const box = await page.locator('#canvas').boundingBox();
	return { x: box.x + (x / 800) * box.width, y: box.y + (y / 600) * box.height };
}

async function runStep(step) {
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
		case 'f': {	// wait (up to 40 min) until the game has reached logic frame N ("Appended CRC on frame N" in the log)
			const target = Number(arg);
			const until = Date.now() + 40 * 60000;
			while (Date.now() < until && lastFrame() < target && !matchEnded()) await page.waitForTimeout(500);
			if (lastFrame() < target && !matchEnded()) fail('frame ' + target + ' not reached, at ' + lastFrame());
			break;
		}
		case 'F': {	// wait for N more logic frames (F:N), counted from the last "Appended CRC" line
			const target = lastFrame() + Number(arg);
			const until = Date.now() + 40 * 60000;
			while (Date.now() < until && lastFrame() < target && !matchEnded()) await page.waitForTimeout(500);
			if (lastFrame() < target && !matchEnded()) fail('frame ' + target + ' not reached, at ' + lastFrame());
			break;
		}
		case 'W': {	// wait (up to 40 min) until a log line matches the regex; fails when none does
			const re = new RegExp(arg.replace(/~/g, ' '));
			const until = Date.now() + 40 * 60000;
			while (Date.now() < until && !logs.some((l) => re.test(l))) await page.waitForTimeout(500);
			if (!logs.some((l) => re.test(l))) fail('no log line matched /' + arg + '/ in 40 min');
			break;
		}
		case 'Wt': {	// Wt:SEC:RE  like W:, but fails after SEC seconds
			const sec = Number(arg.slice(0, arg.indexOf(':')));
			const re = new RegExp(arg.slice(arg.indexOf(':') + 1).replace(/~/g, ' '));
			const until = Date.now() + sec * 1000;
			while (Date.now() < until && !logs.some((l) => re.test(l))) await page.waitForTimeout(500);
			if (!logs.some((l) => re.test(l))) fail('no log line matched /' + re.source + '/ in ' + sec + ' s');
			break;
		}
		case 'P': {	// break into the engine's thread(s) and print the call stack (finds a hang; needs the browser's DevTools protocol)
			try {
				const bcdp = await browser.newBrowserCDPSession();
				const { targetInfos } = await bcdp.send('Target.getTargets');
				const sessions = new Map();
				bcdp.on('Target.receivedMessageFromTarget', ({ sessionId, message }) => {
					const m = JSON.parse(message);
					if (m.method === 'Debugger.paused') {
						console.log('PAUSED worker ' + sessions.get(sessionId) + ', call stack:');
						for (const f of m.params.callFrames.slice(0, 30)) console.log('  ' + (f.functionName || '?') + ' ' + (f.url || '').split('/').pop() + ':' + f.location.lineNumber + ':' + f.location.columnNumber);
					}
				});
				for (const t of targetInfos.filter((x) => x.type === 'worker')) {
					const { sessionId } = await bcdp.send('Target.attachToTarget', { targetId: t.targetId, flatten: false });
					sessions.set(sessionId, t.title || t.url);
					for (const [id, method] of [[1, 'Debugger.enable'], [2, 'Debugger.pause']])
						await bcdp.send('Target.sendMessageToTarget', { sessionId, message: JSON.stringify({ id, method }) });
				}
				await page.waitForTimeout(3000);
			} catch (err) { console.log('P: failed: ' + err); }
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
		case 'K': {	// K:N:REGEX  fail unless at least N log lines match
			const m = /^(\d+):(.*)$/.exec(arg.replace(/~/g, ' '));
			const n = logs.filter((l) => new RegExp(m[2]).test(l)).length;
			if (n < Number(m[1])) fail('only ' + n + ' log lines match /' + m[2] + '/, expected ' + m[1]);
			break;
		}
		case 'N': { arg = arg.replace(/~/g, ' '); const bad = logs.find((l) => new RegExp(arg).test(l)); if (bad) fail('unexpected log line: ' + bad.slice(0, 200)); break; }
		default: console.log('unknown step ' + step);
	}
	flush(step);
}

for (const step of opt.steps.split(/\s+/).filter(Boolean)) await runStep(step);

// --repl FILE: keep the game running and execute the lines that are appended to FILE (one list of steps per line, '#'
// starts a comment); prints 'DONE n' after each line. The line 'quit' ends the session.
if (opt.repl) {
	fs.writeFileSync(opt.repl, '');
	let offset = 0, done = false, n = 0;
	console.log('REPL ready: ' + opt.repl);
	while (!done) {
		const text = fs.readFileSync(opt.repl, 'utf8');
		const fresh = text.slice(offset);
		const end = fresh.lastIndexOf('\n');
		if (end < 0) { await new Promise((r) => setTimeout(r, 300)); continue; }
		offset += end + 1;
		for (const line of fresh.slice(0, end).split('\n')) {
			if (line.trim() === 'quit') { done = true; break; }
			for (const step of line.replace(/#.*/, ' ').split(/\s+/).filter(Boolean)) await runStep(step);
			console.log('DONE ' + (++n));
		}
	}
}

const errors = await page.evaluate(() => ({ shown: !document.getElementById('errors').hidden, text: document.getElementById('errors-log').textContent })).catch(() => null);
if (errors && errors.shown) console.log('launcher error panel: ' + errors.text.slice(0, 600));
const trapped = logs.some((l) => /RuntimeError|unreachable|memory access out of bounds|Aborted\(|abort\(/.test(l));
console.log('workers created: ' + workerCount);
console.log(trapped ? 'RESULT: wasm trap or abort' : 'RESULT: no trap');
if (opt.expectTrap && !trapped) { failures++; console.log('FAIL: the engine was expected to trap'); }
if (failures) console.log('RESULT: ' + failures + ' failed expectation(s)');
await browser.close();
process.exit((trapped && !opt.expectTrap) || failures ? 1 : 0);
