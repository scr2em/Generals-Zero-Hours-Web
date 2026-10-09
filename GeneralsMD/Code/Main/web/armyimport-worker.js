// The army converter in a Web Worker: tools/zharmy (Python) running under Pyodide (CPython compiled to WebAssembly).
//
// The page (armyimport.js) talks to this worker with messages { id, cmd, ... } and gets back
//   { id, type: 'progress', text }     a line of the converter's progress output
//   { id, type: 'status',   text }     what the worker is doing (loading, reading)
//   { id, type: 'done',     result, files? }   the answer (files: [{ name, bytes }] are transferred)
//   { id, type: 'error',    message }
//
// Commands
//   init   { base }                     load Pyodide and the converter (base: URL of the folder this file is in)
//   retail { names }                    which archive names are retail ones (the converter's list)
//   open   { mounts, params }           mount the player's folders, read the mod, list the armies
//   convert{ job }                      convert one army, answer with the row and the package bytes
//   close  {}                           forget the mod and unmount the folders
//
// The player's files are never copied into the worker: each folder is mounted with Emscripten's WORKERFS, which keeps the
// File objects and reads a byte range with FileReaderSync whenever Python reads the file (open/seek/read). A mod of several
// GB costs only the bytes the converter really reads (the directory of each archive and the files it copies).
//
// Nothing is fetched except pyodide/ and zharmy.zip from the page's own site.

'use strict';

let py = null;
let webapi = null;
let currentId = 0;
let mounted = [];
const OUT_DIR = '/out';

const post = (message, transfer) => self.postMessage(message, transfer || []);
const say = (text) => post({ id: currentId, type: 'status', text });

async function init({ base }) {
	if (py) return { version: py.version, ms: 0 };
	const started = performance.now();
	say('Loading the converter (about 12 MB, kept by the browser for next time)…');
	importScripts(base + 'pyodide/pyodide.js');
	py = await self.loadPyodide({
		indexURL: base + 'pyodide/',
		stdout: (text) => post({ id: currentId, type: 'progress', text }),
		stderr: (text) => post({ id: currentId, type: 'progress', text }),
	});
	const response = await fetch(base + 'zharmy.zip');
	if (!response.ok) throw new Error('The converter (zharmy.zip) could not be loaded: HTTP ' + response.status);
	py.FS.writeFile('/zharmy.zip', new Uint8Array(await response.arrayBuffer()));
	py.runPython('import sys\nsys.path.insert(0, "/zharmy.zip")\nfrom zharmy import webapi\n');
	webapi = py.pyimport('zharmy.webapi');
	return { version: py.version, converter: webapi.version(), ms: performance.now() - started };
}

function unmountAll() {
	for (const point of mounted.reverse()) {
		try { py.FS.unmount(point); } catch (e) { /* not mounted */ }
	}
	mounted = [];
}

// mounts: [{ point: '/mnt/game', files: [{ path: 'Data/INI/x.ini', file: File }] }]
function mountAll(mounts) {
	unmountAll();
	const FS = py.FS;
	for (const m of mounts) {
		FS.mkdirTree(m.point);
		// Names with "/" become sub folders of the mount; the File objects stay where they are.
		FS.mount(FS.filesystems.WORKERFS, { blobs: m.files.map((f) => ({ name: f.path, data: f.file })) }, m.point);
		mounted.push(m.point);
	}
}

function open({ mounts, params }) {
	mountAll(mounts);
	say('Reading the mod…');
	const answer = JSON.parse(webapi.open_json(JSON.stringify(params), (text) => post({ id: currentId, type: 'progress', text })));
	return answer;
}

function convert({ job }) {
	const FS = py.FS;
	try { FS.mkdirTree(OUT_DIR); } catch (e) { /* exists */ }
	const row = JSON.parse(webapi.convert_json(JSON.stringify(job), OUT_DIR));
	const files = [];
	if (row.ok && row.file) {
		const bytes = FS.readFile(row.file);
		files.push({ name: row.id + '.zharmy', bytes });
		try { FS.unlink(row.file); } catch (e) { /* ignore */ }
		delete row.file;
	}
	return { row, files };
}

function retail({ names }) {
	return JSON.parse(webapi.retail_json(JSON.stringify(names)));
}

function close() {
	if (webapi) webapi.close();
	if (py) unmountAll();
	return {};
}

const COMMANDS = { init, retail, open, convert, close };

self.onmessage = async (event) => {
	const { id, cmd } = event.data;
	currentId = id;
	try {
		const out = await COMMANDS[cmd](event.data);
		const files = out && out.files ? out.files : [];
		const result = out && out.files ? out.row : out;
		post({ id, type: 'done', result, files }, files.map((f) => f.bytes.buffer));
	} catch (e) {
		// A PythonError carries the whole traceback; the last line is the message.
		let message = String(e && e.message || e);
		if (e && e.type && /Error|Exception/.test(String(e.type))) message = message.trim().split('\n').pop();
		post({ id, type: 'error', message, detail: String(e && e.stack || e) });
	}
};
