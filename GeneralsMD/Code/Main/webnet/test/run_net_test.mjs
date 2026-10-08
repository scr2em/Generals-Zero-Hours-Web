// Transport level test of the browser network: two headless Chromium pages in one room of a local signaling server
// run web_net_test (Core/GameEngineDevice/Source/WebDevice/Network/Tests), which exercises the engine's socket API
// (lobby port, game port, broadcast, loss, bursts) over WebRTC data channels, or over the server's relay.
//
//   node run_net_test.mjs <build dir with web_net_test.html and zhnet.js> [--mode p2p|relay|both] [--seconds 120]
//
// Needs Playwright (NODE_PATH=/opt/node22/lib/node_modules) and `npm ci` in ../server.
import path from 'node:path';
import { loadPlaywright, chromiumPath, WEBRTC_FLAGS, startStaticServer, startSignaling, sleep } from './common.mjs';

const args = process.argv.slice(2);
const buildDir = args.find((a) => !a.startsWith('--'));
const option = (name, fallback) => { const i = args.indexOf('--' + name); return i >= 0 ? args[i + 1] : fallback; };
if (!buildDir) { console.error('usage: node run_net_test.mjs <build dir> [--mode p2p|relay|both] [--seconds 120]'); process.exit(2); }
const modes = option('mode', 'both') === 'both' ? ['p2p', 'relay'] : [option('mode', 'both')];
const timeout = Number(option('seconds', '120')) * 1000;

const { chromium } = loadPlaywright();
const files = await startStaticServer(path.resolve(buildDir));
const filesPort = files.address().port;
let failed = false;

for (const mode of modes) {
	console.log(`=== ${mode} ===`);
	const signaling = await startSignaling();
	const browser = await chromium.launch({ executablePath: chromiumPath(), args: WEBRTC_FLAGS });
	const room = 'NET' + mode.toUpperCase();
	const pages = [];
	const logs = [[], []];
	let finished = false;
	for (let i = 0; i < 2; ++i) {
		const context = await browser.newContext();
		const page = await context.newPage();
		const tag = i === 0 ? 'A' : 'B';
		const echo = (text) => {
			logs[i].push(text);
			if (process.env.VERBOSE || /NETTEST|FAIL|JOIN|Error|error/.test(text)) console.log(`[${tag}] ${text}`);
			if (text.includes('NETTEST_RESULT')) finished = true;
		};
		page.on('console', (m) => echo(m.text()));
		page.on('worker', (w) => w.on('console', (m) => echo(m.text())));
		page.on('pageerror', (e) => echo('PAGEERROR ' + e.message));
		pages.push(page);
		const url = `http://127.0.0.1:${filesPort}/web_net_test.html?room=${room}&signal=${encodeURIComponent(signaling.url)}&name=Player${tag}` +
			(mode === 'relay' ? '&relay=1' : '');
		await page.goto(url);
		// The second page joins only after the first is in the room, so the first is "A" (the driver).
		await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('joined'), null, { timeout: 15000 });
	}
	const t0 = Date.now();
	while (!finished && Date.now() - t0 < timeout) await sleep(250);
	await sleep(1000);

	const info = [];
	for (const page of pages) {
		info.push(await page.evaluate(() => ({ state: zhNet.state, stats: zhNet.stats() })));
	}
	console.log('page A:', JSON.stringify(info[0].stats), JSON.stringify(info[0].state.peers));
	console.log('page B:', JSON.stringify(info[1].stats), JSON.stringify(info[1].state.peers));
	const pass = logs[0].some((l) => l.includes('NETTEST_RESULT PASS'));
	const connection = info.every((i) => i.state.peers.length === 1 && i.state.peers[0].mode === (mode === 'p2p' ? 'p2p' : 'relay'));
	const traffic = mode === 'p2p'
		? info.every((i) => i.stats.sentP2P > 100 && i.stats.recvP2P > 100 && i.stats.sentRelay < 20)
		: info.every((i) => i.stats.sentRelay > 100 && i.stats.recvRelay > 100 && i.stats.sentP2P === 0);
	console.log(`${mode}: tests ${pass ? 'PASS' : 'FAIL'}, connection mode ${connection ? 'as expected' : 'UNEXPECTED'}, traffic path ${traffic ? 'as expected' : 'UNEXPECTED'}`);
	if (!pass || !connection || !traffic) failed = true;
	await browser.close();
	await signaling.close();
}
files.close();
console.log(failed ? 'RESULT: FAIL' : 'RESULT: PASS');
process.exit(failed ? 1 : 0);
