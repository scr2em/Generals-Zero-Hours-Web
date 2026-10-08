// The LAN lobby of the real game between two browser pages, over the virtual LAN room and WebRTC.
//
//   node lan_flow.mjs --site <build/GeneralsMD> [--seconds 60] [--frames 300] [--out <dir>] [--mode p2p|relay]
//                     [--resolution 800x600] [--loss 0.1] [--pack <built starterpack dir>]
//
// Starts the signaling server and a static server for the site, opens two headless Chromium pages (A and B) with
// the free starter content and the same room code, and drives both through the game's own menus:
//
//   A: Play -> Play with friends (the LAN lobby) -> Host game          B: Play -> Play with friends
//   B sees A's game in the list, selects it and joins; both are in the game setup screen
//   B presses Accept, A presses Start; both load the map and play
//
// (chat in the lobby first), then lets the game run for --seconds, and at least --frames frames, in lockstep and compares what the engines logged: every network game appends
// the checksum of its simulation state every 100 frames ("Appended CRC on frame N: XXXXXXXX") and the engine
// reports differences ("CRC Mismatch"); a player that stops hearing from the other shows the disconnect dialog.
// The test passes when both engines logged the same checksums for the same frames, no mismatch and no disconnect.
//
// Screenshots (lobby of both, setup of both, game of both) and the engine logs go to --out. Needs Playwright
// (NODE_PATH=/opt/node22/lib/node_modules) and `npm ci` in ../server.
import fs from 'node:fs';
import path from 'node:path';
import { loadPlaywright, chromiumPath, WEBRTC_FLAGS, startStaticServer, startSignaling, sleep } from './common.mjs';

const args = process.argv.slice(2);
const option = (name, fallback) => { const i = args.indexOf('--' + name); return i >= 0 ? args[i + 1] : fallback; };
const site = option('site');
if (!site) { console.error('--site <build dir with z_generals.html> is required'); process.exit(2); }
const seconds = Number(option('seconds', '60'));
const minFrames = Number(option('frames', '300'));
const loss = Number(option('loss', '0'));   // fraction of the datagrams each page throws away once the game runs
const out = path.resolve(option('out', '.'));
const mode = option('mode', 'p2p');
const resolution = option('resolution', '800x600');
const extraArgs = args.flatMap((a, i) => (a === '--arg' ? [args[i + 1]] : []));
fs.mkdirSync(out, { recursive: true });
if (option('pack')) fs.cpSync(path.resolve(option('pack')), path.join(site, 'starterpack'), { recursive: true });

