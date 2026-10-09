// Small .zharmy packages for the launcher tests (see e2e.mjs): a ZIP writer (stored or deflated entries) and a manifest
// builder. Only what the launcher reads matters here: the ZIP directory and manifest.json.

import { deflateRawSync } from 'node:zlib';
import { mkdirSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const CRC_TABLE = (() => {
	const t = new Uint32Array(256);
	for (let n = 0; n < 256; n++) {
		let c = n;
		for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
		t[n] = c >>> 0;
	}
	return t;
})();

export function crc32(buf) {
	let c = 0xffffffff;
	for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
	return (c ^ 0xffffffff) >>> 0;
}

/** entries: [{ name, data: Buffer|string, deflate?: boolean, flags?: number }] -> Buffer of a ZIP archive. */
export function makeZip(entries, { comment = '' } = {}) {
	const parts = [];
	const central = [];
	let offset = 0;
	for (const e of entries) {
		const name = Buffer.from(e.name, 'utf8');
		const raw = Buffer.isBuffer(e.data) ? e.data : Buffer.from(e.data, 'utf8');
		const method = e.deflate ? 8 : 0;
		const data = e.deflate ? deflateRawSync(raw) : raw;
		const flags = (e.flags || 0) | 0x800;
		const local = Buffer.alloc(30);
		local.writeUInt32LE(0x04034b50, 0);
		local.writeUInt16LE(20, 4);
		local.writeUInt16LE(flags, 6);
		local.writeUInt16LE(method, 8);
		local.writeUInt32LE(crc32(raw), 14);
		local.writeUInt32LE(data.length, 18);
		local.writeUInt32LE(raw.length, 22);
		local.writeUInt16LE(name.length, 26);
		parts.push(local, name, data);
		const cd = Buffer.alloc(46);
		cd.writeUInt32LE(0x02014b50, 0);
		cd.writeUInt16LE(20, 4);
		cd.writeUInt16LE(20, 6);
		cd.writeUInt16LE(flags, 8);
		cd.writeUInt16LE(method, 10);
		cd.writeUInt32LE(crc32(raw), 16);
		cd.writeUInt32LE(data.length, 20);
		cd.writeUInt32LE(raw.length, 24);
		cd.writeUInt16LE(name.length, 28);
		cd.writeUInt32LE(offset, 42);
		central.push(cd, name);
		offset += 30 + name.length + data.length;
	}
	const cdBuf = Buffer.concat(central);
	const tail = Buffer.alloc(22);
	tail.writeUInt32LE(0x06054b50, 0);
	tail.writeUInt16LE(entries.length, 8);
	tail.writeUInt16LE(entries.length, 10);
	tail.writeUInt32LE(cdBuf.length, 12);
	tail.writeUInt32LE(offset, 16);
	const note = Buffer.from(comment, 'utf8');
	tail.writeUInt16LE(note.length, 20);
	return Buffer.concat([...parts, cdBuf, tail, note]);
}

/** A valid manifest (format 1) for tag/id, with overrides. */
export function manifestFor(id, tag, over = {}) {
	return {
		format: 1,
		id,
		tag,
		name: over.name || tag + ' Army',
		version: '1.0.0',
		description: 'A test army.',
		authors: ['Test Author'],
		license: 'Test licence for personal use.',
		source: { mod: 'TestMod', modVersion: '9.1' },
		requires: ['starter'],
		factions: [{ playerTemplate: tag + '_Faction', side: tag + '_Side', displayName: (over.name || tag + ' Army') + ' Faction', ai: true }],
		contentHash: 'sha256:' + '0'.repeat(64),
		converter: { tool: 'zharmy', version: '1.0.0', warnings: [] },
		...over,
	};
}

/** A package with this manifest (deflated), plus extra entries. */
export function packageFile(manifest, extra = [], { deflateManifest = true } = {}) {
	return makeZip([{ name: 'manifest.json', data: JSON.stringify(manifest), deflate: deflateManifest }, ...extra]);
}

/** Writes the test packages into dir; returns { dir, files: { name: Buffer }, valid, invalid } (names are paths below dir). */
export function writeTestPackages(dir) {
	const files = {};
	const put = (name, buf) => {
		files[name] = buf;
		const file = path.join(dir, name);
		mkdirSync(path.dirname(file), { recursive: true });
		writeFileSync(file, buf);
	};
	const filler = (n, seed) => Buffer.alloc(n, seed);
	// valid
	put('ironwood.zharmy', packageFile(
		manifestFor('test.ironwood', 'IRW', {
			name: 'Ironwood Army',
			factions: [
				{ playerTemplate: 'IRW_FactionA', side: 'IRW_A', displayName: 'Ironwood Vanguard', ai: true },
				{ playerTemplate: 'IRW_FactionB', side: 'IRW_B', displayName: 'Ironwood Humans', ai: false },
			],
		}),
		[
			{ name: 'LICENSE.txt', data: 'Free test licence: do what you like.\n' },
			{ name: 'Art/W3D/irw_unit.w3d', data: filler(300000, 0x37) },
		]));
	put('zh-only.zharmy', packageFile(manifestFor('test.zh-only', 'ZHO', { name: 'Zero Hour Only', requires: ['zerohour'] }), [], { deflateManifest: false }));
	put('any.zharmy', packageFile(manifestFor('test.any-army', 'ANY', { name: 'Works Anywhere', requires: [] })));
	// a big one: 30 MB of art and 1500 more entries, so the launcher must not read the whole file
	const many = [{ name: 'Art/Textures/big_texture.dds', data: filler(30 * 1024 * 1024, 0x55) }];
	for (let i = 0; i < 1500; i++) many.push({ name: 'Art/Textures/big_t' + String(i).padStart(4, '0') + '.tga', data: 'x' });
	put('Big Pack (copy).zharmy', packageFile(manifestFor('test.bigpack', 'BIG', { name: 'Big Pack', requires: ['starter', 'zerohour'] }), many));
	put('skipme.zharmy', packageFile(manifestFor('test.skipme', 'SKP', { name: 'Skip Me' })));
	put('more/sub.zharmy', packageFile(manifestFor('test.sub', 'SUB', { name: 'In A Subfolder' })));
	// invalid
	put('bad-zip.zharmy', Buffer.from(Array.from({ length: 1000 }, (_, i) => (i * 37) & 0xff)));
	put('tiny.zharmy', Buffer.from('0123456789'));
	put('no-manifest.zharmy', makeZip([{ name: 'readme.txt', data: 'hello' }]));
	put('bad-json.zharmy', makeZip([{ name: 'manifest.json', data: '{oops' }]));
	put('format2.zharmy', packageFile(manifestFor('test.format2', 'FM2', { format: 2 })));
	put('bad-tag.zharmy', packageFile(manifestFor('test.bad-tag', 'x1', { name: 'Bad Tag' })));
	put('bad-id.zharmy', packageFile(manifestFor('Bad ID', 'BID', { name: 'Bad Id' })));
	put('z-dup-id.zharmy', packageFile(manifestFor('test.ironwood', 'DUP', { name: 'Duplicate Id' })));
	put('z-dup-tag.zharmy', packageFile(manifestFor('test.other', 'IRW', { name: 'Duplicate Tag' })));
	put('notes.txt', Buffer.from('not a package'));
	return files;
}

/** Many small valid packages (for the long list test). */
export function writeManyPackages(dir, count) {
	for (let i = 0; i < count; i++) {
		const tag = 'M' + String(i).padStart(3, '0').replace(/\d/g, (d) => 'ABCDEFGHIJ'[d]);
		const id = 'many.army-' + String(i).padStart(3, '0');
		mkdirSync(dir, { recursive: true });
		writeFileSync(path.join(dir, 'many-' + String(i).padStart(3, '0') + '.zharmy'), packageFile(manifestFor(id, tag, { name: 'Many Army ' + (i + 1) })));
	}
}
