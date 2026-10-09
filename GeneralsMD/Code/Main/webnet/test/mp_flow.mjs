// Multiplayer scenarios of the real game: 2 to 8 browser pages, one virtual LAN room, the game's own menus and
// lockstep. See README.md ("Tests") for what each scenario proves.
//
//   node mp_flow.mjs --site <build/GeneralsMD> --players 3 [--scenario basic|leave|hostleave|again|reload|restart]
//                    [--seconds 60] [--frames 300] [--mode p2p|relay|turn] [--choose-map] [--lowres 320x240]
//                    [--latency 150 --jitter 50 --loss 0.05] [--served-by-hub] [--out <dir>]
//
// The signaling server runs in this process (or serves the site itself with --served-by-hub); every player is a
// separate browser context. The map needs a free slot for every player: the starter pack's two player map is
// enough for 2, "Ironwood Ring" (8 start positions) for more.
import fs from 'node:fs';
import path from 'node:path';
import { Game, sleep } from './game.mjs';
import { startCoturn } from './common.mjs';

const args = process.argv.slice(2);
const option = (name, fallback) => { const i = args.indexOf('--' + name); return i >= 0 ? args[i + 1] : fallback; };
const flag = (name) => args.includes('--' + name);
const site = option('site');
if (!site) { console.error('--site <build dir with z_generals.html> is required'); process.exit(2); }
const N = Number(option('players', '3'));
const scenario = option('scenario', 'basic');
const seconds = Number(option('seconds', '60'));
const minFrames = Number(option('frames', '300'));
const mode = option('mode', 'p2p');
// --choose-map: pick the second map of the list ("Ironwood Ring", 8 start positions) in the map dialog; without it the
// game's default map is used (a pack whose only map has enough start positions, or the 2 player map for 2 players).
const chooseMap = flag('choose-map');
const latency = Number(option('latency', '0'));
const jitter = Number(option('jitter', '0'));
const loss = Number(option('loss', '0'));
const out = path.resolve(option('out', '.'));
const resolution = option('resolution', '800x600');
// The engine's own render size, smaller than the page's (-xres/-yres after the page's): headless software
// rendering of several games on a few cores is the bottleneck of these tests, not the network.
const lowres = option('lowres', '');
const names = ['Alice', 'Bob', 'Carol', 'Dave', 'Erin', 'Frank', 'Grace', 'Heidi'];

let turn = null;
const signalingExtra = {};
if (mode === 'turn') {
	turn = await startCoturn({ logFile: path.join(out, 'coturn.log') });
	Object.assign(signalingExtra, { turn: [turn.url], turnSecret: turn.secret, stun: [], iceRelayOnly: true });
}
const g = new Game({
	site, out, mode, resolution, signalingExtra, extraArgs: lowres ? ['-xres', lowres.split('x')[0], '-yres', lowres.split('x')[1]] : [], servedByHub: flag('served-by-hub'),
	room: 'MP' + scenario.toUpperCase().slice(0, 5) + mode.toUpperCase().slice(0, 1) + N + Math.floor(Math.random() * 900 + 100),
});
await g.start();
const host = () => g.players[0];
const guests = () => g.players.slice(1);

/** All pages in the lobby of the game (after the room is joined). */
async function openAll() {
	for (let i = 0; i < N; ++i) {
		const p = await g.addPlayer(names[i]);
		await g.waitOnline(p, i);
		g.log(`${p.tag} (${p.name}) is in the room at ${await p.page.evaluate(() => zhNet.state.self.ip)}`);
	}
	for (const p of g.players) await g.waitOnline(p, N - 1);
	g.check(`all ${N} players see each other in the room`, true);
	if (mode === 'p2p' || mode === 'turn') {
		for (const p of g.players) await p.page.waitForFunction((n) => zhNet.state.peers.length === n && zhNet.state.peers.every((q) => q.mode === 'p2p'), N - 1, { timeout: 60000 }).catch(() => {});
		const modes = await Promise.all(g.players.map((p) => p.page.evaluate(() => zhNet.state.peers.map((q) => q.mode + (q.route ? '/' + q.route : '')))));
		g.check(`the full mesh of ${N * (N - 1) / 2} data channels is up (${mode === 'turn' ? 'through TURN' : 'WebRTC'})`,
			modes.every((m) => m.length === N - 1 && m.every((x) => x.startsWith('p2p') && (mode !== 'turn' || x.endsWith('/turn')))), JSON.stringify(modes));
	}
}

async function toLobbies() {
	await Promise.all(g.players.map((p) => p.page.click('#play')));
	for (const p of g.players) await g.waitLog(p, /Shell:push\(Menus\/MainMenu\.wnd\)/, 240, 'the main menu');
	await sleep(3000);
	for (const p of g.players) await g.toLobby(p);
	await sleep(3000);
	await g.shotAll('3-lobby');
}

async function lobbyChat() {
	for (const p of g.players) {
		await g.click(p, 230, 493);
		await g.type(p, `Hello from ${p.name}`);
		await sleep(1800);
	}
	await sleep(1500);
	// every chat line was received by every other player: the game logs nothing for chat, so look at the screens
	await g.shotAll('3b-lobby-chat');
	g.log('lobby chat typed on every page (screenshots *-3b-lobby-chat.png)');
}

