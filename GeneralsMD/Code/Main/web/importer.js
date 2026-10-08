// Copies the player's own game install into the browser's Origin Private File
// System (OPFS), where the game finds it. Nothing is uploaded anywhere and the
// game data is never part of the build.
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
	},
	generals: {
		label: 'Generals',
		dir: 'generals',
		signature: ['ini.big', 'w3d.big', 'textures.big', 'terrain.big'],
		hint: 'Command & Conquer Generals',
	},
};

// What is not needed to run the game in the browser.
const SKIP_DIRECTORIES = new Set(['movies', 'redist', 'support', 'directx', '__macosx', '.git']);
const SKIP_EXTENSIONS = new Set([
	'exe', 'dll', 'ocx', 'sys', 'vxd', 'msi', 'cab', 'pdb', 'lib', 'bat', 'cmd', 'lnk', 'url',
	'bik', 'bk2', 'tmp', 'log', 'dmp', 'ds_store',
]);

const MANIFEST_VERSION = 1;

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
			let size = 0;
			try {
				size = (await handle.getFile()).size;
			} catch (e) {
				continue; // unreadable (e.g. locked), skip
			}
			out.push({ segments: prefix.concat(entry.name), size, getFile: () => handle.getFile() });
		}
	}
}

// Shows the browser's folder picker (Chromium and Safari). Returns null when
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
	return { name: handle.name, files };
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
// Planning
// ---------------------------------------------------------------------------------------------

export function planImport(source, targetKey, options = {}) {
	const target = TARGETS[targetKey];
	const includeVideos = !!options.includeVideos;
	const names = new Set(source.files.filter(f => f.segments.length === 1).map(f => f.segments[0].toLowerCase()));
	const looksRight = target.signature.some(n => names.has(n));

	const files = [];
	let skipped = 0;
	for (const file of source.files) {
		const dirs = file.segments.slice(0, -1).map(s => s.toLowerCase());
		const name = file.segments[file.segments.length - 1].toLowerCase();
		const ext = name.includes('.') ? name.slice(name.lastIndexOf('.') + 1) : '';
		const skipDir = dirs.some(d => SKIP_DIRECTORIES.has(d) && !(includeVideos && d === 'movies'));
		const skipExt = SKIP_EXTENSIONS.has(ext) && !(includeVideos && (ext === 'bik' || ext === 'bk2'));
		if (skipDir || skipExt) {
			skipped++;
			continue;
		}
		files.push({ ...file, segments: dirs.concat(name) });
	}
	const bytes = files.reduce((sum, f) => sum + f.size, 0);
	return { targetKey, sourceName: source.name, looksRight, files, skipped, bytes };
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

// Copies the planned files into OPFS. onProgress({ done, total, bytesDone, bytesTotal, path }).
// Files already present with the same size are skipped, so an interrupted import resumes.
export async function runImport(plan, { onProgress = () => {}, signal = null, forceWorker = false } = {}) {
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
		const file = await entry.getFile();

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

	const manifest = {
		version: MANIFEST_VERSION,
		source: plan.sourceName,
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
	onProgress({ done: plan.files.length, total: plan.files.length, bytesDone: plan.bytes, bytesTotal: plan.bytes, path: '' });
	return { copied, manifest };
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

export function formatBytes(bytes) {
	if (bytes >= 1024 ** 3) return (bytes / 1024 ** 3).toFixed(2) + ' GB';
	if (bytes >= 1024 ** 2) return (bytes / 1024 ** 2).toFixed(1) + ' MB';
	if (bytes >= 1024) return (bytes / 1024).toFixed(0) + ' KB';
	return bytes + ' B';
}
