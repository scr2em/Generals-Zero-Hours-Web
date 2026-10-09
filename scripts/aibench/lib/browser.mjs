// Drives the WebAssembly game in headless Chromium (Playwright): puts the game data in the browser's file
// storage once, then plays one match per page and collects the "AIMATCH_RESULT" line the engine prints.
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';

const require = createRequire(import.meta.url);
function loadPlaywright() {
	try { return require('playwright'); } catch { /* fall through */ }
	const roots = [process.env.NODE_PATH, '/opt/node22/lib/node_modules', '/usr/local/lib/node_modules'].filter(Boolean);
	for (const r of roots) {
		for (const root of r.split(path.delimiter)) {
			try { return require(path.join(root, 'playwright')); } catch { /* next */ }
		}
	}
	throw new Error('The playwright npm package was not found. Install it (npm i playwright) or set NODE_PATH.');
}

function findChromium(explicit) {
	const candidates = [explicit, process.env.CHROMIUM_PATH, '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', '/opt/pw-browsers/chromium/chrome',
		'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'];
	return candidates.find((p) => p && fs.existsSync(p));   // undefined: Playwright's own browser
}

export class Browser {
	// options: port, profileDir, chromium, headful
	constructor(options) { this.o = options; this.context = null; }

	async open() {
		const playwright = loadPlaywright();
		fs.mkdirSync(this.o.profileDir, { recursive: true });
		const executablePath = findChromium(this.o.chromium);
		this.context = await playwright.chromium.launchPersistentContext(this.o.profileDir, {
			executablePath,
			headless: !this.o.headful,
			viewport: { width: 800, height: 600 },
			// No rendering happens in a match, but the page still creates a WebGL canvas; SwiftShader is the software GL.
			args: ['--no-sandbox', '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
				'--enable-webgl', '--enable-features=SharedArrayBuffer'],
		});
	}

	async close() { if (this.context) await this.context.close().catch(() => {}); this.context = null; }

	url(build, query) { return `http://127.0.0.1:${this.o.port}/${build}/z_generals.html?${query}`; }

	// Makes sure the game data is in the browser: the starter content downloaded from the build, or the files of a
	// Zero Hour install copied from `data` ({ zeroHour, generals }; generals may be null). Done once; the
	// browser profile keeps it between runs.
	async prepareData(build, data, log) {
		const page = await this.context.newPage();
		try {
			await page.goto(this.url(build, 'picker=input&copy=1'));
			await page.waitForFunction(() => window.__zh && document.getElementById('state-game'), null, { timeout: 60000 });
			await page.waitForTimeout(800);
			const status = await page.evaluate(async () => { await window.__zh.refreshStatus(); return window.__zh.importer.getStatus(); });
			const have = status && status.game ? status.game : null;
			if (data === 'starter') {
				if (have && have.kind === 'starter') { log(`[${build}] starter content already in the browser (${have.files} files)`); return; }
				log(`[${build}] downloading the starter content into the browser ...`);
				await page.click('#download-starter');
				await page.waitForFunction(() => /^(Ready|Download failed|Could not|This server)/.test(document.getElementById('state-starter').textContent), null, { timeout: 300000 });
				const text = await page.textContent('#state-starter');
				if (!/^Ready/.test(text)) throw new Error('starter content: ' + text);
				return;
			}
			if (have && have.kind !== 'starter') { log(`[${build}] game data already in the browser (${have.files} files)`); return; }
			for (const [btn, id, full] of [['#pick-game', 'state-game', data.zeroHour], ['#pick-generals', 'state-generals', data.generals]]) {
				if (!full || !fs.existsSync(full)) {
					if (id === 'state-game') throw new Error(`${full} does not exist: give the Zero Hour install with --zh (or $ZH_PATH), or --data with a ZeroHour folder in it`);
					continue;
				}
				log(`[${build}] copying ${full} into the browser (once; this takes a while) ...`);
				const [chooser] = await Promise.all([page.waitForEvent('filechooser'), page.click(btn)]);
				await chooser.setFiles(full);
				await page.waitForFunction((i) => /^(Ready|That does not|Import failed|Not enough|Original Generals)/.test(document.getElementById(i).textContent), id, { timeout: 3600000 });
				log(`[${build}] ${id}: ${await page.textContent('#' + id)}`);
			}
		} finally {
			await page.close().catch(() => {});
		}
	}

