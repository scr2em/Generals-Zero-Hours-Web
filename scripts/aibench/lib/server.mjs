// A static file server for the web builds under test. Every build is mounted under its own path
// (/<name>/z_generals.html) on ONE origin, so the browser's file storage (OPFS) holds the game data once for
// all of them. The headers make the pages cross-origin isolated, which the game's threads need.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';

const TYPES = {
	'.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
	'.json': 'application/json', '.css': 'text/css', '.svg': 'image/svg+xml', '.png': 'image/png', '.ico': 'image/x-icon',
	'.txt': 'text/plain', '.ini': 'text/plain', '.big': 'application/octet-stream',
};

export function startServer(mounts, port) {
	const server = http.createServer((req, res) => {
		const url = new URL(req.url, 'http://x');
		const parts = decodeURIComponent(url.pathname).split('/').filter(Boolean);
		const dir = mounts[parts[0]];
		const headers = {
			'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
			'Cross-Origin-Resource-Policy': 'same-origin', 'Cache-Control': 'no-cache',
		};
		if (!dir) { res.writeHead(404, headers); res.end('unknown build'); return; }
		const rel = parts.slice(1).join('/') || 'z_generals.html';
		const file = path.resolve(dir, rel);
		if (file !== path.resolve(dir) && !file.startsWith(path.resolve(dir) + path.sep)) { res.writeHead(403, headers); res.end(); return; }
		fs.stat(file, (err, st) => {
			if (err || !st.isFile()) { res.writeHead(404, headers); res.end('not found'); return; }
			res.writeHead(200, { ...headers, 'Content-Type': TYPES[path.extname(file).toLowerCase()] || 'application/octet-stream', 'Content-Length': st.size });
			fs.createReadStream(file).pipe(res);
		});
	});
	return new Promise((resolve, reject) => {
		server.once('error', reject);
		server.listen(port, '127.0.0.1', () => resolve({ server, port: server.address().port }));
	});
}
