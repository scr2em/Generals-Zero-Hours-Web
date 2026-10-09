// Bench-only game data overlays: small, original edits of the free starter content (INI text) that let the bench exercise
// behaviour the starter units cannot show on their own (a splash weapon, a gun tower ...).  An overlay is a JSON file in
// scripts/aibench/fixtures/:
//   { "description": "...", "edits": [ { "file": "data/ini/weapon.ini", "find": "text", "replace": "text" },
//                                      { "file": "data/ini/aidata.ini", "append": "text" },
//                                      { "generate": "maps/gates.py" } ] }
// "file" is a path inside the starter pack (the lower case paths of its manifest.json).  `find` must occur exactly once.
// "generate" runs `python3 <script relative to the fixtures folder> <out dir>`; the files the script writes under <out dir> are
// added to the pack at the same relative paths (a new map, for instance), except those under <out dir>/append/, whose text is
// appended to the pack file of the same relative path (maps/mapcache.ini).
// The edited copy of the pack is served instead of the build's own; nothing in the repository's game data changes.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import os from 'node:os';

export function loadOverlay(fixturesDir, name) {
	const file = path.join(fixturesDir, name.endsWith('.json') ? name : name + '.json');
	if (!fs.existsSync(file)) throw new Error(`overlay ${name}: ${file} does not exist`);
	const o = JSON.parse(fs.readFileSync(file, 'utf8'));
	if (!Array.isArray(o.edits)) throw new Error(`overlay ${name}: "edits" is missing`);
	return { name, fixturesDir, ...o, edits: o.edits.map((e) => (e.generate ? { ...e, fixturesDir } : e)) };
}

function walk(dir, rel = '') {
	const out = [];
	for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
		const r = rel ? rel + '/' + e.name : e.name;
		if (e.isDirectory()) out.push(...walk(path.join(dir, e.name), r));
		else out.push(r);
	}
	return out;
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
	const added = [];
	for (const ed of overlay.edits) {
		if (ed.generate) {
			const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'aibench-gen-'));
			execFileSync('python3', ['-I', path.join(ed.fixturesDir, ed.generate), tmp], { stdio: ['ignore', 'inherit', 'inherit'] });
			for (const rel of walk(tmp)) {
				const data = fs.readFileSync(path.join(tmp, rel));
				if (rel.startsWith('append/')) {
					const target = rel.slice(7);
					if (!manifest.files.find((f) => f.path === target)) throw new Error(`overlay ${overlay.name}: ${target} is not in the starter pack`);
					const text = touched.has(target) ? touched.get(target) : fs.readFileSync(path.join(pack, target), 'utf8').replace(/\r\n/g, '\n');
					const add = data.toString('utf8').replace(/\r\n/g, '\n');
					touched.set(target, text + (text.endsWith('\n') ? '' : '\n') + add);
				} else {
					added.push({ rel, data });
				}
			}
			fs.rmSync(tmp, { recursive: true, force: true });
			continue;
		}
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
	for (const { rel, data } of added) {
		const file = path.join(packOut, rel);
		fs.mkdirSync(path.dirname(file), { recursive: true });
		fs.rmSync(file, { force: true });
		fs.writeFileSync(file, data);
		manifest.files = manifest.files.filter((f) => f.path !== rel);
		manifest.files.push({ path: rel, sha256: crypto.createHash('sha256').update(data).digest('hex'), size: data.length });
	}
	fs.rmSync(manifestPath, { force: true });
	fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 1));
	return site;
}
