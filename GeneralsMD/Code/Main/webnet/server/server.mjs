#!/usr/bin/env node
// Signaling and virtual LAN room server for the WebAssembly port of Zero Hour.
//
//   node server.mjs [--port 8787] [--host 0.0.0.0] [--static <dir>] [--origin https://play.example.com] ...
//
// See ../README.md for the design and for deployment. Everything can be set by option or environment variable.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { parseArgs } from 'node:util';
import { fileURLToPath } from 'node:url';
import zlib from 'node:zlib';
import { WebSocketServer } from 'ws';
import { Hub, DEFAULTS, MAX_TEXT } from './hub.mjs';

export const DEFAULT_STUN = 'stun:stun.l.google.com:19302';

const OPTIONS = {
	port: { type: 'string', short: 'p', env: 'PORT', default: '8787', help: 'TCP port to listen on (0 = any free port)' },
	host: { type: 'string', env: 'HOST', default: '0.0.0.0', help: 'address to listen on' },
	path: { type: 'string', env: 'ZHNET_PATH', default: '/signal', help: 'URL path of the WebSocket endpoint' },
	static: { type: 'string', env: 'ZHNET_STATIC', help: 'also serve this directory (the game build) with the COOP/COEP headers the game needs' },
	origin: { type: 'string', multiple: true, env: 'ZHNET_ORIGINS', help: 'allowed Origin of WebSocket clients (repeat, or comma separated in the variable); default: any' },
	'trust-proxy': { type: 'boolean', env: 'ZHNET_TRUST_PROXY', help: 'log the client address from X-Forwarded-For (behind nginx, Caddy, ...)' },
	subnet: { type: 'string', env: 'ZHNET_SUBNET', default: '10.77.0', help: 'first three octets of the virtual addresses' },
	'max-players': { type: 'string', env: 'ZHNET_MAX_PLAYERS', default: '16', help: 'players per room' },
	'max-rooms': { type: 'string', env: 'ZHNET_MAX_ROOMS', default: '500', help: 'rooms at the same time' },
	'max-rooms-per-ip': { type: 'string', env: 'ZHNET_MAX_ROOMS_PER_IP', default: String(DEFAULTS.maxRoomsPerIp), help: 'rooms one client address may have open' },
	'max-connections-per-ip': { type: 'string', env: 'ZHNET_MAX_PER_IP', default: '24', help: 'WebSocket connections per client address' },
	'max-connections': { type: 'string', env: 'ZHNET_MAX_CONNECTIONS', default: '2000', help: 'WebSocket connections in total' },
	stun: { type: 'string', multiple: true, env: 'ZHNET_STUN', help: `STUN server URL (repeat); default ${DEFAULT_STUN}; "none" disables STUN` },
	turn: { type: 'string', multiple: true, env: 'ZHNET_TURN', help: 'TURN server URL, e.g. turn:turn.example.com:3478 or turns:turn.example.com:5349 (repeat)' },
	'turn-user': { type: 'string', env: 'ZHNET_TURN_USER', help: 'fixed TURN user name' },
	'turn-pass': { type: 'string', env: 'ZHNET_TURN_PASS', help: 'fixed TURN password' },
	'turn-secret': { type: 'string', env: 'ZHNET_TURN_SECRET', help: 'shared secret of coturn (use-auth-secret): time-limited credentials are made per player' },
	'turn-ttl': { type: 'string', env: 'ZHNET_TURN_TTL', default: '86400', help: 'seconds the time-limited TURN credentials stay valid' },
	'ice-relay-only': { type: 'boolean', env: 'ZHNET_ICE_RELAY_ONLY', help: 'tell the clients to connect only through TURN (needs --turn; for tests and strict networks)' },
	quiet: { type: 'boolean', short: 'q', help: 'no log lines' },
	help: { type: 'boolean', short: 'h' },
};

