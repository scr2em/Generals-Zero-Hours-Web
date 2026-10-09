// The armies folder of the launcher: finds the player's army packages (*.zharmy, see docs/ARMY_PACKAGES.md) in a folder,
// reads what the launcher shows about each one, and decides which ones can be used with the game data that is chosen.
//
// A package is a ZIP file that can be big (models, textures, sounds). Nothing here reads a whole package: the end of the
// file (the ZIP "central directory", which lists the entries) and the few small entries the launcher shows
// (manifest.json, LICENSE*, README*) are read with File.slice, which is all that is needed to list 50 packages quickly.
//
// Pure functions where possible, so they can be tried without the page:
//   readPackage(file)                    -> { ok, error, manifest, license, readme }
//   validateManifest(manifest)           -> null, or the reason it is not usable
//   listPackageFiles(dirHandle) / listPackageFilesFromFileList(fileList)
//   scanPackages(items, { onProgress })  -> package records, duplicates marked
//   availability(manifest, ruleset)      -> { ok, needs: [rulesets] }
//
// The file name a package gets inside the game's file system (/armies/<served>) is made here, too (servedNames).

export const ARMIES_MOUNT = '/armies';
export const MAX_PACKAGES = 400;

const ID_RE = /^[a-z0-9][a-z0-9.-]{2,63}$/;
const TAG_RE = /^[A-Z][A-Z0-9]{1,5}$/;
const IDENT_RE = /^[A-Za-z0-9_]+$/;
const MANIFEST_LIMIT = 1024 * 1024;
const TEXT_LIMIT = 256 * 1024;       // a LICENSE/README bigger than this (compressed) is not read
const TEXT_SHOWN = 6000;             // characters of it that are kept
const CENTRAL_LIMIT = 16 * 1024 * 1024;

/** The words a player sees for a ruleset (the base game data a package works with). */
export const RULESET_LABEL = {
	starter: 'the free starter content',
	zerohour: 'your Zero Hour files',
};

export const rulesetLabel = (r) => RULESET_LABEL[r] || String(r);

export class PackageError extends Error {}

// ---------------------------------------------------------------------------------------------
// Reading a package (ZIP) with slices
// ---------------------------------------------------------------------------------------------

async function readSlice(file, start, end) {
	return new Uint8Array(await file.slice(start, end).arrayBuffer());
}

async function inflateRaw(bytes) {
	if (typeof DecompressionStream !== 'function') throw new PackageError('This browser cannot unpack compressed files (DecompressionStream is missing).');
	const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
	return new Uint8Array(await new Response(stream).arrayBuffer());
}

/** The entries of a ZIP: [{ name, method, flags, csize, usize, offset }] from its central directory. */
export async function readZipDirectory(file) {
	const size = file.size;
	if (size < 22) throw new PackageError('Not a package: the file is too small to be a ZIP archive.');
	const tailStart = Math.max(0, size - (22 + 65535));
	const tail = await readSlice(file, tailStart, size);
	let eocd = -1;
	for (let i = tail.length - 22; i >= 0; i--) {
		if (tail[i] === 0x50 && tail[i + 1] === 0x4b && tail[i + 2] === 0x05 && tail[i + 3] === 0x06) { eocd = i; break; }
	}
	if (eocd < 0) throw new PackageError('Not a package: this is not a ZIP archive (no end-of-archive record).');
	const view = new DataView(tail.buffer, tail.byteOffset, tail.byteLength);
	const disk = view.getUint16(eocd + 4, true);
	const count = view.getUint16(eocd + 10, true);
	const cdSize = view.getUint32(eocd + 12, true);
	const cdOffset = view.getUint32(eocd + 16, true);
	if (disk !== 0 || view.getUint16(eocd + 6, true) !== 0) throw new PackageError('Not a package: split ZIP archives are not supported.');
	if (count === 0xffff || cdSize === 0xffffffff || cdOffset === 0xffffffff) throw new PackageError('Not a package: it uses ZIP64, which the format does not allow.');
	if (cdOffset + cdSize > size || cdSize > CENTRAL_LIMIT) throw new PackageError('Not a package: the ZIP directory is damaged.');
	const cd = await readSlice(file, cdOffset, cdOffset + cdSize);
	const dv = new DataView(cd.buffer, cd.byteOffset, cd.byteLength);
	const decoder = new TextDecoder('utf-8');
	const entries = [];
	let p = 0;
	for (let n = 0; n < count; n++) {
		if (p + 46 > cd.length || dv.getUint32(p, true) !== 0x02014b50) throw new PackageError('Not a package: the ZIP directory is damaged.');
		const flags = dv.getUint16(p + 8, true);
		const method = dv.getUint16(p + 10, true);
		const csize = dv.getUint32(p + 20, true);
		const usize = dv.getUint32(p + 24, true);
		const nameLen = dv.getUint16(p + 28, true);
		const extraLen = dv.getUint16(p + 30, true);
		const commentLen = dv.getUint16(p + 32, true);
		const offset = dv.getUint32(p + 42, true);
		if (p + 46 + nameLen > cd.length) throw new PackageError('Not a package: the ZIP directory is damaged.');
		const name = decoder.decode(cd.subarray(p + 46, p + 46 + nameLen));
		entries.push({ name, flags, method, csize, usize, offset });
		p += 46 + nameLen + extraLen + commentLen;
	}
	return entries;
}

