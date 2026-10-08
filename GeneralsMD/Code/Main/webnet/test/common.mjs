// Helpers of the webnet tests: Playwright and Chromium, a static file server with the headers the game needs
// (cross origin isolation), and the signaling server.
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
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
