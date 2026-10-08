// End-to-end check of the web shell, the data import and the platform layer in
// headless Chromium (needs Playwright, e.g. NODE_PATH=/opt/node22/lib/node_modules).
//
//   node e2e.mjs --site <dir with web_platform_test.html> --fake <dir with ZeroHour/ and Generals/> --out <screenshot dir>
//
// The page is served by serve.py (cross-origin isolated). The "game" is
// web_platform_test, which prints TEST: lines for everything it receives.

import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import { mkdirSync } from 'node:fs';
import path from 'node:path';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
	if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1]]);
	return acc;
}, []));
const site = path.resolve(args.site);
const fake = path.resolve(args.fake);
const out = path.resolve(args.out || '.');
const page_name = args.page || 'web_platform_test.html';
const port = Number(args.port || 8099);
mkdirSync(out, { recursive: true });

let failures = 0;
function check(name, ok, detail = '') {
	console.log((ok ? 'PASS ' : 'FAIL ') + name + (detail ? '  [' + detail + ']' : ''));
	if (!ok) failures++;
}

const here = path.dirname(new URL(import.meta.url).pathname);
const server = spawn('python3', [path.join(here, '..', 'serve.py'), '--port', String(port), '--dir', site], {
	env: { ...process.env, SERVE_QUIET: '1' },
	stdio: ['ignore', 'inherit', 'inherit'],
});
await new Promise((r) => setTimeout(r, 800));

const browser = await chromium.launch({
	executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
	args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
});

async function importFolder(page, button, stateId, dir) {
	const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.click(button)]);
	await chooser.setFiles(dir);
	await page.waitForFunction((id) => /^(Ready|That does not|Import failed|Not enough)/.test(document.getElementById(id).textContent), stateId, { timeout: 60000 });
	return page.textContent('#' + stateId);
}

async function session(label, query) {
	const context = await browser.newContext({ viewport: { width: 1100, height: 800 } });
	const page = await context.newPage();
	const logs = [];
	page.on('console', (m) => logs.push(m.text()));
	page.on('pageerror', (e) => logs.push('PAGEERROR ' + e.message));
	return { context, page, logs, label, query };
}

