// "Read in place" mode, part 3 of 3: the page side. Lets the game read the player's Zero Hour (and
// Generals) files straight from the folder they picked, without copying ~2 GB into the browser's
// Origin Private File System. See INTEGRATION.md next to this file.
//
// What this module does
//   * registers the import mode 'direct' with importer.js: runImport(plan) with plan.mode = 'direct'
//     gets a File object for each planned file (a reference to the file on disk, nothing is read),
//     remembers them and writes the usual OPFS manifest (mode: 'direct') so getStatus() sees the target;
//   * keeps the picked folder handles in IndexedDB (rememberFolder / recallFolders), so a later visit only
//     needs a permission grant (requestAccess, from a click; Chrome may remember it, then queryAccess says
//     'granted' right away);
//   * attachCurrent(Module) makes the game read those files: call it before the game script is added.
//
// The same pieces are also available without the launcher's plan flow: pickFolder + openInstall.
//
// Deployed next to importer.js (the build copies both next to z_generals.html).

import {
	TARGETS, detectInstall, subSource, planImport, sourceFromFileList, registerImportMode, writeManifest,
} from './importer.js';

export const DIRECT_PAYLOAD_VERSION = 1;

const DB_NAME = 'zh-direct-source';
const STORE = 'folders';
const KEY = 'saved';
const ARMIES_KEY = 'armies';   // the armies folder (army packages), kept apart from the game folders

/** An error with a code the launcher can show text for. */
export class DirectError extends Error {
	constructor(code, message) {
		super(message);
		this.name = 'DirectError';
		this.code = code; // 'unsupported' | 'cancelled' | 'permission' | 'no-game' | 'no-folders' | 'read' | 'not-open'
	}
}

/** Whether this browser can read in place with a folder that is remembered: showDirectoryPicker (Chrome) and IndexedDB. */
export function isSupported() {
	return typeof window.showDirectoryPicker === 'function' && typeof indexedDB !== 'undefined';
}

// ---------------------------------------------------------------------------------------------
// Remembering the folder handles (IndexedDB)
// ---------------------------------------------------------------------------------------------

function openDb() {
	return new Promise((resolve, reject) => {
		const request = indexedDB.open(DB_NAME, 1);
		request.onupgradeneeded = () => request.result.createObjectStore(STORE);
		request.onsuccess = () => resolve(request.result);
		request.onerror = () => reject(request.error);
	});
}

async function dbRun(mode, fn) {
	const db = await openDb();
	try {
		return await new Promise((resolve, reject) => {
			const tx = db.transaction(STORE, mode);
			const request = fn(tx.objectStore(STORE));
			tx.oncomplete = () => resolve(request ? request.result : undefined);
			tx.onerror = tx.onabort = () => reject(tx.error);
		});
	} finally {
		db.close();
	}
}

/** Remembers the folders [{ name, handle, role? }] (one or two) for later visits; replaces what was saved. Throws when the browser will not store them (private window). */
export async function saveFolders(folders) {
	await dbRun('readwrite', (store) => store.put(folders.map((f) => ({ name: f.name, handle: f.handle, role: f.role || '' })), KEY));
}

/** The remembered folders, [{ name, handle, role }], or [] (nothing saved, or no IndexedDB). */
export async function loadFolders() {
	try {
		const saved = await dbRun('readonly', (store) => store.get(KEY));
		return Array.isArray(saved) ? saved.filter((f) => f && f.handle) : [];
	} catch (e) {
		return [];
	}
}

/** Remembers the folder picked for a role ('game': the Zero Hour folder or the one above it; 'generals': a separate pick for the original game). */
export async function rememberFolder(role, handle) {
	const others = (await loadFolders()).filter((f) => f.role !== role);
	if (handle) others.push({ name: handle.name, handle, role });
	await saveFolders(others);
}

/** Forgets the remembered folders. Does not touch the files. */
export async function forgetFolders() {
	try {
		await dbRun('readwrite', (store) => store.delete(KEY));
	} catch (e) { /* nothing to forget */ }
}

/** Remembers the armies folder ({ name, handle }) for later visits. Throws when the browser will not store it. */
export async function rememberArmiesFolder(handle) {
	await dbRun('readwrite', (store) => store.put({ name: handle.name, handle }, ARMIES_KEY));
}

/** The remembered armies folder, { name, handle }, or null. */
export async function loadArmiesFolder() {
	try {
		const saved = await dbRun('readonly', (store) => store.get(ARMIES_KEY));
		return saved && saved.handle ? saved : null;
	} catch (e) {
		return null;
	}
}

