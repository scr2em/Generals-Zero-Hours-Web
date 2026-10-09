// The room controls of the launcher page (shell.html + client/zhnet.js) against a local signaling server:
// create a room, join one by code and by invite link, leave, the error when the server cannot be reached,
// and that the page shows the other player. Does not start the game.
//
//   node launcher_net.mjs --site <build/GeneralsMD> [--page z_generals.html] [--out <screenshot dir>]
import fs from 'node:fs';
import path from 'node:path';
import { loadPlaywright, chromiumPath, WEBRTC_FLAGS, startStaticServer, startSignaling, sleep } from './common.mjs';

const args = process.argv.slice(2);
const option = (name, fallback) => { const i = args.indexOf('--' + name); return i >= 0 ? args[i + 1] : fallback; };
const site = option('site');
if (!site) { console.error('--site <build dir> is required'); process.exit(2); }
const pageName = option('page', 'z_generals.html');
const out = path.resolve(option('out', '.'));
fs.mkdirSync(out, { recursive: true });

const { chromium } = loadPlaywright();
const files = await startStaticServer(path.resolve(site));
const signaling = await startSignaling();
const browser = await chromium.launch({ executablePath: chromiumPath(), args: WEBRTC_FLAGS });
const base = `http://127.0.0.1:${files.address().port}/${pageName}`;
const signal = encodeURIComponent(signaling.url);

