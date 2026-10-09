// Importing armies from a mod inside the browser: the logic behind "Import armies from a mod…" of the launcher.
//
//   * which files of a picked folder the converter needs, and how they are collected without reading them;
//   * what a picked folder is (a game folder with a mod installed in it, or a mod's own folder);
//   * ArmyImporter: the Web Worker that runs the converter (tools/zharmy, Python, under Pyodide) on those files.
//
// The page that shows all of this is armyimport-ui.js; the packages end up in armylibrary.js.
//
// Nothing here reads a whole file. A File from the player's disk is only a reference; the worker mounts the references
// (WORKERFS) and Python reads byte ranges of them while it converts.

import { detectInstall } from './importer.js';

// ---------------------------------------------------------------------------------------------
// Collecting the files of a folder
// ---------------------------------------------------------------------------------------------

// What the converter can use. Everything else (movies, programs, maps, pictures of the manual...) is left out, so a game
// folder of 8 GB gives the worker a few hundred references.
const SKIP_DIRECTORIES = new Set(['movies', 'redist', 'support', 'directx', '__macosx', 'mss', 'userdata', 'maps']);
const WANTED_EXTENSIONS = new Set(['big', 'ini', 'inc', 'str', 'csf', 'scb', 'w3d', 'dds', 'tga', 'wav', 'mp3', 'ogg']);

/** Whether the converter can use a file (segments: the path below the picked folder). */
export function wantFile(segments) {
	const name = segments[segments.length - 1].toLowerCase();
	const dot = name.lastIndexOf('.');
	if (dot < 0 || !WANTED_EXTENSIONS.has(name.slice(dot + 1))) return false;
	for (const dir of segments.slice(0, -1)) {
		const d = dir.toLowerCase();
		if (SKIP_DIRECTORIES.has(d) || d.startsWith('.')) return false;
	}
	return true;
}

/**
 * The wanted files below a directory handle: { name, handle, files: [{ segments, file, size }] }. Only the folder tree is
 * listed and a File reference is taken for each wanted file (nothing is read).
 */
export async function collectFromHandle(root, { onProgress = () => {}, signal = null } = {}) {
	const files = [];
	let seen = 0;
	async function walk(dir, prefix) {
		const subs = [];
		for await (const entry of dir.values()) {
			if (signal && signal.aborted) throw new DOMException('Cancelled', 'AbortError');
			if (entry.kind === 'directory') {
				subs.push(entry);
				continue;
			}
			const segments = prefix.concat(entry.name);
			if (!wantFile(segments)) continue;
			try {
				const file = await entry.getFile();
				files.push({ segments, file, size: file.size });
			} catch (e) {
				// a locked or vanished file is left out, like a file the converter does not need
			}
			if (++seen % 50 === 0) onProgress(seen);
		}
		for (const sub of subs) {
			const d = sub.name.toLowerCase();
			if (SKIP_DIRECTORIES.has(d) || d.startsWith('.')) continue;
			await walk(sub, prefix.concat(sub.name));
		}
	}
	await walk(root, []);
	return { name: root.name, handle: root, files };
}

/** The same from an <input webkitdirectory> pick (no handle). */
export function collectFromFileList(fileList) {
	const files = [];
	let name = '';
	for (const file of fileList) {
		const parts = (file.webkitRelativePath || file.name).split('/');
		if (parts.length > 1) {
			name = parts[0];
			parts.shift();
		}
		if (wantFile(parts)) files.push({ segments: parts, file, size: file.size });
	}
	return { name, handle: null, files };
}

/**
 * What the picked folder is: a folder that holds Zero Hour (somewhere below it) or something else. Returns
 * { installPrefix: segments of the Zero Hour folder below the picked one | null, files: all the files,
 *   archives: [{ path, size }] }. The whole picked folder is given to the converter, which finds the installs in it itself
 * (a folder with Zero Hour and Generals side by side is layered like the engine does). Which archives are retail ones is
 * the converter's knowledge (layout.is_retail_archive, the same as --mod-archives auto): ask ArmyImporter.retail().
 */
export function analyse(source) {
	const found = detectInstall({ files: source.files });
	const archives = source.files.filter((f) => /\.big$/i.test(f.segments[f.segments.length - 1]))
		.map((f) => ({ path: f.segments.join('/'), size: f.size }))
		.sort((a, b) => a.path.toLowerCase().localeCompare(b.path.toLowerCase()));
	return { installPrefix: found.game, files: source.files, archives };
}

/** A name for the mod from its archives when it sits in the game folder ("!ModMain.big" -> "ModMain"). */
export function modNameFromArchives(paths) {
	const first = paths.map((p) => p.split('/').pop().replace(/\.big$/i, '').replace(/^[!_\-.\s]+/, '')).find((n) => n);
	return first || 'Mod';
}

// ---------------------------------------------------------------------------------------------
// Names
// ---------------------------------------------------------------------------------------------

export const TAG_RE = /^[A-Z][A-Z0-9]{1,5}$/;
export const ID_RE = /^[a-z0-9][a-z0-9.-]{2,63}$/;

const slug = (text) => String(text).toLowerCase().replace(/[^a-z0-9]+/g, '');