function splitList(values) {
	return (values ?? []).flatMap((v) => String(v).split(',')).map((s) => s.trim()).filter(Boolean);
}

/** Options and environment into a configuration object (the command line wins). */
export function loadConfig(argv = process.argv.slice(2), env = process.env) {
	const { values } = parseArgs({
		args: argv,
		options: Object.fromEntries(Object.entries(OPTIONS).map(([k, v]) => [k, { type: v.type, ...(v.short ? { short: v.short } : {}), multiple: !!v.multiple }])),
	});
	const get = (key) => {
		if (values[key] !== undefined) return values[key];
		const spec = OPTIONS[key];
		if (spec.env && env[spec.env] !== undefined && env[spec.env] !== '') {
			return spec.type === 'boolean' ? /^(1|true|yes|on)$/i.test(env[spec.env]) : (spec.multiple ? [env[spec.env]] : env[spec.env]);
		}
		return spec.default;
	};
	const stun = splitList(get('stun'));
	const config = {
		port: Number(get('port')),
		host: get('host'),
		path: get('path'),
		static: get('static') ? path.resolve(get('static')) : null,
		origins: splitList(get('origin')),
		trustProxy: !!get('trust-proxy'),
		subnet: get('subnet'),
		maxPlayers: Number(get('max-players')),
		maxRooms: Number(get('max-rooms')),
		maxRoomsPerIp: Number(get('max-rooms-per-ip')),
		maxPerIp: Number(get('max-connections-per-ip')),
		maxConnections: Number(get('max-connections')),
		stun: stun.length === 0 ? [DEFAULT_STUN] : stun.filter((s) => s !== 'none'),
		turn: splitList(get('turn')),
		turnUser: get('turn-user'),
		turnPass: get('turn-pass'),
		turnSecret: get('turn-secret'),
		turnTtl: Number(get('turn-ttl')),
		iceRelayOnly: !!get('ice-relay-only'),
		quiet: !!get('quiet'),
		help: !!get('help'),
	};
	if (!/^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(config.subnet)) throw new Error(`--subnet must be three octets, e.g. 10.77.0 (got "${config.subnet}")`);
	if (!(config.maxPlayers >= 2 && config.maxPlayers <= 253)) throw new Error('--max-players must be between 2 and 253');
	if (!(config.port >= 0 && config.port < 65536)) throw new Error('--port is not a port number');
	for (const [key, flag] of [['maxRooms', 'max-rooms'], ['maxRoomsPerIp', 'max-rooms-per-ip'], ['maxPerIp', 'max-connections-per-ip'],
		['maxConnections', 'max-connections'], ['turnTtl', 'turn-ttl']]) {
		if (!Number.isInteger(config[key]) || config[key] < 1) throw new Error(`--${flag} must be a positive number`);
	}
	if (config.iceRelayOnly && !config.turn.length) throw new Error('--ice-relay-only needs --turn');
	if (config.turn.length && !config.turnSecret && !config.turnUser) {
		// Browsers refuse to create a connection with a turn: URL that has no user name and credential.
		throw new Error('--turn needs --turn-secret (coturn use-auth-secret) or --turn-user and --turn-pass');
	}
	for (const url of config.turn) {
		if (!/^turns?:[^\s?]+(\?transport=(udp|tcp))?$/i.test(url)) throw new Error(`--turn ${url}: expected turn:host:port or turns:host:port`);
	}
	for (const url of config.stun) {
		if (!/^stuns?:[^\s]+$/i.test(url)) throw new Error(`--stun ${url}: expected stun:host:port`);
	}
	return config;
}

export function usage() {
	const lines = ['Usage: node server.mjs [options]', ''];
	for (const [key, spec] of Object.entries(OPTIONS)) {
		const flag = `--${key}${spec.type === 'string' ? ' <value>' : ''}`;
		lines.push(`  ${flag.padEnd(36)} ${spec.help ?? ''}${spec.env ? `  [$${spec.env}]` : ''}${spec.default ? ` (default ${spec.default})` : ''}`);
	}
	return lines.join('\n');
}