const { chromium } = loadPlaywright();
const files = await startStaticServer(path.resolve(site));
const signaling = await startSignaling();
const browser = await chromium.launch({
	executablePath: chromiumPath(),
	args: [...WEBRTC_FLAGS, '--use-angle=swiftshader', '--use-gl=angle', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--enable-webgl'],
});

const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(6);
const players = [];

async function openPlayer(tag, name) {
	const context = await browser.newContext({ viewport: { width: 1100, height: 800 } });
	const page = await context.newPage();
	const logs = [];
	const player = { tag, name, page, logs, mark: 0 };
	const push = (kind, text) => logs.push(`${stamp()} ${kind}: ${text}`);
	page.on('console', (m) => push(m.type(), m.text()));
	page.on('pageerror', (e) => push('PAGEERROR', e.message));
	page.on('worker', (w) => w.on('console', (m) => push('worker', m.text())));
	const query = ['picker=input', `room=LANTEST${mode === 'relay' ? 'R' : 'P'}`, `name=${name}`, `signal=${encodeURIComponent(signaling.url)}`,
		...(mode === 'relay' ? ['net=relay'] : []), ...['-noshellmap', ...extraArgs].map((a) => 'arg=' + encodeURIComponent(a))].join('&');
	await page.goto(`http://127.0.0.1:${files.address().port}/z_generals.html?${query}`);
	await page.waitForFunction(() => document.getElementById('download-starter'), null, { timeout: 30000 });
	await page.click('#download-starter');
	await page.waitForFunction(() => /^(Ready|Download failed|Could not|This server)/.test(document.getElementById('state-starter').textContent), null, { timeout: 180000 });
	await page.waitForFunction(() => !document.getElementById('play').disabled, null, { timeout: 30000 });
	await page.selectOption('#resolution', resolution);
	players.push(player);
	return player;
}

// A point of the 800x600 design resolution of the menus on the page's canvas.
async function point(player, x, y) {
	const box = await player.page.locator('#canvas').boundingBox();
	return { x: box.x + (x / 800) * box.width, y: box.y + (y / 600) * box.height };
}
async function click(player, x, y) {
	const p = await point(player, x, y);
	await player.page.mouse.move(p.x, p.y, { steps: 4 });
	await sleep(150);
	await player.page.mouse.down();
	await sleep(80);
	await player.page.mouse.up();
	await sleep(300);
}
const shot = (player, name) => player.page.screenshot({ path: path.join(out, `${player.tag}-${name}.png`) });
const seen = (player, re) => player.logs.some((l) => re.test(l));
async function waitLog(player, re, timeout = 60, what = String(re)) {
	const until = Date.now() + timeout * 1000;
	while (Date.now() < until && !seen(player, re)) await sleep(250);
	if (!seen(player, re)) throw new Error(`${player.tag}: no log line for ${what} within ${timeout} s`);
}
const checks = [];
function check(name, ok, detail = '') {
	checks.push(ok);
	console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
}

let failed = false;
try {
	// A joins the room first and is the lower address (10.77.0.2): the host.
	const A = await openPlayer('A', 'Alice');
	await A.page.waitForFunction(() => zhNet.state.status === 'online', null, { timeout: 20000 });
	const B = await openPlayer('B', 'Bob');
	await B.page.waitForFunction(() => zhNet.state.status === 'online', null, { timeout: 20000 });
	for (const p of players) await p.page.waitForFunction(() => zhNet.state.peers.length === 1, null, { timeout: 20000 });
	console.log('launcher: ' + await A.page.textContent('#state-net'));
	console.log('launcher: ' + await B.page.textContent('#state-net'));
	await shot(A, '1-launcher');
	await shot(B, '1-launcher');

	// Play on both
	for (const p of players) {
		await p.page.click('#play');
		p.page.waitForFunction(() => window.__zhFrames > 30, null, { timeout: 120000 }).catch(() => {});
	}
	for (const p of players) await waitLog(p, /Shell:push\(Menus\/MainMenu\.wnd\)/, 120, 'the main menu');
	await sleep(3000);
	await shot(A, '2-mainmenu');

	// Into the LAN lobby
	for (const p of players) { await click(p, 186, 296); await sleep(500); }
	for (const p of players) await waitLog(p, /Shell:push\(Menus\/LanLobbyMenu\.wnd\)/, 30, 'the lobby');
	await sleep(3000);
	console.log('lobby: ' + [A, B].map((p) => `${p.tag} ${(p.logs.filter((l) => /Hostname|IP: 0x/.test(l)).slice(-1)[0] || '').trim()}`).join(' | '));
	await shot(A, '3-lobby');
	await shot(B, '3-lobby');

	// chat: A writes, B answers; the text arrives as a broadcast
	await click(A, 230, 493);
	await A.page.keyboard.type('Hello from Alice', { delay: 40 });
	await A.page.keyboard.press('Enter');
	await sleep(2500);
	await click(B, 230, 493);
	await B.page.keyboard.type('Hi Alice, this is Bob', { delay: 40 });
	await B.page.keyboard.press('Enter');
	await sleep(2500);
	await shot(A, '3b-lobby-chat');
	await shot(B, '3b-lobby-chat');
	// (Typing needs the web window procedure to hand WM_CHAR to the IME manager, as WinMain.cpp does.)

	// A hosts
	await click(A, 696, 566);
	await waitLog(A, /Shell:push\(Menus\/LanGameOptionsMenu\.wnd\)/, 30, 'the host\'s game setup');
	await sleep(1500);
	await shot(A, '4-setup-host');

	// B waits for A's game announcement, selects it and joins
	await sleep(4000);
	await shot(B, '4-lobby-gamelist');
	await click(B, 200, 92);
	await sleep(500);
	await click(B, 520, 566);
	await waitLog(B, /Shell:push\(Menus\/LanGameOptionsMenu\.wnd\)/, 30, 'the guest\'s game setup');
	await sleep(2500);
	await shot(A, '5-setup-host-with-guest');
	await shot(B, '5-setup-guest');

	// B accepts, A starts
	await click(B, 700, 574);
	await sleep(2000);
	await click(A, 700, 574);
	for (const p of players) await waitLog(p, /Appended CRC on frame/, 120, 'the first network frames');
	await sleep(3000);
	await shot(A, '6-game');
	await shot(B, '6-game');

	if (loss > 0) {
		console.log(`dropping ${(loss * 100).toFixed(0)}% of the incoming datagrams on both pages`);
		for (const p of players) await p.page.evaluate((f) => { zhNet.debug.dropRx = f; }, loss);
	}

	// let it run
	console.log(`running the lockstep game for ${seconds} s`);
	const start = Date.now();
	const enough = () => Date.now() - start >= seconds * 1000 && Math.min(...players.map(lastFrame)) >= minFrames;
	while (!enough() && Date.now() - start < seconds * 3000) {
		await sleep(5000);
		const frames = players.map((p) => lastFrame(p));
		process.stdout.write(`  ${((Date.now() - start) / 1000).toFixed(0)} s: frame A ${frames[0]}, B ${frames[1]}\n`);
	}
	await shot(A, '7-game-later');
	await shot(B, '7-game-later');
} catch (e) {
	console.log('ERROR ' + (e && e.stack || e));
	failed = true;
	for (const p of players) await shot(p, 'error').catch(() => {});
}

function crcLines(player) {
	const crcs = new Map();
	for (const l of player.logs) {
		const m = /Appended CRC on frame (\d+): ([0-9A-F]+)/.exec(l);
		if (m) crcs.set(Number(m[1]), m[2]);
	}
	return crcs;
}
function lastFrame(player) { const k = [...crcLines(player).keys()]; return k.length ? Math.max(...k) : 0; }

for (const p of players) fs.writeFileSync(path.join(out, `${p.tag}.log`), p.logs.join('\n') + '\n');
if (players.length === 2) {
	const [A, B] = players;
	const a = crcLines(A), b = crcLines(B);
	const common = [...a.keys()].filter((f) => b.has(f));
	const differing = common.filter((f) => a.get(f) !== b.get(f));
	console.log(`checksums: A logged ${a.size} (last frame ${lastFrame(A)}), B logged ${b.size} (last frame ${lastFrame(B)}), ${common.length} frames in common`);
	check('both engines ran network frames', a.size >= 2 && b.size >= 2);
	check(`both games reached frame ${minFrames}`, Math.min(lastFrame(A), lastFrame(B)) >= minFrames, `last frames ${lastFrame(A)} / ${lastFrame(B)}`);
	check('the checksums of the shared frames are equal', common.length >= 2 && differing.length === 0,
		differing.length ? 'differ at frames ' + differing.join(',') : `${common.length} frames`);
	const bad = /CRC Mismatch|Not enough CRCs|DisconnectManager::|disconnectPlayer|processPlayerLeave|timed out/i;
	const trouble = (p) => p.logs.filter((l) => bad.test(l));
	check('no CRC mismatch or disconnect in A\'s log', trouble(A).length === 0, trouble(A).slice(0, 3).join(' | '));
	check('no CRC mismatch or disconnect in B\'s log', trouble(B).length === 0, trouble(B).slice(0, 3).join(' | '));
	console.log('lobby chat: typed on both sides, see A-3b-lobby-chat.png and B-3b-lobby-chat.png');
	check('no wasm trap', !players.some((p) => p.logs.some((l) => /RuntimeError|unreachable|memory access out of bounds|Aborted\(/.test(l))));
	for (const p of players) {
		const s = await p.page.evaluate(() => ({ peers: zhNet.state.peers, stats: zhNet.stats() })).catch(() => null);
		if (s) console.log(`page ${p.tag}: ${JSON.stringify(s.peers)} ${JSON.stringify(s.stats)}`);
	}
	failed = failed || checks.includes(false);
}
await browser.close();
await signaling.close();
files.close();
console.log(failed ? 'RESULT: FAIL' : 'RESULT: PASS');
process.exit(failed ? 1 : 0);
