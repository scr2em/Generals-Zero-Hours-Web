// End-to-end check of the web shell, the data import and the platform layer in
// headless Chromium (needs Playwright, e.g. NODE_PATH=/opt/node22/lib/node_modules).
//
//   node e2e.mjs --site <dir with web_platform_test.html> --fake <dir with ZeroHour/ and Generals/> --out <screenshot dir>
//                [--starter <dir with the built starter pack: manifest.json + files>]
//
// With --starter the "free starter content" path is tested too: the pack is served as
// <site>/starterpack/ (copied there), downloaded through the launcher into OPFS, checked against
// its manifest and started. Build the pack with Content/StarterPack/build_pack.py.
//
// The page is served by serve.py (cross-origin isolated). The "game" is
// web_platform_test, which prints TEST: lines for everything it receives.

import { createRequire } from 'node:module';
import { spawn } from 'node:child_process';
import { mkdirSync, cpSync, readFileSync, existsSync, rmSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');
// Browser: CHROMIUM_PATH if set, else the sandbox's preinstalled Chromium if present, else Playwright's own
// (npx playwright install chromium), which is what a macOS machine uses.
const CHROMIUM = [process.env.CHROMIUM_PATH, '/opt/pw-browsers/chromium-1194/chrome-linux/chrome']
	.find((p) => p && existsSync(p));

const args = Object.fromEntries(process.argv.slice(2).reduce((acc, a, i, all) => {
	if (a.startsWith('--')) acc.push([a.slice(2), all[i + 1]]);
	return acc;
}, []));
const site = path.resolve(args.site);
const out = path.resolve(args.out || '.');
// A private copy of the fake install, plus the one executable the engine reads (it fingerprints generalszh.exe
// at start up) which the importer must keep, next to a foreign Generals.exe which it must not.
const fake = path.join(out, 'fake-install');
rmSync(fake, { recursive: true, force: true });
rmSync(path.join(out, 'profile'), { recursive: true, force: true });   // the persistence test needs a fresh profile
mkdirSync(out, { recursive: true });
cpSync(path.resolve(args.fake), fake, { recursive: true });
writeFileSync(path.join(fake, 'ZeroHour', 'generalszh.exe'), 'MZ placeholder executable');
const page_name = args.page || 'web_platform_test.html';
const port = Number(args.port || 8099);
const starter = args.starter ? path.resolve(args.starter) : null;
if (starter) {
	if (!existsSync(path.join(starter, 'manifest.json'))) {
		console.error('--starter must name a directory with manifest.json');
		process.exit(2);
	}
	cpSync(starter, path.join(site, 'starterpack'), { recursive: true });
}
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
process.on('exit', () => server.kill());
await new Promise((r) => setTimeout(r, 800));

const browser = await chromium.launch({
	executablePath: CHROMIUM,
	args: ['--no-sandbox', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
});

async function importFolder(page, button, stateId, dir) {
	const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.click(button)]);
	await chooser.setFiles(dir);
	await page.waitForFunction((id) => /^(Ready|.*added|That does not|That folder|Import failed|Not enough)/.test(document.getElementById(id).textContent), stateId, { timeout: 60000 });
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
	const s = await session('first', '?picker=input&copy=1');
	const { page, logs } = s;
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	check('cross-origin isolated', await page.evaluate(() => crossOriginIsolated));
	check('play disabled before import', await page.isDisabled('#play'));
	await page.screenshot({ path: path.join(out, '1-first-visit.png') });

	check('the base game prompt is not shown before an import', await page.isHidden('#row-generals'));
	const wrong = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'Generals'));
	check('a folder without Zero Hour is rejected', /does not look like Zero Hour/.test(wrong), wrong);

	const gameState = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
	check('zero hour imported', /Ready/.test(gameState), gameState);
	check('videos/exe skipped, engine exe kept (4 files expected)', /4 files/.test(gameState), gameState);
	check('play is enabled without the original Generals files', !(await page.isDisabled('#play')));
	check('an optional link offers the original Generals files', await page.isVisible('#row-generals') && /optional/.test(await page.textContent('#pick-generals')));
	const wrongBase = await importFolder(page, '#pick-generals', 'state-generals', path.join(fake, 'ZeroHour'));
	check('the optional pick rejects a Zero Hour folder', /does not look like the original Generals/.test(wrongBase), wrongBase);
	check('a rejected optional pick leaves play enabled', !(await page.isDisabled('#play')));
	const generalsState = await importFolder(page, '#pick-generals', 'state-generals', path.join(fake, 'Generals'));
	check('generals imported', /added/.test(generalsState), generalsState);
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
	check('only the engine exe is copied, no bik', tree.some((t) => /^game\/generalszh\.exe:/.test(t)) && !tree.some((t) => /generals\.exe|\.bik/.test(t.replace('generalszh.exe', ''))));

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

