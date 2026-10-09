// Helpers of the webnet tests: Playwright and Chromium, a static file server with the headers the game needs
// (cross origin isolation), and the signaling server.
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import net from 'node:net';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

export const here = path.dirname(fileURLToPath(import.meta.url));
export const serverDir = path.join(here, '..', 'server');

const require = createRequire(import.meta.url);
export function loadPlaywright() {
	try { return require('playwright'); } catch { /* fall through */ }
	return require(path.join(process.env.NODE_PATH || '/opt/node22/lib/node_modules', 'playwright'));
}

/** Flags that make two pages of one headless Chromium reach each other over WebRTC without a network. */
export const WEBRTC_FLAGS = [
	'--no-sandbox',
	'--enable-features=SharedArrayBuffer',
	'--disable-features=WebRtcHideLocalIpsWithMdns',   // host candidates as addresses: mDNS names do not resolve in a sandbox
	'--allow-loopback-in-peer-connection',
];

export function chromiumPath() {
	return [process.env.CHROMIUM_PATH, '/opt/pw-browsers/chromium-1194/chrome-linux/chrome'].find((p) => p && fs.existsSync(p));
}

const TYPES = {
	'.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
	'.png': 'image/png', '.json': 'application/json', '.css': 'text/css',
};

export function startStaticServer(root, port = 0) {
	const server = http.createServer((req, res) => {
		const url = new URL(req.url, 'http://localhost');
		let file = path.join(root, decodeURIComponent(url.pathname));
		if (!file.startsWith(path.resolve(root))) { res.writeHead(403); res.end(); return; }
		if (fs.existsSync(file) && fs.statSync(file).isDirectory()) file = path.join(file, 'index.html');
		if (!fs.existsSync(file)) { res.writeHead(404); res.end('not found'); return; }
		res.writeHead(200, {
			'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream',
			'Cross-Origin-Opener-Policy': 'same-origin',
			'Cross-Origin-Embedder-Policy': 'require-corp',
			'Cross-Origin-Resource-Policy': 'same-origin',
			'Cache-Control': 'no-store',
		});
		fs.createReadStream(file).pipe(res);
	});
	return new Promise((resolve) => server.listen(port, '127.0.0.1', () => resolve(server)));
}

/** The signaling server in this process, on a free port. */
export async function startSignaling(extra = {}) {
	const { loadConfig, startServer } = await import(path.join(serverDir, 'server.mjs'));
	const config = { ...loadConfig(['--port', '0', '--host', '127.0.0.1', '--quiet']), ...extra };
	const running = await startServer(config);
	return { ...running, url: `ws://127.0.0.1:${running.port}/signal` };
}

export const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/** A free TCP port (and, most of the time, the same UDP port). */
export function freePort() {
	return new Promise((resolve, reject) => {
		const server = net.createServer();
		server.listen(0, '127.0.0.1', () => { const { port } = server.address(); server.close(() => resolve(port)); });
		server.on('error', reject);
	});
}

/** Starts coturn on the loopback interface with the shared-secret ("TURN REST API") credentials that the
 *  signaling server hands out, logging to a file. Resolves to {url, port, secret, realm, log(), stop()}. */
export async function startCoturn({ secret = 'zh-test-secret', realm = 'zh.test', logFile } = {}) {
	const port = await freePort();
	const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'zh-coturn-'));
	logFile ??= path.join(dir, 'turn.log');
	const base = 20000 + Math.floor(Math.random() * 20000);
	const child = spawn('turnserver', [
		'--no-cli', '--no-tls', '--no-dtls', '-n', '--no-software-attribute',
		'--listening-ip=127.0.0.1', `--listening-port=${port}`, '--relay-ip=127.0.0.1', `--min-port=${base}`, `--max-port=${base + 2000}`,
		`--realm=${realm}`, '--use-auth-secret', `--static-auth-secret=${secret}`, '--fingerprint', '--no-stdout-log',
		'--allow-loopback-peers',      // both players are on this machine
		`--log-file=${logFile}`, '--verbose', `--pidfile=${path.join(dir, 'turn.pid')}`, '--simple-log',
	], { stdio: 'ignore' });
	let exited = false;
	child.on('exit', () => { exited = true; });
	// wait until it answers STUN binding requests
	const dgram = await import('node:dgram');
	const request = Buffer.alloc(20);
	request.writeUInt16BE(0x0001, 0); request.writeUInt32BE(0x2112a442, 4); request.fill(7, 8);
	for (let i = 0; i < 50; ++i) {
		if (exited) throw new Error('turnserver exited (is coturn installed? apt-get install coturn)');
		const ok = await new Promise((resolve) => {
			const socket = dgram.createSocket('udp4');
			const timer = setTimeout(() => { socket.close(); resolve(false); }, 150);
			socket.on('message', () => { clearTimeout(timer); socket.close(); resolve(true); });
			socket.send(request, port, '127.0.0.1');
		});
		if (ok) break;
		await sleep(100);
	}
	return {
		port, secret, realm, logFile, url: `turn:127.0.0.1:${port}`,
		log: () => (fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8') : ''),
		stop: () => new Promise((resolve) => { if (exited) { resolve(); return; } child.once('exit', resolve); child.kill('SIGTERM'); setTimeout(resolve, 2000).unref(); }),
	};
}