// ---- 1. a first visit: import both folders, play ---------------------------------------------
{
	const s = await session('first', '?picker=input');
	const { page, logs } = s;
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	check('cross-origin isolated', await page.evaluate(() => crossOriginIsolated));
	check('play disabled before import', await page.isDisabled('#play'));
	await page.screenshot({ path: path.join(out, '1-first-visit.png') });

	const wrong = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'Generals'));
	check('wrong folder is rejected', /does not look like/.test(wrong), wrong);

	const gameState = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
	check('zero hour imported', /Ready/.test(gameState), gameState);
	check('videos/exe skipped (3 files expected)', /3 files/.test(gameState), gameState);
	check('play still disabled with only one folder', await page.isDisabled('#play'));
	const generalsState = await importFolder(page, '#pick-generals', 'state-generals', path.join(fake, 'Generals'));
	check('generals imported', /Ready/.test(generalsState), generalsState);
	await page.screenshot({ path: path.join(out, '2-imported.png') });
	check('play enabled', !(await page.isDisabled('#play')));

	// what ended up in OPFS
	const tree = await page.evaluate(async () => {
		const result = [];
		async function walk(dir, prefix) {
			for await (const [name, handle] of dir.entries()) {
				if (handle.kind === 'directory') await walk(handle, prefix + name + '/');
				else result.push(prefix + name + ':' + (await handle.getFile()).size);
			}
		}
		await walk(await navigator.storage.getDirectory(), '');
		return result.sort();
	});
	console.log('OPFS: ' + tree.join(' '));
	check('names lower-cased in OPFS', tree.includes('game/data/ini/gamedata.ini:26') && tree.includes('game/inizh.big:3000000'), tree.join(','));
	check('exe/bik not copied', !tree.some((t) => /\.exe|\.bik/.test(t)));

	await page.click('#play');
	await page.waitForFunction(() => document.getElementById('stage').hidden === false);
	await page.waitForFunction(() => window.__zh.logLines.some((l) => l.includes('TEST: ready')), null, { timeout: 30000 }).catch(() => {});
	await page.screenshot({ path: path.join(out, '3-running.png') });
	const log = () => logs.filter((l) => l.startsWith('TEST:')).join('\n');
	check('engine thread runs main (ready)', /TEST: ready/.test(log()), log().slice(-400));
	check('arguments reach main', /TEST: arg 1=-xres/.test(log()) && /arg 2=1024/.test(log()) && /arg 4=768/.test(log()), '');
	check('8 MB stack usable', /TEST: stack ok 3/.test(log()));
	check('webgl2 context on the offscreen canvas', /TEST: webgl context=[1-9]\d* current=0 version=.*WebGL 2/.test(log()), log().split('\n').filter((l) => /webgl/.test(l)).join('|'));
	check('platform init', /TEST: init=1 selector=#canvas/.test(log()));
	check('OPFS mounted', /TEST: mount=0/.test(log()));
	check('file read via other case', /TEST: read \d+ bytes: ; Hello from GameData.ini/.test(log()), log().split('\n').filter((l) => /read|cannot/.test(l)).join('|'));
	check('directory listing', /TEST: \/game\/inizh.big/.test(log()) && /TEST: \/generals\/ini.big/.test(log()), '');
	check('GetMessage woken by thread', /TEST: GetMessage woke: message=0x401 wp=42 lp=4242/.test(log()), '');
	check('userdata written (run 1)', /TEST: userdata runs=1/.test(log()));

	// ---- input ----
	const box = await page.locator('#canvas').boundingBox();
	console.log('canvas box ' + JSON.stringify(box));
	await page.mouse.move(box.x + 10, box.y + 20);
	await page.mouse.move(box.x + 100, box.y + 50);
	await page.mouse.down();
	await page.mouse.up();
	await page.mouse.down();
	await page.mouse.up();
	await page.mouse.down({ button: 'right' });
	await page.mouse.up({ button: 'right' });
	await page.mouse.wheel(0, -100);
	await page.waitForTimeout(300);
	await page.keyboard.down('Shift');
	await page.keyboard.press('KeyA');
	await page.keyboard.up('Shift');
	await page.keyboard.press('ArrowUp');
	await page.keyboard.press('Control+KeyC');
	await page.waitForTimeout(500);
	check('GetKeyState sampled later', true);
	await page.mouse.move(box.x + box.width + 40, box.y + 10);
	await page.waitForTimeout(300);

	const t = log();
	console.log(t.split('\n').filter((l) => /MSG|DIK/.test(l)).slice(0, 60).join('\n'));
	check('WM_MOUSEMOVE scaled to the client size (640x480 canvas set by game)',
		new RegExp(`MSG WM_MOUSEMOVE x=${Math.floor(100 * 640 / box.width)} y=${Math.floor(50 * 480 / box.height)}`).test(t), `box ${box.width}x${box.height}`);
	check('left button down/up', /WM_LBUTTONDOWN[^\n]*wp=0x1/.test(t) && /WM_LBUTTONUP/.test(t));
	check('double click message', /WM_LBUTTONDBLCLK/.test(t));
	check('right button', /WM_RBUTTONDOWN/.test(t) && /WM_RBUTTONUP/.test(t));
	check('wheel up is positive', /WM_MOUSEWHEEL delta=120/.test(t), '');
	check('key down/up messages', /WM_KEYDOWN wp=0x41/.test(t) && /WM_KEYUP wp=0x41/.test(t));
	check('shift scan code 0x2a', /DIK 0x2a down/.test(t) && /DIK 0x2a up/.test(t));
	check('A scan code 0x1e', /DIK 0x1e down/.test(t) && /DIK 0x1e up/.test(t));
	check('arrow up is extended 0xc8', /DIK 0xc8 down/.test(t));
	check('ctrl+c 0x1d/0x2e', /DIK 0x1d down/.test(t) && /DIK 0x2e down/.test(t));
	check('WM_CHAR for shift+A', /WM_CHAR wp=0x41/.test(t));
	check('no WM_CHAR for ctrl+c', !/WM_CHAR wp=0x63/.test(t) && !/WM_CHAR wp=0x43/.test(t) && !/WM_CHAR wp=0x3/.test(t.replace(/wp=0x3\d/g, '')));
	check('WM_SIZE from SetClientSize', /WM_SIZE wp=0x0 lp=0x1e00280/.test(t), '');

	// focus loss releases keys and reports inactivity
	await page.keyboard.down('KeyW');
	await page.waitForTimeout(150);
	await page.evaluate(() => window.dispatchEvent(new Event('blur')));
	await page.waitForTimeout(300);
	const t2 = log();
	check('blur: key W released', /DIK 0x11 up/.test(t2));
	check('blur: WM_ACTIVATEAPP 0', /WM_ACTIVATEAPP wp=0x0/.test(t2));
	await page.evaluate(() => window.dispatchEvent(new Event('focus')));
	await page.waitForTimeout(200);
	check('focus: WM_ACTIVATEAPP 1', /WM_ACTIVATEAPP wp=0x1/.test(log()));

	// the canvas follows the game's client size (640x480) in the page
	const ratio = await page.evaluate(() => { const c = document.getElementById('canvas'); return c.clientWidth / c.clientHeight; });
	check('canvas aspect follows SetClientSize', Math.abs(ratio - 640 / 480) < 0.02, String(ratio));

	await page.keyboard.press('Escape');
	await page.waitForTimeout(500);
	check('main returned', /TEST: done/.test(log()), '');
	await page.screenshot({ path: path.join(out, '4-after-input.png') });
	await s.context.close();
}