/** Forgets the remembered armies folder. Does not touch the files. */
export async function forgetArmiesFolder() {
	try {
		await dbRun('readwrite', (store) => store.delete(ARMIES_KEY));
	} catch (e) { /* nothing to forget */ }
}

// ---------------------------------------------------------------------------------------------
// Picking and permission
// ---------------------------------------------------------------------------------------------

/**
 * Shows the folder picker. Must be called from a click. Resolves to { name, handle }.
 * Throws DirectError('cancelled') when the player dismisses the picker.
 */
export async function pickFolder({ id = 'zh-install' } = {}) {
	if (typeof window.showDirectoryPicker !== 'function') {
		throw new DirectError('unsupported', 'This browser cannot open a folder in place. Use Chrome, or copy the game into browser storage.');
	}
	try {
		const handle = await window.showDirectoryPicker({ id, mode: 'read' });
		return { name: handle.name, handle };
	} catch (e) {
		if (e && e.name === 'AbortError') throw new DirectError('cancelled', 'No folder was chosen.');
		throw e;
	}
}

/**
 * The read permission of remembered folders without asking: 'granted' (nothing to click), 'prompt'
 * (the player has to click: requestAccess) or 'denied'. 'none' for an empty list.
 */
export async function queryAccess(folders) {
	if (!folders || folders.length === 0) return 'none';
	let result = 'granted';
	for (const folder of folders) {
		let state = 'prompt';
		try {
			state = await folder.handle.queryPermission({ mode: 'read' });
		} catch (e) { /* stale handle: ask again */ }
		if (state === 'denied') return 'denied';
		if (state !== 'granted') result = 'prompt';
	}
	return result;
}

/**
 * Asks for read permission for remembered folders. Call it from a click, as the first thing the handler
 * does (it needs the user gesture); all folders are asked in the same turn. Resolves to 'granted' or 'denied'.
 */
export async function requestAccess(folders) {
	const states = await Promise.all(folders.map(async (folder) => {
		try {
			return await folder.handle.requestPermission({ mode: 'read' });
		} catch (e) {
			return 'denied';
		}
	}));
	return states.every((s) => s === 'granted') ? 'granted' : 'denied';
}

// ---------------------------------------------------------------------------------------------
// The files that are open
// ---------------------------------------------------------------------------------------------

// targetKey ('game' | 'generals') -> { entries: [{ path: 'game/data/x.ini', file }], files, bytes, folder }
const open = new Map();

// Runs fn over items, at most `limit` at a time.
async function pool(items, limit, fn) {
	let next = 0;
	const workers = [];
	for (let w = 0; w < Math.min(limit, items.length); w++) {
		workers.push((async () => {
			while (next < items.length) {
				const i = next++;
				await fn(items[i], i);
			}
		})());
	}
	await Promise.all(workers);
}

// A File object for each planned file (planImport output). A file is only a reference to the file on disk.
async function resolveFiles(plan, onProgress, signal) {
	const root = TARGETS[plan.targetKey].dir;
	const entries = new Array(plan.files.length);
	let opened = 0;
	let bytes = 0;
	await pool(plan.files, 8, async (item, i) => {
		if (signal && signal.aborted) throw new DOMException('Cancelled', 'AbortError');
		let file;
		try {
			file = item.file || await item.getFile();
		} catch (e) {
			throw new DirectError('read', 'Cannot open ' + item.segments.join('/') + ' (' + (e && e.message || e) + ').');
		}
		entries[i] = { path: root + '/' + item.segments.join('/'), file };
		bytes += file.size;
		opened++;
		if (opened % 50 === 0 || opened === plan.files.length) {
			onProgress({ done: opened, total: plan.files.length, bytesDone: bytes, bytesTotal: plan.bytes, path: item.segments.join('/') });
		}
	});
	return { entries, files: entries.length, bytes, folder: plan.sourceName };
}

// The import mode of importer.js: runImport(plan) with plan.mode = 'direct' does not copy anything.
registerImportMode('direct', async (plan, { onProgress = () => {}, signal = null } = {}) => {
	open.delete(plan.targetKey);
	const result = await resolveFiles(plan, onProgress, signal);
	open.set(plan.targetKey, result);
	const manifest = await writeManifest(plan, { mode: 'direct' });
	onProgress({ done: result.files, total: result.files, bytesDone: result.bytes, bytesTotal: result.bytes, path: '' });
	return { copied: 0, manifest };
});

/** Whether the files of a target ('game' or 'generals') are open (this visit), so that attachCurrent() can serve them. */
export function isOpen(targetKey) {
	return open.has(targetKey);
}

/** { files, bytes, folder } of an open target, or null. */
export function describeOpen(targetKey) {
	const o = open.get(targetKey);
	return o ? { files: o.files, bytes: o.bytes, folder: o.folder } : null;
}