	// Starts the game the way a player does (no -aiMatch): intro videos (clicked away, as a player does), then the main
	// menu with its shell map, which has computer players. Watches for `seconds` and fails on any engine failure.
	// { ok, error, log }.
	async runBoot(build, seconds) {
		const lines = [];
		const page = await this.context.newPage();
		const note = (text) => { for (const l of String(text).split('\n')) lines.push(l); };
		page.on('console', (m) => note(m.text()));
		page.on('pageerror', (e) => note('PAGEERROR ' + e.message));
		page.on('worker', (w) => w.on('console', (m) => note(m.text())));
		page.on('crash', () => note('PAGECRASH the browser tab crashed'));
		let error = null;
		try {
			await page.goto(this.url(build, 'picker=input&copy=1'));
			await page.waitForFunction(() => document.getElementById('play') && !document.getElementById('play').disabled, null, { timeout: 60000 });
			await page.click('#play');
			const t0 = Date.now();
			let nextClick = t0 + 8000;
			while (Date.now() - t0 < seconds * 1000) {
				const fatal = lines.find((l) => /Engine thread stopped|worker sent an error|RuntimeError|Aborted\(|abort\(|PAGECRASH|memory access out of bounds|Fatal error|Assertion failed/.test(l));
				if (fatal) { error = 'engine failure: ' + fatal.slice(0, 300); break; }
				if (lines.some((l) => /^Exited with code/.test(l))) { error = 'the engine exited'; break; }
				// skip the intro videos like a player: a click on the game every few seconds during the first minute
				if (Date.now() > nextClick && Date.now() - t0 < 60000) {
					await page.mouse.click(400, 300).catch(() => {});
					nextClick = Date.now() + 4000;
				}
				await page.waitForTimeout(200);
			}
		} catch (e) {
			error = 'browser: ' + (e && e.message || e);
		} finally {
			await page.close().catch(() => {});
		}
		return { ok: !error, error, log: lines.filter((l) => !/^\s*$/.test(l)) };
	}

	// Plays one match. `args` are the engine's command line arguments; `tag` names the result lines ("AIMATCH" for
	// -aiMatch, "ASSISTMATCH" for -assistMatch). Resolves to
	// { ok, result (parsed <tag>_RESULT), error, log (engine output lines), wallMs }.
	async runMatch(build, args, timeoutMs, tag = 'AIMATCH') {
		const t0 = Date.now();
		const lines = [];
		const page = await this.context.newPage();
		const note = (text) => { for (const l of String(text).split('\n')) lines.push(l); };
		page.on('console', (m) => note(m.text()));
		page.on('pageerror', (e) => note('PAGEERROR ' + e.message));
		page.on('worker', (w) => w.on('console', (m) => note(m.text())));
		page.on('crash', () => note('PAGECRASH the browser tab crashed'));
		const query = ['picker=input', 'copy=1', ...args.map((a) => 'arg=' + encodeURIComponent(a))].join('&');
		let result = null, error = null;
		try {
			await page.goto(this.url(build, query));
			await page.waitForFunction(() => document.getElementById('play') && !document.getElementById('play').disabled, null, { timeout: 60000 });
			await page.click('#play');
			const deadline = Date.now() + timeoutMs;
			let seen = 0;
			for (;;) {
				for (; seen < lines.length; ++seen) {
					const l = lines[seen];
					if (l.startsWith(tag + '_RESULT ')) { try { result = JSON.parse(l.slice(tag.length + 8)); } catch (e) { error = 'unreadable result: ' + e.message; } }
					else if (l.startsWith(tag + '_ERROR ')) error = l.slice(tag.length + 7);
				}
				if (result || error) break;
				const fatal = lines.find((l) => /RuntimeError|Aborted\(|abort\(|PAGECRASH|PAGEERROR|memory access out of bounds|unreachable|Fatal error|Assertion failed/.test(l));
				if (fatal) { error = 'engine failure: ' + fatal.slice(0, 300); break; }
				if (lines.some((l) => /^Exited with code/.test(l))) { error = 'the engine exited without a result'; break; }
				if (Date.now() > deadline) { error = `no result after ${Math.round(timeoutMs / 1000)} s (hang or too slow)`; break; }
				await page.waitForTimeout(100);
			}
			if (result && result.result && result.result.outcome === 'error') { error = result.result.endReason; result = null; }
		} catch (e) {
			error = 'browser: ' + (e && e.message || e);
		} finally {
			await page.close().catch(() => {});
		}
		return { ok: !!result && !error, result, error, log: lines.filter((l) => !/^\s*$/.test(l)), wallMs: Date.now() - t0 };
	}
}
