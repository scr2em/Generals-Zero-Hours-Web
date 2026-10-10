// "Import armies from a mod…" in the launcher: the panel inside the Armies section.
//
//   1. choose the mod's folder (or the Zero Hour folder when the mod is installed in it)
//   2. say which archives belong to the mod (only when the folder is a Zero Hour folder)
//   3. the armies found in the mod: tick, rename, import
//   4. progress, then a table with what each army got, and the converter's report
//
// The markup is in shell.html; the converter runs in a worker (armyimport.js); the finished packages go to the library in
// the browser's storage (armylibrary.js). shell.html passes what it knows in `env` (see initArmyImport).

import * as lib from './armylibrary.js';
import * as imp from './armyimport.js';

// The converter names files by the places they are mounted at in its worker (/mnt/game/...); the player sees the file names.
const plain = (text) => String(text).replace(/ Details: zharmy inspect[^.]*(\.\w+)*$/, '').replace(/\/mnt\/(?:game|mod|base)\//g, '').replace(/(archive )(\S+) was skipped: \2: /g, '$1$2 was skipped: ');

/**
 * env: {
 *   $(id)                         document.getElementById
 *   params                        URLSearchParams of the page
 *   directoryPicker               true when showDirectoryPicker exists
 *   formatBytes(n)
 *   game()                        what the launcher plays with: 'install' | 'starter' | 'access' (install, folder not open) | null
 *   gameFiles()                   async: [{ path, file }] of the Zero Hour files the launcher has (path below the game folder)
 *   existing()                    { tagOwners: Map of upper case tag -> package id of the armies the launcher lists, ids: Set of the ids in the library }
 *   onImported(ids)               async: the library changed; the launcher lists it again and ticks these ids
 *   onBusy(boolean)               importing: the launcher keeps Play and the pickers off
 *   log(text)
 * }
 */
export function initArmyImport(env) {
	const $ = env.$;
	const el = {
		open: $('import-open'), panel: $('army-import'), close: $('imp-close'), state: $('state-import'),
		pick: $('imp-pick'), choose: $('imp-choose'), pickState: $('imp-pick-state'), gameNote: $('imp-game-note'), input: $('import-input'),
		files: $('imp-files'), filesTitle: $('imp-files-title'), filesText: $('imp-files-text'), archives: $('imp-archives'),
		selfContained: $('imp-selfcontained'), look: $('imp-look'), lookHint: $('imp-look-hint'),
		armies: $('imp-armies'), modName: $('imp-modname'), armyList: $('imp-army-list'), go: $('imp-go'), back: $('imp-back'), armiesNote: $('imp-armies-note'), retail: $('imp-retail'),
		run: $('imp-run'), runList: $('imp-run-list'), bar: $('imp-bar'), runText: $('imp-run-text'), log: $('imp-log'), cancel: $('imp-cancel'),
		result: $('imp-result'), resultText: $('imp-result-text'), table: $('imp-table'), reports: $('imp-reports'), more: $('imp-more'),
		error: $('imp-error'),
	};

	const st = {
		importer: null,
		source: null,           // the picked folder: { name, handle, files }
		analysis: null,         // imp.analyse(source)
		mode: '',               // 'installed' | 'separate'
		session: null,          // the answer of open: { factions, ... }
		running: false,
		filesToken: 0,
		timings: {},            // for tests and the measurements in the docs
		available: null,
	};
	const showStep = (...names) => {
		for (const n of ['pick', 'files', 'armies', 'run', 'result']) el[n].hidden = !names.includes(n);
	};
	const setError = (text) => {
		el.error.hidden = !text;
		el.error.textContent = text || '';
	};
	const logLine = (text) => {
		const t = ((performance.now() - (st.runStart || performance.now())) / 1000).toFixed(1);
		el.log.textContent += 'zharmy [' + t.padStart(5) + 's]: ' + text + '\n';
		el.log.scrollTop = el.log.scrollHeight;
	};
	const dispose = () => {
		if (st.importer) st.importer.dispose();
		st.importer = null;
	};
	const importer = () => st.importer || (st.importer = new imp.ArmyImporter());

	// ---- opening and closing ----------------------------------------------------------------------------------
	async function show() {
		el.panel.hidden = false;
		el.open.setAttribute('aria-expanded', 'true');
		setError('');
		showStep('pick');
		describeGame();
		if (st.available === null) st.available = await imp.converterAvailable();
		el.choose.disabled = !st.available;
		if (!st.available) setError('This copy of the page does not include the converter, so armies cannot be imported here. The armies folder above still works.');
	}
	function hide() {
		if (st.running) return;
		el.panel.hidden = true;
		el.open.setAttribute('aria-expanded', 'false');
		dispose();
		st.source = st.analysis = st.session = null;
		el.log.textContent = '';
	}
	function describeGame() {
		const g = env.game();
		el.gameNote.textContent = g === 'install' ? 'Your Zero Hour files are ready to compare the mod with.'
			: g === 'access' ? 'Your Zero Hour folder needs your permission again first (see “Your game” above). You can also choose the Zero Hour folder here when the mod is installed in it.'
			: g === 'starter' ? 'You are using the free starter content. To import a mod, choose the folder with Zero Hour and the mod installed in it, or choose your Zero Hour folder above first.'
			: 'No Zero Hour folder chosen yet. If the mod is installed in your Zero Hour folder, choose that folder here. If it is a folder of its own, choose your Zero Hour folder above first.';
	}

	// ---- 1. choose the folder ---------------------------------------------------------------------------------
	async function chooseFolder() {
		if (st.running) return;
		setError('');
		let source;
		try {
			env.onBusy(true);
			el.choose.disabled = true;
			if (env.directoryPicker && !env.params.has('picker')) {
				const handle = await window.showDirectoryPicker({ id: 'zh-armyimport', mode: 'read' });
				el.pickState.textContent = 'Looking at the folder…';
				source = await imp.collectFromHandle(handle, { onProgress: (n) => { el.pickState.textContent = 'Looking at the folder… ' + n.toLocaleString() + ' files'; } });
			} else {
				const files = await new Promise((resolve, reject) => {
					const input = el.input;
					input.value = '';
					input.onchange = () => (input.files && input.files.length ? resolve(input.files) : reject(new DOMException('No folder selected', 'AbortError')));
					input.oncancel = () => reject(new DOMException('No folder selected', 'AbortError'));
					input.click();
				});
				el.pickState.textContent = 'Looking at the folder…';
				source = imp.collectFromFileList(files);
			}
		} catch (e) {
			el.pickState.textContent = '';
			if (!(e && e.name === 'AbortError')) setError('Cannot read that folder: ' + (e && e.message || e));
			return;
		} finally {
			env.onBusy(false);
			el.choose.disabled = !st.available;
		}
		el.pickState.textContent = '';
		st.source = source;
		st.analysis = imp.analyse(source);
		renderFiles();
	}

	// ---- 2. which files belong to the mod ---------------------------------------------------------------------
	const modArchivesTicked = () => [...el.archives.querySelectorAll('input[type="checkbox"]:checked')].map((b) => b.getAttribute('data-path'));

	async function renderFiles() {
		const a = st.analysis;
		const folder = '“' + (st.source.name || 'the folder') + '”';
		st.mode = a.installPrefix ? 'installed' : 'separate';
		el.archives.textContent = '';
		el.filesTitle.hidden = el.archives.hidden = st.mode !== 'installed';
		el.selfContained.checked = false;
		const token = ++st.filesToken;
		if (st.mode === 'installed') {
			// Which archive names are the original game's is the converter's knowledge: it is loaded now (about 12 MB, kept by
			// the browser for the next visit) and is needed for the import anyway.
			el.filesText.textContent = 'Zero Hour was found in ' + folder + '. Looking at its files…';
			showStep('files');
			el.look.disabled = true;
			let retail = null;
			if (st.available) {
				try {
					retail = await importer().retail(a.archives.map((x) => x.path), { onStatus: (t) => { if (token === st.filesToken) el.filesText.textContent = t; } });
				} catch (e) {
					dispose();
					if (token === st.filesToken) setError('The converter could not be loaded. ' + plain(e && e.message || e));
					return;
				}
			}
			if (token !== st.filesToken) return;       // another folder was chosen meanwhile
			a.archives.forEach((x, i) => { x.retail = !!(retail && retail[i]); });
			const ticked = a.archives.filter((x) => !x.retail).length;
			el.filesText.textContent = 'Zero Hour was found in ' + folder + '. The mod’s own files are in the same folder as the game’s. ' +
				(ticked ? 'The files that do not come with the original game are ticked: please check the list.' : 'Every file here has the name of a file that comes with the original game, so none is ticked. Tick the mod’s files if you know them.');
			for (const x of a.archives) {
				const li = document.createElement('li');
				const label = document.createElement('label');
				const box = document.createElement('input');
				box.type = 'checkbox';
				box.checked = !x.retail;
				box.setAttribute('data-path', x.path);
				box.onchange = updateLookButton;
				const name = document.createElement('span');
				name.textContent = x.path;
				const meta = document.createElement('span');
				meta.className = 'imp-meta';
				meta.textContent = env.formatBytes(x.size) + (x.retail ? ' · comes with the game' : ' · not from the original game');
				label.append(box, name, meta);
				li.append(label);
				el.archives.append(li);
			}
		} else {
			const bigs = a.archives.length;
			el.filesText.textContent = folder + ' looks like a mod’s own folder (' + a.files.length.toLocaleString() + ' usable files' + (bigs ? ', ' + bigs + ' archive' + (bigs === 1 ? '' : 's') : '') + '). It will be compared with your Zero Hour files.';
			// Start loading the converter now while the player looks at the page.
			if (st.available) importer().init().then((r) => { st.timings.initMs = r.ms; }).catch(() => { dispose(); });
		}
		showStep('files');
		updateLookButton();
	}

	function updateLookButton() {
		const own = el.selfContained.checked;
		let why = '';
		if (st.mode === 'installed') {
			if (!own && modArchivesTicked().length === 0) why = 'Tick the files that belong to the mod.';
		} else if (!own && env.game() !== 'install') {
			why = 'Choose your Zero Hour folder above first, or tick “Make armies that do not need your Zero Hour files”.';
		}
		el.look.disabled = !!why;
		el.lookHint.textContent = why;
	}

	// ---- 3. read the mod and list its armies -------------------------------------------------------------------
	async function lookForArmies() {
		if (st.running) return;
		setError('');
		const own = el.selfContained.checked;
		const a = st.analysis;
		const ticked = st.mode === 'installed' && !own ? modArchivesTicked() : null;
		// What the converter does by default (every archive without a retail name) is asked for by name; other ticks are exact paths.
		const automatic = ticked && a.archives.filter((x) => !x.retail).map((x) => x.path).sort().join('\n') === ticked.slice().sort().join('\n');
		const modArchives = ticked ? (automatic ? 'auto' : ticked) : null;
		st.requires = own ? 'none' : 'zerohour';
		st.modName = st.mode === 'installed' ? imp.modNameFromArchives(ticked && ticked.length ? ticked : a.archives.filter((x) => !x.retail).map((x) => x.path)) : (st.source.name || 'Mod');
		st.running = true;
		env.onBusy(true);
		showStep('run');
		el.runList.textContent = '';
		el.log.textContent = '';
		el.bar.style.width = '0%';
		el.cancel.hidden = false;
		el.runText.textContent = 'Starting the converter…';
		st.runStart = performance.now();
		const handlers = { onProgress: logLine, onStatus: (t) => { el.runText.textContent = t; } };
		try {
			const t0 = performance.now();
			const initialised = await importer().init(handlers);
			st.timings.loadMs = performance.now() - t0;
			if (initialised && initialised.ms) st.timings.initMs = initialised.ms;
			// the folders to mount
			const mounts = [];
			const files = (list) => list.map((f) => ({ path: f.segments.join('/'), file: f.file }));
			let params;
			if (st.mode === 'installed') {
				mounts.push({ point: '/mnt/game', files: files(a.files) });
				params = own ? { modPaths: ['/mnt/game'], basePaths: [], requires: 'none' }
					: { modPaths: ['/mnt/game'], basePaths: ['/mnt/game'], requires: 'zerohour', modArchives };
			} else if (own) {
				mounts.push({ point: '/mnt/mod', files: files(a.files) });
				params = { modPaths: ['/mnt/mod'], basePaths: [], requires: 'none' };
			} else {
				el.runText.textContent = 'Getting your Zero Hour files ready…';
				const base = (await env.gameFiles() || []).filter((f) => imp.wantFile(f.path.split('/')));
				if (!base.length) throw new Error('Your Zero Hour files are not available. Choose your Zero Hour folder above and try again.');
				mounts.push({ point: '/mnt/mod', files: files(a.files) }, { point: '/mnt/base', files: base });
				params = { modPaths: ['/mnt/mod'], basePaths: ['/mnt/base'], requires: 'zerohour' };
			}
			params.modName = st.modName;
			el.runText.textContent = 'Reading the mod…';
			const t1 = performance.now();
			const answer = await importer().open({ mounts, params }, handlers);
			st.timings.openMs = performance.now() - t1;
			if (!answer.ok) throw new Error(plain(answer.error));
			st.session = answer;
			st.running = false;
			env.onBusy(false);
			renderArmies();
		} catch (e) {
			st.running = false;
			env.onBusy(false);
			dispose();
			showStep('files');
			setError('The mod could not be read. ' + plain(e && e.message || e));
		}
	}

	const sizeWord = (n) => (n < 40 ? 'small' : n < 150 ? 'medium' : 'big');

	function renderArmies() {
		const s = st.session;
		el.modName.value = st.modName;
		el.armyList.textContent = '';
		el.armiesNote.textContent = '';
		if (!s.factions.length) {
			const li = document.createElement('li');
			li.className = 'imp-none';
			li.textContent = 'No armies were found in this mod. It adds no side that a player can choose in a skirmish game' +
				(st.mode === 'installed' ? ', or the ticked files are not the mod’s. Go back and check the list of files.' : '.');
			el.armyList.append(li);
			el.go.hidden = true;
			el.retail.hidden = true;
			el.armiesNote.textContent = '';
			showStep('armies');
			return;
		}
		el.go.hidden = false;
		const existing = env.existing();
		const modId = (f) => imp.packageId(st.modName, f.playerTemplate);
		const own = new Set(s.factions.map(modId));           // an army that is imported again replaces the old one: its tag is free
		const taken = new Set([...existing.tagOwners].filter(([, id]) => !own.has(id)).map(([tag]) => tag));
		for (const f of s.factions) {
			f.pick = !f.unplayable;
			f.tag = imp.uniqueTag(f.suggestedTag, taken);
			taken.add(f.tag);
			f.name = f.suggestedName;
		}
		for (const f of s.factions) el.armyList.append(armyItem(f));
		const warns = s.warnings.map(plain).concat(s.iniErrors ? [s.iniErrors + ' problem' + (s.iniErrors === 1 ? '' : 's') + ' in the mod’s settings files were skipped while reading.'] : []);
		el.armiesNote.textContent = 'Reading the mod took ' + (st.timings.openMs / 1000).toFixed(1) + ' seconds.' + (warns.length ? ' ' + warns.join(' ') : '');
		showStep('armies');
		el.retail.hidden = !s.factions.some((f) => f.inRuleset);
		refreshArmies();
	}

	function armyItem(f) {
		const li = document.createElement('li');
		li.className = 'imp-army';
		const head = document.createElement('div');
		head.className = 'imp-army-head';
		const label = document.createElement('label');
		const box = document.createElement('input');
		box.type = 'checkbox';
		box.checked = !f.unplayable;
		box.disabled = !!f.unplayable;
		box.setAttribute('data-faction', f.playerTemplate);
		box.onchange = () => { f.pick = box.checked; refreshArmies(); };
		const title = document.createElement('span');
		title.className = 'army-name';
		title.textContent = f.displayName;
		label.append(box, title);
		const ai = document.createElement('span');
		ai.className = 'pill ' + (f.skirmishBuildList && f.skirmishScripts ? 'ready' : 'wait');
		ai.textContent = f.skirmishBuildList && f.skirmishScripts ? 'Computer can play it' : 'Humans only';
		ai.title = f.skirmishBuildList && f.skirmishScripts ? 'The mod has a build list and scripts for this army.' : 'The mod has no ' + (f.skirmishBuildList ? 'scripts' : 'build list') + ' for the computer to play this army.';
		head.append(label, ai);
		li.append(head);
		const meta = document.createElement('div');
		meta.className = 'army-meta';
		const bits = [(f.objects === 1 ? '1 unit or building' : f.objects + ' units and buildings') + ' (' + sizeWord(f.objects) + ')', f.inRuleset ? 'changes an army that comes with Zero Hour' : 'a new army'];
		if (f.side) bits.push('side ' + f.side);
		meta.textContent = bits.join(' · ');
		li.append(meta);
		const note = document.createElement('div');
		note.className = 'army-reason needs';
		note.hidden = true;
		li.append(note);
		if (f.unplayable) {
			note.hidden = false;
			note.className = 'army-reason bad';
			note.textContent = 'This army cannot be played, not even in the mod itself: ' + plain(f.unplayable) + '.';
			f.ui = { li, note, tagInput: document.createElement('input'), box };
			return li;
		}
		const d = document.createElement('details');
		const sum = document.createElement('summary');
		sum.textContent = 'Change name or tag';
		d.append(sum);
		const row = document.createElement('div');
		row.className = 'imp-fields';
		const nameLabel = document.createElement('label');
		nameLabel.textContent = 'Name in the game ';
		const nameInput = document.createElement('input');
		nameInput.type = 'text';
		nameInput.maxLength = 64;
		nameInput.value = f.name;
		nameInput.setAttribute('data-name', f.playerTemplate);
		nameInput.oninput = () => { f.name = nameInput.value; refreshArmies(); };
		nameLabel.append(nameInput);
		const tagLabel = document.createElement('label');
		tagLabel.textContent = 'Tag ';
		tagLabel.title = 'Two to six capital letters or digits. Everything this army adds to the game starts with it, so it must be different for each army.';
		const tagInput = document.createElement('input');
		tagInput.type = 'text';
		tagInput.maxLength = 6;
		tagInput.size = 7;
		tagInput.value = f.tag;
		tagInput.setAttribute('data-tag', f.playerTemplate);
		tagInput.oninput = () => { f.tag = tagInput.value.toUpperCase(); refreshArmies(); };
		tagLabel.append(tagInput);
		row.append(nameLabel, tagLabel);
		d.append(row);
		li.append(d);
		f.ui = { li, note, tagInput, box };
		return li;
	}

	// Checks tags and ids of the ticked armies and updates the Import button.
	function refreshArmies() {
		const s = st.session;
		const existing = env.existing();
		const seen = new Map();
		let picked = 0;
		let invalid = false;
		const modName = el.modName.value.trim() || st.modName;
		for (const f of s.factions) f.id = imp.packageId(modName, f.playerTemplate);
		const own = new Set(s.factions.filter((f) => f.pick).map((f) => f.id));
		const taken = new Set([...existing.tagOwners].filter(([, id]) => !own.has(id)).map(([tag]) => tag));
		for (const f of s.factions) {
			if (f.unplayable) continue;
			f.ui.note.hidden = true;
			f.ui.tagInput.removeAttribute('aria-invalid');
			if (!f.pick) continue;
			picked++;
			let problem = '';
			if (!imp.TAG_RE.test(f.tag)) problem = 'The tag needs 2 to 6 capital letters or digits and must start with a letter.';
			else if (taken.has(f.tag)) problem = 'The tag ' + f.tag + ' is used by another army you have. Choose a different one.';
			else if (seen.has(f.tag)) problem = 'The tag ' + f.tag + ' is used twice here. Choose a different one.';
			else if (!f.name.trim()) problem = 'The army needs a name.';
			else if (!imp.ID_RE.test(f.id)) problem = 'The mod name gives a name the game cannot use. Use letters and digits.';
			seen.set(f.tag, f);
			if (problem) {
				invalid = true;
				f.ui.note.hidden = false;
				f.ui.note.textContent = problem;
				f.ui.note.className = 'army-reason bad';
				f.ui.tagInput.setAttribute('aria-invalid', 'true');
				f.ui.li.querySelector('details').open = true;
			} else if (existing.ids.has(f.id)) {
				f.ui.note.hidden = false;
				f.ui.note.className = 'army-reason needs';
				f.ui.note.textContent = 'You imported this army before. Importing again replaces it.';
			}
		}
		el.go.textContent = picked === 0 ? 'Tick the armies to import' : 'Import ' + picked + (picked === 1 ? ' army' : ' armies');
		el.go.disabled = picked === 0 || invalid;
	}

	// ---- 4. convert and store ----------------------------------------------------------------------------------
	async function runImport() {
		const s = st.session;
		const modName = el.modName.value.trim() || st.modName;
		const jobs = s.factions.filter((f) => f.pick).map((f) => ({ playerTemplate: f.playerTemplate, tag: f.tag, id: f.id, name: f.name.trim(), display: f.displayName }));
		if (!jobs.length) return;
		st.running = true;
		env.onBusy(true);
		setError('');
		showStep('run');
		el.cancel.hidden = false;
		el.runList.textContent = '';
		el.log.textContent = '';
		el.bar.style.width = '0%';
		st.runStart = performance.now();
		const items = jobs.map((j) => {
			const li = document.createElement('li');
			li.textContent = j.name + ': waiting';
			el.runList.append(li);
			return li;
		});
		const rows = [];
		const done = [];
		const t0 = performance.now();
		for (let n = 0; n < jobs.length; n++) {
			const job = jobs[n];
			items[n].textContent = job.name + ': converting…';
			el.runText.textContent = 'Army ' + (n + 1) + ' of ' + jobs.length + ': ' + job.name;
			el.bar.style.width = (100 * n / jobs.length).toFixed(1) + '%';
			const started = performance.now();
			try {
				const { row, bytes } = await importer().convert({ ...job, modVersion: '', modName }, { onProgress: logLine, onStatus: (t) => { el.runText.textContent = t; } });
				if (row.error) row.error = plain(row.error);
				row.name = row.name || job.name;
				row.display = job.display;
				row.ms = performance.now() - started;
				if (row.ok && !row.valid) {
					row.ok = false;
					row.error = 'The finished package did not pass the check: ' + (row.validationErrors || []).map(plain).join('; ');
				}
				if (row.ok && bytes) {
					try {
						await lib.save(row.id, bytes);
						done.push(row.id);
					} catch (e) {
						row.ok = false;
						row.error = 'The package could not be saved in this browser: ' + (e && e.message || e) + '. The browser’s storage may be full.';
					}
				}
				rows.push(row);
				items[n].textContent = job.name + (row.ok ? ': done (' + (row.sizeMB).toFixed(1) + ' MB, ' + (row.ms / 1000).toFixed(1) + ' s)' : ': failed. ' + row.error);
			} catch (e) {
				if (!st.importer) return;             // stopped by the player
				rows.push({ ok: false, faction: job.playerTemplate, tag: job.tag, id: job.id, name: job.name, display: job.display, error: plain(e && e.message || e) });
				items[n].textContent = job.name + ': failed. ' + plain(e && e.message || e);
			}
		}
		st.timings.convertMs = performance.now() - t0;
		el.bar.style.width = '100%';
		st.running = false;
		env.onBusy(false);
		st.rows = rows;
		try { await importer().close(); } catch (e) { /* ignore */ }
		dispose();
		st.session = null;
		if (done.length) {
			try { await env.onImported(done); } catch (e) { env.log('Armies list: ' + (e && e.message || e)); }
		}
		renderResult(rows);
	}

	function cancel() {
		if (!st.running) return;
		st.running = false;
		env.onBusy(false);
		dispose();
		st.session = null;
		showStep('pick');
		setError('Stopped. Armies that were finished before are kept.');
		env.onImported([]).catch(() => {});
	}

	function renderResult(rows) {
		const good = rows.filter((r) => r.ok);
		const bad = rows.length - good.length;
		el.resultText.textContent = good.length
			? 'Imported ' + good.length + (good.length === 1 ? ' army' : ' armies') + '. ' + (good.length === 1 ? 'It is' : 'They are') + ' in the list of armies above and ticked, so the game will use ' + (good.length === 1 ? 'it' : 'them') + '.' + (bad ? ' ' + bad + ' could not be imported (see below).' : '')
			: 'Nothing was imported.';
		el.table.textContent = '';
		const head = el.table.createTHead().insertRow();
		for (const h of ['Army', 'Size', 'Objects', 'Weapons', 'Models', 'Textures', 'Sounds', 'Warnings', 'Result']) {
			const th = document.createElement('th');
			th.textContent = h;
			head.append(th);
		}
		const body = el.table.createTBody();
		for (const r of rows) {
			const tr = body.insertRow();
			const cells = r.ok ? [r.name, r.sizeMB.toFixed(2) + ' MB', r.objects, r.weapons, r.models, r.textures, r.sounds, r.warnings.length, 'Ready'] : [r.name || r.faction, '', '', '', '', '', '', '', 'Failed'];
			cells.forEach((c, i) => {
				const td = tr.insertCell();
				td.textContent = String(c);
				if (i === 8) td.className = r.ok ? 'ok' : 'err';
				if (i > 0 && i < 8) td.className = 'num';
			});
		}
		el.reports.textContent = '';
		for (const r of rows) {
			const d = document.createElement('details');
			d.open = !r.ok;
			const sum = document.createElement('summary');
			sum.textContent = (r.name || r.faction) + (r.ok ? ': report' : ': what went wrong');
			d.append(sum);
			if (!r.ok) {
				const p = document.createElement('p');
				p.className = 'imp-error-text';
				p.textContent = r.error || 'The army could not be converted.';
				d.append(p);
			} else {
				if (r.warnings.length) {
					const ul = document.createElement('ul');
					for (const w of r.warnings) { const li = document.createElement('li'); li.textContent = plain(w); ul.append(li); }
					d.append(ul);
				}
				const pre = document.createElement('pre');
				pre.textContent = plain(r.report + '\n' + r.validation);
				d.append(pre);
			}
			el.reports.append(d);
		}
		showStep('result');
	}

	function again() {
		setError('');
		st.source = st.analysis = st.session = null;
		showStep('pick');
		describeGame();
	}

	// ---- wiring --------------------------------------------------------------------------------------------------
	el.open.onclick = () => (el.panel.hidden ? show() : hide());
	el.close.onclick = hide;
	el.choose.onclick = chooseFolder;
	el.selfContained.onchange = updateLookButton;
	el.look.onclick = lookForArmies;
	el.back.onclick = again;
	el.go.onclick = runImport;
	el.cancel.onclick = cancel;
	el.more.onclick = again;
	el.retail.onclick = () => {
		for (const f of st.session.factions) if (f.inRuleset) { f.pick = false; f.ui.box.checked = false; }
		refreshArmies();
	};
	el.modName.oninput = () => { if (st.session) refreshArmies(); };

	return {
		show, hide, refresh: () => { if (!el.panel.hidden && !st.running && el.pick && !el.pick.hidden) describeGame(); if (!el.panel.hidden && st.mode === 'separate') updateLookButton(); },
		busy: () => st.running,
		testApi: { state: () => ({ mode: st.mode, running: st.running, timings: st.timings, rows: st.rows || null, factions: st.session ? st.session.factions.map((f) => ({ playerTemplate: f.playerTemplate, tag: f.tag, id: f.id, pick: f.pick })) : null }) },
	};
}