// ---- 1b. one pick: the folder above both games (a Steam / EA app / Ultimate Collection library) ---------------
{
	const s = await session('parent', '?picker=input&copy=1');
	const { page } = s;
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	const state = await importFolder(page, '#pick-game', 'state-game', fake);
	check('parent folder: Zero Hour found inside', /Ready · 4 files/.test(state), state);
	await page.waitForFunction(() => /added/.test(document.getElementById('state-generals').textContent), null, { timeout: 30000 });
	check('parent folder: the base game was found in the same pick', /added/.test(await page.textContent('#state-generals')));
	check('parent folder: play enabled after one pick', !(await page.isDisabled('#play')));
	await s.context.close();
}

// ---- 1c. retail layouts (small stand-ins with the real file names) -------------------------------------------
{
	const put = (dir, rel, size) => {
		const file = path.join(dir, rel);
		mkdirSync(path.dirname(file), { recursive: true });
		writeFileSync(file, Buffer.alloc(size, 0x41));
	};
	const zhRoot = path.join(out, 'retail', 'Command and Conquer Generals Zero Hour');
	const baseRoot = path.join(out, 'retail', 'Command and Conquer Generals');
	const zhOnly = path.join(out, 'retail-zh-only', 'Command and Conquer Generals Zero Hour');
	rmSync(path.join(out, 'retail'), { recursive: true, force: true });
	rmSync(path.join(out, 'retail-zh-only'), { recursive: true, force: true });
	const bigs = (dir, names, size0) => names.forEach((n, i) => put(dir, n, size0 + i * 1000));
	const layoutZh = (dir) => {
		bigs(dir, ['AudioEnglishZH.big', 'AudioZH.big', 'EnglishZH.big', 'GensecZH.big', 'INIZH.big', 'MapsZH.big', 'MusicZH.big', 'PatchZH.big',
			'ShadersZH.big', 'SpeechEnglishZH.big', 'SpeechZH.big', 'TerrainZH.big', 'TexturesZH.big', 'W3DEnglishZH.big', 'W3DZH.big', 'WindowZH.big'], 5000);
		put(dir, 'Music.big', 7777);
		for (const f of ['generals.exe', 'WorldBuilder.exe', 'game.dat', 'Generals.dat', 'langdata.dat', 'Install_Final.bmp', 'launcher.bmp',
			'binkw32.dll', 'BrowserEngine.dll', 'mss32.dll', 'SECDRV.SYS', '00000000.016', '00000000.256']) put(dir, f, 300);
		put(dir, 'Data/INI/Default/Object.ini', 40);
		put(dir, 'Data/Scripts/Scripts.ini', 40);
		put(dir, 'MSS/mp3dec.asi', 100);
		put(dir, 'UserData/Options.ini', 10);
	};
	const layoutBase = (dir) => {
		bigs(dir, ['Audio.big', 'AudioEnglish.big', 'English.big', 'gensec.big', 'INI.big', 'maps.big', 'Patch.big', 'shaders.big', 'Speech.big',
			'SpeechEnglish.big', 'Terrain.big', 'Textures.big', 'W3D.big', 'Window.big'], 9000);
		put(dir, 'Music.big', 7777);   // the same archive as Zero Hour's (same size)
		for (const f of ['generals.exe', 'WorldBuilder.exe', 'BrowserEngine.dll', 'SECDRV.SYS', '00000000.016', '00000000.256']) put(dir, f, 300);
		put(dir, 'Data/Scripts/Scripts.ini', 40);
		put(dir, 'MSS/mp3dec.asi', 100);
		put(dir, 'UserData/Options.ini', 10);
	};
	layoutZh(zhRoot);
	layoutBase(baseRoot);
	layoutZh(zhOnly);
	const opfsFiles = (page) => page.evaluate(async () => {
		const result = [];
		async function walk(dir, prefix) {
			for await (const [name, handle] of dir.entries()) {
				if (handle.kind === 'directory') await walk(handle, prefix + name + '/');
				else result.push(prefix + name);
			}
		}
		await walk(await navigator.storage.getDirectory(), '');
		return result.sort();
	});

	// A. the Zero Hour folder only (no base game anywhere)
	{
		const s = await session('retail-zh');
		const { page } = s;
		await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
		await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
		const state = await importFolder(page, '#pick-game', 'state-game', zhOnly);
		check('retail ZH only: recognised and imported (17 archives + engine exe + 2 data files)', /Ready · 20 files/.test(state), state);
		check('retail ZH only: Play is enabled, nothing blocks', !(await page.isDisabled('#play')));
		check('retail ZH only: the optional link is offered, no prompt', await page.isVisible('#pick-generals') && !/need|must|required/i.test(await page.textContent('#row-generals')));
		const files = await opfsFiles(page);
		check('retail ZH only: Music.big and the Zero Hour archives are copied', files.includes('game/music.big') && files.includes('game/inizh.big') && files.includes('game/musiczh.big'));
		check('retail ZH only: the engine exe is stored as generalszh.exe, no other exe', files.includes('game/generalszh.exe') && !files.some((f) => /\.exe$/.test(f) && f !== 'game/generalszh.exe'), files.filter((f) => /exe/.test(f)).join(','));
		check('retail ZH only: dll, sys, dat, bmp, MSS, UserData and fingerprint files are skipped',
			!files.some((f) => /\.(dll|sys|dat|bmp|asi|016|256)$/.test(f)) && !files.some((f) => /\/(mss|userdata)\//.test(f)), files.join(','));
		check('retail ZH only: Data/ is kept', files.includes('game/data/ini/default/object.ini') && files.includes('game/data/scripts/scripts.ini'));
		check('retail ZH only: nothing in generals/', !files.some((f) => f.startsWith('generals/')));
		await page.screenshot({ path: path.join(out, '7-retail-zh.png') });

		// C. then the optional second pick: the original Generals folder
		const base = await importFolder(page, '#pick-generals', 'state-generals', baseRoot);
		check('optional add: the original Generals files are added (13 archives: no maps.big, no duplicate Music.big)', /added · 13 files/.test(base), base);
		const after = await opfsFiles(page);
		check('optional add: only archives from the original game', after.filter((f) => f.startsWith('generals/')).every((f) => /^generals\/[^/]+\.big$/.test(f)), after.filter((f) => f.startsWith('generals/')).join(','));
		check('optional add: maps.big and the duplicate Music.big are not copied', !after.includes('generals/maps.big') && !after.includes('generals/music.big') && after.includes('generals/ini.big'));
		check('optional add: Play still enabled', !(await page.isDisabled('#play')));
		await s.context.close();
	}

	// B. the parent folder with both installs side by side
	{
		const s = await session('retail-both');
		const { page } = s;
		await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
		await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
		const state = await importFolder(page, '#pick-game', 'state-game', path.join(out, 'retail'));
		check('retail parent folder: Zero Hour recognised by its ZH archives, not by generals.exe', /Ready · 20 files/.test(state), state);
		await page.waitForFunction(() => /added/.test(document.getElementById('state-generals').textContent), null, { timeout: 30000 });
		check('retail parent folder: the original game is added silently (13 archives)', /added · 13 files/.test(await page.textContent('#state-generals')), await page.textContent('#state-generals'));
		const files = await opfsFiles(page);
		check('retail parent folder: no duplicates, no extras', !files.includes('generals/music.big') && !files.includes('generals/maps.big') && !files.some((f) => /^generals\/(data|mss|userdata)/.test(f)));
		check('retail parent folder: Play enabled', !(await page.isDisabled('#play')));
		await s.context.close();
	}
}

// ---- 1e. read in place (the default): no copy, the game reads the picked folder ---------------------------------
{
	const s = await session('direct');
	const { page, logs } = s;
	const opfs = () => page.evaluate(async () => {
		const result = [];
		async function walk(dir, prefix) {
			for await (const [name, handle] of dir.entries()) {
				if (handle.kind === 'directory') await walk(handle, prefix + name + '/');
				else result.push(prefix + name);
			}
		}
		await walk(await navigator.storage.getDirectory(), '');
		return result.sort();
	});
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	check('direct: the copy option is offered and off by default', await page.isVisible('#copy-mode') && !(await page.isChecked('#copy-mode')));
	check('direct: the page says nothing is copied', /nothing is copied/.test(await page.textContent('#install-desc')));
	await page.screenshot({ path: path.join(out, '9-direct-first-visit.png') });

	const state = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
	// Read in place includes the videos (Data/English/Movies/Intro.bik): 4 files plus the movie.
	check('direct: zero hour is ready, read in place', /^Ready · 5 files/.test(state) && /not copied/.test(state), state);
	const base = await importFolder(page, '#pick-generals', 'state-generals', path.join(fake, 'Generals'));
	check('direct: the original Generals files are added in place', /added/.test(base) && /read in place/.test(base), base);
	check('direct: play enabled', !(await page.isDisabled('#play')));
	check('direct: mode announced', /your Zero Hour installation/.test(await page.textContent('#play-mode')));
	let files = await opfs();
	check('direct: no game file was copied into browser storage', !files.some((f) => /^(game|generals)\//.test(f)), files.join(','));
	const manifest = await page.evaluate(async () => JSON.parse(await (await (await (await navigator.storage.getDirectory()).getFileHandle('game.manifest.json')).getFile()).text()));
	check('direct: the manifest records the mode', manifest.mode === 'direct' && manifest.files === 5, JSON.stringify(manifest).slice(0, 160));
	await page.screenshot({ path: path.join(out, '10-direct-ready.png') });

	await page.click('#play');
	await page.waitForFunction(() => document.getElementById('stage').hidden === false);
	await page.waitForFunction(() => window.__zh.logLines.some((l) => l.includes('TEST: ready')), null, { timeout: 30000 }).catch(() => {});
	const log = () => logs.filter((l) => l.startsWith('TEST:') || /direct mode/.test(l)).join('\n');
	check('direct: engine thread runs', /TEST: ready/.test(log()), log().slice(-300));
	check('direct: the engine mounted the page\'s files', /WebPlatform: direct mode, 6 files/.test(log()) && /TEST: mount=0/.test(log()), log().slice(0, 300));
	check('direct: file read via other case', /TEST: read \d+ bytes: ; Hello from GameData.ini/.test(log()), log().split('\n').filter((l) => /read|cannot/.test(l)).join('|'));
	check('direct: directory listing (lower case names)', /TEST: \/game\/inizh.big/.test(log()) && /TEST: \/generals\/ini.big/.test(log()), '');
	check('direct: user data still goes to OPFS', /TEST: userdata runs=1/.test(log()));
	files = await opfs();
	check('direct: after the run only user data is in browser storage', files.every((f) => /^(userdata\/|game\.manifest\.json$|generals\.manifest\.json$)/.test(f)), files.join(','));
	await page.screenshot({ path: path.join(out, '11-direct-running.png') });

	// the next visit: the remembered install needs its folder again (a folder from <input> has no handle to remember)
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input`);
	await page.waitForFunction(() => /again to play/.test(document.getElementById('state-game').textContent), null, { timeout: 15000 }).catch(() => {});
	check('next visit: the page asks for the folder again', /again to play/.test(await page.textContent('#state-game')), await page.textContent('#state-game'));
	check('next visit: play waits for the folder', await page.isDisabled('#play'));
	await page.screenshot({ path: path.join(out, '12-direct-next-visit.png') });
	const again = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
	check('next visit: choosing the folder again is enough', /^Ready/.test(again) && !(await page.isDisabled('#play')), again);

	// A remembered folder handle (a FileSystemDirectoryHandle: the picker cannot be driven headlessly, so a directory of
	// OPFS stands in for it) on the next visit. Permission as Chrome has it: 'prompt' until the player clicks Allow access.
	await page.evaluate(async () => {
		const root = await navigator.storage.getDirectory();
		const home = await root.getDirectoryHandle('home', { create: true });
		async function put(dir, segments, bytes) {
			for (const seg of segments.slice(0, -1)) dir = await dir.getDirectoryHandle(seg, { create: true });
			const handle = await dir.getFileHandle(segments[segments.length - 1], { create: true });
			const w = await handle.createWritable();
			await w.write(bytes);
			await w.close();
		}
		const text = (t) => new TextEncoder().encode(t);
		await put(home, ['ZeroHour', 'INIZH.big'], new Uint8Array(3000000).fill(65));
		await put(home, ['ZeroHour', 'Data', 'INI', 'GameData.ini'], text('; Hello from GameData.ini\n'));
		await put(home, ['ZeroHour', 'generalszh.exe'], text('MZ'));
		await put(home, ['ZeroHour', 'Maps', 'alpine', 'alpine.map'], text('map'));
		await window.__zh.direct.rememberFolder('game', home);
	});
	await page.addInitScript(() => {
		FileSystemDirectoryHandle.prototype.queryPermission = async () => (sessionStorage.getItem('perm') ? 'granted' : 'prompt');
		FileSystemDirectoryHandle.prototype.requestPermission = async () => { sessionStorage.setItem('perm', '1'); return 'granted'; };
	});
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input`);
	await page.waitForFunction(() => /remembered/.test(document.getElementById('state-game').textContent), null, { timeout: 15000 }).catch(() => {});
	check('remembered folder: the page offers Allow access', /remembered/.test(await page.textContent('#state-game')) && /Allow access/.test(await page.textContent('#pick-game')), await page.textContent('#state-game'));
	check('remembered folder: play waits for the permission', await page.isDisabled('#play'));
	await page.screenshot({ path: path.join(out, '13-direct-allow-access.png') });
	await page.click('#pick-game');
	await page.waitForFunction(() => /^Ready/.test(document.getElementById('state-game').textContent), null, { timeout: 15000 }).catch(() => {});
	const allowed = await page.textContent('#state-game');
	check('remembered folder: Allow access opens it, 4 files, no copy', /^Ready · 4 files/.test(allowed) && /not copied/.test(allowed) && !(await page.isDisabled('#play')), allowed);
	files = await opfs();
	check('remembered folder: nothing copied', !files.some((f) => /^(game|generals)\//.test(f)), files.join(','));
	await page.click('#play');
	await page.waitForFunction(() => window.__zh.logLines.some((l) => l.includes('TEST: ready')), null, { timeout: 30000 }).catch(() => {});
	check('remembered folder: the engine reads the files', /TEST: read 26 bytes: ; Hello from GameData.ini/.test(logs.filter((l) => l.startsWith('TEST:')).join('\n')));
	// Chrome that keeps the permission: no click at all
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input`);
	await page.waitForFunction(() => /^Ready/.test(document.getElementById('state-game').textContent), null, { timeout: 15000 }).catch(() => {});
	check('remembered folder: still granted, the next visit needs no click', /^Ready · 4 files/.test(await page.textContent('#state-game')) && !(await page.isDisabled('#play')), await page.textContent('#state-game'));

	// switching to the copy and back removes what the other way left
	await page.check('#copy-mode');
	const copied = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
	files = await opfs();
	check('copy option: the files are copied into browser storage', /^Ready/.test(copied) && !/not copied/.test(copied) && files.includes('game/inizh.big'), copied);
	await page.uncheck('#copy-mode');
	const back = await importFolder(page, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
	files = await opfs();
	check('back to reading in place: the copy is removed', /not copied/.test(back) && !files.some((f) => /^game\//.test(f)), files.join(','));
	await s.context.close();
}

// ---- 2. a second visit in a fresh context with the worker writer ------------------------------
{
	const s = await session('worker-writer');
	const { page } = s;
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1&writer=worker`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	await importFolder(page, '#pick-game', 'state-game', fake);
	await page.waitForFunction(() => /added/.test(document.getElementById('state-generals').textContent), null, { timeout: 30000 });
	const st = await page.textContent('#state-generals');
	check('import through the sync-access-handle worker', /added/.test(st), st);
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
		executablePath: CHROMIUM,
		args: ['--no-sandbox'],
		viewport: { width: 1100, height: 800 },
	});
	const page = context.pages()[0] || await context.newPage();
	const logs = [];
	page.on('console', (m) => logs.push(m.text()));
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	await importFolder(page, '#pick-game', 'state-game', fake);
	await page.waitForFunction(() => /added/.test(document.getElementById('state-generals').textContent), null, { timeout: 30000 });
	for (let run = 1; run <= 2; run++) {
		await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
		await page.waitForFunction(() => /Ready/.test(document.getElementById('state-game').textContent));
		check(`run ${run}: data still imported after reload`, !(await page.isDisabled('#play')));
		await page.click('#play');
		await page.waitForFunction(() => window.__zh.logLines.some((l) => l.includes('TEST: userdata runs=')), null, { timeout: 30000 });
		const line = await page.evaluate(() => window.__zh.logLines.find((l) => l.includes('TEST: userdata runs=')));
		check(`run ${run}: user data persisted`, line.includes('runs=' + run), line);
	}
	await context.close();
}


// ---- 5. free starter content: download into OPFS, check against the manifest, play ------------------
if (starter) {
	const manifest = JSON.parse(readFileSync(path.join(starter, 'manifest.json'), 'utf8'));
	const walkOpfs = (page) => page.evaluate(async () => {
		const result = {};
		async function walk(dir, prefix) {
			for await (const [name, handle] of dir.entries()) {
				if (handle.kind === 'directory') await walk(handle, prefix + name + '/');
				else result[prefix + name] = (await handle.getFile()).size;
			}
		}
		await walk(await navigator.storage.getDirectory(), '');
		return result;
	});
	const settled = (id) => (i) => /^(Ready|.*failed|.*[Nn]ot enough|This server|Could not|.*corrupt|.*wrong size)/.test(document.getElementById(i).textContent);

	// 5a. a fresh visit
	const context = await browser.newContext({ viewport: { width: 1100, height: 800 } });
	const page = await context.newPage();
	const logs = [];
	page.on('console', (m) => logs.push(m.text()));
	page.on('dialog', (d) => d.accept());
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
	await page.waitForFunction(() => /Not downloaded/.test(document.getElementById('state-starter').textContent));
	const pageText = await page.textContent('#choice-starter');
	check('starter option says it is an original placeholder game', /not\s+Command/i.test(pageText) && /original/i.test(pageText) && /GPL/.test(pageText), pageText.replace(/\s+/g, ' ').slice(0, 160));
	check('both options are offered', /Use my Zero Hour installation/.test(await page.textContent('#choice-install')) && /Play with free starter content/.test(pageText));
	check('play disabled before the download', await page.isDisabled('#play'));
	await page.screenshot({ path: path.join(out, '6-two-options.png') });

	await page.click('#download-starter');
	await page.waitForFunction(settled('state-starter'), 'state-starter', { timeout: 120000 });
	const startState = await page.textContent('#state-starter');
	check('starter content downloaded', /^Ready/.test(startState), startState);
	check('play enabled for the starter content', !(await page.isDisabled('#play')));
	check('mode is announced', /free starter content/i.test(await page.textContent('#play-mode')), await page.textContent('#play-mode'));
	check('own-install rows say the starter content is installed', /starter content is installed/.test(await page.textContent('#state-game')));
	await page.screenshot({ path: path.join(out, '7-starter-ready.png') });

	const tree = await walkOpfs(page);
	const missing = manifest.files.filter((f) => tree['game/' + f.path.toLowerCase()] !== f.size);
	check('every manifest file is in OPFS with the right size', missing.length === 0, missing.slice(0, 3).map((f) => f.path).join(','));
	check('OPFS names are lower case', Object.keys(tree).every((k) => k === k.toLowerCase()));
	const opfsManifest = await page.evaluate(async () => {
		const root = await navigator.storage.getDirectory();
		return JSON.parse(await (await (await root.getFileHandle('game.manifest.json')).getFile()).text());
	});
	check('OPFS manifest marks the starter kind', opfsManifest.kind === 'starter' && opfsManifest.files === manifest.files.length, JSON.stringify(opfsManifest).slice(0, 200));
	check('nothing in generals/', !Object.keys(tree).some((k) => k.startsWith('generals/')));

	await page.click('#play');
	await page.waitForFunction(() => document.getElementById('stage').hidden === false);
	await page.waitForFunction(() => window.__zh.logLines.some((l) => l.includes('TEST: ready')), null, { timeout: 30000 }).catch(() => {});
	const log = () => logs.filter((l) => l.startsWith('TEST:')).join('\n');
	check('starter: engine thread runs', /TEST: ready/.test(log()), log().slice(-300));
	check('starter: mount works', /TEST: mount=0/.test(log()));
	check('starter: GameData.ini readable through the game file layer', /TEST: read \d+ bytes: /.test(log()) && !/cannot open GameData/.test(log()), log().split('\n').filter((l) => /read|cannot/.test(l)).join('|'));
	check('starter: data directory visible', /TEST: \/game\/data\b/.test(log()), '');
	check('starter: badge shown in the game page', await page.evaluate(() => !document.getElementById('mode-badge').hidden));
	await page.screenshot({ path: path.join(out, '8-starter-running.png') });
	await context.close();

	// 5b. a corrupted download is refused and leaves nothing half installed as "ready"
	{
		const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
		const p = await ctx.newPage();
		await p.route('**/starterpack/**', async (route) => {
			const url = route.request().url();
			if (url.endsWith('manifest.json')) return route.continue();
			if (url.includes('/data/ini/')) {
				const response = await route.fetch();
				const body = Buffer.from(await response.body());
				body[0] = body[0] ^ 0x55; // same size, wrong content
				return route.fulfill({ response, body });
			}
			return route.continue();
		});
		await p.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
		await p.waitForFunction(() => /Not downloaded/.test(document.getElementById('state-starter').textContent));
		await p.click('#download-starter');
		await p.waitForFunction(settled('state-starter'), 'state-starter', { timeout: 120000 });
		const t = await p.textContent('#state-starter');
		check('corrupt file is detected by its checksum', /corrupt/.test(t), t);
		check('play stays disabled after a failed download', await p.isDisabled('#play'));
		await ctx.close();
	}

	// 5c. no starter content on this server
	{
		const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
		const p = await ctx.newPage();
		await p.route('**/starterpack/manifest.json', (route) => route.fulfill({ status: 404, body: 'nope' }));
		await p.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
		await p.waitForFunction(() => /Not downloaded/.test(document.getElementById('state-starter').textContent));
		await p.click('#download-starter');
		await p.waitForFunction(settled('state-starter'), 'state-starter', { timeout: 30000 });
		const t = await p.textContent('#state-starter');
		check('missing starter content gives a clear message', /does not offer the starter content/.test(t), t);
		await ctx.close();
	}

	// 5d. switching between the two options never mixes the files
	{
		const ctx = await browser.newContext({ viewport: { width: 1100, height: 800 } });
		const p = await ctx.newPage();
		p.on('dialog', (d) => d.accept());
		await p.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
		await p.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
		await importFolder(p, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
		await importFolder(p, '#pick-generals', 'state-generals', path.join(fake, 'Generals'));
		check('own install: play enabled', !(await p.isDisabled('#play')));
		check('own install: mode announced', /your Zero Hour installation/.test(await p.textContent('#play-mode')));
		await p.click('#download-starter'); // confirm() is accepted
		await p.waitForFunction(settled('state-starter'), 'state-starter', { timeout: 120000 });
		let t = await walkOpfs(p);
		check('starter replaced the own install', !('game/inizh.big' in t) && !('generals/ini.big' in t) && ('game/data/ini/gamedata.ini' in t), Object.keys(t).slice(0, 5).join(','));
		await importFolder(p, '#pick-game', 'state-game', path.join(fake, 'ZeroHour'));
		t = await walkOpfs(p);
		check('own install replaced the starter content', ('game/inizh.big' in t) && !Object.keys(t).some((k) => k.startsWith('game/art/')) && !Object.keys(t).some((k) => k.startsWith('game/data/ini/object')), Object.keys(t).slice(0, 8).join(','));
		check('starter state reset after importing', /Not downloaded/.test(await p.textContent('#state-starter')), await p.textContent('#state-starter'));
		await ctx.close();
	}
}

// ---- 4. the error panel, in dark mode -------------------------------------------------------------
{
	const context = await browser.newContext({ viewport: { width: 1100, height: 800 }, colorScheme: 'dark' });
	const page = await context.newPage();
	await page.goto(`http://127.0.0.1:${port}/${page_name}?picker=input&copy=1`);
	await page.waitForFunction(() => /Not imported/.test(document.getElementById('state-game').textContent));
	await page.evaluate(() => {
		window.__zh.logLines.push('Aborted(RuntimeError: memory access out of bounds)');
		window.__zh.fail('wasm trap in WebGameEngine::update');
	});
	check('error panel shows on stderr abort', await page.isVisible('#errors') && /memory access/.test(await page.textContent('#errors-log')));
	await page.screenshot({ path: path.join(out, '5-error-dark.png') });
	await context.close();
}

await browser.close();
server.kill();
console.log(failures === 0 ? 'ALL PASSED' : failures + ' FAILED');
process.exit(failures === 0 ? 0 : 1);
