// The server against hostile or broken clients: raw HTTP requests, oversized and malformed WebSocket frames,
// connection floods, a trusted proxy's address header, a restart.
import test from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import zlib from 'node:zlib';
import { WebSocket } from 'ws';
import { loadConfig, startServer } from '../server.mjs';

async function boot(args = [], extra = {}) {
	const config = { ...loadConfig(['--port', '0', '--host', '127.0.0.1', '--quiet', ...args], {}), ...extra };
	return startServer(config);
}

/** One raw HTTP request over a TCP socket (so that paths are sent exactly as written); resolves {status, headers, body}. */
function raw(port, request) {
	return new Promise((resolve, reject) => {
		const socket = net.connect(port, '127.0.0.1', () => socket.write(request));
		const chunks = [];
		socket.on('data', (c) => chunks.push(c));
		socket.on('error', reject);
		socket.on('close', () => {
			const all = Buffer.concat(chunks);
			const end = all.indexOf('\r\n\r\n');
			const head = all.subarray(0, end < 0 ? all.length : end).toString('latin1').split('\r\n');
			const headers = Object.fromEntries(head.slice(1).map((l) => [l.slice(0, l.indexOf(':')).toLowerCase(), l.slice(l.indexOf(':') + 1).trim()]));
			resolve({ status: Number(/ (\d{3})/.exec(head[0])?.[1] ?? 0), headers, body: end < 0 ? Buffer.alloc(0) : all.subarray(end + 4) });
		});
		setTimeout(() => socket.end(), 1500).unref();
	});
}
const get = (port, target, extra = '') => raw(port, `GET ${target} HTTP/1.1\r\nHost: x\r\nConnection: close\r\n${extra}\r\n`);

function open(url, options) {
	return new Promise((resolve, reject) => {
		const ws = new WebSocket(url, options);
		ws.inbox = [];
		ws.on('message', (data, isBinary) => ws.inbox.push(isBinary ? { binary: Buffer.from(data) } : JSON.parse(data.toString())));
		ws.closed = new Promise((r) => ws.once('close', (code, reason) => r({ code, reason: reason.toString() })));
		ws.once('open', () => resolve(ws));
		ws.once('error', reject);
		ws.once('unexpected-response', (req, res) => reject(Object.assign(new Error('refused'), { status: res.statusCode })));
	});
}
async function until(fn, ms = 3000) {
	const end = Date.now() + ms;
	for (;;) {
		const v = fn();
		if (v) return v;
		if (Date.now() > end) throw new Error('timeout');
		await new Promise((r) => setTimeout(r, 10));
	}
}

async function untilAsync(fn, ms = 3000) {
	const end = Date.now() + ms;
	for (;;) {
		const v = await fn();
		if (v) return v;
		if (Date.now() > end) throw new Error('timeout');
		await new Promise((r) => setTimeout(r, 10));
	}
}

function site() {
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'zhnet-site-'));
	fs.writeFileSync(path.join(dir, 'index.html'), '<!doctype html><title>x</title>' + 'hello '.repeat(2000));
	fs.writeFileSync(path.join(dir, 'game.wasm'), Buffer.alloc(100000, 7));
	fs.writeFileSync(path.join(dir, 'pic.png'), Buffer.alloc(1000, 1));
	fs.writeFileSync(path.join(dir, '.secret'), 'nope');
	fs.mkdirSync(path.join(dir, '.git'));
	fs.writeFileSync(path.join(dir, '.git', 'config'), 'nope');
	fs.mkdirSync(path.join(dir, 'sub'));
	fs.writeFileSync(path.join(dir, 'sub', 'index.html'), 'sub index');
	return dir;
}

test('static files: hostile paths neither crash the server nor leave the root', async (t) => {
	const dir = site();
	const s = await boot(['--static', dir]);
	t.after(() => { s.close(); fs.rmSync(dir, { recursive: true }); });
	const outside = fs.mkdtempSync(path.join(os.tmpdir(), 'zhnet-outside-'));
	fs.writeFileSync(path.join(outside, 'secret.txt'), 'outside');
	t.after(() => fs.rmSync(outside, { recursive: true }));
	const attacks = ['/%00', '/index.html%00.png', '/%2e%2e/%2e%2e/etc/passwd', '/..%2f..%2fetc/passwd', '/../../etc/passwd', '/%252e%252e/etc/passwd',
		'/.git/config', '/.secret', '/sub/../.secret', '/%5c..%5c..%5cetc%5cpasswd', '/\\..\\..\\etc\\passwd', '/%', '/%zz', '/' + 'a'.repeat(5000), '//etc/passwd',
		'/' + path.relative(dir, path.join(outside, 'secret.txt'))];
	for (const target of attacks) {
		const res = await get(s.port, target);
		assert.ok([400, 403, 404, 414].includes(res.status) || (res.status === 0), `${target.slice(0, 40)} -> ${res.status}`);
		assert.ok(!res.body.toString().includes('outside') && !res.body.toString().includes('root:') && !res.body.toString().includes('nope'));
	}
	// the server is still there
	assert.equal((await get(s.port, '/healthz')).status, 200);
	assert.equal((await get(s.port, '/index.html')).status, 200);
});