async function setupGame() {
	await g.host(host());
	if (chooseMap) {
		await g.click(host(), 666, 362);
		await sleep(1500);
		await g.click(host(), 449, 205);
		await sleep(800);
		await g.click(host(), 410, 509);
		await sleep(1500);
	}
	await g.shot(host(), '4-setup-host');
	await sleep(4000);     // the announcement reaches the lobbies
	for (const p of guests()) {
		await g.joinFirstGame(p);
		await sleep(1500);
	}
	await sleep(3000);
	await g.shotAll('5-setup');
	const joined = host().logs.filter((l) => /handleRequestJoin - added player/.test(l)).length;
	g.check(`the host accepted ${N - 1} joins`, joined >= N - 1, `${joined} joins logged`);
}

async function acceptAndStart() {
	for (const p of guests()) await g.accept(p);
	await sleep(2500);
	await g.shotAll('5b-accepted');
	await g.startGame(host());
	for (const p of g.players) await g.waitLog(p, /Appended CRC on frame/, 300, 'the first network frames');
	await sleep(3000);
	await g.shotAll('6-game');
	g.log('the game is running on all pages');
}

async function applyNetworkConditions() {
	if (!latency && !jitter && !loss) return;
	g.log(`network conditions on every page: ${latency} ms +-${jitter} ms, ${(loss * 100).toFixed(0)}% loss (each way)`);
	for (const p of g.players) await p.page.evaluate(([l, j, f]) => { zhNet.debug.latencyMs = l; zhNet.debug.jitterMs = j; zhNet.debug.dropRx = f; }, [latency, jitter, loss]);
}

/** Lets the game run until every live player passed minFrames and `seconds` went by; logs progress. */
async function run(live, { secs = seconds, frames = minFrames } = {}) {
	const start = Date.now();
	const done = () => Date.now() - start >= secs * 1000 && Math.min(...live.map((p) => g.lastFrame(p))) >= frames;
	let lastReport = 0;
	while (!done() && Date.now() - start < secs * 4000 + 120000) {
		await sleep(2000);
		if (Date.now() - lastReport > 10000) {
			lastReport = Date.now();
			g.log(`  ${((Date.now() - start) / 1000).toFixed(0)} s: frames ${live.map((p) => `${p.tag}${g.lastFrame(p)}`).join(' ')}`);
		}
	}
}

function verifyLockstep(live, label) {
	const { common, differing } = g.compare(live);
	const frames = live.map((p) => g.lastFrame(p));
	g.check(`${label}: the checksums of ${live.length} players agree on ${common.length} frames (to frame ${common.at(-1) ?? 0})`,
		common.length >= 2 && differing.length === 0, differing.length ? 'differ at frames ' + differing.slice(0, 8).join(',') : `last frames ${frames.join('/')}`);
	g.check(`${label}: nobody logged a CRC mismatch`, !live.some((p) => g.seen(p, /CRC Mismatch|mismatch on frame/i)));
}

const BAD = /RuntimeError|unreachable|memory access out of bounds|Aborted\(|PAGEERROR|\bCRASH: |the page crashed/;
function verifyHealthy(live, label) {
	for (const p of live) {
		const bad = p.logs.filter((l) => BAD.test(l));
		g.check(`${label}: ${p.tag} has no wasm trap or page error`, bad.length === 0, bad.slice(0, 2).join(' | '));
	}
}

let exit = 0;
try {
	await openAll();
	await g.shotAll('1-launcher');
	await toLobbies();
	await lobbyChat();
	await setupGame();
	await acceptAndStart();
	await applyNetworkConditions();
	g.log(`running the lockstep game with ${N} players for ${seconds} s / ${minFrames} frames`);
	await run(g.players);
	await g.shotAll('7-game-later');
	verifyLockstep(g.players, `${N} players`);
	verifyHealthy(g.players, `${N} players`);
	for (const p of g.players) {
		const s = await g.netStats(p);
		if (s) g.log(`page ${p.tag}: ${JSON.stringify(s.stats)}`);
	}
	const health = await (await fetch(`${g.signaling.url.replace('ws:', 'http:').replace('/signal', '')}/healthz`)).json();
	g.check('the server never rate limited anybody during normal play', health.stats.rateLimitedControl === 0 && health.stats.rateLimitedDatagrams === 0 && health.stats.refusedJoins === 0, JSON.stringify(health.stats));
} catch (e) {
	console.log('ERROR ' + (e && e.stack || e));
	g.checks.push({ name: 'no exception', ok: false, detail: String(e && e.message) });
	await g.shotAll('error');
	exit = 1;
}
g.writeLogs();
await g.stop();
await turn?.stop();
const failed = g.failed || exit;
console.log(failed ? 'RESULT: FAIL' : 'RESULT: PASS');
console.log('logs and screenshots: ' + out);
process.exit(failed ? 1 : 0);
