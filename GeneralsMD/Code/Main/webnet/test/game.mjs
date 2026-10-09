// A harness for driving several copies of the real game (the free starter content) in headless Chromium pages that
// share one signaling server: opening the pages, clicking through the game's own menus, reading what the engines
// log and comparing their lockstep checksums. Used by mp_flow.mjs; see there for what is tested.
//
// Every player is a separate browser context (own storage, as on separate computers). Coordinates are in the
// 800x600 design resolution of the menus.
import fs from 'node:fs';
import path from 'node:path';
import { loadPlaywright, chromiumPath, WEBRTC_FLAGS, startStaticServer, startSignaling, sleep } from './common.mjs';

export { sleep };

export class Game {
	/**
	 * site: build dir with z_generals.html. mode: 'p2p' | 'relay' | 'turn'. servedByHub: the signaling server serves
	 * the site itself (--static) and the pages find it by their own address, as in a real deployment.
	 */
	constructor({ site, out, mode = 'p2p', resolution = '800x600', room = 'MPTEST', servedByHub = false, signalingExtra = {}, extraArgs = [], launch = {}, pageQuery = '' }) {
		this.site = path.resolve(site);
		this.out = path.resolve(out);
		this.mode = mode;
		this.resolution = resolution;
		this.room = room;
		this.servedByHub = servedByHub;
		this.signalingExtra = signalingExtra;
		this.extraArgs = extraArgs;
		this.pageQuery = pageQuery;
		this.launch = launch;
		this.menuYs = [296, 270];
		this.players = [];
		this.checks = [];
		this.t0 = Date.now();
		fs.mkdirSync(this.out, { recursive: true });
	}

	stamp() { return ((Date.now() - this.t0) / 1000).toFixed(1).padStart(7); }
	log(text) { console.log(`${this.stamp()} ${text}`); }

	async start() {
		const { chromium } = loadPlaywright();
		this.signaling = await startSignaling({ ...(this.servedByHub ? { static: this.site } : {}), ...this.signalingExtra });
		this.files = this.servedByHub ? null : await startStaticServer(this.site);
		this.origin = this.servedByHub ? `http://127.0.0.1:${this.signaling.port}` : `http://127.0.0.1:${this.files.address().port}`;
		this.browser = await chromium.launch({
			executablePath: chromiumPath(),
			args: [...WEBRTC_FLAGS, '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl',
				'--disable-renderer-backgrounding', '--disable-background-timer-throttling', '--disable-backgrounding-occluded-windows', ...(this.launch.args || [])],
		});
	}

	async stop() {
		await this.browser?.close().catch(() => {});
		await this.signaling?.close().catch(() => {});
		this.files?.close();
	}

	// ---- players --------------------------------------------------------------------------------

	/** Opens a page, downloads the starter content and waits until Play is possible. */
	async addPlayer(name, { query = '' } = {}) {
		const tag = String.fromCharCode(65 + this.players.length);
		const context = await this.browser.newContext({ viewport: { width: 1100, height: 800 } });
		const page = await context.newPage();
		const logs = [];
		const player = { tag, name, context, page, logs, game: this, closed: false };
		const push = (kind, text) => logs.push(`${this.stamp()} ${kind}: ${text}`);
		page.on('console', (m) => push(m.type(), m.text()));
		page.on('pageerror', (e) => push('PAGEERROR', e.message));
		page.on('worker', (w) => w.on('console', (m) => push('worker', m.text())));
		page.on('crash', () => push('CRASH', 'the page crashed'));
		this.players.push(player);
		player.url = this.pageUrl(name, query);
		await page.goto(player.url);
		await page.waitForFunction(() => document.getElementById('download-starter'), null, { timeout: 60000 });
		await page.click('#download-starter');
		await page.waitForFunction(() => /^(Ready|Download failed|Could not|This server)/.test(document.getElementById('state-starter').textContent), null, { timeout: 240000 });
		await page.waitForFunction(() => !document.getElementById('play').disabled, null, { timeout: 60000 });
		await page.selectOption('#resolution', this.resolution);
		return player;
	}

	pageUrl(name, query = '') {
		const parts = ['picker=input', `room=${this.room}`, `name=${encodeURIComponent(name)}`];
		if (!this.servedByHub) parts.push(`signal=${encodeURIComponent(this.signaling.url)}`);
		if (this.mode === 'relay') parts.push('net=relay');
		if (this.mode === 'turn') parts.push('ice=relay');
		for (const a of ['-noshellmap', ...this.extraArgs]) parts.push('arg=' + encodeURIComponent(a));
		if (this.pageQuery) parts.push(this.pageQuery);
		if (query) parts.push(query);
		return `${this.origin}/z_generals.html?${parts.join('&')}`;
	}

	async waitOnline(player, peers = null) {
		await player.page.waitForFunction(() => zhNet.state.status === 'online', null, { timeout: 30000 });
		if (peers !== null) await player.page.waitForFunction((n) => zhNet.state.peers.length === n, peers, { timeout: 30000 });
	}