test('static files: methods, index files, ETag, gzip, headers', async (t) => {
	const dir = site();
	const s = await boot(['--static', dir]);
	t.after(() => { s.close(); fs.rmSync(dir, { recursive: true }); });
	const index = await get(s.port, '/');
	assert.equal(index.status, 200);
	assert.equal(index.headers['cross-origin-opener-policy'], 'same-origin');
	assert.equal(index.headers['cross-origin-embedder-policy'], 'require-corp');
	assert.equal(index.headers['x-content-type-options'], 'nosniff');
	assert.match(index.headers['content-type'], /^text\/html/);
	assert.equal((await get(s.port, '/sub/')).body.toString(), 'sub index');
	assert.equal((await get(s.port, '/sub')).body.toString(), 'sub index');
	assert.equal((await get(s.port, '/nothing.html')).status, 404);
	for (const method of ['POST', 'PUT', 'DELETE', 'PATCH']) {
		const res = await raw(s.port, `${method} /index.html HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nConnection: close\r\n\r\n`);
		assert.equal(res.status, 405, method);
		assert.equal(res.headers.allow, 'GET, HEAD');
	}
	const head = await raw(s.port, 'HEAD /game.wasm HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n');
	assert.equal(head.status, 200);
	assert.equal(head.headers['content-length'], '100000');
	assert.equal(head.body.length, 0);
	// ETag
	const again = await get(s.port, '/game.wasm', `If-None-Match: ${(await get(s.port, '/game.wasm')).headers.etag}\r\n`);
	assert.equal(again.status, 304);
	// gzip for what compresses, and the data is intact
	const gz = await get(s.port, '/game.wasm', 'Accept-Encoding: gzip\r\n');
	assert.equal(gz.headers['content-encoding'], 'gzip');
	assert.deepEqual(zlib.gunzipSync(gz.body), Buffer.alloc(100000, 7));
	assert.ok(gz.body.length < 2000);
	assert.equal((await get(s.port, '/pic.png', 'Accept-Encoding: gzip\r\n')).headers['content-encoding'], undefined);
	assert.equal((await get(s.port, '/game.wasm')).headers['content-encoding'], undefined);
});

test('a malformed HTTP request does not take the server down', async (t) => {
	const s = await boot();
	t.after(() => s.close());
	await raw(s.port, 'GARBAGE\r\n\r\n');
	await raw(s.port, 'GET / HTTP/1.1\r\n' + 'X-A: ' + 'a'.repeat(100000) + '\r\n\r\n');
	await raw(s.port, 'GET /signal HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 99\r\n\r\n');
	await raw(s.port, 'GET /signal HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n');
	assert.equal((await get(s.port, '/healthz')).status, 200);
	const health = JSON.parse((await get(s.port, '/healthz')).body.toString());
	assert.equal(health.connections, 0, 'failed handshakes do not leak connection slots');
});

test('WebSocket: oversized, malformed and unjoined clients are dropped', async (t) => {
	const s = await boot([], { joinTimeoutMs: 300 });
	t.after(() => s.close());
	const url = `ws://127.0.0.1:${s.port}/signal`;

	const big = await open(url);
	big.send(Buffer.alloc(100_000, 1));
	assert.equal((await big.closed).code, 1009, 'a 100 kB frame is refused by the transport');

	const text = await open(url);
	text.send(' '.repeat(20_000));
	assert.equal((await text.closed).code, 1009);

	const junk = await open(url);
	junk.send('{{{');
	assert.equal((await junk.closed).code, 1008);

	const noType = await open(url);
	noType.send('{"room":"X"}');
	assert.equal((await noType.closed).code, 1008);

	const lazy = await open(url);     // never says anything
	assert.equal((await lazy.closed).code, 1008, 'closed for not joining');

	const binaryFirst = await open(url);
	binaryFirst.send(Buffer.from([1, 2, 3, 4, 5, 6, 7]));
	assert.equal((await binaryFirst.closed).code, 1008, 'a datagram before joining');

	const ok = await open(url);
	ok.send(JSON.stringify({ t: 'join', room: 'OKROOM' }));
	await until(() => ok.inbox.find((m) => m.t === 'welcome'));
	ok.send(Buffer.alloc(2000, 1));     // too big for a datagram: ignored, the client stays
	ok.send(Buffer.from([2, 0xff, 0, 1, 0, 2, 9]));   // not a data frame: ignored
	ok.send(JSON.stringify({ t: 'ping', n: 1 }));
	await until(() => ok.inbox.find((m) => m.t === 'pong'));
	assert.equal(ok.readyState, WebSocket.OPEN);
	ok.close();
	assert.equal((await get(s.port, '/healthz')).status, 200);
});

