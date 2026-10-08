import test from 'node:test';
import assert from 'node:assert/strict';
import { WebSocket } from 'ws';
import { loadConfig, startServer } from '../server.mjs';

async function boot(args = []) {
	return startServer(loadConfig(['--port', '0', '--host', '127.0.0.1', '--quiet', ...args], {}));
}

function connect(url, options) {
	return new Promise((resolve, reject) => {
		const ws = new WebSocket(url, options);
		ws.messages = [];
		ws.waiters = [];
		ws.on('message', (data, isBinary) => {
			const msg = isBinary ? { binary: Buffer.from(data) } : JSON.parse(data.toString());
			ws.messages.push(msg);
			for (const w of ws.waiters.splice(0)) w();
		});
		ws.once('open', () => resolve(ws));
		ws.once('error', reject);
		ws.once('unexpected-response', (req, res) => reject(Object.assign(new Error('refused'), { status: res.statusCode })));
	});
}

async function next(ws, predicate) {
	for (;;) {
		const i = ws.messages.findIndex(predicate);
		if (i >= 0) return ws.messages.splice(i, 1)[0];
		await new Promise((r) => { ws.waiters.push(r); setTimeout(r, 2000); });
		if (ws.messages.findIndex(predicate) < 0 && ws.waiters.length === 0) throw new Error('timeout');
	}
}

test('two WebSocket clients: join, signal, relay, leave', async (t) => {
	const s = await boot();
	t.after(() => s.close());
	const url = `ws://127.0.0.1:${s.port}/signal`;
	const a = await connect(url);
	a.send(JSON.stringify({ t: 'join', room: 'LAN1', name: 'A' }));
	const wa = await next(a, (m) => m.t === 'welcome');
	assert.equal(wa.ip, '10.77.0.2');
	const b = await connect(url);
	b.send(JSON.stringify({ t: 'join', room: 'lan1', name: 'B' }));
	const wb = await next(b, (m) => m.t === 'welcome');
	assert.equal(wb.peers.length, 1);
	await next(a, (m) => m.t === 'peer-joined');

	b.send(JSON.stringify({ t: 'signal', to: wa.id, data: { candidate: { candidate: 'x' } } }));
	assert.deepEqual((await next(a, (m) => m.t === 'signal')).data, { candidate: { candidate: 'x' } });

	b.send(Buffer.from([1, 0xff, 0x1f, 0x96, 0x1f, 0x96, 65, 66]));
	const relayed = await next(a, (m) => m.binary);
	assert.deepEqual([...relayed.binary], [1, wb.id, 0x1f, 0x96, 0x1f, 0x96, 65, 66]);

	const health = await (await fetch(`http://127.0.0.1:${s.port}/healthz`)).json();
	assert.deepEqual(health, { ok: true, rooms: 1, players: 2 });

	b.close();
	assert.equal((await next(a, (m) => m.t === 'peer-left')).id, wb.id);
	a.close();
});

test('origin allow list and connection limit', async (t) => {
	const s = await boot(['--origin', 'https://play.example.com', '--max-connections-per-ip', '2']);
	t.after(() => s.close());
	const url = `ws://127.0.0.1:${s.port}/signal`;
	await assert.rejects(connect(url, { origin: 'https://evil.example' }), { status: 403 });
	await assert.rejects(connect(url), { status: 403 });
	const a = await connect(url, { origin: 'https://play.example.com' });
	const b = await connect(url, { origin: 'https://play.example.com' });
	await assert.rejects(connect(url, { origin: 'https://play.example.com' }), { status: 429 });
	a.close(); b.close();
});

test('other paths are not WebSocket endpoints', async (t) => {
	const s = await boot();
	t.after(() => s.close());
	await assert.rejects(connect(`ws://127.0.0.1:${s.port}/other`), { status: 404 });
	assert.equal((await fetch(`http://127.0.0.1:${s.port}/`)).status, 404);
});

test('configuration: STUN default, TURN with a secret, environment variables', () => {
	assert.deepEqual(loadConfig([], {}).stun, ['stun:stun.l.google.com:19302']);
	assert.deepEqual(loadConfig(['--stun', 'none'], {}).stun, []);
	const c = loadConfig(['--turn', 'turn:t.example:3478', '--turn-secret', 'x'], { ZHNET_STUN: 'stun:a:1,stun:b:2', PORT: '9000' });
	assert.deepEqual(c.stun, ['stun:a:1', 'stun:b:2']);
	assert.deepEqual(c.turn, ['turn:t.example:3478']);
	assert.equal(c.port, 9000);
	assert.throws(() => loadConfig(['--subnet', 'nope'], {}));
});

test('static files carry the headers of a cross origin isolated page', async (t) => {
	const s = await boot(['--static', new URL('..', import.meta.url).pathname]);
	t.after(() => s.close());
	const res = await fetch(`http://127.0.0.1:${s.port}/package.json`);
	assert.equal(res.status, 200);
	assert.equal(res.headers.get('cross-origin-opener-policy'), 'same-origin');
	assert.equal(res.headers.get('cross-origin-embedder-policy'), 'require-corp');
	assert.equal((await fetch(`http://127.0.0.1:${s.port}/../etc/passwd`)).status, 404);
	assert.ok([403, 404].includes((await fetch(`http://127.0.0.1:${s.port}/%2e%2e/%2e%2e/etc/passwd`)).status));
});
