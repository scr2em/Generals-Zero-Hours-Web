// The in-browser army library: the packages the launcher made from a mod (armyimport.js) are kept as ordinary
// .zharmy files in this browser's Origin Private File System, in the folder armies-library/ (file name: <id>.zharmy).
//
// They are listed with the same code as the packages of an armies folder (armies.scanPackages: only the ZIP directory and
// the manifest are read) and given to the engine the same way: on Play the launcher takes each ticked package's File
// (an OPFS file is a File like any other) and the engine serves it at /armies/library/<name> (direct-source.js setArmyFiles).
// So nothing in the engine knows the difference between a package from a folder and one from the library.

export const LIBRARY_DIR = 'armies-library';
export const LIBRARY_PREFIX = 'library';     // the path below /armies

const NAME_RE = /^[a-z0-9][a-z0-9.-]{2,63}\.zharmy$/;

async function directory(create) {
	const root = await navigator.storage.getDirectory();
	return root.getDirectoryHandle(LIBRARY_DIR, { create });
}

export const fileNameFor = (id) => id + '.zharmy';

/** The library as items for armies.scanPackages: [{ segments: ['library', '<id>.zharmy'], getFile, handle }]. [] when empty. */
export async function listItems() {
	let dir;
	try {
		dir = await directory(false);
	} catch (e) {
		return [];
	}
	const items = [];
	for await (const entry of dir.values()) {
		if (entry.kind !== 'file' || !/\.zharmy$/i.test(entry.name)) continue;
		items.push({ segments: [LIBRARY_PREFIX, entry.name], getFile: () => entry.getFile(), handle: entry, library: true });
	}
	return items;
}

/** Writes a package (bytes) under its id. Replaces an older package with the same id. Resolves to the file name. */
export async function save(id, bytes) {
	const name = fileNameFor(id);
	if (!NAME_RE.test(name)) throw new Error('The package id ' + id + ' cannot be used as a file name.');
	const dir = await directory(true);
	const handle = await dir.getFileHandle(name, { create: true });
	const writable = await handle.createWritable();
	try {
		await writable.write(bytes);
		await writable.close();
	} catch (e) {
		try { await writable.abort(); } catch (e2) { /* already closed */ }
		throw e;
	}
	return name;
}

/** Removes a package from the library. True when it was there. */
export async function remove(name) {
	try {
		const dir = await directory(false);
		await dir.removeEntry(name);
		return true;
	} catch (e) {
		if (e && e.name === 'NotFoundError') return false;
		throw e;
	}
}

/** Offers a package as a download (the same file the launcher gives to the game). */
export async function download(name) {
	const dir = await directory(false);
	const file = await (await dir.getFileHandle(name)).getFile();
	const url = URL.createObjectURL(file);
	const a = document.createElement('a');
	a.href = url;
	a.download = name;
	document.body.appendChild(a);
	a.click();
	a.remove();
	setTimeout(() => URL.revokeObjectURL(url), 60000);
}

/** Whether a package with this id is in the library. */
export async function has(id) {
	try {
		const dir = await directory(false);
		await dir.getFileHandle(fileNameFor(id));
		return true;
	} catch (e) {
		return false;
	}
}
