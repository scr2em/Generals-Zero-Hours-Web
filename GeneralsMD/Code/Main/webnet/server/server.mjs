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
import { WebSocketServer } from 'ws';
import { Hub, MAX_DATAGRAM } from './hub.mjs';

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
	'max-connections-per-ip': { type: 'string', env: 'ZHNET_MAX_PER_IP', default: '24', help: 'WebSocket connections per client address' },
	stun: { type: 'string', multiple: true, env: 'ZHNET_STUN', help: `STUN server URL (repeat); default ${DEFAULT_STUN}; "none" disables STUN` },
	turn: { type: 'string', multiple: true, env: 'ZHNET_TURN', help: 'TURN server URL, e.g. turn:turn.example.com:3478 or turns:turn.example.com:5349 (repeat)' },
	'turn-user': { type: 'string', env: 'ZHNET_TURN_USER', help: 'fixed TURN user name' },
	'turn-pass': { type: 'string', env: 'ZHNET_TURN_PASS', help: 'fixed TURN password' },
	'turn-secret': { type: 'string', env: 'ZHNET_TURN_SECRET', help: 'shared secret of coturn (use-auth-secret): time-limited credentials are made per player' },
	'turn-ttl': { type: 'string', env: 'ZHNET_TURN_TTL', default: '86400', help: 'seconds the time-limited TURN credentials stay valid' },
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
		maxPerIp: Number(get('max-connections-per-ip')),
		stun: stun.length === 0 ? [DEFAULT_STUN] : stun.filter((s) => s !== 'none'),
		turn: splitList(get('turn')),
		turnUser: get('turn-user'),
		turnPass: get('turn-pass'),
		turnSecret: get('turn-secret'),
		turnTtl: Number(get('turn-ttl')),
		quiet: !!get('quiet'),
		help: !!get('help'),
	};
	if (!/^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(config.subnet)) throw new Error(`--subnet must be three octets, e.g. 10.77.0 (got "${config.subnet}")`);
	if (!(config.maxPlayers >= 2 && config.maxPlayers <= 253)) throw new Error('--max-players must be between 2 and 253');
	if (!(config.port >= 0 && config.port < 65536)) throw new Error('--port is not a port number');
	if (config.turn.length && !config.turnSecret && !config.turnUser) {
		console.warn('warning: --turn without --turn-user/--turn-secret: the TURN server is offered without credentials');
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
	'.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
	'.json': 'application/json', '.css': 'text/css', '.png': 'image/png', '.jpg': 'image/jpeg', '.svg': 'image/svg+xml',
	'.ico': 'image/x-icon', '.txt': 'text/plain; charset=utf-8', '.big': 'application/octet-stream',
};

function serveStatic(root, req, res) {
	let pathname;
	try { pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname); } catch { res.writeHead(400).end(); return; }
	let file = path.join(root, pathname);
	if (file !== root && !file.startsWith(root + path.sep)) { res.writeHead(403).end(); return; }
	fs.stat(file, (err, st) => {
		if (!err && st.isDirectory()) { file = path.join(file, 'index.html'); }
		fs.stat(file, (err2, st2) => {
			if (err2 || !st2.isFile()) { res.writeHead(404, { 'Content-Type': 'text/plain' }).end('not found'); return; }
			res.writeHead(200, {
				'Content-Type': MIME[path.extname(file).toLowerCase()] ?? 'application/octet-stream',
				'Content-Length': st2.size,
				// The game needs SharedArrayBuffer, hence a cross origin isolated page.
				'Cross-Origin-Opener-Policy': 'same-origin',
				'Cross-Origin-Embedder-Policy': 'require-corp',
				'Cross-Origin-Resource-Policy': 'same-origin',
				'Cache-Control': 'no-cache',
			});
			if (req.method === 'HEAD') { res.end(); return; }
			fs.createReadStream(file).pipe(res);
		});
	});
}

