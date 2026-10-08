// TURN for real: a coturn instance (apt-get install coturn) with the shared-secret credentials the signaling
// server makes, Chromium restricted to relay candidates, and what that means for the virtual LAN.
//
//   node turn_test.mjs [--out <dir>]
//
// 1. credentials: valid time-limited credentials from the server get a relay allocation; a wrong secret, an
//    expired credential, a tampered user name and no credentials do not
// 2. two pages join a room with iceTransportPolicy 'relay': their data channel opens through TURN (route 'turn'),
//    datagrams flow both ways (unicast and broadcast) and coturn's log shows the allocations and the traffic
// 3. the server's own relay-only switch (--ice-relay-only) gives the same result without the page asking
// 4. without a reachable TURN server the pages fall back to the WebSocket relay and still exchange datagrams
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createHmac } from 'node:crypto';
import { loadPlaywright, chromiumPath, WEBRTC_FLAGS, startStaticServer, startSignaling, startCoturn, sleep } from './common.mjs';

const args = process.argv.slice(2);
const out = path.resolve(args.includes('--out') ? args[args.indexOf('--out') + 1] : fs.mkdtempSync(path.join(os.tmpdir(), 'zh-turn-')));
fs.mkdirSync(out, { recursive: true });

const clientDir = path.join(path.dirname(new URL(import.meta.url).pathname), '..', 'client');
const site = fs.mkdtempSync(path.join(os.tmpdir(), 'zh-turn-site-'));
fs.copyFileSync(path.join(clientDir, 'zhnet.js'), path.join(site, 'zhnet.js'));
fs.writeFileSync(path.join(site, 'index.html'), '<!doctype html><meta charset="utf-8"><title>zhnet</title><script src="zhnet.js"></script><body>zhnet test page');

const { chromium } = loadPlaywright();
const files = await startStaticServer(site);
const turn = await startCoturn({ logFile: path.join(out, 'coturn.log') });
const browser = await chromium.launch({ executablePath: chromiumPath(), args: WEBRTC_FLAGS });
const base = `http://127.0.0.1:${files.address().port}/index.html`;

let failures = 0;
function check(name, ok, detail = '') {
	console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
	if (!ok) failures++;
}

/** Gathers candidates with the given ICE server and reports the relay ones and the errors. */
async function gather(page, iceServer) {
	return page.evaluate(async (server) => {
		const pc = new RTCPeerConnection({ iceServers: [server], iceTransportPolicy: 'relay' });
		pc.createDataChannel('x');
		const errors = [];
		const candidates = [];
		pc.onicecandidateerror = (e) => errors.push(e.errorCode + ' ' + e.errorText);
		pc.onicecandidate = (e) => { if (e.candidate) candidates.push(e.candidate.candidate); };
		await pc.setLocalDescription(await pc.createOffer());
		await new Promise((resolve) => {
			if (pc.iceGatheringState === 'complete') resolve();
			pc.onicegatheringstatechange = () => { if (pc.iceGatheringState === 'complete') resolve(); };
			setTimeout(resolve, 8000);
		});
		pc.close();
		return { relay: candidates.filter((c) => / typ relay /.test(c)).length, errors };
	}, iceServer);
}

async function player(query, signalUrl) {
	const context = await browser.newContext();
	const page = await context.newPage();
	const received = [];
	await page.exposeFunction('__got', (from, srcPort, dstPort, text) => received.push({ from, srcPort, dstPort, text }));
	await page.goto(`${base}?${query}&signal=${encodeURIComponent(signalUrl)}&netlog`);
	await page.evaluate(() => {
		zhNet.onDatagram = (peer, srcPort, dstPort, payload) => window.__got(peer.ip, srcPort, dstPort, new TextDecoder().decode(payload));
	});
	page.received = received;
	return page;
}
const send = (page, ip, text) => page.evaluate(([ip, text]) => zhNet.sendDatagram(ip, 8088, 8088, new TextEncoder().encode(text)), [ip, text]);