/**
 * The files of an open target as [{ path: 'data/ini/x.ini' (below the target's folder), file }], or null when it is not open.
 * Each file is a reference to the file on disk; nothing is read. The army importer hands them to its converter.
 */
export function openFiles(targetKey) {
	const o = open.get(targetKey);
	if (!o) return null;
	const prefix = TARGETS[targetKey].dir + '/';
	return o.entries.map((e) => ({ path: e.path.startsWith(prefix) ? e.path.slice(prefix.length) : e.path, file: e.file }));
}

/** Closes the open files of a target (all with no argument). */
export function closeOpen(targetKey) {
	if (targetKey) open.delete(targetKey); else open.clear();
}

// ---------------------------------------------------------------------------------------------
// Army packages: served at /armies next to the game files (or on their own)
// ---------------------------------------------------------------------------------------------

// [{ path: 'armies/<name>', file }]: the packages the player checked. They are read in place like the game files, whichever
// way the game data is provided (read in place, copied into OPFS, or the starter content): the engine mounts them at /armies
// (WebStorage.cpp). Each entry is a reference to a file on disk.
let armyEntries = [];

/** Sets the packages to serve at /armies: [{ name: 'file.zharmy' (below /armies, '/' for sub folders), file }]. [] for none. */
export function setArmyFiles(list) {
	armyEntries = (list || []).map((a) => ({ path: 'armies/' + a.name, file: a.file }));
}

/** How many package files are set to be served. */
export function armyFileCount() {
	return armyEntries.length;
}

/**
 * Prepares the page's Module (before the game script is added) to serve the packages of setArmyFiles() when the game data
 * does not come from attachCurrent() (the starter content, or a copy in OPFS). With the game read in place, attachCurrent()
 * serves them, too. Returns the number of files, 0 when there is nothing to do.
 */
export function attachArmies(Module, options = {}) {
	if (armyEntries.length === 0) return 0;
	const payload = buildPayload({ ...options, only: 'armies' });
	addPreRun(Module, payload);
	return armyEntries.length;
}

function addPreRun(Module, payload) {
	Module.preRun = [].concat(Module.preRun || [], [() => {
		if (typeof Module.zhDirectAttach !== 'function') {
			throw new DirectError('unsupported', 'This build of the game cannot read files in place (no zhdirect support in the module).');
		}
		Module.zhDirectAttach(payload);
	}]);
}

function buildPayload(options = {}) {
	const entries = [];
	if (options.only !== 'armies') {
		for (const key of ['game', 'generals']) {
			const o = open.get(key);
			if (o) for (const e of o.entries) entries.push({ path: e.path, file: e.file });
		}
	}
	for (const e of armyEntries) entries.push({ path: e.path, file: e.file });
	const tuning = {};
	for (const name of ['cacheBytes', 'threadCacheBytes', 'blockSize', 'maxSegment', 'directThreshold']) {
		if (typeof options[name] === 'number' && options[name] > 0) tuning[name] = options[name];
	}
	return { version: DIRECT_PAYLOAD_VERSION, entries, options: tuning };
}

/**
 * Prepares the page's Module (window.Module, before the game script is added) to read the open files:
 * adds a preRun step that hands the File objects to the engine's threads, and the -webdirect argument that
 * makes the engine stop with a clear message if they did not arrive (instead of looking in OPFS).
 * options: the read cache tuning (cacheBytes, threadCacheBytes, blockSize, maxSegment, directThreshold; see
 * library_zhdirect.js; the defaults suit the game).
 */
export function attachCurrent(Module, options = {}) {
	if (!open.has('game')) throw new DirectError('not-open', 'The game folder is not open. Choose it (or allow access to it) first.');
	const payload = buildPayload(options);
	addPreRun(Module, payload);
	const args = Array.from(Module.arguments || []);
	if (!args.includes('-webdirect')) args.push('-webdirect');
	Module.arguments = args;
	return Module;
}

// ---------------------------------------------------------------------------------------------
// Without the launcher's plan flow: folders in, install out
// ---------------------------------------------------------------------------------------------

// A source in the importer's sense from a directory handle without opening any file: only the names are listed.
async function sourceFromHandleLazy(rootHandle, onFound) {
	const files = [];
	async function walk(dir, prefix) {
		try {
			for await (const entry of dir.values()) {
				if (entry.kind === 'directory') {
					await walk(entry, prefix.concat(entry.name));
				} else {
					files.push({ segments: prefix.concat(entry.name), size: 0, getFile: () => entry.getFile() });
					if (onFound && files.length % 200 === 0) onFound(files.length);
				}
			}
		} catch (e) {
			// An unreadable directory is skipped, like a file the game does not need.
		}
	}
	await walk(rootHandle, []);
	return { name: rootHandle.name, files, handle: rootHandle };
}