const MIME = {
	'.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8', '.wasm': 'application/wasm',
	'.json': 'application/json', '.css': 'text/css; charset=utf-8', '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml',
	'.ico': 'image/x-icon', '.txt': 'text/plain; charset=utf-8', '.big': 'application/octet-stream', '.map': 'application/json',
};
const COMPRESSIBLE = new Set(['.html', '.js', '.mjs', '.wasm', '.json', '.css', '.svg', '.txt']);
const MAX_COMPRESS_BYTES = 64 * 1024 * 1024;
const MAX_CACHE_BYTES = 256 * 1024 * 1024;

// The game build is a few large files that rarely change: gzip them once and keep the result in memory.
const gzipCache = new Map();   // file -> {etag, buffer}
let gzipCacheBytes = 0;
function gzipped(file, etag, raw) {
	const hit = gzipCache.get(file);
	if (hit && hit.etag === etag) return hit.buffer;
	const buffer = zlib.gzipSync(raw, { level: 6 });
	if (hit) gzipCacheBytes -= hit.buffer.length;
	if (gzipCacheBytes + buffer.length <= MAX_CACHE_BYTES) {
		gzipCache.set(file, { etag, buffer });
		gzipCacheBytes += buffer.length;
	}
	return buffer;
}

const plain = (res, code, text, headers = {}) => { res.writeHead(code, { 'Content-Type': 'text/plain; charset=utf-8', 'X-Content-Type-Options': 'nosniff', ...headers }).end(text); };

/** Static files for --static: no directory listings, no dot files, no way out of the root, ETag + gzip. */
function serveStatic(root, req, res) {
	if (req.method !== 'GET' && req.method !== 'HEAD') { plain(res, 405, 'method not allowed', { Allow: 'GET, HEAD' }); return; }
	let pathname;
	try { pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname); } catch { plain(res, 400, 'bad request'); return; }
	// A NUL byte makes the fs functions throw; a backslash is a separator on Windows; dot files and ".." are never served.
	if (pathname.includes('\0') || pathname.includes('\\') || pathname.split('/').some((seg) => seg.startsWith('.'))) { plain(res, 404, 'not found'); return; }
	let file = path.join(root, pathname);
	if (file !== root && !file.startsWith(root + path.sep)) { plain(res, 403, 'forbidden'); return; }
	fs.stat(file, (err, st) => {
		if (!err && st.isDirectory()) file = path.join(file, 'index.html');
		fs.stat(file, (err2, st2) => {
			if (err2 || !st2.isFile()) { plain(res, 404, 'not found'); return; }
			const ext = path.extname(file).toLowerCase();
			const etag = `"${st2.size.toString(16)}-${Math.floor(st2.mtimeMs).toString(16)}"`;
			const headers = {
				'Content-Type': MIME[ext] ?? 'application/octet-stream',
				'X-Content-Type-Options': 'nosniff',
				// The game needs SharedArrayBuffer, hence a cross origin isolated page.
				'Cross-Origin-Opener-Policy': 'same-origin',
				'Cross-Origin-Embedder-Policy': 'require-corp',
				'Cross-Origin-Resource-Policy': 'same-origin',
				'Cache-Control': 'no-cache',     // revalidate: the ETag makes that cheap
				ETag: etag,
				Vary: 'Accept-Encoding',
			};
			if (req.headers['if-none-match'] === etag) { res.writeHead(304, headers).end(); return; }
			const wantsGzip = COMPRESSIBLE.has(ext) && st2.size <= MAX_COMPRESS_BYTES && /\bgzip\b/.test(req.headers['accept-encoding'] ?? '');
			if (wantsGzip) {
				fs.readFile(file, (err3, raw) => {
					if (err3) { plain(res, 404, 'not found'); return; }
					const body = gzipped(file, etag, raw);
					res.writeHead(200, { ...headers, 'Content-Encoding': 'gzip', 'Content-Length': body.length });
					res.end(req.method === 'HEAD' ? undefined : body);
				});
				return;
			}
			res.writeHead(200, { ...headers, 'Content-Length': st2.size });
			if (req.method === 'HEAD') { res.end(); return; }
			const stream = fs.createReadStream(file);
			stream.on('error', () => res.destroy());
			stream.pipe(res);
		});
	});
}