async function waitFor(fn, ms, what) {
	const end = Date.now() + ms;
	while (Date.now() < end) { if (await fn()) return true; await sleep(100); }
	console.log('timeout waiting for ' + what);
	return false;
}

try {
	// ---- 1. credentials ----------------------------------------------------------------------
	const signaling = await startSignaling({ turn: [turn.url], turnSecret: turn.secret, turnTtl: 600, stun: [] });
	const probe = await player('x=1', signaling.url);
	const welcome = await probe.evaluate(async (url) => {
		await zhNet.join('CREDS', { name: 'probe', server: url });
		return zhNet._ice;
	}, signaling.url);
	const turnServer = welcome.iceServers.find((s) => s.username);
	check('the server hands out TURN credentials with the user name expiry:label', /^\d+:CREDS-2$/.test(turnServer?.username ?? ''), turnServer?.username);
	const expiry = Number(turnServer.username.split(':')[0]);
	check('the credential expires after the configured time to live', Math.abs(expiry - (Date.now() / 1000 + 600)) < 30);
	const good = await gather(probe, turnServer);
	check('valid credentials get a relay allocation', good.relay > 0 && good.errors.length === 0, JSON.stringify(good));
	const hmac = (secret, user) => createHmac('sha1', secret).update(user).digest('base64');
	const wrongSecret = await gather(probe, { urls: turn.url, username: turnServer.username, credential: hmac('another-secret', turnServer.username) });
	check('a credential made with another secret is refused (401)', wrongSecret.relay === 0 && wrongSecret.errors.some((e) => e.startsWith('401')), JSON.stringify(wrongSecret));
	const expiredUser = `${Math.floor(Date.now() / 1000) - 60}:CREDS-9`;
	const expired = await gather(probe, { urls: turn.url, username: expiredUser, credential: hmac(turn.secret, expiredUser) });
	check('an expired credential is refused', expired.relay === 0 && expired.errors.length > 0, JSON.stringify(expired));
	const tampered = await gather(probe, { urls: turn.url, username: `${expiry + 99999}:CREDS-2`, credential: turnServer.credential });
	check('changing the expiry of a credential invalidates it', tampered.relay === 0 && tampered.errors.length > 0, JSON.stringify(tampered));
	const none = await gather(probe, { urls: turn.url, username: 'guest', credential: 'guest' });
	check('made up credentials get no allocation', none.relay === 0 && none.errors.length > 0, JSON.stringify(none));
	await probe.context().close();

	// ---- 2. two players, relay candidates only ------------------------------------------------
	async function twoPlayers(label, query, signalUrl, expectRoute) {
		const room = 'TURN' + label;
		const a = await player(query, signalUrl);
		const b = await player(query, signalUrl);
		await a.evaluate(([room, url]) => zhNet.join(room, { name: 'Alice', server: url }), [room, signalUrl]);
		await b.evaluate(([room, url]) => zhNet.join(room, { name: 'Bob', server: url }), [room, signalUrl]);
		const connected = await waitFor(async () => {
			const sa = await a.evaluate(() => zhNet.state.peers[0]), sb = await b.evaluate(() => zhNet.state.peers[0]);
			return sa && sb && sa.mode === 'p2p' && sb.mode === 'p2p' && sa.route && sb.route;
		}, expectRoute === 'relay' ? 25000 : 20000, 'the data channels');
		const routes = [(await a.evaluate(() => zhNet.state.peers[0])), (await b.evaluate(() => zhNet.state.peers[0]))].map((p) => p?.route ?? p?.mode);
		return { a, b, connected, routes };
	}

	const relay = await twoPlayers('A', 'ice=relay', signaling.url, 'turn');
	check('the data channel opens through TURN when only relay candidates are allowed', relay.connected && relay.routes.every((r) => r === 'turn'), relay.routes.join(','));
	const aIp = (await relay.a.evaluate(() => zhNet.state.self.ip)), bIp = (await relay.b.evaluate(() => zhNet.state.self.ip));
	for (let i = 0; i < 100; ++i) { await send(relay.a, bIp, 'a>b ' + i); await send(relay.b, aIp, 'b>a ' + i); }
	await send(relay.a, '255.255.255.255', 'broadcast from a');
	await waitFor(async () => relay.b.received.length >= 101 && relay.a.received.length >= 100, 10000, 'the datagrams');
	const gotB = relay.b.received.filter((r) => r.text.startsWith('a>b')).length;
	const gotA = relay.a.received.filter((r) => r.text.startsWith('b>a')).length;
	check('datagrams A -> B arrive over TURN (UDP-like: a few may be lost)', gotB >= 95, `${gotB}/100`);
	check('datagrams B -> A arrive over TURN', gotA >= 95, `${gotA}/100`);
	check('a broadcast reaches the other player', relay.b.received.some((r) => r.text === 'broadcast from a' && r.from === aIp));
	const stats = await relay.a.evaluate(() => zhNet.stats());
	check('they went through the data channel, not the WebSocket relay', stats.sentP2P >= 100 && stats.sentRelay === 0, JSON.stringify(stats));
	await sleep(500);
	await relay.a.context().close(); await relay.b.context().close();
	await sleep(1500);
	const log = turn.log();
	const allocations = (log.match(/ALLOCATE processed, success/g) || []).length;
	check('coturn log: allocations were made for the players', allocations >= 2, `${allocations} successful allocations`);
	const usage = [...log.matchAll(/usage: realm=<[^>]*>, username=<[^>]*>, rp=(\d+), rb=(\d+), sp=(\d+), sb=(\d+)/g)].map((m) => ({ rp: +m[1], rb: +m[2], sp: +m[3], sb: +m[4] }));
	check('coturn log: it relayed the game datagrams (packets in and out)', usage.some((u) => u.rp >= 90 && u.sp >= 90), JSON.stringify(usage.slice(-4)));
	check('coturn log: the users are the time-limited ones', /user <\d+:TURNA-\d>/.test(log));
	await signaling.close();

	// ---- 3. the server forces relay-only itself ----------------------------------------------
	const forcing = await startSignaling({ turn: [turn.url], turnSecret: turn.secret, stun: [], iceRelayOnly: true });
	const forced = await twoPlayers('B', 'x=1', forcing.url, 'turn');
	check('--ice-relay-only: the clients use TURN without being asked', forced.connected && forced.routes.every((r) => r === 'turn'), forced.routes.join(','));
	await forced.a.context().close(); await forced.b.context().close();
	await forcing.close();

	// ---- 4. TURN unreachable: the WebSocket relay carries the game --------------------------
	const dead = await startSignaling({ turn: ['turn:127.0.0.1:9'], turnSecret: 'x', stun: [] });
	const fallback = await twoPlayers('C', 'ice=relay', dead.url, 'relay');
	const modes = [await fallback.a.evaluate(() => zhNet.state.peers[0].mode), await fallback.b.evaluate(() => zhNet.state.peers[0].mode)];
	check('with a dead TURN server the peers stay on the WebSocket relay', modes.every((m) => m === 'relay'), modes.join(','));
	const fbA = await fallback.a.evaluate(() => zhNet.state.self.ip), fbB = await fallback.b.evaluate(() => zhNet.state.self.ip);
	for (let i = 0; i < 50; ++i) { await send(fallback.a, fbB, 'x' + i); await send(fallback.b, fbA, 'y' + i); }
	await waitFor(async () => fallback.a.received.length >= 50 && fallback.b.received.length >= 50, 10000, 'relayed datagrams');
	check('and datagrams still arrive both ways', fallback.a.received.length >= 45 && fallback.b.received.length >= 45, `${fallback.a.received.length}/${fallback.b.received.length}`);
	await dead.close();
} catch (e) {
	console.log('ERROR ' + (e && e.stack || e));
	failures++;
}
await browser.close();
await turn.stop();
files.close();
fs.writeFileSync(path.join(out, 'coturn-tail.log'), turn.log().split('\n').slice(-200).join('\n'));
console.log(failures ? `RESULT: FAIL (${failures})` : 'RESULT: PASS');
console.log('logs: ' + out);
process.exit(failures ? 1 : 0);