/** The bytes of one entry (stored or deflated), at most `limit` uncompressed. */
export async function readEntry(file, entry, limit) {
	if (entry.flags & 1) throw new PackageError('Not a package: ' + entry.name + ' is encrypted.');
	if (entry.method !== 0 && entry.method !== 8) throw new PackageError('Not a package: ' + entry.name + ' uses compression method ' + entry.method + ' (only stored and deflate are allowed).');
	if (entry.usize > limit || entry.csize > limit) throw new PackageError(entry.name + ' is too big (' + entry.usize + ' bytes).');
	if (entry.offset + 30 > file.size) throw new PackageError('Not a package: the ZIP directory is damaged.');
	const local = await readSlice(file, entry.offset, entry.offset + 30);
	const dv = new DataView(local.buffer, local.byteOffset, local.byteLength);
	if (dv.getUint32(0, true) !== 0x04034b50) throw new PackageError('Not a package: the ZIP directory is damaged.');
	const start = entry.offset + 30 + dv.getUint16(26, true) + dv.getUint16(28, true);
	if (start + entry.csize > file.size) throw new PackageError('Not a package: the ZIP directory is damaged.');
	const data = await readSlice(file, start, start + entry.csize);
	const out = entry.method === 0 ? data : await inflateRaw(data);
	if (out.length !== entry.usize) throw new PackageError('Not a package: ' + entry.name + ' is damaged (wrong size).');
	return out;
}

// ---------------------------------------------------------------------------------------------
// The manifest
// ---------------------------------------------------------------------------------------------

/** null when the manifest is usable, else the reason (in words for the player). */
export function validateManifest(m) {
	if (!m || typeof m !== 'object' || Array.isArray(m)) return 'manifest.json is not a JSON object.';
	if (typeof m.format !== 'number' || !Number.isInteger(m.format) || m.format < 1) return 'manifest.json has no valid "format" number.';
	if (m.format > 1) return 'Made for a newer version of the package format (format ' + m.format + '); this launcher understands format 1.';
	if (typeof m.id !== 'string' || !ID_RE.test(m.id)) return 'The package id is not valid (lower case letters, digits, "." and "-", 3 to 64 characters).';
	if (typeof m.tag !== 'string' || !TAG_RE.test(m.tag)) return 'The package tag is not valid (2 to 6 capital letters or digits, starting with a letter).';
	if (typeof m.name !== 'string' || !m.name.trim()) return 'The package has no name.';
	if (typeof m.version !== 'string' || !m.version.trim()) return 'The package has no version.';
	if (!Array.isArray(m.requires) || m.requires.some((r) => typeof r !== 'string')) return 'manifest.json has no valid "requires" list.';
	if (!Array.isArray(m.factions) || m.factions.length < 1) return 'The package lists no factions.';
	if (m.factions.length > 8) return 'The package lists more than 8 factions.';
	for (const f of m.factions) {
		if (!f || typeof f !== 'object') return 'A faction entry is not valid.';
		if (typeof f.displayName !== 'string' || f.displayName.length < 1 || f.displayName.length > 64) return 'A faction has no valid display name.';
		for (const key of ['playerTemplate', 'side']) {
			if (typeof f[key] !== 'string' || !IDENT_RE.test(f[key]) || !f[key].startsWith(m.tag + '_')) return 'A faction ' + key + ' does not start with the package tag (' + m.tag + '_).';
		}
		if (f.ai !== undefined && typeof f.ai !== 'boolean') return 'A faction has an invalid "ai" value.';
	}
	return null;
}