	/** Press Play and wait for the main menu. */
	async play(player) {
		await player.page.click('#play');
		await this.waitLog(player, /Shell:push\(Menus\/MainMenu\.wnd\)/, 180, 'the main menu');
		await sleep(2500);
	}

	// ---- input ------------------------------------------------------------------------------------

	async point(player, x, y) {
		const box = await player.page.locator('#canvas').boundingBox();
		return { x: box.x + (x / 800) * box.width, y: box.y + (y / 600) * box.height };
	}
	async click(player, x, y, { hold = 80 } = {}) {
		const p = await this.point(player, x, y);
		await player.page.mouse.move(p.x, p.y, { steps: 4 });
		await sleep(150);
		await player.page.mouse.down();
		await sleep(hold);
		await player.page.mouse.up();
		await sleep(300);
	}
	async type(player, text) {
		await player.page.keyboard.type(text, { delay: 40 });
		await player.page.keyboard.press('Enter');
	}
	shot(player, name) { return player.page.screenshot({ path: path.join(this.out, `${player.tag}-${name}.png`) }).catch(() => {}); }
	async shotAll(name) { await Promise.all(this.players.filter((p) => !p.closed).map((p) => this.shot(p, name))); }

	// ---- logs ---------------------------------------------------------------------------------------

	seen(player, re) { return player.logs.some((l) => re.test(l)); }
	count(player, re) { return player.logs.filter((l) => re.test(l)).length; }
	async waitLog(player, re, timeout = 60, what = String(re)) {
		const until = Date.now() + timeout * 1000;
		while (Date.now() < until && !this.seen(player, re)) await sleep(250);
		if (!this.seen(player, re)) throw new Error(`${player.tag}: no log line for ${what} within ${timeout} s`);
	}
	/** Number of log lines so far: pass the result as `from` to see only newer lines. */
	mark(player) { return player.logs.length; }
	async waitLogSince(player, from, re, timeout = 60, what = String(re)) {
		const until = Date.now() + timeout * 1000;
		const found = () => player.logs.slice(from).some((l) => re.test(l));
		while (Date.now() < until && !found()) await sleep(250);
		if (!found()) throw new Error(`${player.tag}: no log line for ${what} within ${timeout} s`);
	}

	// ---- the game's menus -------------------------------------------------------------------------------

	/** 'Play with friends' of the main menu; its place depends on how many entries the pack's menu has. */
	async toLobby(player) {
		const from = this.mark(player);
		for (const y of this.menuYs) {
			await this.click(player, 186, y);
			const until = Date.now() + 6000;
			while (Date.now() < until && !player.logs.slice(from).some((l) => /Shell:push\(Menus\/LanLobbyMenu\.wnd\)/.test(l))) await sleep(250);
			if (player.logs.slice(from).some((l) => /Shell:push\(Menus\/LanLobbyMenu\.wnd\)/.test(l))) return;
		}
		throw new Error(`${player.tag}: could not open the lobby from the main menu`);
	}
	async host(player) {
		await this.click(player, 696, 566);
		await this.waitLog(player, /Shell:push\(Menus\/LanGameOptionsMenu\.wnd\)/, 30, 'the host\'s game setup');
	}
	async joinFirstGame(player) {
		const from = this.mark(player);
		await this.click(player, 200, 92);
		await sleep(400);
		await this.click(player, 520, 566);
		await this.waitLogSince(player, from, /Shell:push\(Menus\/LanGameOptionsMenu\.wnd\)/, 30, 'the guest\'s game setup');
	}
	/** The guest's Accept button of the game setup (the same place as the host's Start). */
	async accept(player) { await this.click(player, 700, 574); }
	async startGame(host) { await this.click(host, 700, 574); }

	// ---- lockstep ----------------------------------------------------------------------------------------------

	crcs(player) {
		const crcs = new Map();
		for (const l of player.logs) {
			const m = /Appended CRC on frame (\d+): ([0-9A-F]+)/.exec(l);
			if (m) crcs.set(Number(m[1]), m[2]);
		}
		return crcs;
	}
	lastFrame(player) { let max = 0; for (const k of this.crcs(player).keys()) if (k > max) max = k; return max; }

	/** Compares the checksums of the given players: {frames: common frames, differing: [...]} */
	compare(players) {
		const maps = players.map((p) => this.crcs(p));
		const common = [...maps[0].keys()].filter((f) => maps.every((m) => m.has(f))).sort((a, b) => a - b);
		const differing = common.filter((f) => new Set(maps.map((m) => m.get(f))).size > 1);
		return { common, differing };
	}

	check(name, ok, detail = '') {
		this.checks.push({ name, ok, detail });
		console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
		return ok;
	}
	get failed() { return this.checks.some((c) => !c.ok); }

	writeLogs() {
		for (const p of this.players) fs.writeFileSync(path.join(this.out, `${p.tag}.log`), p.logs.join('\n') + '\n');
	}

	async netStats(player) {
		return player.page.evaluate(() => ({ peers: zhNet.state.peers, stats: zhNet.stats(), error: zhNet.state.error })).catch(() => null);
	}
}
