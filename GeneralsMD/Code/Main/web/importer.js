// Puts game data into the browser's Origin Private File System (OPFS), where the
// game finds it. Two sources:
//
//   * the player's own Zero Hour / Generals install, copied from a folder they
//     pick (nothing is uploaded anywhere; the game data is never part of the build);
//   * the free starter content: an original placeholder game (not Command &
//     Conquer content), downloaded from the server next to the game, described
//     by starterpack/manifest.json (see Content/StarterPack/build_pack.py).
//
// Each target has a manifest in OPFS ("game.manifest.json") whose "kind" tells
// which of the two put the files there, so the page never mixes them.
//
// Layout in OPFS (the game mounts the same tree, see WebStorage.cpp):
//   game/        Zero Hour install
//   generals/    Generals install (Zero Hour loads the original game's archives too)
//   userdata/    saves, replays, options.ini; written by the game, kept here
//
// All names are stored in lower case: Windows file names are case insensitive
// and the game's file system layer on top of OPFS is, too (WasmFS ignore-case
// backend), which requires lower case names underneath.

export const TARGETS = {
	game: {
		label: 'Zero Hour',
		dir: 'game',
		// Archives that identify the folder; at least one must exist.
		signature: ['inizh.big', 'w3dzh.big', 'textureszh.big', 'terrainzh.big'],
		hint: 'Command & Conquer Generals - Zero Hour',
		// The engine fingerprints one executable at start up (GlobalData::generateExeCRC) and asserts when it is missing.
		// It is only read, never run, and the CRC only matters for network/replay compatibility, so whichever of these the
		// install has is stored as engineExe. (Retail Zero Hour ships generals.exe; the real game binary is game.dat.)
		engineExe: 'generalszh.exe',
		engineExeSources: ['generalszh.exe', 'generals.exe'],
		// At the top level of the folder only archives are used by the engine; Data/ and other sub folders are kept.
		rootFiles: 'big',
	},
	generals: {
		label: 'Generals',
		dir: 'generals',
		signature: ['ini.big', 'w3d.big', 'textures.big', 'terrain.big'],
		hint: 'Command & Conquer Generals',
		// Zero Hour only reads *.big files from the Generals install directory (StdBIGFileSystem::init), nothing else.
		rootFiles: 'big',
		bigOnly: true,
		// Only used for the original game's own maps, which Zero Hour lists next to its own. Skipped to save space.
		optionalBig: ['maps.big'],
	},
};

// What is not needed to run the game in the browser.
// mss: the Miles Sound System codecs (the web build plays audio through Web Audio); userdata: saves and options live in
// the browser's own userdata/ folder, never in an install.
const SKIP_DIRECTORIES = new Set(['movies', 'redist', 'support', 'directx', '__macosx', '.git', 'mss', 'userdata']);
const SKIP_EXTENSIONS = new Set([
	'exe', 'dll', 'ocx', 'sys', 'vxd', 'msi', 'cab', 'pdb', 'lib', 'bat', 'cmd', 'lnk', 'url',
	'bik', 'bk2', 'tmp', 'log', 'dmp', 'ds_store',
]);

const MANIFEST_VERSION = 2;

// Where the free starter content is served from, relative to the page.
export const STARTER = {
	label: 'Free starter content',
	baseUrl: 'starterpack/',
	targetKey: 'game',
};

export function checkEnvironment() {
	const canvas = document.createElement('canvas');
	return {
		crossOriginIsolated: !!self.crossOriginIsolated,
		sharedArrayBuffer: typeof SharedArrayBuffer !== 'undefined',
		opfs: !!(navigator.storage && navigator.storage.getDirectory),
		offscreenCanvas: typeof OffscreenCanvas !== 'undefined' && typeof canvas.transferControlToOffscreen === 'function',
		directoryPicker: typeof window.showDirectoryPicker === 'function',
		secureContext: !!self.isSecureContext,
	};
}

// ---------------------------------------------------------------------------------------------
// Sources: a folder the user picked, as a flat list of files
// ---------------------------------------------------------------------------------------------

// { name, files: [{ segments: ['Data', 'INI', 'x.ini'], size, getFile() }] }