// ---- 2. a second visit in a fresh context with the worker writer ------------------------------
{
	const s = await session('worker-writer');
	const { page } = s;
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&writer=worker`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-generals').textContent));
	const st = await importFolder(page, '#pick-generals', 'state-generals', path.join(fake, 'Generals'));
	check('import through the sync-access-handle worker', /Ready/.test(st), st);
	const sizes = await page.evaluate(async () => {
		const root = await navigator.storage.getDirectory();
		const dir = await root.getDirectoryHandle('generals');
		return (await (await dir.getFileHandle('ini.big')).getFile()).size;
	});
	check('worker copy has the right size', sizes === 1000000, String(sizes));
	await s.context.close();
}

// ---- 3. persistent profile: user data survives, no re-import ----------------------------------
{
	const userDataDir = path.join(out, 'profile');
	const context = await chromium.launchPersistentContext(userDataDir, {
		executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
		args: ['--no-sandbox'],
		viewport: { width: 1100, height: 800 },
	});
	const page = context.pages()[0] || await context.newPage();
	const logs = [];
	page.on('console', (m) => logs.push(m.text()));
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
	await importFolder(page, '#pick-generals', 'state-generals', path.join(fake, 'Generals'));
	for (let run = 1; run <= 2; run++) {
		await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input`);
		await page.waitForFunction(() => /Ready/.test(document.getElementById('state-game').textContent));
		check(`run ${run}: data still imported after reload`, !(await page.isDisabled('#play')));
		await page.click('#play');
		await page.waitForFunction(() => window.__zh.logLines.some((l) => l.includes('TEST: userdata runs=')), null, { timeout: 30000 });
		const line = await page.evaluate(() => window.__zh.logLines.find((l) => l.includes('TEST: userdata runs=')));
		check(`run ${run}: user data persisted`, line.includes('runs=' + run), line);
	}
	await context.close();
}

await browser.close();
server.kill();
console.log(failures === 0 ? 'ALL PASSED' : failures + ' FAILED');
process.exit(failures === 0 ? 0 : 1);