let failures = 0;
function check(name, ok, detail = '') {
	console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail ? '  [' + detail + ']' : ''}`);
	if (!ok) failures++;
}
const text = (page, id) => page.textContent('#' + id);

async function open(query) {
	const context = await browser.newContext({ viewport: { width: 1100, height: 900 } });
	const page = await context.newPage();
	const errors = [];
	page.on('pageerror', (e) => errors.push(e.message));
	page.on('console', (m) => { if (m.type() === 'error' && !/Failed to load resource|WebSocket connection to/.test(m.text())) errors.push(m.text()); });
	await page.goto(`${base}?${query}`);
	await page.waitForFunction(() => window.zhNet && document.getElementById('net-join'), null, { timeout: 15000 });
	page.errors = errors;
	return page;
}

try {
	// New room
	const a = await open(`signal=${signal}&name=Alice`);
	check('the room section is there and starts disconnected', (await text(a, 'state-net')).includes('Not connected'));
	check('Leave is hidden before joining', await a.isHidden('#net-leave'));
	await a.click('#net-create');
	await a.waitForFunction(() => zhNet.state.status === 'online', null, { timeout: 10000 });
	const code = await a.evaluate(() => zhNet.state.room);
	check('New room gets a code from the server', /^[A-Z0-9]{6}$/.test(code), code);
	check('the state line names the room and the address', /Room [A-Z0-9]{6} .* 10\.77\.0\.2 as .Alice/.test(await text(a, 'state-net')), await text(a, 'state-net'));
	check('the URL carries the room', new URL(a.url()).searchParams.get('room') === code);
	check('the invite link has the room code', (await a.inputValue('#net-link')).includes('room=' + code), await a.inputValue('#net-link'));
	check('the inputs are locked while in a room', await a.isDisabled('#net-room') && await a.isDisabled('#net-name'));

	// Join by invite link
	const invite = (await a.inputValue('#net-link')) + `&signal=${signal}&name=Bob`;
	const b = await open('x=1');
	await b.goto(invite);
	await b.waitForFunction(() => window.zhNet && zhNet.state.status === 'online', null, { timeout: 10000 });
	check('the invite link joins at once', (await b.evaluate(() => zhNet.state.room)) === code);
	check('B got the next address', (await b.evaluate(() => zhNet.state.self.ip)) === '10.77.0.3');
	await a.waitForFunction(() => zhNet.state.peers.length === 1, null, { timeout: 10000 });
	await b.waitForFunction(() => zhNet.state.peers.length === 1, null, { timeout: 10000 });
	check('both see one other player', true);
	await a.waitForFunction(() => zhNet.state.peers[0].mode === 'p2p', null, { timeout: 20000 }).catch(() => {});
	check('the players are connected directly (WebRTC)', (await a.evaluate(() => zhNet.state.peers[0].mode)) === 'p2p');
	check('A\'s page names Bob', (await text(a, 'state-net')).includes('Bob'), await text(a, 'state-net'));
	check('the toolbar badge counts the players', (await text(a, 'net-badge')).includes('2 players'), await text(a, 'net-badge'));
	await a.screenshot({ path: path.join(out, 'launcher-room-A.png') });
	await b.screenshot({ path: path.join(out, 'launcher-room-B.png') });

	// Leave and rejoin by code
	await b.click('#net-leave');
	await a.waitForFunction(() => zhNet.state.peers.length === 0, null, { timeout: 10000 });
	check('leaving removes the player from the other page', true);
	check('B is back to Join', (await text(b, 'state-net')).includes('Not connected') && !(new URL(b.url()).searchParams.has('room')));
	await b.fill('#net-room', code.toLowerCase());
	await b.click('#net-join');
	await b.waitForFunction(() => zhNet.state.status === 'online', null, { timeout: 10000 });
	check('joining by typing the code (any case)', (await b.evaluate(() => zhNet.state.room)) === code);
	check('the freed address is reused', (await b.evaluate(() => zhNet.state.self.ip)) === '10.77.0.3');

	// Errors
	const c = await open('signal=' + encodeURIComponent('ws://127.0.0.1:9/signal'));
	await c.fill('#net-room', 'ABCDE');
	await c.click('#net-join');
	await c.waitForFunction(() => zhNet.state.status === 'failed', null, { timeout: 15000 });
	check('an unreachable server is reported', /Cannot reach the server/.test(await text(c, 'state-net')), await text(c, 'state-net'));
	check('the join button is usable again', !(await c.isDisabled('#net-join')));
	await c.screenshot({ path: path.join(out, 'launcher-room-error.png') });

	const d = await open(`signal=${signal}`);
	await d.fill('#net-room', 'x');
	await d.click('#net-join');
	await d.waitForFunction(() => zhNet.state.status === 'failed', null, { timeout: 10000 });
	check('a bad room code is reported by the server', /3 to 16/.test(await text(d, 'state-net')), await text(d, 'state-net'));

	// Hostile names and codes are text, never markup
	const evil = await open(`signal=${signal}&room=EVIL1&name=${encodeURIComponent('<img src=x onerror=window.__pwned=1>')}`);
	await evil.waitForFunction(() => zhNet.state.status === 'online', null, { timeout: 10000 });
	const victim = await open(`signal=${signal}&room=EVIL1&name=Victim`);
	await victim.waitForFunction(() => zhNet.state.status === 'online' && zhNet.state.peers.length === 1, null, { timeout: 10000 });
	const shown = await text(victim, 'state-net');
	check('a name with markup is shown as text', shown.includes('<img src=x onerror=window.__pwned=1>'), shown);
	check('and nothing was injected into the page', (await victim.evaluate(() => !window.__pwned && !document.querySelector('#state-net img, #net-badge img'))), '');
	check('the invite link and the badge are plain text, too', !(await victim.evaluate(() => document.getElementById('net-badge').innerHTML.includes('<'))));
	const urlEvil = await open(`signal=${signal}&room=${encodeURIComponent('"><script>window.__pwned2=1</script>')}`);
	await sleep(500);
	check('a hostile room code in the URL is only text in the input, and is refused', !(await urlEvil.evaluate(() => window.__pwned2)) && /3 to 16/.test(await text(urlEvil, 'state-net')), await text(urlEvil, 'state-net'));

	check('no script errors on the pages', [a, b, c, d, evil, victim].every((p) => p.errors.length === 0), [a, b, c, d, evil, victim].flatMap((p) => p.errors).join(' | '));
} catch (e) {
	console.log('ERROR ' + (e && e.stack || e));
	failures++;
}
await browser.close();
await signaling.close();
files.close();
await sleep(100);
console.log(failures ? `RESULT: FAIL (${failures})` : 'RESULT: PASS');
process.exit(failures ? 1 : 0);