async function walkDirectoryHandle(dirHandle, prefix, out) {
	for await (const entry of dirHandle.values()) {
		if (entry.kind === 'directory') {
			await walkDirectoryHandle(entry, prefix.concat(entry.name), out);
		} else {
			const handle = entry;
			let file;
			try {
				file = await handle.getFile();
			} catch (e) {
				continue; // unreadable (e.g. locked), skip
			}
			// getFile() gives a fresh snapshot each time (the copy reads it when its turn comes); file is the one
			// from the walk, which the read in place mode (webdirect/direct-source.js) keeps.
			out.push({ segments: prefix.concat(entry.name), size: file.size, getFile: () => handle.getFile(), file });
		}
	}
}

// Shows the browser's folder picker (Chrome/Chromium only; other browsers are out of scope). Returns null when
// the API does not exist; use sourceFromFileList with an <input webkitdirectory>.
export async function pickDirectory() {
	if (typeof window.showDirectoryPicker !== 'function') {
		return null;
	}
	const handle = await window.showDirectoryPicker({ id: 'zh-install', mode: 'read' });
	return sourceFromDirectoryHandle(handle);
}

export async function sourceFromDirectoryHandle(handle) {
	const files = [];
	await walkDirectoryHandle(handle, [], files);
	return { name: handle.name, files, handle };
}

export function sourceFromFileList(fileList) {
	const files = [];
	let name = '';
	for (const file of fileList) {
		const parts = (file.webkitRelativePath || file.name).split('/');
		if (parts.length > 1) {
			name = parts[0];
			parts.shift(); // the picked folder itself
		}
		files.push({ segments: parts, size: file.size, getFile: async () => file });
	}
	return { name, files };
}

// Drag and drop of a folder (DataTransferItem.getAsFileSystemHandle).
export async function sourceFromDataTransfer(dataTransfer) {
	for (const item of dataTransfer.items) {
		if (typeof item.getAsFileSystemHandle === 'function') {
			const handle = await item.getAsFileSystemHandle();
			if (handle && handle.kind === 'directory') {
				return sourceFromDirectoryHandle(handle);
			}
		}
	}
	return null;
}

// ---------------------------------------------------------------------------------------------
// Finding the games inside what the user picked
// ---------------------------------------------------------------------------------------------

// The user may pick the Zero Hour folder itself or any folder above it (a Steam, EA app or Ultimate Collection
// library holds both games side by side). Zero Hour is recognised by its own archives (INIZH.big...), the base game
// it builds on by its archives (INI.big...). Returns { game, base }: each the folder as an array of path segments
// below the picked folder ([] for the picked folder itself) or null when it was not found. The shallowest match wins,
// and the base game is never looked for inside the Zero Hour folder.
export function detectInstall(source) {
	const folders = new Map(); // lower case path -> { segments, names }
	for (const file of source.files) {
		const dirs = file.segments.slice(0, -1);
		const key = dirs.map((d) => d.toLowerCase()).join('/');
		let entry = folders.get(key);
		if (!entry) folders.set(key, entry = { segments: dirs, names: new Set(), key });
		entry.names.add(file.segments[file.segments.length - 1].toLowerCase());
	}
	const find = (signature, skip) => {
		let best = null;
		for (const entry of folders.values()) {
			if (!signature.some((n) => entry.names.has(n))) continue;
			if (skip && skip(entry)) continue;
			if (!best || entry.segments.length < best.segments.length) best = entry;
		}
		return best ? best.segments : null;
	};
	const game = find(TARGETS.game.signature);
	const gameKey = game ? game.map((d) => d.toLowerCase()).join('/') : null;
	const inside = (entry) => game && (gameKey === '' || entry.key === gameKey || entry.key.startsWith(gameKey + '/'));
	// The base game is never the Zero Hour folder or below it, unless the user picked a folder that holds only the base game.
	const base = find(TARGETS.generals.signature, (entry) => inside(entry));
	return { game, base };
}

// The part of a picked source that lies below a folder (as returned by detectInstall).
export function subSource(source, prefix) {
	if (!prefix || prefix.length === 0) return source;
	const lower = prefix.map((p) => p.toLowerCase());
	const files = [];
	for (const file of source.files) {
		if (file.segments.length > prefix.length && lower.every((p, i) => file.segments[i].toLowerCase() === p)) {
			files.push({ ...file, segments: file.segments.slice(prefix.length) });
		}
	}
	return { name: prefix[prefix.length - 1], files, handle: source.handle };
}

// ---------------------------------------------------------------------------------------------
// Planning
// ---------------------------------------------------------------------------------------------

