// Bench-only game data overlays: small, original edits of the free starter content (INI text) that let the bench exercise
// behaviour the starter units cannot show on their own (a splash weapon, a gun tower ...).  An overlay is a JSON file in
// scripts/aibench/fixtures/:
//   { "description": "...", "edits": [ { "file": "data/ini/weapon.ini", "find": "text", "replace": "text" },
//                                      { "file": "data/ini/aidata.ini", "append": "text" } ] }
// "file" is a path inside the starter pack (the lower case paths of its manifest.json).  `find` must occur exactly once.
// The edited copy of the pack is served instead of the build's own; nothing in the repository's game data changes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

export function loadOverlay(fixturesDir, name) {
	const file = path.join(fixturesDir, name.endsWith('.json') ? name : name + '.json');
	if (!fs.existsSync(file)) throw new Error(`overlay ${name}: ${file} does not exist`);
	const o = JSON.parse(fs.readFileSync(file, 'utf8'));
	if (!Array.isArray(o.edits)) throw new Error(`overlay ${name}: "edits" is missing`);
	return { name, ...o };
}

function linkTree(src, dst, skip) {
	fs.mkdirSync(dst, { recursive: true });
	for (const e of fs.readdirSync(src, { withFileTypes: true })) {
		if (skip && skip(e.name)) continue;
		const s = path.join(src, e.name), d = path.join(dst, e.name);
		if (e.isDirectory()) linkTree(s, d, null);
		else fs.symlinkSync(s, d);
	}
}

// Makes a copy of the build directory (symlinks) whose starterpack/ carries the overlay's edits. Returns its path.
export function makeOverlaySite(buildDir, overlay, outDir) {
	const site = path.join(outDir, 'overlay-' + overlay.name + '-' + path.basename(path.dirname(buildDir)) + '-' + path.basename(buildDir));
	fs.rmSync(site, { recursive: true, force: true });
	linkTree(buildDir, site, (n) => n === 'starterpack');
	const pack = path.join(buildDir, 'starterpack');
	const packOut = path.join(site, 'starterpack');
	linkTree(pack, packOut, null);

	const manifestPath = path.join(packOut, 'manifest.json');
	const manifest = JSON.parse(fs.readFileSync(path.join(pack, 'manifest.json'), 'utf8'));
	const touched = new Map();
	for (const ed of overlay.edits) {
		const rel = ed.file;
		const entry = manifest.files.find((f) => f.path === rel);
		if (!entry) throw new Error(`overlay ${overlay.name}: ${rel} is not in the starter pack`);
		// The pack uses CRLF line ends: edit with LF and convert back.
		let text = touched.has(rel) ? touched.get(rel) : fs.readFileSync(path.join(pack, rel), 'utf8').replace(/\r\n/g, '\n');
		if (ed.append !== undefined) {
			text += (text.endsWith('\n') ? '' : '\n') + ed.append + '\n';
		} else {
			const parts = text.split(ed.find);
			if (parts.length !== 2) throw new Error(`overlay ${overlay.name}: "${String(ed.find).slice(0, 50)}" occurs ${parts.length - 1} times in ${rel} (must be once)`);
			text = parts[0] + ed.replace + parts[1];
		}
		touched.set(rel, text);
	}
	for (const [rel, text] of touched) {
		const file = path.join(packOut, rel);
		fs.rmSync(file, { force: true });
		fs.writeFileSync(file, text.replace(/\n/g, '\r\n'));
		const entry = manifest.files.find((f) => f.path === rel);
		const buf = Buffer.from(text.replace(/\n/g, '\r\n'));
		entry.size = buf.length;
		entry.sha256 = crypto.createHash('sha256').update(buf).digest('hex');
	}
	fs.rmSync(manifestPath, { force: true });
	fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 1));
	return site;
}