/** Whether a manifest works with a ruleset ('starter' | 'zerohour' | null when no game data is chosen yet). */
export function availability(manifest, ruleset) {
	const requires = manifest.requires || [];
	if (requires.length === 0) return { ok: true, needs: [] };
	if (ruleset === null) return { ok: false, needs: requires, unknown: true };
	return { ok: requires.includes(ruleset), needs: requires };
}

// ---------------------------------------------------------------------------------------------
// One package
// ---------------------------------------------------------------------------------------------

const decodeText = (bytes) => {
	const text = new TextDecoder('utf-8').decode(bytes);
	return text.length > TEXT_SHOWN ? text.slice(0, TEXT_SHOWN) + '\n…' : text;
};

/**
 * Reads the manifest (and a LICENSE / README when there is one) of a package file without reading the rest.
 * Resolves to { ok: true, manifest, license, readme } or { ok: false, error } (never rejects).
 */
export async function readPackage(file) {
	try {
		const entries = await readZipDirectory(file);
		const root = (re) => entries.find((e) => re.test(e.name.toLowerCase()) && !e.name.endsWith('/'));
		const manifestEntry = root(/^manifest\.json$/);
		if (!manifestEntry) return { ok: false, error: 'Not a package: it has no manifest.json.' };
		let manifest;
		try {
			const bytes = await readEntry(file, manifestEntry, MANIFEST_LIMIT);
			manifest = JSON.parse(new TextDecoder('utf-8', { fatal: true }).decode(bytes).replace(/^﻿/, ''));
		} catch (e) {
			if (e instanceof PackageError) return { ok: false, error: e.message };
			return { ok: false, error: 'manifest.json is not valid JSON (' + (e && e.message || e) + ').' };
		}
		const reason = validateManifest(manifest);
		if (reason) return { ok: false, error: reason, manifest: null };
		const text = async (entry) => {
			if (!entry) return '';
			try { return decodeText(await readEntry(file, entry, TEXT_LIMIT)); } catch (e) { return ''; }
		};
		return {
			ok: true,
			manifest,
			license: await text(root(/^(license|licence|copying)[^/]*$/)),
			readme: await text(root(/^readme[^/]*$/)),
		};
	} catch (e) {
		return { ok: false, error: e instanceof PackageError ? e.message : 'Cannot read this file (' + (e && e.message || e) + ').' };
	}
}

// ---------------------------------------------------------------------------------------------
// A folder
// ---------------------------------------------------------------------------------------------

const isPackageName = (name) => /\.zharmy$/i.test(name);

/** The *.zharmy files of a directory handle, top level and up to two levels of sub folders: [{ segments, getFile }]. */
export async function listPackageFiles(dir, { maxDepth = 3 } = {}) {
	const found = [];
	async function walk(handle, prefix, depth) {
		const subs = [];
		for await (const entry of handle.values()) {
			if (entry.kind === 'file') {
				if (isPackageName(entry.name) && found.length < MAX_PACKAGES + 1) found.push({ segments: prefix.concat(entry.name), getFile: () => entry.getFile(), handle: entry });
			} else if (depth < maxDepth && !entry.name.startsWith('.')) {
				subs.push(entry);
			}
		}
		for (const sub of subs) await walk(sub, prefix.concat(sub.name), depth + 1);
	}
	await walk(dir, [], 1);
	return found;
}

/** The same from an <input webkitdirectory> pick (no handles: nothing to remember). */
export function listPackageFilesFromFileList(fileList) {
	const found = [];
	for (const file of fileList) {
		if (!isPackageName(file.name)) continue;
		const parts = (file.webkitRelativePath || file.name).split('/');
		if (parts.length > 1) parts.shift();   // the picked folder itself
		if (parts.length > 3) continue;
		found.push({ segments: parts, getFile: async () => file });
	}
	return found;
}

const SAFE_NAME = /^[A-Za-z0-9._()+-]+$/;

/**
 * The path each package gets in the game's file system, below /armies: the file name when it is plain (letters, digits and
 * . _ ( ) + -), else a plain version of it; sub folders are kept. Names are unique ignoring case (the game's file system
 * ignores case). Returns an array of the same length as `paths` (arrays of segments).
 */