// options: includeVideos; includeOptional (base game: also take maps.big); skip: [{ name, size }] files that are already
// in the browser from the other target (same name and size: not copied twice, e.g. Music.big is in both installs).
export function planImport(source, targetKey, options = {}) {
	const target = TARGETS[targetKey];
	const includeVideos = !!options.includeVideos;
	const optional = new Set(options.includeOptional ? [] : (target.optionalBig || []));
	const already = new Set((options.skip || []).map((f) => f.name.toLowerCase() + ':' + f.size));
	const names = new Set(source.files.filter(f => f.segments.length === 1).map(f => f.segments[0].toLowerCase()));
	const looksRight = target.signature.some(n => names.has(n));
	const exeSource = (target.engineExeSources || []).find((n) => names.has(n)) || null;

	const files = [];
	let skipped = 0;
	let skippedBytes = 0;
	for (const file of source.files) {
		const dirs = file.segments.slice(0, -1).map(s => s.toLowerCase());
		const name = file.segments[file.segments.length - 1].toLowerCase();
		const ext = name.includes('.') ? name.slice(name.lastIndexOf('.') + 1) : '';
		const atRoot = dirs.length === 0;
		const skipDir = dirs.some(d => SKIP_DIRECTORIES.has(d) && !(includeVideos && d === 'movies'));
		const isEngineExe = atRoot && name === exeSource;
		let skip = skipDir;
		if (!skip && atRoot && target.rootFiles === 'big' && ext !== 'big' && !isEngineExe) skip = true;
		if (!skip && target.bigOnly && !(atRoot && ext === 'big')) skip = true;
		if (!skip && SKIP_EXTENSIONS.has(ext) && !isEngineExe && !(includeVideos && (ext === 'bik' || ext === 'bk2'))) skip = true;
		if (!skip && atRoot && optional.has(name)) skip = true;
		if (!skip && atRoot && already.has(name + ':' + file.size)) skip = true;
		if (skip) {
			skipped++;
			skippedBytes += file.size;
			continue;
		}
		// The executable the engine reads is always stored under the one name it asks for.
		files.push({ ...file, segments: isEngineExe ? [target.engineExe] : dirs.concat(name) });
	}
	const bytes = files.reduce((sum, f) => sum + f.size, 0);
	// The archives at the top level, remembered in the manifest so that the other target can skip identical ones later.
	const bigs = files.filter((f) => f.segments.length === 1 && f.segments[0].endsWith('.big')).map((f) => ({ name: f.segments[0], size: f.size }));
	return { targetKey, sourceName: source.name, looksRight, files, skipped, skippedBytes, bytes, manifestExtra: { bigs } };
}

// ---------------------------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------------------------

const WORKER_SOURCE = `
self.onmessage = async (event) => {
	const { segments, name, file } = event.data;
	try {
		let dir = await navigator.storage.getDirectory();
		for (const segment of segments) dir = await dir.getDirectoryHandle(segment, { create: true });
		const handle = await dir.getFileHandle(name, { create: true });
		const access = await handle.createSyncAccessHandle();
		access.truncate(0);
		const reader = file.stream().getReader();
		let position = 0;
		for (;;) {
			const { done, value } = await reader.read();
			if (done) break;
			access.write(value, { at: position });
			position += value.length;
			self.postMessage({ progress: value.length });
		}
		access.flush();
		access.close();
		self.postMessage({ done: true });
	} catch (error) {
		self.postMessage({ error: String(error) });
	}
};`;

let writerWorker = null;

function writeWithWorker(segments, name, file, onBytes) {
	if (!writerWorker) {
		writerWorker = new Worker(URL.createObjectURL(new Blob([WORKER_SOURCE], { type: 'text/javascript' })));
	}
	return new Promise((resolve, reject) => {
		writerWorker.onmessage = (event) => {
			const m = event.data;
			if (m.progress) onBytes(m.progress);
			else if (m.error) reject(new Error(m.error));
			else if (m.done) resolve();
		};
		writerWorker.postMessage({ segments, name, file });
	});
}

async function writeWithWritable(fileHandle, file, onBytes) {
	const writable = await fileHandle.createWritable();
	try {
		const reader = file.stream().getReader();
		for (;;) {
			const { done, value } = await reader.read();
			if (done) break;
			await writable.write(value);
			onBytes(value.length);
		}
		await writable.close();
	} catch (error) {
		try { await writable.abort(); } catch (e) { /* ignore */ }
		throw error;
	}
}