async function toSources(input, onFound) {
	if (!input) return [];
	if (typeof FileList !== 'undefined' && input instanceof FileList) return [sourceFromFileList(input)];
	const list = Array.isArray(input) ? input : [input];
	const sources = [];
	for (const item of list) {
		if (item && item.handle && item.handle.kind === 'directory') sources.push(await sourceFromHandleLazy(item.handle, onFound));
		else if (item && item.kind === 'directory') sources.push(await sourceFromHandleLazy(item, onFound));
		else if (item && Array.isArray(item.files)) sources.push(item);
		else if (typeof FileList !== 'undefined' && item instanceof FileList) sources.push(sourceFromFileList(item));
		else throw new TypeError('openInstall: unsupported folder ' + item);
	}
	return sources;
}

/**
 * Finds the game in the folders and opens it for reading in place (the plan flow of the launcher does the
 * same with its own steps; this is the one call version, used by the tests and as a reference).
 *
 * input:  [{ name, handle }] from pickFolder()/loadFolders() (one or two folders: Zero Hour and the original
 *         game may be in one or in two), a FileList from <input webkitdirectory>, or importer.js sources.
 * options: onProgress({ phase: 'scan'|'open', done, total }), signal, includeVideos, and the read cache tuning
 *          of attachCurrent().
 * Resolves to { game: {files, bytes, folder}, generals: {...}|null, warnings: [string], attach(Module) }.
 * Throws DirectError('no-game') when no folder holds Zero Hour.
 */
export async function openInstall(input, options = {}) {
	const { onProgress = () => {}, signal = null } = options;
	onProgress({ phase: 'scan', done: 0, total: 0 });
	const sources = await toSources(input, (n) => onProgress({ phase: 'scan', done: n, total: 0 }));
	if (sources.length === 0) throw new DirectError('no-folders', 'No folder was chosen.');

	let game = null;
	let generals = null;
	for (const source of sources) {
		const found = detectInstall(source);
		if (!game && found.game) game = { source, prefix: found.game };
		if (!generals && found.base) generals = { source, prefix: found.base };
	}
	if (!game) {
		const names = sources.map((s) => s.name || 'the folder').join(', ');
		throw new DirectError('no-game', 'Zero Hour was not found in ' + names + ' (looked for INIZH.big, W3DZH.big, TexturesZH.big or TerrainZH.big). Choose the folder that contains them, or a folder above it.');
	}

	const warnings = [];
	const plans = [{ key: 'game', source: game.source, prefix: game.prefix }];
	if (generals) plans.push({ key: 'generals', source: generals.source, prefix: generals.prefix });
	else warnings.push('The original Generals (the base game Zero Hour builds on) was not found; the game may not start without it. Choose a folder that holds both games, or add the Generals folder.');

	const results = {};
	for (const p of plans) {
		const plan = planImport(subSource(p.source, p.prefix), p.key, { includeVideos: !!options.includeVideos });
		plan.mode = 'direct';
		onProgress({ phase: 'open', done: 0, total: plan.files.length });
		results[p.key] = await resolveFiles(plan, (q) => onProgress({ phase: 'open', done: q.done, total: q.total }), signal);
		if (p.key === 'game' && !results.game.entries.some((e) => e.path === 'game/' + TARGETS.game.engineExe)) {
			warnings.push('The Zero Hour folder has no ' + TARGETS.game.engineExe + ' (or generals.exe); the engine may stop at start up.');
		}
	}
	open.clear();
	for (const key of Object.keys(results)) open.set(key, results[key]);

	const brief = (r) => r && { files: r.files, bytes: r.bytes, folder: r.folder };
	return {
		kind: 'direct',
		game: brief(results.game),
		generals: brief(results.generals) || null,
		warnings,
		attach: (Module) => attachCurrent(Module, options),
	};
}

/**
 * "Play from a folder" in one function, for a click handler: with `folders` (from loadFolders()) their
 * permission is requested again, without them the picker is shown and the choice remembered (save: false
 * to not). Call it directly from the click handler, before any other await.
 */
export async function prepareDirect({ folders = null, save = true, ...options } = {}) {
	if (!folders || folders.length === 0) {
		folders = [await pickFolder()];
		if (save) {
			try { await rememberFolder('game', folders[0].handle); } catch (e) { /* not remembered: private window */ }
		}
	} else if (await requestAccess(folders) !== 'granted') {
		throw new DirectError('permission', 'Permission to read the game folder was not given.');
	}
	return openInstall(folders, options);
}