/** Starts the server; resolves to {server, hub, port, close()}. */
export async function startServer(config) {
	const log = config.quiet ? () => {} : (line) => console.log(`${new Date().toISOString()} ${line}`);
	const hub = new Hub(config, log);
	const perAddress = new Map();

	const server = http.createServer((req, res) => {
		const pathname = req.url.split('?')[0];
		if (pathname === '/healthz') {
			res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
			res.end(JSON.stringify({ ok: true, rooms: hub.rooms.size, players: hub.peerCount }));
			return;
		}
		if (config.static && (req.method === 'GET' || req.method === 'HEAD')) { serveStatic(config.static, req, res); return; }
		res.writeHead(404, { 'Content-Type': 'text/plain' }).end('Zero Hour web network server: connect with a WebSocket to ' + config.path + '\n');
	});

	const wss = new WebSocketServer({ noServer: true, maxPayload: 2 + 4 + MAX_DATAGRAM + 64, perMessageDeflate: false });

	const addressOf = (req) => {
		const forwarded = config.trustProxy ? String(req.headers['x-forwarded-for'] ?? '').split(',')[0].trim() : '';
		return forwarded || req.socket.remoteAddress || '?';
	};

	server.on('upgrade', (req, socket, head) => {
		const reject = (code, text) => { socket.write(`HTTP/1.1 ${code} ${text}\r\nConnection: close\r\n\r\n`); socket.destroy(); };
		if (req.url.split('?')[0] !== config.path) { reject(404, 'Not Found'); return; }
		const origin = req.headers.origin;
		if (config.origins.length && !config.origins.includes(origin)) { log(`refused origin ${origin}`); reject(403, 'Forbidden'); return; }
		const address = addressOf(req);
		if ((perAddress.get(address) ?? 0) >= config.maxPerIp) { log(`too many connections from ${address}`); reject(429, 'Too Many Requests'); return; }
		wss.handleUpgrade(req, socket, head, (ws) => {
			perAddress.set(address, (perAddress.get(address) ?? 0) + 1);
			onConnection(ws, address);
		});
	});

	function onConnection(ws, address) {
		const peer = hub.createPeer({
			send: (data, isBinary) => { if (ws.readyState === ws.OPEN) ws.send(data, { binary: isBinary }); },
			close: (code, reason) => ws.close(code, reason),
		});
		let alive = true;
		ws.on('pong', () => { alive = true; });
		const heartbeat = setInterval(() => {
			if (!alive) { ws.terminate(); return; }
			alive = false;
			ws.ping();
		}, 20000);
		ws.on('message', (data, isBinary) => {
			let keep;
			if (isBinary) keep = hub.handleBinary(peer, data);
			else keep = hub.handleText(peer, data.toString('utf8'));
			if (!keep) ws.close(1008, 'protocol violation');
		});
		ws.on('close', () => {
			clearInterval(heartbeat);
			hub.removePeer(peer);
			const n = (perAddress.get(address) ?? 1) - 1;
			if (n <= 0) perAddress.delete(address); else perAddress.set(address, n);
		});
		ws.on('error', () => {});
	}

	await new Promise((resolve, reject) => {
		server.once('error', reject);
		server.listen(config.port, config.host, resolve);
	});
	const port = server.address().port;
	log(`listening on ${config.host}:${port}${config.path}${config.static ? `, serving ${config.static}` : ''}`);
	log(`ICE: ${config.stun.join(', ') || 'no STUN'}${config.turn.length ? `, TURN ${config.turn.join(', ')}${config.turnSecret ? ' (time-limited credentials)' : ''}` : ', no TURN'}`);

	return {
		server, hub, port,
		close: () => new Promise((resolve) => {
			for (const client of wss.clients) client.terminate();
			server.close(() => resolve());
			server.closeAllConnections?.();
		}),
	};
}

if (process.argv[1] && fileURLToPath(import.meta.url) === path.resolve(process.argv[1])) {
	let config;
	try { config = loadConfig(); } catch (e) { console.error(e.message); console.error(usage()); process.exit(2); }
	if (config.help) { console.log(usage()); process.exit(0); }
	const running = await startServer(config);
	const stop = () => { running.close().then(() => process.exit(0)); setTimeout(() => process.exit(0), 2000).unref(); };
	process.on('SIGINT', stop);
	process.on('SIGTERM', stop);
}