async function getDirectory(root, segments, cache) {
	let dir = root;
	let key = '';
	for (const segment of segments) {
		key += '/' + segment;
		let next = cache.get(key);
		if (!next) {
			next = await dir.getDirectoryHandle(segment, { create: true });
			cache.set(key, next);
		}
		dir = next;
	}
	return dir;
}

export async function opfsRoot() {
	return navigator.storage.getDirectory();
}

// Import modes. A plan says how it wants to be put in place with plan.mode (default 'copy': every file is copied into OPFS).
// Another mode, for example reading the picked files in place through File System Access handles without copying them, is
// added with registerImportMode(name, async (plan, options) => ({ copied, manifest })) and set by the code that builds the
// plan; the launcher, the status line and the manifest do not change. A mode has to write <target.dir>.manifest.json
// (use writeManifest) so that getStatus() sees the target.
const importModes = new Map();
export function registerImportMode(name, fn) {
	importModes.set(name, fn);
}

export async function runImport(plan, options = {}) {
	const mode = plan.mode || 'copy';
	const fn = importModes.get(mode);
	if (!fn) throw new Error('Unknown import mode ' + mode);
	return fn(plan, options);
}

export async function writeManifest(plan, extra = {}) {
	const root = await opfsRoot();
	const target = TARGETS[plan.targetKey];
	const manifest = {
		version: MANIFEST_VERSION,
		kind: plan.kind || 'install',
		source: plan.sourceName,
		mode: plan.mode || 'copy',	// 'copy': the files are in OPFS; 'direct': they are read from the player's folder (webdirect/)
		...(plan.manifestExtra || {}),
		...extra,
		files: plan.files.length,
		bytes: plan.bytes,
		importedAt: new Date().toISOString(),
	};
	const manifestHandle = await root.getFileHandle(target.dir + '.manifest.json', { create: true });
	const writable = await manifestHandle.createWritable().catch(() => null);
	if (writable) {
		await writable.write(JSON.stringify(manifest));
		await writable.close();
	} else {
		await writeWithWorker([], target.dir + '.manifest.json', new Blob([JSON.stringify(manifest)]), () => {});
	}
	return manifest;
}

// Copies the planned files into OPFS. onProgress({ done, total, bytesDone, bytesTotal, path }).
// Files already present with the same size are skipped, so an interrupted import resumes.
// plan.prefetch (default 1) is how many files are requested ahead of the one being written,
// which only matters when getFile() is slow (the starter download).
async function copyIntoOpfs(plan, { onProgress = () => {}, signal = null, forceWorker = false } = {}) {
	const root = await opfsRoot();
	const target = TARGETS[plan.targetKey];
	const base = await root.getDirectoryHandle(target.dir, { create: true });
	const cache = new Map();
	const useWorker = forceWorker || typeof FileSystemFileHandle.prototype.createWritable !== 'function';

	let bytesDone = 0;
	let lastReport = 0;
	const report = (path, done, force) => {
		const now = performance.now();
		if (force || now - lastReport > 100) {
			lastReport = now;
			onProgress({ done, total: plan.files.length, bytesDone, bytesTotal: plan.bytes, path });
		}
	};

	let copied = 0;
	for (let i = 0; i < plan.files.length; i++) {
		if (signal && signal.aborted) {
			throw new DOMException('Import cancelled', 'AbortError');
		}
		const entry = plan.files[i];
		const path = entry.segments.join('/');
		const dirSegments = entry.segments.slice(0, -1);
		const name = entry.segments[entry.segments.length - 1];
		report(path, i, true);

		const dir = await getDirectory(base, dirSegments, cache);
		for (let ahead = i + 1; ahead <= i + (plan.prefetch || 1) && ahead < plan.files.length; ahead++) {
			const next = plan.files[ahead];
			if (!next.pending) {
				next.pending = next.getFile();
				next.pending.catch(() => {}); // reported when it is awaited
			}
		}
		const file = await (entry.pending || entry.getFile());
		entry.pending = null;

		let existing = null;
		try {
			existing = await (await dir.getFileHandle(name)).getFile();
		} catch (e) { /* not there yet */ }
		if (existing && existing.size === file.size) {
			bytesDone += file.size;
			continue;
		}

		const onBytes = (n) => { bytesDone += n; report(path, i, false); };
		if (useWorker) {
			await writeWithWorker([target.dir].concat(dirSegments), name, file, onBytes);
		} else {
			const handle = await dir.getFileHandle(name, { create: true });
			await writeWithWritable(handle, file, onBytes);
		}
		copied++;
	}

	const manifest = await writeManifest(plan);
	onProgress({ done: plan.files.length, total: plan.files.length, bytesDone: plan.bytes, bytesTotal: plan.bytes, path: '' });
	return { copied, manifest };
}
registerImportMode('copy', copyIntoOpfs);