/** The package id for a mod and a faction: "<mod>.<faction>" in lower case letters and digits. */
export function packageId(modName, template) {
	let faction = String(template).replace(/^faction/i, '');
	faction = slug(faction) || 'army';
	const mod = slug(modName).slice(0, 24) || 'mod';
	return (mod + '.' + faction.slice(0, 36)).replace(/^\.+/, '');
}

/** A tag that is not in `taken` (upper case tags): the wish itself, else with a number at the end. */
export function uniqueTag(wish, taken) {
	let tag = String(wish).toUpperCase().replace(/[^A-Z0-9]/g, '').slice(0, 6);
	if (!/^[A-Z]/.test(tag)) tag = ('A' + tag).slice(0, 6);
	if (tag.length < 2) tag += 'X';
	if (!taken.has(tag)) return tag;
	for (let n = 2; n < 1000; n++) {
		const candidate = tag.slice(0, 6 - String(n).length) + n;
		if (!taken.has(candidate)) return candidate;
	}
	return tag;
}

// ---------------------------------------------------------------------------------------------
// The converter in a worker
// ---------------------------------------------------------------------------------------------

/** Whether this copy of the page ships the converter (the build can leave it out: -DRTS_WEB_PYODIDE=OFF). */
export async function converterAvailable(baseUrl = new URL('./', import.meta.url).href) {
	try {
		const [a, b] = await Promise.all([fetch(baseUrl + 'zharmy.zip', { method: 'HEAD' }), fetch(baseUrl + 'pyodide/pyodide.asm.wasm', { method: 'HEAD' })]);
		return a.ok && b.ok;
	} catch (e) {
		return false;
	}
}

export class ImportError extends Error {}

export class ArmyImporter {
	constructor({ baseUrl = new URL('./', import.meta.url).href } = {}) {
		this.baseUrl = baseUrl;
		this.worker = null;
		this.nextId = 1;
		this.pending = new Map();
		this.ready = null;
		this.startedAt = 0;
	}

	_start() {
		if (this.worker) return;
		this.worker = new Worker(new URL('./armyimport-worker.js', import.meta.url));
		this.worker.onmessage = (event) => {
			const m = event.data;
			const p = this.pending.get(m.id);
			if (!p) return;
			if (m.type === 'progress') { if (p.handlers.onProgress) p.handlers.onProgress(m.text); } else if (m.type === 'status') { if (p.handlers.onStatus) p.handlers.onStatus(m.text); } else if (m.type === 'done') {
				this.pending.delete(m.id);
				p.resolve({ result: m.result, files: m.files || [] });
			} else if (m.type === 'error') {
				this.pending.delete(m.id);
				const err = new ImportError(m.message);
				err.detail = m.detail;
				p.reject(err);
			}
		};
		this.worker.onerror = (event) => {
			const err = new ImportError('The converter stopped: ' + (event.message || 'unknown error'));
			for (const p of this.pending.values()) p.reject(err);
			this.pending.clear();
		};
	}

	_call(cmd, payload, handlers = {}) {
		this._start();
		const id = this.nextId++;
		return new Promise((resolve, reject) => {
			this.pending.set(id, { resolve, reject, handlers });
			this.worker.postMessage({ id, cmd, ...payload });
		});
	}

	/** Loads Pyodide and the converter (about 12 MB, cached by the browser). Safe to call more than once. */
	init(handlers) {
		if (!this.ready) {
			this.ready = this._call('init', { base: this.baseUrl }, handlers).then((r) => r.result);
			this.ready.catch(() => { this.ready = null; });
		}
		return this.ready;
	}

	/** For archive paths: which are retail Zero Hour / Generals archives (the converter's own list). Resolves to booleans. */
	async retail(names, handlers) {
		await this.init(handlers);
		return (await this._call('retail', { names }, handlers)).result;
	}

	/** Mounts the folders and reads the mod. Resolves to { ok, factions, warnings, ... } or { ok: false, error }. */
	async open({ mounts, params }, handlers) {
		await this.init(handlers);
		return (await this._call('open', { mounts, params }, handlers)).result;
	}

	/** Converts one army. Resolves to { row, bytes } (bytes: the package, when row.ok). */
	async convert(job, handlers) {
		const { result, files } = await this._call('convert', { job }, handlers);
		return { row: result, bytes: files.length ? files[0].bytes : null };
	}

	async close() {
		if (this.worker && this.ready) {
			try { await this._call('close', {}); } catch (e) { /* the worker is gone */ }
		}
	}

	/** Stops the worker at once (cancel); its memory is returned to the browser. */
	dispose() {
		if (this.worker) this.worker.terminate();
		this.worker = null;
		this.ready = null;
		for (const p of this.pending.values()) p.reject(new ImportError('Cancelled'));
		this.pending.clear();
	}
}

/** Files of an install that was copied into the browser's storage (OPFS folder game/): [{ path, file }]. */
export async function opfsInstallFiles(dirName = 'game') {
	const out = [];
	const root = await (await navigator.storage.getDirectory()).getDirectoryHandle(dirName);
	async function walk(dir, prefix) {
		for await (const [name, handle] of dir.entries()) {
			if (handle.kind === 'directory') await walk(handle, prefix.concat(name));
			else if (wantFile(prefix.concat(name))) out.push({ path: prefix.concat(name).join('/'), file: await handle.getFile() });
		}
	}
	await walk(root, []);
	return out;
}