export function servedNames(paths) {
	const used = new Set();
	return paths.map((segments) => {
		const clean = segments.map((s) => (SAFE_NAME.test(s) && s !== '.' && s !== '..' ? s : s.replace(/[^A-Za-z0-9._()+-]+/g, '_').replace(/^\.+$/, '_') || '_'));
		let name = clean.join('/');
		let n = 1;
		while (used.has(name.toLowerCase())) {
			n++;
			const last = clean[clean.length - 1];
			const dot = last.lastIndexOf('.');
			const alt = dot > 0 ? last.slice(0, dot) + '-' + n + last.slice(dot) : last + '-' + n;
			name = clean.slice(0, -1).concat(alt).join('/');
		}
		used.add(name.toLowerCase());
		return name;
	});
}

/**
 * Reads every package of a listing. items: [{ segments, getFile }]. Resolves to the package records, sorted by path:
 *   { path, served, file, size, ok, error, manifest, license, readme }
 * A record that repeats the id or the tag of an earlier one (in path order) is marked not ok.
 */
export async function scanPackages(items, { onProgress = () => {}, concurrency = 4 } = {}) {
	const sorted = items.slice().sort((a, b) => a.segments.join('/').toLowerCase().localeCompare(b.segments.join('/').toLowerCase()));
	const truncated = sorted.length > MAX_PACKAGES;
	if (truncated) sorted.length = MAX_PACKAGES;
	const served = servedNames(sorted.map((i) => i.segments));
	const records = new Array(sorted.length);
	let next = 0;
	let done = 0;
	const worker = async () => {
		while (next < sorted.length) {
			const i = next++;
			const item = sorted[i];
			const record = { path: item.segments.join('/'), served: served[i], handle: item.handle || null, getFile: item.getFile, file: null, size: 0, ok: false, error: '' };
			try {
				record.file = await item.getFile();
				record.size = record.file.size;
				Object.assign(record, await readPackage(record.file));
			} catch (e) {
				record.ok = false;
				record.error = 'Cannot read this file (' + (e && e.message || e) + ').';
			}
			records[i] = record;
			onProgress(++done, sorted.length);
		}
	};
	await Promise.all(Array.from({ length: Math.min(concurrency, sorted.length) }, worker));
	const ids = new Map();
	const tags = new Map();
	for (const r of records) {
		if (!r.ok) continue;
		const sameId = ids.get(r.manifest.id);
		const sameTag = tags.get(r.manifest.tag.toUpperCase());
		if (sameId) { r.ok = false; r.error = 'The same package (id ' + r.manifest.id + ') is already listed from ' + sameId.path + '.'; continue; }
		if (sameTag) { r.ok = false; r.error = 'Uses the tag ' + r.manifest.tag + ', like ' + sameTag.manifest.name + ' (' + sameTag.path + '). Two packages cannot be used together with the same tag.'; continue; }
		ids.set(r.manifest.id, r);
		tags.set(r.manifest.tag.toUpperCase(), r);
	}
	records.truncated = truncated;
	return records;
}

// ---------------------------------------------------------------------------------------------
// What the player ticked (remembered per package id, in this browser)
// ---------------------------------------------------------------------------------------------

const CHECKED_KEY = 'zh-armies-checked';

export function loadChecked() {
	try {
		const list = JSON.parse(localStorage.getItem(CHECKED_KEY) || '[]');
		return new Set(Array.isArray(list) ? list.filter((s) => typeof s === 'string') : []);
	} catch (e) {
		return new Set();
	}
}

export function saveChecked(set) {
	try { localStorage.setItem(CHECKED_KEY, JSON.stringify([...set].sort())); } catch (e) { /* not remembered: private window */ }
}

// ---------------------------------------------------------------------------------------------
// The engine's answer
// ---------------------------------------------------------------------------------------------

/** A log line `ZHARMY: <id> loaded|skipped|failed[: <reason>]` -> { id, status, reason } or null. */
export function parseArmyLogLine(text) {
	const m = /ZHARMY:\s+(\S+)\s+(loaded|skipped|failed)(?::\s*(.*))?\s*$/.exec(String(text));
	return m ? { id: m[1], status: m[2], reason: (m[3] || '').trim() } : null;
}