// ---------------------------------------------------------------------------------------------
// Free starter content
// ---------------------------------------------------------------------------------------------

function cleanStarterPath(path) {
	if (typeof path !== 'string' || !path || path.startsWith('/') || path.includes('\\') || path.includes('\0')) {
		throw new Error('The starter content manifest lists a bad path: ' + JSON.stringify(path));
	}
	const segments = path.split('/');
	if (segments.some((s) => !s || s === '.' || s === '..')) {
		throw new Error('The starter content manifest lists a bad path: ' + path);
	}
	return segments.map((s) => s.toLowerCase());
}

async function sha256Hex(blob) {
	if (!(self.crypto && self.crypto.subtle)) return null;
	const digest = await self.crypto.subtle.digest('SHA-256', await blob.arrayBuffer());
	return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('');
}

// Downloads and validates starterpack/manifest.json. Throws with a readable message.
export async function fetchStarterManifest({ baseUrl = STARTER.baseUrl, signal = null } = {}) {
	let response;
	try {
		response = await fetch(new URL(baseUrl + 'manifest.json', document.baseURI), { cache: 'no-store', signal });
	} catch (e) {
		if (e && e.name === 'AbortError') throw e;
		throw new Error('Could not reach the starter content on this server (' + (e && e.message || e) + ').');
	}
	if (!response.ok) {
		throw new Error('This server does not offer the starter content (manifest.json: HTTP ' + response.status + ').');
	}
	let manifest;
	try {
		manifest = await response.json();
	} catch (e) {
		throw new Error('The starter content manifest is not valid JSON.');
	}
	if (!manifest || !Array.isArray(manifest.files) || manifest.files.length === 0) {
		throw new Error('The starter content manifest lists no files.');
	}
	let total = 0;
	for (const f of manifest.files) {
		cleanStarterPath(f.path);
		if (!Number.isInteger(f.size) || f.size < 0) throw new Error('Bad size for ' + f.path + ' in the manifest.');
		total += f.size;
	}
	manifest.totalSize = total;
	return manifest;
}

// A plan (see runImport) that fetches each file of the starter manifest.
export function planStarter(manifest, { baseUrl = STARTER.baseUrl, signal = null } = {}) {
	const files = manifest.files.map((entry) => {
		const segments = cleanStarterPath(entry.path);
		return {
			segments,
			size: entry.size,
			getFile: async () => {
				const url = new URL(baseUrl + entry.path.split('/').map(encodeURIComponent).join('/'), document.baseURI);
				let response;
				try {
					response = await fetch(url, { signal });
				} catch (e) {
					if (e && e.name === 'AbortError') throw e;
					throw new Error('Download failed for ' + entry.path + ': ' + (e && e.message || e));
				}
				if (!response.ok) throw new Error('Download failed for ' + entry.path + ' (HTTP ' + response.status + ').');
				const blob = await response.blob();
				if (blob.size !== entry.size) {
					throw new Error(entry.path + ' has the wrong size (' + blob.size + ', expected ' + entry.size + ').');
				}
				if (entry.sha256) {
					const hash = await sha256Hex(blob);
					if (hash && hash !== entry.sha256) throw new Error(entry.path + ' is corrupt (checksum mismatch).');
				}
				return blob;
			},
		};
	});
	return {
		targetKey: STARTER.targetKey,
		sourceName: STARTER.label,
		kind: 'starter',
		manifestExtra: {
			name: manifest.name || STARTER.label,
			packVersion: manifest.version,
			license: manifest.license || '',
			description: manifest.description || '',
		},
		prefetch: 4,
		looksRight: true,
		files,
		skipped: 0,
		bytes: files.reduce((sum, f) => sum + f.size, 0),
	};
}