const SLOW_CONSUMER_BYTES = 1 << 20;     // datagrams for a client with this much unsent data are dropped (like UDP)
const HARD_LIMIT_BYTES = 8 << 20;        // a client this far behind is disconnected
const JOIN_TIMEOUT_MS = 15000;           // a connection that does not join a room in this time is closed

/** Starts the server; resolves to {server, hub, port, close()}. */
export async function startServer(config) {
	const log = config.quiet ? () => {} : (line) => console.log(`${new Date().toISOString()} ${line}`);
	const hub = new Hub(config, log);
	const perAddress = new Map();
	const started = Date.now();
	let connections = 0;

	const server = http.createServer((req, res) => {
		const pathname = req.url.split('?')[0];
		if (pathname === '/healthz') {
			const body = JSON.stringify({ ok: true, rooms: hub.rooms.size, players: hub.peerCount, connections, uptime: Math.round((Date.now() - started) / 1000), stats: hub.stats });
			res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store', 'Content-Length': Buffer.byteLength(body), 'X-Content-Type-Options': 'nosniff' });
			res.end(body);
			return;
		}
		if (config.static) { serveStatic(config.static, req, res); return; }
		plain(res, 404, 'Zero Hour web network server: connect with a WebSocket to ' + config.path + '\n');
	});
	// Slow or stalled HTTP clients (slowloris): bounded header and request times.
	server.headersTimeout = 15000;
	server.requestTimeout = 60000;
	server.keepAliveTimeout = 10000;
	server.maxHeadersCount = 64;
	server.on('clientError', (err, socket) => {
		if (socket.writable) socket.end('HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n');
		else socket.destroy();
	});

	// JSON messages are small; a relayed datagram is at most MAX_DATAGRAM + 6 (the hub checks that for binary frames).
	const wss = new WebSocketServer({ noServer: true, maxPayload: MAX_TEXT, perMessageDeflate: false });

	const addressOf = (req) => {
		let address = req.socket.remoteAddress || '?';
		if (config.trustProxy) {
			// The last entry is the one our own proxy added; the ones before it are whatever the client claimed.
			const forwarded = String(req.headers['x-forwarded-for'] ?? '').split(',').pop().trim();
			if (forwarded) address = forwarded;
		}
		return address.replace(/^::ffff:/i, '').slice(0, 64);
	};

	server.on('upgrade', (req, socket, head) => {
		socket.on('error', () => {});
		const reject = (code, text) => { socket.write(`HTTP/1.1 ${code} ${text}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`); socket.destroy(); };
		if (req.url.split('?')[0] !== config.path) { reject(404, 'Not Found'); return; }
		const origin = req.headers.origin;
		if (config.origins.length && !config.origins.includes(origin)) { log(`refused origin ${clipForLog(origin)}`); reject(403, 'Forbidden'); return; }
		const address = addressOf(req);
		if (connections >= config.maxConnections) { log('too many connections'); reject(503, 'Service Unavailable'); return; }
		if ((perAddress.get(address) ?? 0) >= config.maxPerIp) { log(`too many connections from ${address}`); reject(429, 'Too Many Requests'); return; }
		perAddress.set(address, (perAddress.get(address) ?? 0) + 1);
		++connections;
		let upgraded = false;
		try {
			wss.handleUpgrade(req, socket, head, (ws) => { upgraded = true; onConnection(ws, address); });
		} catch (e) {
			// (a malformed handshake)
		}
		if (!upgraded && socket.destroyed) release(address);
		else if (!upgraded) socket.once('close', () => { if (!upgraded) release(address); });
	});

	function release(address) {
		--connections;
		const n = (perAddress.get(address) ?? 1) - 1;
		if (n <= 0) perAddress.delete(address); else perAddress.set(address, n);
	}

	function onConnection(ws, address) {
		const peer = hub.createPeer({
			send: (data, isBinary) => {
				if (ws.readyState !== ws.OPEN) return;
				const queued = ws.bufferedAmount;
				if (queued > HARD_LIMIT_BYTES) { ws.terminate(); return; }
				if (isBinary && queued > SLOW_CONSUMER_BYTES) return;     // a slow reader loses datagrams, not the server's memory
				ws.send(data, { binary: isBinary });
			},
			close: (code, reason) => ws.close(code, reason),
		}, address);
		let alive = true;
		ws.on('pong', () => { alive = true; });
		const heartbeat = setInterval(() => {
			if (!alive) { ws.terminate(); return; }
			alive = false;
			ws.ping();
		}, 20000);
		const joinTimer = setTimeout(() => { if (!peer.room) ws.close(1008, 'join a room first'); }, config.joinTimeoutMs ?? JOIN_TIMEOUT_MS);
		ws.on('message', (data, isBinary) => {
			let keep;
			if (isBinary) keep = hub.handleBinary(peer, data);
			else keep = hub.handleText(peer, data.toString('utf8'));
			if (!keep) ws.close(1008, 'protocol violation');
		});
		ws.on('close', () => {
			clearInterval(heartbeat);
			clearTimeout(joinTimer);
			hub.removePeer(peer);
			release(address);
		});
		ws.on('error', () => {});
	}

	const pruneTimer = setInterval(() => hub.prune(), 60000);
	pruneTimer.unref();

	await new Promise((resolve, reject) => {
		server.once('error', reject);
		server.listen(config.port, config.host, resolve);
	});
	const port = server.address().port;
	log(`listening on ${config.host}:${port}${config.path}${config.static ? `, serving ${config.static}` : ''}`);
	log(`ICE: ${config.stun.join(', ') || 'no STUN'}${config.turn.length ? `, TURN ${config.turn.join(', ')}${config.turnSecret ? ' (time-limited credentials)' : ''}${config.iceRelayOnly ? ', relay only' : ''}` : ', no TURN'}`);

	return {
		server, hub, port,
		/** Closes the server; the players are told to expect it back (WebSocket close code 1012, service restart). */
		close: () => new Promise((resolve) => {
			clearInterval(pruneTimer);
			for (const client of wss.clients) client.close(1012, 'server restarting');
			const force = setTimeout(() => { for (const client of wss.clients) client.terminate(); server.closeAllConnections?.(); }, 300);
			server.close(() => { clearTimeout(force); resolve(); });
			server.closeIdleConnections?.();
		}),
	};
}

function clipForLog(value) { return String(value ?? '').replace(/[^\x20-\x7e]/g, '?').slice(0, 100); }

if (process.argv[1] && fileURLToPath(import.meta.url) === path.resolve(process.argv[1])) {
	let config;
	try { config = loadConfig(); } catch (e) { console.error(e.message); console.error(usage()); process.exit(2); }
	if (config.help) { console.log(usage()); process.exit(0); }
	const running = await startServer(config);
	const stop = () => { running.close().then(() => process.exit(0)); setTimeout(() => process.exit(0), 2000).unref(); };
	process.on('SIGINT', stop);
	process.on('SIGTERM', stop);
	// A bug must not take every room down silently: log it and keep serving.
	process.on('uncaughtException', (e) => console.error('uncaught exception', e));
	process.on('unhandledRejection', (e) => console.error('unhandled rejection', e));
}