test('connection limits: per address and in total, slots are given back', async (t) => {
	const s = await boot(['--max-connections-per-ip', '3', '--max-connections', '5']);
	t.after(() => s.close());
	const url = `ws://127.0.0.1:${s.port}/signal`;
	const held = [await open(url), await open(url), await open(url)];
	await assert.rejects(open(url), { status: 429 });
	held[0].close();
	await held[0].closed;
	const again = await untilAsync(() => open(url).catch(() => null), 2000);
	assert.ok(again, 'a closed connection frees its slot');
	again.close(); held[1].close(); held[2].close();
	await untilAsync(async () => JSON.parse((await get(s.port, '/healthz')).body.toString()).connections === 0, 2000).catch(() => {});
	assert.equal(JSON.parse((await get(s.port, '/healthz')).body.toString()).connections, 0);
});

test('total connection limit', async (t) => {
	const s = await boot(['--max-connections', '2']);
	t.after(() => s.close());
	const url = `ws://127.0.0.1:${s.port}/signal`;
	const a = await open(url);
	const b = await open(url);
	await assert.rejects(open(url), { status: 503 });
	a.close(); b.close();
});

test('behind a proxy only the address the proxy added counts (X-Forwarded-For, last entry)', async (t) => {
	const s = await boot(['--trust-proxy', '--max-connections-per-ip', '2']);
	t.after(() => s.close());
	const url = `ws://127.0.0.1:${s.port}/signal`;
	const as = (client, spoof = '') => open(url, { headers: { 'X-Forwarded-For': `${spoof}${spoof ? ', ' : ''}${client}` } });
	const a1 = await as('10.0.0.1', '8.8.8.8');
	const a2 = await as('10.0.0.1', '9.9.9.9');          // a different spoofed prefix does not make a new client
	await assert.rejects(as('10.0.0.1', '7.7.7.7'), { status: 429 });
	const b = await as('10.0.0.2');
	for (const w of [a1, a2, b]) w.close();
});

test('a restart tells the players to come back (close code 1012)', async (t) => {
	const s = await boot();
	const ws = await open(`ws://127.0.0.1:${s.port}/signal`);
	ws.send(JSON.stringify({ t: 'join', room: 'RESTART' }));
	await until(() => ws.inbox.find((m) => m.t === 'welcome'));
	await s.close();
	const closed = await ws.closed;
	assert.equal(closed.code, 1012);
});

test('rate limiting does not hit a realistic 8 player join (signaling burst)', async (t) => {
	// Every newcomer sends an offer and ~12 candidates to each earlier player; everybody answers: ~100 messages.
	const s = await boot(['--max-players', '8']);
	t.after(() => s.close());
	const url = `ws://127.0.0.1:${s.port}/signal`;
	const clients = [];
	for (let i = 0; i < 8; ++i) {
		const ws = await open(url);
		ws.send(JSON.stringify({ t: 'join', room: 'BIG8', name: 'P' + i }));
		const welcome = await until(() => ws.inbox.find((m) => m.t === 'welcome'));
		clients.push({ ws, welcome });
		const sdp = 'v=0\r\n' + 'a=x\r\n'.repeat(120);          // ~600 bytes, like a data channel only offer
		for (const peer of welcome.peers) {
			ws.send(JSON.stringify({ t: 'signal', to: peer.id, data: { sdp: { type: 'offer', sdp } } }));
			for (let c = 0; c < 12; ++c) ws.send(JSON.stringify({ t: 'signal', to: peer.id, data: { candidate: { candidate: 'candidate:' + 'x'.repeat(100), sdpMid: '0', sdpMLineIndex: 0 } } }));
		}
	}
	await new Promise((r) => setTimeout(r, 300));
	const errors = clients.flatMap((c) => c.ws.inbox.filter((m) => m.t === 'error'));
	assert.deepEqual(errors, []);
	assert.equal(s.hub.stats.rateLimitedControl, 0);
	for (const c of clients) c.ws.close();
});