// Replaces whatever is in game/ (and generals/) with the starter content. The caller has to
// confirm with the player first when status shows the player's own install there.
export async function downloadStarter({ onProgress = () => {}, signal = null, forceWorker = false } = {}) {
	const manifest = await fetchStarterManifest({ signal });
	const plan = planStarter(manifest, { signal });
	const { usage, quota } = await storageEstimate();
	if (quota && quota - usage < plan.bytes * 1.1) {
		throw new Error(quotaMessage(plan.bytes, quota - usage));
	}
	// Start clean: no leftovers of another install or an older pack, no half downloads.
	await clearTarget('game');
	await clearTarget('generals');
	const result = await runImport(plan, { onProgress, signal, forceWorker });
	return { ...result, plan };
}

// ---------------------------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------------------------

export async function getStatus() {
	const status = {};
	let root;
	try {
		root = await opfsRoot();
	} catch (e) {
		return status;
	}
	for (const key of Object.keys(TARGETS)) {
		try {
			const handle = await root.getFileHandle(TARGETS[key].dir + '.manifest.json');
			status[key] = JSON.parse(await (await handle.getFile()).text());
			if (status[key] && !status[key].kind) status[key].kind = 'install';
		} catch (e) {
			status[key] = null;
		}
	}
	return status;
}

export async function clearTarget(key) {
	const root = await opfsRoot();
	const dir = TARGETS[key].dir;
	for (const name of [dir, dir + '.manifest.json']) {
		try {
			await root.removeEntry(name, { recursive: true });
		} catch (e) {
			if (e.name !== 'NotFoundError') throw e;
		}
	}
}

export async function clearUserData() {
	const root = await opfsRoot();
	try {
		await root.removeEntry('userdata', { recursive: true });
	} catch (e) {
		if (e.name !== 'NotFoundError') throw e;
	}
}

// The files in the user data (saves, replays, screenshots, options), recursively, as
// [{ path: 'savegame/save.sav', size, modified }] (paths use the lower case names of the OPFS
// backend). Reading OPFS from the page while the game runs is fine: the files are only listed and read.
export async function listUserData(prefix = '') {
	const out = [];
	let root;
	try {
		root = await (await opfsRoot()).getDirectoryHandle('userdata');
	} catch (e) {
		return out;
	}
	async function walk(dir, base) {
		for await (const [name, handle] of dir.entries()) {
			if (handle.kind === 'directory') {
				await walk(handle, base + name + '/');
			} else if ((base + name).startsWith(prefix)) {
				try {
					const file = await handle.getFile();
					out.push({ path: base + name, size: file.size, modified: file.lastModified });
				} catch (e) { /* the game may be replacing it right now */ }
			}
		}
	}
	await walk(root, '');
	return out;
}

// One file of the user data as a File (null if it is not there).
export async function readUserFile(path) {
	try {
		let dir = await (await opfsRoot()).getDirectoryHandle('userdata');
		const parts = path.split('/');
		for (const part of parts.slice(0, -1)) dir = await dir.getDirectoryHandle(part);
		return await (await dir.getFileHandle(parts[parts.length - 1])).getFile();
	} catch (e) {
		return null;
	}
}

export async function storageEstimate() {
	if (navigator.storage && navigator.storage.estimate) {
		const { usage = 0, quota = 0 } = await navigator.storage.estimate();
		return { usage, quota };
	}
	return { usage: 0, quota: 0 };
}

// Asks the browser not to evict the data under storage pressure.
export async function requestPersistence() {
	try {
		if (navigator.storage && navigator.storage.persist) {
			return await navigator.storage.persist();
		}
	} catch (e) { /* ignore */ }
	return false;
}

// The message for a browser storage quota that is too small. Chrome gives a site a share of the free disk space, so the
// number changes with the disk, and freeing space on the computer is the fix.
export function quotaMessage(needed, available) {
	return 'Not enough browser storage: this needs ' + formatBytes(needed) + ' but the browser allows only ' + formatBytes(Math.max(0, available)) +
		' more. Chrome limits a site to a share of the free disk space on this computer, so freeing some disk space (about ' +
		formatBytes(Math.max(0, needed - available)) + ' or more) and trying again usually fixes it.';
}

export function formatBytes(bytes) {
	if (bytes >= 1024 ** 3) return (bytes / 1024 ** 3).toFixed(2) + ' GB';
	if (bytes >= 1024 ** 2) return (bytes / 1024 ** 2).toFixed(1) + ' MB';
	if (bytes >= 1024) return (bytes / 1024).toFixed(0) + ' KB';
	return bytes + ' B';
}
